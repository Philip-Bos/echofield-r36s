/* ECHOFIELD - user interface: screens, controls, jobs.
 * Interaction model:
 *   MAIN    D-pad left/right pick a knob, up/down turn it    L1/R1 knob page
 *           A play/stop   X dice   B undo dice   Y next scene   L2 freeze
 *           left stick: TONE (left/right) + REVERB (up/down)
 *           right stick: ECHO (left/right) + SHIMMER (up/down)
 *           SELECT next visualiser   R2 samples   START menu   SELECT+START quit
 *   LISTS   up/down select, A confirm, B back, other buttons as shown in the hint bar
 */
#include "app.h"

/* knob pages on the main screen: four knobs each, one colour per position */
static const int k_pages[4][4] = {
    {P_TEXTURE, P_DENS_MUL, P_SCATTER, P_PITCH},
    {P_POSITION, P_DRIFT, P_MOTION, P_HARMONY},
    {P_CUTOFF, P_SPACE, P_DLY_MIX, P_SHIMMER},
    {P_TAPE, P_WIDTH, P_REVERSE, P_LEVEL},
};
static const char *const k_page_names[4] = {"GRAIN", "MOTION", "SPACE", "COLOUR"};
static const uint32_t k_slot_col[4] = {C_BLUE, C_GREEN, C_WHITE, C_ORANGE};
#define PAGE_SPACE 2
/* analog sticks drive the SPACE page: LX, LY, RX, RY */
static const int k_stick_param[4] = {P_CUTOFF, P_SPACE, P_DLY_MIX, P_SHIMMER};
static const char *const k_stick_label[4] = {"TONE", "REVERB", "ECHO", "SHIMMER"};

static const int k_grain_page[] = {P_TEXTURE, P_SIZE_MUL, P_DENS_MUL, P_SCATTER, P_SCAN, P_REVERSE, P_PITCH_RAND, P_SIZE_RAND,
                                   P_WINDOW, P_GRAIN_TONE, P_HARMONY};
static const int k_fx_page[] = {P_SPACE, P_REV_SIZE, P_REV_DAMP, P_SHIMMER, P_DLY_MIX, P_DLY_TIME, P_DLY_FB, P_DLY_PINGPONG,
                                P_FILTER_TYPE, P_CUTOFF, P_RESO, P_DRIVE, P_TAPE, P_TAPE_WOW, P_TAPE_FLUTTER, P_TAPE_AGE, P_TAPE_NOISE};
static const int k_oracle_page[] = {P_OR_ON, P_OR_ROOT, P_OR_SCALE, P_OR_ACTIVITY, P_OR_AMBIG, P_OR_RANGE, P_OR_GLIDE, P_OR_DRONE, P_OR_HOLD};
enum { CA_NONE, CA_QUIT, CA_DELETE_FILE, CA_DELETE_PRESET, CA_DELETE_PROJECT };

static const char *const k_menu[] = {"SAMPLES", "SOUNDS", "EDIT SAMPLE", "GRAIN", "EFFECTS", "DRIFT ORACLE",
                                     "PROJECTS", "SETTINGS", "DIAGNOSTICS", "HELP", "QUIT"};
static const char *const k_menu_desc[] = {
    "factory sounds and your own WAV files", "ready-made ambient patches, save your own",
    "trim, reverse, normalize, render a new sample", "every grain setting",
    "reverb, shimmer, echo, filter, tape", "generative harmony: key, scale, chance",
    "save and restore whole sessions", "quality, buttons, display",
    "audio, CPU and system information", "controls and tips", "save and exit"};
static const EfScreen k_menu_scr[] = {SCR_SAMPLES, SCR_PRESETS, SCR_EDIT, SCR_GRAIN, SCR_FX, SCR_ORACLE,
                                      SCR_PROJECTS, SCR_SETTINGS, SCR_DIAG, SCR_HELP, SCR_COUNT};

static Col accent(App *a) { (void)a; return col(C_ORANGE, 255); }

void ui_toast(App *a, Col c, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(a->toast, sizeof a->toast, fmt, ap); va_end(ap);
    a->toast_until = ef_now_s() + 3.0;
    a->toast_col = c;
    ef_log("toast: %s", a->toast);
}

static void confirm(App *a, int action, int arg, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static void confirm(App *a, int action, int arg, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(a->confirm_msg, sizeof a->confirm_msg, fmt, ap); va_end(ap);
    a->confirm_open = true; a->confirm_action = action; a->confirm_arg = arg;
}

static void set_param(App *a, int id, float v) { ef_param_set(&a->P, id, v); a->dirty = true; }
static float get_param(App *a, int id) { return ef_param_get(&a->P, id); }

/* ------------------------------------------------------------------ jobs (worker thread) */
static int job_thread(void *arg) {
    EfJob *j = arg;
    EfWavInfo info;
    j->ok = false;
    switch (j->kind) {
    case JOB_LOAD: case JOB_PREVIEW: {
        EfSample *s = ef_wav_load(j->path, &info);
        if (!s) { snprintf(j->error, sizeof j->error, "%s", info.error); break; }
        if (j->kind == JOB_LOAD && s->rate != j->target_rate) {
            EfSample *r = ef_sample_resample(s, j->target_rate);
            ef_sample_free(s);
            s = r;
            if (!s) { snprintf(j->error, sizeof j->error, "out of memory while resampling"); break; }
        }
        j->result = s; j->ok = true;
        break;
    }
    case JOB_RENDER: {
        bool w = ef_wav_write_sample(j->path, j->src, j->a, j->b, j->reverse, j->gain, 32);
        if (!w) snprintf(j->error, sizeof j->error, "could not write %s", ef_basename(j->path));
        EfSample *r = ef_sample_render(j->src, j->a, j->b, j->reverse, j->gain);
        if (r && r->rate != j->target_rate) { EfSample *q = ef_sample_resample(r, j->target_rate); ef_sample_free(r); r = q; }
        if (!r) { snprintf(j->error, sizeof j->error, "out of memory"); break; }
        ef_strlcpy(r->path, w ? j->path : "", sizeof r->path);
        ef_strlcpy(r->name, ef_basename(j->path), sizeof r->name);
        char *dot = strrchr(r->name, '.'); if (dot) *dot = 0;
        j->result = r; j->ok = true;   /* still usable in memory when the write failed; error shown */
        break;
    }
    default: break;
    }
    j->src = NULL;
    atomic_store(&j->done, 1);
    return 0;
}

static bool job_start(App *a, EfJobKind kind, const char *label) {
    if (a->job_busy) { ui_toast(a, col(C_WARN, 255), "Busy: %s", a->job_label); return false; }
    a->job.kind = kind;
    a->job.target_rate = a->sr;
    a->job.result = NULL; a->job.error[0] = 0; a->job.ok = false;
    atomic_store(&a->job.done, 0);
    ef_strlcpy(a->job_label, label, sizeof a->job_label);
    a->job.thread = SDL_CreateThread(job_thread, "ef_job", &a->job);
    if (!a->job.thread) { ui_toast(a, col(C_ERR, 255), "Cannot start worker thread"); return false; }
    a->job_busy = true;
    return true;
}

/* pending region applied when the next sample load completes (project restore) */
static bool g_pending_region;
static EfProject g_pending_proj;

static void apply_region(App *a) {
    a->region_a = EF_CLAMP(a->region_a, 0.0f, 1.0f);
    a->region_b = EF_CLAMP(a->region_b, 0.0f, 1.0f);
    if (a->region_b - a->region_a < 0.002f) a->region_b = EF_MIN(1.0f, a->region_a + 0.002f);
    if (a->region_b - a->region_a < 0.002f) a->region_a = a->region_b - 0.002f;
    atomic_store(&a->E.region_a, a->region_a);
    atomic_store(&a->E.region_b, a->region_b);
    atomic_store(&a->E.region_reverse, a->reverse ? 1 : 0);
    float gain = 1.0f;
    /* read-only peek: only the UI thread ever frees samples, so the pointer stays valid */
    EfSample *s = a->E.next ? a->E.next : a->E.cur;
    if (a->normalize && s) {
        int i0 = (int)(a->region_a * s->frames), i1 = (int)(a->region_b * s->frames);
        float pk = 0;
        for (int i = i0; i < i1; i++) { pk = EF_MAX(pk, fabsf(s->L[i])); pk = EF_MAX(pk, fabsf(s->R[i])); }
        gain = pk > 1e-4f ? EF_MIN(16.0f, 0.89f / pk) : 1.0f;
    }
    atomic_store(&a->E.sample_gain, gain);
    a->dirty = true;
}

static void stop_preview(App *a) {
    ef_audio_lock(&a->A);
    ef_engine_preview(&a->E, NULL, 0, 0, false);
    ef_audio_unlock(&a->A);
}

static void install_sample(App *a, EfSample *s, const char *src_path, int src_rate, int src_ch) {
    ef_audio_lock(&a->A);
    if (a->E.preview == a->E.cur || a->E.preview == a->E.next) ef_engine_preview(&a->E, NULL, 0, 0, false);
    ef_engine_set_sample(&a->E, s);
    EfSample *ret = ef_engine_take_retired(&a->E);
    ef_audio_unlock(&a->A);
    if (ret) ef_sample_free(ret);
    ef_strlcpy(a->sample_name, s->name, sizeof a->sample_name);
    ef_strlcpy(a->sample_path, src_path, sizeof a->sample_path);
    a->sample_secs = (float)s->frames / s->rate;
    a->sample_channels = src_ch;
    a->sample_src_rate = src_rate;
    if (g_pending_region) {
        a->region_a = g_pending_proj.region_a; a->region_b = g_pending_proj.region_b;
        a->reverse = g_pending_proj.reverse; a->normalize = g_pending_proj.normalize;
        g_pending_region = false;
    } else { a->region_a = 0.0f; a->region_b = 1.0f; a->reverse = false; a->normalize = false; }
    apply_region(a);
}

void app_load_sample_async(App *a, const char *path) {
    ef_strlcpy(a->job.path, path, sizeof a->job.path);
    char lbl[64]; snprintf(lbl, sizeof lbl, "loading %s", ef_basename(path));
    job_start(a, JOB_LOAD, lbl);
}

static void job_poll(App *a) {
    if (!a->job_busy || !atomic_load(&a->job.done)) return;
    SDL_WaitThread(a->job.thread, NULL);
    a->job.thread = NULL;
    a->job_busy = false;
    EfJob *j = &a->job;
    if (!j->ok || !j->result) {
        if (j->kind != JOB_PREVIEW || j->play) ui_toast(a, col(C_ERR, 255), "%s failed: %s", a->job_label, j->error);
        if (j->kind == JOB_PREVIEW) ef_strlcpy(a->preview_path, j->path, sizeof a->preview_path);   /* don't retry a broken file */
        g_pending_region = false;
        return;
    }
    EfWavInfo info;
    switch (j->kind) {
    case JOB_LOAD:
        if (!ef_wav_probe(j->path, &info)) { info.rate = j->result->rate; info.channels = j->result->channels; }
        install_sample(a, j->result, j->path, info.rate, info.channels);
        ui_toast(a, col(C_WHITE, 255), "%s  %.1fs %s", a->sample_name, (double)a->sample_secs, a->sample_channels >= 2 ? "stereo" : "mono");
        break;
    case JOB_PREVIEW:
        stop_preview(a);
        if (a->preview_sample) ef_sample_free(a->preview_sample);
        a->preview_sample = j->result;
        ef_strlcpy(a->preview_path, j->path, sizeof a->preview_path);
        if (j->play) {
            ef_audio_lock(&a->A);
            ef_engine_preview(&a->E, a->preview_sample, 0, a->preview_sample->frames, false);
            ef_audio_unlock(&a->A);
        }
        break;
    case JOB_RENDER:
        install_sample(a, j->result, j->path, j->result->rate, j->result->channels);
        if (j->error[0]) ui_toast(a, col(C_WARN, 255), "Playing from memory - %s", j->error);
        else ui_toast(a, col(C_WHITE, 255), "Rendered %s", ef_basename(j->path));
        break;
    default: break;
    }
}

/* ------------------------------------------------------------------ session */
void app_save_session(App *a) {
    EfProject pr;
    memset(&pr, 0, sizeof pr);
    ef_strlcpy(pr.name, "last session", sizeof pr.name);
    ef_strlcpy(pr.sample_path, a->sample_path, sizeof pr.sample_path);
    pr.region_a = a->region_a; pr.region_b = a->region_b; pr.reverse = a->reverse; pr.normalize = a->normalize;
    memcpy(pr.locks, a->locks, sizeof pr.locks);
    if (g_paths.data_writable && ef_project_save(a->session_path, &pr, &a->P)) a->dirty = false;
    ef_settings_save(&a->S, a->settings_path);
}

static void load_project(App *a, const char *path, bool quiet) {
    EfProject pr;
    char err[128];
    if (!ef_project_load(path, &pr, &a->P, err, sizeof err)) { if (!quiet) ui_toast(a, col(C_ERR, 255), "Project: %s", err); return; }
    memcpy(a->locks, pr.locks, sizeof a->locks);
    ef_param_set(&a->P, P_PLAY, 1);
    if (pr.sample_path[0] && ef_file_exists(pr.sample_path)) {
        if (!strcmp(pr.sample_path, a->sample_path)) {
            a->region_a = pr.region_a; a->region_b = pr.region_b; a->reverse = pr.reverse; a->normalize = pr.normalize;
            apply_region(a);
        } else {
            g_pending_proj = pr; g_pending_region = true;
            app_load_sample_async(a, pr.sample_path);
        }
        if (!quiet) ui_toast(a, col(C_WHITE, 255), "Project %s loaded", pr.name);
    } else if (pr.sample_path[0]) {
        ui_toast(a, col(C_WARN, 255), "Sample missing: %s - pick another in SAMPLES", ef_basename(pr.sample_path));
    }
}

bool app_restore_session(App *a) {
    if (!a->S.autoload_session || !ef_file_exists(a->session_path)) return false;
    load_project(a, a->session_path, true);
    ef_strlcpy(a->preset_name, "last session", sizeof a->preset_name);
    return true;
}

void app_apply_settings(App *a) {
    atomic_store(&a->E.max_voices, a->S.max_voices);
    atomic_store(&a->E.eco, a->S.eco ? 1 : 0);
    atomic_store(&a->E.monitor_on, 0);   /* no live input in this version */
}

/* ------------------------------------------------------------------ browser */
static void refresh_files(App *a) {
    if (!a->files) {
        a->files = calloc(512, sizeof(EfFileEntry));
        a->file_secs = calloc(512, sizeof(float));
        a->file_ch = calloc(512, sizeof(int));
        a->file_rate = calloc(512, sizeof(int));
    }
    if (!a->files || !a->file_secs || !a->file_ch || !a->file_rate) { a->nfiles = 0; return; }
    if (a->tab == 0) a->nfiles = ef_list_files(g_paths.factory_samples, ".wav", a->files, 512);
    else {   /* MY SAMPLES: your WAVs plus takes recorded by earlier versions */
        a->nfiles = ef_list_files(g_paths.samples, ".wav", a->files, 512);
        a->nfiles += ef_list_files(g_paths.recordings, ".wav", a->files + a->nfiles, 512 - a->nfiles);
    }
    for (int i = 0; i < a->nfiles; i++) {
        EfWavInfo info;
        if (ef_wav_probe(a->files[i].path, &info)) { a->file_secs[i] = (float)info.frames / info.rate; a->file_ch[i] = info.channels; a->file_rate[i] = info.rate; }
        else { a->file_secs[i] = -1; a->file_ch[i] = 0; a->file_rate[i] = 0; }
    }
    a->sel[SCR_SAMPLES] = EF_CLAMP(a->sel[SCR_SAMPLES], 0, EF_MAX(0, a->nfiles - 1));
    a->browse_t = ef_now_s();
}

static void refresh_presets(App *a) { a->npresets = ef_presets_list(a->presets, 160); a->sound_cache_sel = -1; }
static void refresh_projects(App *a) {
    if (!a->projects) a->projects = calloc(256, sizeof(EfFileEntry));
    a->nprojects = a->projects ? ef_list_files(g_paths.projects, ".efproj", a->projects, 256) : 0;
}

static void open_screen(App *a, EfScreen s) {
    stop_preview(a);
    a->scr = s;
    switch (s) {
    case SCR_SAMPLES:
        /* open on the tab and row of the playing sample */
        a->tab = strncmp(a->sample_path, g_paths.factory_samples, strlen(g_paths.factory_samples)) ? 1 : 0;
        refresh_files(a);
        for (int i = 0; i < a->nfiles; i++) if (!strcmp(a->files[i].path, a->sample_path)) a->sel[SCR_SAMPLES] = i;
        if (a->tab == 1 && a->nfiles == 0) { a->tab = 0; refresh_files(a); }
        break;
    case SCR_PRESETS: refresh_presets(a); break;
    case SCR_PROJECTS: refresh_projects(a); break;
    case SCR_DIAG: free(a->diag_text); a->diag_text = diag_report(a); a->scroll[SCR_DIAG] = 0; break;
    case SCR_EDIT: a->edit_marker = 0; break;
    default: break;
    }
}

void ui_open_screen(App *a, EfScreen s) { open_screen(a, s); }

/* ------------------------------------------------------------------ actions */
static void do_randomize(App *a) {
    for (int i = 0; i < P_COUNT; i++) a->undo[i] = get_param(a, i);
    a->has_undo = true;
    ef_randomize(&a->P, a->locks, &a->rng);
    a->dirty = true;
    ef_strlcpy(a->preset_name, "dice", sizeof a->preset_name);
    ui_toast(a, col(C_WHITE, 255), "Dice rolled %s B undoes", G_MIDDOT);
}

static void do_undo(App *a) {
    if (!a->has_undo) { ui_toast(a, col(C_DIM, 255), "Nothing to undo"); return; }
    float cur[P_COUNT];
    for (int i = 0; i < P_COUNT; i++) cur[i] = get_param(a, i);
    for (int i = 0; i < P_COUNT; i++) if (i != P_PLAY) ef_param_set(&a->P, i, a->undo[i]);
    memcpy(a->undo, cur, sizeof cur);
    ui_toast(a, col(C_WHITE, 255), "Undo dice");
}

static void change_mode(App *a, int d) {
    int m = ((int)get_param(a, P_MODE) + d + MODE_COUNT) % MODE_COUNT;
    set_param(a, P_MODE, (float)m);
    ui_toast(a, col(C_WHITE, 255), "%s %s %s", g_mode_names[m], G_MIDDOT, g_mode_blurb[m]);
}

static float step_mult(App *a, EfButton b) {
    double held = ef_now_s() - a->down_t[b];
    if (a->down[B_L2] && a->scr != SCR_MAIN) return 0.2f;   /* fine (L2 is FREEZE on the main screen) */
    if (held > 1.6) return 4.0f;
    if (held > 0.8) return 2.0f;
    return 1.0f;
}

static void adjust_param(App *a, int id, int dir, EfButton b) {
    const EfParamInfo *pi = &g_param_info[id];
    float v = get_param(a, id);
    if (pi->kind == PF_ENUM || pi->kind == PF_BOOL || pi->kind == PF_INT) {
        float nv = v + dir;
        if (nv > pi->max) nv = pi->kind == PF_ENUM ? pi->min : pi->max;
        if (nv < pi->min) nv = pi->kind == PF_ENUM ? pi->max : pi->min;
        set_param(a, id, nv);
        return;
    }
    if (id == P_PITCH) { set_param(a, id, roundf(v) + dir * (step_mult(a, b) >= 2.0f ? 2.0f : 1.0f)); return; }
    set_param(a, id, v + dir * pi->step * step_mult(a, b));
}

#define SETTINGS_N 6
static void settings_adjust(App *a, int item, int dir) {
    EfSettings *s = &a->S;
    switch (item) {
    case 0: s->max_voices = EF_CLAMP(s->max_voices + dir * 8, 8, 96); break;
    case 1: s->eco = !s->eco; break;
    case 2: s->swap_ab = !s->swap_ab; break;
    case 3: s->autoload_session = !s->autoload_session; break;
    case 4: s->show_cpu = !s->show_cpu; break;
    case 5: s->visualiser = (s->visualiser + dir + VIS_COUNT) % VIS_COUNT; break;
    }
    app_apply_settings(a);
    ef_settings_save(s, a->settings_path);
}

static const int *page_params(EfScreen s, int *n) {
    switch (s) {
    case SCR_GRAIN: *n = EF_ARRAY_LEN(k_grain_page); return k_grain_page;
    case SCR_FX: *n = EF_ARRAY_LEN(k_fx_page); return k_fx_page;
    case SCR_ORACLE: *n = EF_ARRAY_LEN(k_oracle_page); return k_oracle_page;
    default: *n = 0; return NULL;
    }
}

static void do_confirm(App *a) {
    a->confirm_open = false;
    switch (a->confirm_action) {
    case CA_QUIT: a->quit = true; break;
    case CA_DELETE_FILE:
        if (a->confirm_arg >= 0 && a->confirm_arg < a->nfiles) {
            const char *p = a->files[a->confirm_arg].path;
            bool active = !strcmp(p, a->sample_path);
            if (remove(p) == 0) ui_toast(a, col(C_DIM, 255), "Deleted %s%s", a->files[a->confirm_arg].name, active ? " (still playing from memory)" : "");
            else ui_toast(a, col(C_ERR, 255), "Could not delete %s", a->files[a->confirm_arg].name);
            refresh_files(a);
        }
        break;
    case CA_DELETE_PRESET:
        if (a->confirm_arg >= 0 && a->confirm_arg < a->npresets && !a->presets[a->confirm_arg].factory) {
            remove(a->presets[a->confirm_arg].path);
            refresh_presets(a);
            ui_toast(a, col(C_DIM, 255), "Sound deleted");
        }
        break;
    case CA_DELETE_PROJECT:
        if (a->confirm_arg >= 0 && a->confirm_arg < a->nprojects) { remove(a->projects[a->confirm_arg].path); refresh_projects(a); ui_toast(a, col(C_DIM, 255), "Project deleted"); }
        break;
    }
}

static void request_quit(App *a) { confirm(a, CA_QUIT, 0, "Quit ECHOFIELD? Your session is saved automatically."); }

/* apply a SOUNDS entry; with_sample also loads the sample it was made for */
static void apply_sound(App *a, int i, bool with_sample) {
    if (i < 0 || i >= a->npresets) return;
    EfPresetEntry *pe = &a->presets[i];
    char ref[EF_PATH_MAX] = "", err[128] = "";
    bool ok = pe->factory ? ef_preset_apply_factory(pe->factory_idx, &a->P, ref, sizeof ref)
                          : ef_preset_apply_file(pe->path, &a->P, ref, sizeof ref, err, sizeof err);
    if (!ok) { ui_toast(a, col(C_ERR, 255), "Sound: %s", err); return; }
    ef_param_set(&a->P, P_PLAY, 1);
    ef_strlcpy(a->preset_name, pe->name, sizeof a->preset_name);
    a->dirty = true;
    if (with_sample && ref[0] && strcmp(ref, a->sample_path)) {
        if (ef_file_exists(ref)) app_load_sample_async(a, ref);
        else ui_toast(a, col(C_WARN, 255), "Sample missing (%s) - keeping current sample", ef_basename(ref));
    } else ui_toast(a, col(C_WHITE, 255), "%s%s", pe->name, with_sample ? "" : " on your sample");
}

/* ------------------------------------------------------------------ button handling */
static void list_nav(int *sel, int n, int dir) {
    if (n <= 0) { *sel = 0; return; }
    *sel = (*sel + dir + n) % n;
}

static void handle(App *a, EfButton b) {
    if (a->confirm_open) {
        if (b == B_A) do_confirm(a);
        else if (b == B_B) a->confirm_open = false;
        return;
    }
    if (b == B_START && a->down[B_SELECT]) { a->select_used = true; request_quit(a); return; }
    if (b == B_X && a->down[B_SELECT]) { a->select_used = true; do_undo(a); return; }
    if (b == B_START) {
        if (a->scr == SCR_MENU) a->scr = a->menu_return;
        else { a->menu_return = SCR_MAIN; stop_preview(a); a->scr = SCR_MENU; }
        return;
    }
    if (b == B_R2 && a->scr != SCR_MENU) {   /* samples from anywhere, R2 again returns */
        if (a->scr == SCR_SAMPLES) { stop_preview(a); a->scr = SCR_MAIN; }
        else open_screen(a, SCR_SAMPLES);
        return;
    }
    switch (a->scr) {
    case SCR_MAIN: {
        int *s = &a->sel[SCR_MAIN];
        *s = EF_CLAMP(*s, 0, 3);
        int id = k_pages[a->page][*s];
        switch (b) {
        case B_LEFT: *s = (*s + 3) % 4; break;
        case B_RIGHT: *s = (*s + 1) % 4; break;
        case B_UP: adjust_param(a, id, 1, b); break;
        case B_DOWN: adjust_param(a, id, -1, b); break;
        case B_L1: a->page = (a->page + 3) % 4; break;
        case B_R1: a->page = (a->page + 1) % 4; break;
        case B_A: set_param(a, P_PLAY, get_param(a, P_PLAY) > 0.5f ? 0 : 1); break;
        case B_X: do_randomize(a); break;
        case B_B: if (a->has_undo) do_undo(a); break;
        case B_Y: change_mode(a, 1); break;
        case B_L2:
            set_param(a, P_FREEZE, get_param(a, P_FREEZE) > 0.5f ? 0 : 1);
            ui_toast(a, col(C_BLUE, 255), get_param(a, P_FREEZE) > 0.5f ? "FREEZE - the sound holds still" : "Unfrozen");
            break;
        default: break;
        }
        break;
    }
    case SCR_MENU: {
        int n = EF_ARRAY_LEN(k_menu);
        if (b == B_UP) list_nav(&a->sel[SCR_MENU], n, -1);
        else if (b == B_DOWN) list_nav(&a->sel[SCR_MENU], n, 1);
        else if (b == B_B) a->scr = a->menu_return;
        else if (b == B_A) {
            EfScreen t = k_menu_scr[a->sel[SCR_MENU]];
            if (t == SCR_COUNT) request_quit(a);
            else open_screen(a, t);
        }
        break;
    }
    case SCR_GRAIN: case SCR_FX: case SCR_ORACLE: {
        int n; const int *ids = page_params(a->scr, &n);
        int *s = &a->sel[a->scr];
        if (b == B_UP) list_nav(s, n, -1);
        else if (b == B_DOWN) list_nav(s, n, 1);
        else if (b == B_LEFT) adjust_param(a, ids[*s], -1, b);
        else if (b == B_RIGHT) adjust_param(a, ids[*s], 1, b);
        else if (b == B_A && (g_param_info[ids[*s]].kind == PF_BOOL)) adjust_param(a, ids[*s], 1, b);
        else if (b == B_Y && g_param_info[ids[*s]].randomizable) {
            a->locks[ids[*s]] = !a->locks[ids[*s]];
            ui_toast(a, col(C_DIM, 255), "%s %s", g_param_info[ids[*s]].label, a->locks[ids[*s]] ? "locked: dice leaves it alone" : "unlocked");
        }
        else if (b == B_X) do_randomize(a);
        else if (b == B_B) a->scr = SCR_MAIN;
        else if (b == B_L1 || b == B_R1) {   /* hop between the three parameter pages */
            static const EfScreen ring[3] = {SCR_GRAIN, SCR_FX, SCR_ORACLE};
            int k = a->scr == SCR_GRAIN ? 0 : (a->scr == SCR_FX ? 1 : 2);
            a->scr = ring[(k + (b == B_R1 ? 1 : 2)) % 3];
        }
        break;
    }
    case SCR_SETTINGS: {
        int *s = &a->sel[SCR_SETTINGS];
        if (b == B_UP) list_nav(s, SETTINGS_N, -1);
        else if (b == B_DOWN) list_nav(s, SETTINGS_N, 1);
        else if (b == B_LEFT) settings_adjust(a, *s, -1);
        else if (b == B_RIGHT || b == B_A) settings_adjust(a, *s, 1);
        else if (b == B_B) a->scr = SCR_MAIN;
        break;
    }
    case SCR_SAMPLES: {
        int *s = &a->sel[SCR_SAMPLES];
        int old = *s;
        if (b == B_UP) list_nav(s, a->nfiles, -1);
        else if (b == B_DOWN) list_nav(s, a->nfiles, 1);
        else if (b == B_L1 || b == B_R1 || b == B_LEFT || b == B_RIGHT) { a->tab ^= 1; a->sel[SCR_SAMPLES] = 0; stop_preview(a); refresh_files(a); }
        else if (b == B_A && a->nfiles) { stop_preview(a); app_load_sample_async(a, a->files[*s].path); ef_param_set(&a->P, P_PLAY, 1); a->scr = SCR_MAIN; }
        else if (b == B_X && a->nfiles) {
            if (atomic_load(&a->E.preview_playing)) stop_preview(a);
            else if (a->preview_sample && !strcmp(a->preview_path, a->files[*s].path)) {
                ef_audio_lock(&a->A);
                ef_engine_preview(&a->E, a->preview_sample, 0, a->preview_sample->frames, false);
                ef_audio_unlock(&a->A);
            } else {
                ef_strlcpy(a->job.path, a->files[*s].path, sizeof a->job.path);
                a->job.play = true;
                job_start(a, JOB_PREVIEW, "preview");
            }
        }
        else if (b == B_Y && a->nfiles) {
            if (a->tab == 0) ui_toast(a, col(C_DIM, 255), "Factory samples are read-only");
            else confirm(a, CA_DELETE_FILE, *s, "Delete %s from the SD card?", a->files[*s].name);
        }
        else if (b == B_B) { stop_preview(a); a->scr = SCR_MAIN; }
        if (*s != old) { stop_preview(a); a->browse_t = ef_now_s(); }
        break;
    }
    case SCR_EDIT: {
        if (!a->E.cur) { if (b == B_B) a->scr = SCR_MAIN; break; }
        float st = 0.002f * step_mult(a, b);
        if (b == B_UP) a->edit_marker = (a->edit_marker + 2) % 3;
        else if (b == B_DOWN) a->edit_marker = (a->edit_marker + 1) % 3;
        else if (b == B_LEFT || b == B_RIGHT) {
            float d = b == B_LEFT ? -st : st;
            if (a->edit_marker == 0) { a->region_a = EF_CLAMP(a->region_a + d, 0.0f, a->region_b - 0.002f); apply_region(a); }
            else if (a->edit_marker == 1) { a->region_b = EF_CLAMP(a->region_b + d, a->region_a + 0.002f, 1.0f); apply_region(a); }
            else set_param(a, P_POSITION, get_param(a, P_POSITION) + d * 2.0f);
        }
        else if (b == B_A) {
            ef_audio_lock(&a->A);
            if (atomic_load(&a->E.preview_playing)) ef_engine_preview(&a->E, NULL, 0, 0, false);
            else ef_engine_preview(&a->E, a->E.cur, (int)(a->region_a * a->E.cur->frames), (int)(a->region_b * a->E.cur->frames), true);
            ef_audio_unlock(&a->A);
        }
        else if (b == B_X) { a->reverse = !a->reverse; apply_region(a); }
        else if (b == B_Y) { a->normalize = !a->normalize; apply_region(a); }
        else if (b == B_L1) { a->region_a = 0; a->region_b = 1; apply_region(a); ui_toast(a, col(C_DIM, 255), "Region reset to the whole sample"); }
        else if (b == B_R1) {   /* render region as a new sample (never overwrites) */
            if (a->job_busy) break;
            stop_preview(a);
            char ts[32], p[EF_PATH_MAX];
            ef_timestamp(ts, sizeof ts, "file");
            char base[48]; ef_strlcpy(base, a->sample_name, sizeof base);
            for (char *q = base; *q; q++) if (!isalnum((unsigned char)*q) && *q != '-' && *q != '_') *q = '_';
            snprintf(p, sizeof p, "%s/%s_edit_%s.wav", g_paths.samples, base, ts);
            a->job.src = a->E.cur;
            a->job.a = (int)(a->region_a * a->E.cur->frames); a->job.b = (int)(a->region_b * a->E.cur->frames);
            a->job.reverse = a->reverse; a->job.gain = atomic_load(&a->E.sample_gain);
            ef_strlcpy(a->job.path, p, sizeof a->job.path); a->job.path2[0] = 0;
            job_start(a, JOB_RENDER, "rendering");
        }
        else if (b == B_B) { stop_preview(a); a->scr = SCR_MAIN; }
        break;
    }
    case SCR_PRESETS: {
        int *s = &a->sel[SCR_PRESETS];
        if (b == B_UP) list_nav(s, a->npresets, -1);
        else if (b == B_DOWN) list_nav(s, a->npresets, 1);
        else if (b == B_A && a->npresets) { apply_sound(a, *s, true); a->scr = SCR_MAIN; }
        else if (b == B_X && a->npresets) { apply_sound(a, *s, false); a->scr = SCR_MAIN; }
        else if (b == B_Y) {
            char nm[64], path[EF_PATH_MAX];
            ef_random_name(&a->rng, nm, sizeof nm);
            if (ef_preset_save(&a->P, nm, a->sample_path, path, sizeof path)) { ui_toast(a, col(C_WHITE, 255), "Saved sound \"%s\"", nm); ef_strlcpy(a->preset_name, nm, sizeof a->preset_name); }
            else ui_toast(a, col(C_ERR, 255), "Could not save (storage read-only?)");
            refresh_presets(a);
        }
        else if (b == B_R1 && a->npresets) {
            if (a->presets[*s].factory) ui_toast(a, col(C_DIM, 255), "Factory sounds cannot be deleted");
            else confirm(a, CA_DELETE_PRESET, *s, "Delete sound \"%s\"?", a->presets[*s].name);
        }
        else if (b == B_B) a->scr = SCR_MAIN;
        break;
    }
    case SCR_PROJECTS: {
        int *s = &a->sel[SCR_PROJECTS];
        int n = a->nprojects + 1;
        if (b == B_UP) list_nav(s, n, -1);
        else if (b == B_DOWN) list_nav(s, n, 1);
        else if (b == B_A) {
            if (*s == 0) {
                char nm[64], ts[32], p[EF_PATH_MAX];
                ef_random_name(&a->rng, nm, sizeof nm);
                ef_timestamp(ts, sizeof ts, "file");
                snprintf(p, sizeof p, "%s/%s_%s.efproj", g_paths.projects, ts, nm);
                for (char *q = p + strlen(g_paths.projects) + 1; *q; q++) if (*q == ' ') *q = '_';
                EfProject pr; memset(&pr, 0, sizeof pr);
                ef_strlcpy(pr.name, nm, sizeof pr.name);
                ef_strlcpy(pr.sample_path, a->sample_path, sizeof pr.sample_path);
                pr.region_a = a->region_a; pr.region_b = a->region_b; pr.reverse = a->reverse; pr.normalize = a->normalize;
                memcpy(pr.locks, a->locks, sizeof pr.locks);
                if (ef_project_save(p, &pr, &a->P)) ui_toast(a, col(C_WHITE, 255), "Project saved: %s", nm);
                else ui_toast(a, col(C_ERR, 255), "Could not save project");
                refresh_projects(a);
            } else { load_project(a, a->projects[*s - 1].path, false); a->scr = SCR_MAIN; }
        }
        else if (b == B_Y && *s > 0) confirm(a, CA_DELETE_PROJECT, *s - 1, "Delete project %s? (samples are kept)", a->projects[*s - 1].name);
        else if (b == B_B) a->scr = SCR_MAIN;
        break;
    }
    case SCR_DIAG: {
        if (b == B_UP) a->scroll[SCR_DIAG] = EF_MAX(0, a->scroll[SCR_DIAG] - 3);
        else if (b == B_DOWN) a->scroll[SCR_DIAG] += 3;
        else if (b == B_X) { free(a->diag_text); a->diag_text = diag_report(a); ui_toast(a, col(C_DIM, 255), "Refreshed"); }
        else if (b == B_A) {
            char p[EF_PATH_MAX]; ef_path_join(p, sizeof p, g_paths.data_dir, "diagnostics.txt");
            if (a->diag_text && ef_write_text_file_atomic(p, a->diag_text)) ui_toast(a, col(C_OK, 255), "Saved %s", p);
            else ui_toast(a, col(C_ERR, 255), "Could not save report");
        }
        else if (b == B_B) a->scr = SCR_MAIN;
        break;
    }
    case SCR_HELP:
        if (b == B_UP) a->scroll[SCR_HELP] = EF_MAX(0, a->scroll[SCR_HELP] - 2);
        else if (b == B_DOWN) a->scroll[SCR_HELP] += 2;
        else if (b == B_B || b == B_A) a->scr = SCR_MAIN;
        break;
    default: break;
    }
}

void ui_button(App *a, EfButton b, bool down) {
    if (a->S.swap_ab) { if (b == B_A) b = B_B; else if (b == B_B) b = B_A; }
    double now = ef_now_s();
    if (down) {
        if (a->down[b]) return;
        a->down[b] = true;
        a->down_t[b] = now;
        a->repeat_t[b] = now + 0.3;
        if (b == B_SELECT) { a->select_used = false; return; }
        if (a->down[B_SELECT]) a->select_used = true;
        handle(a, b);
    } else {
        if (!a->down[b]) return;
        a->down[b] = false;
        if (b == B_SELECT && !a->select_used && !a->confirm_open) {
            /* SELECT tap: next visualiser on the main screen, help elsewhere */
            if (a->scr == SCR_MAIN) {
                a->S.visualiser = (a->S.visualiser + 1) % VIS_COUNT;
                ef_settings_save(&a->S, a->settings_path);
            } else if (a->scr != SCR_MENU) open_screen(a, SCR_HELP);
        }
    }
}

void ui_poll_repeat(App *a) {
    double now = ef_now_s();
    static const EfButton rep[] = {B_UP, B_DOWN, B_LEFT, B_RIGHT};
    for (int i = 0; i < 4; i++) {
        EfButton b = rep[i];
        if (a->down[b] && now >= a->repeat_t[b] && !a->confirm_open) {
            bool turning = (a->scr == SCR_MAIN && (b == B_UP || b == B_DOWN)) ||
                           ((a->scr == SCR_GRAIN || a->scr == SCR_FX || a->scr == SCR_ORACLE || a->scr == SCR_EDIT) && (b == B_LEFT || b == B_RIGHT));
            a->repeat_t[b] = now + (turning ? 0.05 : 0.12);
            handle(a, b);
        }
    }
}

void ui_init(App *a) {
    a->rng.s = (uint32_t)time(NULL) | 1u;
    ef_path_join(a->settings_path, sizeof a->settings_path, g_paths.data_dir, "settings.ini");
    ef_path_join(a->session_path, sizeof a->session_path, g_paths.data_dir, "session.efproj");
    ef_strlcpy(a->preset_name, "init", sizeof a->preset_name);
    a->scr = SCR_MAIN;
    a->region_b = 1.0f;
    a->last_autosave = ef_now_s();
    a->sound_cache_sel = -1;
}

void app_shutdown_ui(App *a) {
    if (a->job_busy && a->job.thread) { SDL_WaitThread(a->job.thread, NULL); a->job_busy = false;
        if (a->job.result) ef_sample_free(a->job.result); }
    stop_preview(a);
    if (a->preview_sample) ef_sample_free(a->preview_sample);
    free(a->files); free(a->file_secs); free(a->file_ch); free(a->file_rate); free(a->projects); free(a->diag_text);
}

void ui_update(App *a, double dt) {
    job_poll(a);
    /* retired samples freed on the UI thread */
    ef_audio_lock(&a->A);
    EfSample *ret = ef_engine_take_retired(&a->E);
    ef_audio_unlock(&a->A);
    if (ret) ef_sample_free(ret);
    /* analog sticks turn the SPACE knobs like a hand on a dial: push to move, let go to keep */
    float dz = 0.2f;
#define DZ(v) (fabsf(v) < dz ? 0.0f : ((v) - ((v) > 0 ? dz : -dz)) / (1.0f - dz))
    float st[4] = {DZ(a->axis[0]), -DZ(a->axis[1]), DZ(a->axis[2]), -DZ(a->axis[3])};
#undef DZ
    bool active = a->scr == SCR_MAIN && !a->confirm_open;
    for (int i = 0; i < 4; i++) {
        a->stick_vis[i] = active ? st[i] : 0.0f;
        if (!active || st[i] == 0.0f) continue;
        int id = k_stick_param[i];
        const EfParamInfo *pi = &g_param_info[id];
        float rate = st[i] * fabsf(st[i]) * 0.55f * (pi->max - pi->min);   /* squared: fine near centre */
        set_param(a, id, get_param(a, id) + rate * (float)dt);
        a->stick_t = ef_now_s();
    }
    atomic_store(&a->E.perf_pos, 0.0f);
    atomic_store(&a->E.perf_pitch, 0.0f);
    atomic_store(&a->E.perf_texture, 0.0f);
    atomic_store(&a->E.perf_scatter, 0.0f);
    /* output meters */
    for (int c = 0; c < 2; c++) {
        float p = atomic_exchange(c ? &a->E.peak_r : &a->E.peak_l, 0.0f);
        a->out_meter[c] = EF_MAX(p, a->out_meter[c] * (float)pow(0.05, dt));
    }
    if (atomic_exchange(&a->E.limit_active, 0)) a->limit_until = ef_now_s() + 0.6;
    int cc = atomic_exchange(&a->E.clip_count, 0);
    if (cc > 0) a->out_clip_until = ef_now_s() + 1.0;
    int seq = atomic_load(&a->E.oracle_event_seq);
    if (seq != a->last_oracle_seq) {
        a->last_oracle_seq = seq;
        ef_strlcpy(a->oracle_flash, a->E.oracle_event_ui, sizeof a->oracle_flash);
        a->oracle_flash_until = ef_now_s() + 2.5;
    }
    /* SAMPLES: load the selected file's waveform shortly after the cursor stops */
    if (a->scr == SCR_SAMPLES && a->nfiles && !a->job_busy && ef_now_s() - a->browse_t > 0.3) {
        const char *p = a->files[EF_CLAMP(a->sel[SCR_SAMPLES], 0, a->nfiles - 1)].path;
        if (strcmp(p, a->preview_path)) {
            ef_strlcpy(a->job.path, p, sizeof a->job.path);
            a->job.play = false;
            job_start(a, JOB_PREVIEW, "reading");
        }
    }
    /* autosave (session file) every 10 s when something changed: the PortMaster hotkey may kill us */
    if (a->dirty && ef_now_s() - a->last_autosave > 10.0) { app_save_session(a); a->last_autosave = ef_now_s(); }
}

/* ================================================================== drawing */
/* top bar on every screen: wordmark, screen title, optional right-hand text */
static void top_bar(App *a, const char *title, const char *right) {
    draw_text_spaced(F_LOGO, 14, 7, col(C_TEXT, 255), AL_LEFT, 1, "ECHOFIELD");
    if (title) {
        draw_fill(124, 10, 6, 6, col(C_ORANGE, 255));
        draw_text_spaced(F_TINY, 138, 8, col(C_DIM, 255), AL_LEFT, 2, title);
    }
    int x = SCREEN_W - 14;
    if (a->S.show_cpu) {
        float load = atomic_load(&a->A.load);
        x -= draw_textf(F_TINY, x, 8, load > 0.7f ? col(C_ERR, 255) : col(C_DIM, 255), AL_RIGHT, "CPU %2.0f%%", (double)(load * 100.0f)) + 14;
    }
    if (right) draw_text_clipped(F_TINY, EF_MAX(300, x - 300), 8, col(C_DIM, 255), EF_MIN(300, x - 300), right);
}

/* hint bar at the bottom: pairs of button / meaning */
static void hints(App *a, const char *const *chips, int n) {
    int x = 14;
    for (int i = 0; i + 1 < n * 2; i += 2) {
        int w;
        draw_button_chip(x, SCREEN_H - 24, chips[i], chips[i + 1], accent(a), &w);
        x += w + 14;
    }
}

/* One zig-zag polyline per colour run: a filled-looking waveform in ~3 draw calls. */
static void draw_waveform(const EfSample *s, int x, int y, int w, int h, float ra, float rb, Col in, Col out) {
    if (!s || w <= 0) return;
    int mid = y + h / 2;
    float scale = (h / 2 - 2) / EF_MAX(0.05f, s->peak);
    static SDL_Point pts[2 * 1024];
    int ca = (int)(ra * w), cb = (int)(rb * w);
    int bounds[4] = {0, EF_CLAMP(ca, 0, w), EF_CLAMP(cb, 0, w), w};
    for (int seg = 0; seg < 3; seg++) {
        int np = 0;
        for (int c = bounds[seg]; c < bounds[seg + 1] && np < 2046; c++) {
            int ov = c * EF_OVERVIEW_COLS / w;
            int y0 = mid - (int)(s->ov_max[ov] * scale), y1 = mid - (int)(s->ov_min[ov] * scale);
            if (y1 - y0 < 1) y1 = y0 + 1;
            pts[np].x = x + c; pts[np].y = (c & 1) ? y1 : y0; np++;
            pts[np].x = x + c; pts[np].y = (c & 1) ? y0 : y1; np++;
        }
        draw_lines(pts, np, seg == 1 ? in : out);
    }
}

/* knob: 270 degree track, value arc in the slot colour, white pointer */
static void knob(App *a, int cx, int cy, int r, int id, int slot, bool sel) {
    const EfParamInfo *pi = &g_param_info[id];
    float v = get_param(a, id);
    float n = EF_CLAMP((v - pi->min) / (pi->max - pi->min), 0.0f, 1.0f);
    const float a0 = EF_PI * 0.75f, sweep = EF_PI * 1.5f;
    Col c = col(k_slot_col[slot], 255);
    draw_disc(cx, cy, r - 5, col(sel ? 0x1e1e1e : 0x141414, 255));
    draw_arc(cx, cy, r, a0, a0 + sweep, 3, col(C_GRID, 255));
    bool bip = pi->min < 0 && pi->max > 0;
    float from = bip ? 0.5f : 0.0f;
    float lo = EF_MIN(from, n), hi = EF_MAX(from, n);
    if (hi - lo > 0.004f) draw_arc(cx, cy, r, a0 + sweep * lo, a0 + sweep * hi, 3, c);
    float an = a0 + sweep * n;
    draw_line(cx + (int)(cosf(an) * 6), cy + (int)(sinf(an) * 6), cx + (int)(cosf(an) * (r - 7)), cy + (int)(sinf(an) * (r - 7)), col(C_WHITE, 255));
    draw_line(cx + (int)(cosf(an) * 6) + 1, cy + (int)(sinf(an) * 6), cx + (int)(cosf(an) * (r - 7)) + 1, cy + (int)(sinf(an) * (r - 7)), col(C_WHITE, 255));
    if (sel) draw_circle(cx, cy, r + 6, c);
    /* label (inverted when selected) and value */
    const char *lbl = pi->label;
    int lw = draw_text_width(F_TINY, lbl) + 2 * (int)(strlen(lbl) - 1);
    if (sel) draw_fill(cx - lw / 2 - 5, cy + r + 10, lw + 10, 15, c);
    draw_text_spaced(F_TINY, cx, cy + r + 11, sel ? col(C_BG, 255) : col(C_DIM, 255), AL_CENTER, 2, lbl);
    char val[32]; ef_param_format(id, v, val, sizeof val);
    draw_text(F_SMALL, cx, cy + r + 29, sel ? col(C_TEXT, 255) : col(0x9a9a9a, 255), AL_CENTER, val);
    if (a->locks[id]) draw_lock_icon(cx + r - 4, cy - r - 2, col(C_WARN, 255));
}

/* left/right stick indicator: crosshair box with the deflection dot and the two values */
static void stick_hud(App *a, int x, int y, int which) {
    const int s = 30;
    bool live = fabsf(a->stick_vis[which * 2]) + fabsf(a->stick_vis[which * 2 + 1]) > 0.0f;
    Col fr = live ? col(C_WHITE, 255) : col(C_FAINT, 255);
    draw_rect(x, y, s, s, fr);
    draw_fill(x + s / 2, y + 3, 1, s - 6, col(C_GRID, 255));
    draw_fill(x + 3, y + s / 2, s - 6, 1, col(C_GRID, 255));
    int dx = x + s / 2 + (int)(a->stick_vis[which * 2] * (s / 2 - 4)), dy = y + s / 2 - (int)(a->stick_vis[which * 2 + 1] * (s / 2 - 4));
    draw_fill(dx - 2, dy - 2, 5, 5, live ? col(C_ORANGE, 255) : col(C_DIM, 255));
    for (int k = 0; k < 2; k++) {
        int id = k_stick_param[which * 2 + k];
        const EfParamInfo *pi = &g_param_info[id];
        float n = (get_param(a, id) - pi->min) / (pi->max - pi->min);
        int ty = y + k * 16;
        draw_text(F_TINY, x + s + 7, ty, col(C_DIM, 255), AL_LEFT, k_stick_label[which * 2 + k]);
        draw_fill(x + s + 7, ty + 12, 56, 2, col(C_GRID, 255));
        draw_fill(x + s + 7, ty + 12, (int)(56 * n), 2, col(k_slot_col[which * 2 + k], 255));
        draw_text(F_TINY, x + s + 7 + 62, ty + 4, col(C_FAINT, 255), AL_LEFT, k ? G_UPDOWN : G_LEFTRIGHT);
    }
}

static void draw_main(App *a) {
    int mode = EF_CLAMP((int)get_param(a, P_MODE), 0, MODE_COUNT - 1);
    /* top bar: wordmark, scene, sample, state */
    draw_text_spaced(F_LOGO, 14, 7, col(C_TEXT, 255), AL_LEFT, 1, "ECHOFIELD");
    draw_fill(124, 10, 6, 6, col(C_ORANGE, 255));
    draw_text_spaced(F_TINY, 138, 8, col(C_TEXT, 255), AL_LEFT, 2, g_mode_names[mode]);
    char right[128];
    snprintf(right, sizeof right, "%s  %s  %s", a->sample_name[0] ? a->sample_name : "no sample", G_MIDDOT, a->preset_name);
    draw_text_clipped(F_TINY, 214, 8, col(C_DIM, 255), 250, right);
    int x = SCREEN_W - 14;
    bool playing = get_param(a, P_PLAY) > 0.5f;
    if (playing) { draw_text(F_TINY, x, 8, col(C_GREEN, 255), AL_RIGHT, G_RIGHT); }
    else draw_fill(x - 7, 10, 7, 7, col(C_DIM, 255));
    x -= 22;
    if (get_param(a, P_FREEZE) > 0.5f) {
        int w = draw_text_width(F_TINY, "FREEZE") + 10;
        draw_fill(x - w, 6, w, 15, col(C_BLUE, 255));
        draw_text(F_TINY, x - w + 5, 8, col(C_BG, 255), AL_LEFT, "FREEZE");
        x -= w + 8;
    }
    if (a->S.show_cpu) x -= draw_textf(F_TINY, x, 8, col(C_DIM, 255), AL_RIGHT, "CPU %.0f%%", (double)(atomic_load(&a->A.load) * 100)) + 10;
    /* level meter: two thin bars under the top bar */
    for (int c = 0; c < 2; c++) {
        float f = EF_CLAMP((ef_lin_to_db(a->out_meter[c]) + 48.0f) / 48.0f, 0.0f, 1.0f);
        draw_fill(14, 26 + c * 3, (int)(612 * f), 1, ef_now_s() < a->out_clip_until ? col(C_ERR, 255) : col(0x3a3a3a, 255));
    }
    /* visualiser */
    const int vy = 34, vh = 290;
    if (a->E.cur) vis_draw(a, a->S.visualiser, 0, vy, SCREEN_W, vh);
    else {
        draw_text(F_BODY, 320, vy + vh / 2 - 16, col(C_DIM, 255), AL_CENTER, a->job_busy ? a->job_label : "No sample loaded");
        draw_text(F_TINY, 320, vy + vh / 2 + 8, col(C_DIM, 255), AL_CENTER, "R2 opens SAMPLES");
    }
    draw_textf(F_TINY, 14, vy + 4, col(C_FAINT, 255), AL_LEFT, "%02d %s", a->S.visualiser + 1, g_vis_names[a->S.visualiser]);
    if (!playing) draw_text_spaced(F_TINY, SCREEN_W - 14, vy + 4, col(C_DIM, 255), AL_RIGHT, 2, "STOPPED - A PLAYS");
    if (get_param(a, P_OR_ON) > 0.5f) {
        int note = ((atomic_load(&a->E.oracle_note) % 12) + 12) % 12;
        bool flash = ef_now_s() < a->oracle_flash_until;
        draw_textf(F_TINY, SCREEN_W - 14, vy + 18, col(C_ORANGE, 255), AL_RIGHT, "ORACLE %s %s %s %s", g_note_names[(int)get_param(a, P_OR_ROOT)],
                   g_scale_names[(int)get_param(a, P_OR_SCALE)], G_NOTE, flash ? a->oracle_flash : g_note_names[note]);
    }
    stick_hud(a, 14, vy + vh - 38, 0);
    stick_hud(a, SCREEN_W - 14 - 112, vy + vh - 38, 1);
    if (a->job_busy) draw_textf(F_TINY, 320, vy + vh - 16, col(C_WARN, 255), AL_CENTER, "%s %s", a->job_label, (SDL_GetTicks() / 300) % 2 ? G_ELLIPSIS : "");
    /* page tabs */
    int ty = vy + vh + 6;
    draw_button_chip(14, ty - 1, "L1", "", accent(a), NULL);
    draw_button_chip(SCREEN_W - 14 - 22, ty - 1, "R1", "", accent(a), NULL);
    for (int p = 0; p < 4; p++) {
        int cx = 140 + p * 120;
        bool cur = p == a->page;
        bool flash = p == PAGE_SPACE && ef_now_s() - a->stick_t < 0.6 && !cur;
        draw_text_spaced(F_TINY, cx, ty + 1, cur ? col(C_TEXT, 255) : (flash ? col(C_ORANGE, 255) : col(C_FAINT, 255)), AL_CENTER, 3, k_page_names[p]);
        if (cur) draw_fill(cx - 22, ty + 15, 44, 2, col(C_TEXT, 255));
    }
    /* four knobs */
    int ky = ty + 58;
    for (int i = 0; i < 4; i++) knob(a, 80 + i * 160, ky, 26, k_pages[a->page][i], i, a->sel[SCR_MAIN] == i);
    static const char *const chips[] = {G_LEFTRIGHT, "PICK", G_UPDOWN, "TURN", "A", "PLAY", "X", "DICE", "Y", "SCENE", "L2", "FREEZE", "R2", "SAMPLES", "SEL", "VIEW"};
    hints(a, chips, 8);
}

/* list rows: selected row is inverted (light bar, dark text) */
static Col list_row(int x, int y, int w, int h, bool sel) {
    if (sel) { draw_fill(x, y, w, h, col(C_WHITE, 255)); return col(C_BG, 255); }
    draw_fill(x, y + h - 1, w, 1, col(0x1c1c1c, 255));
    return col(0xc8c8c8, 255);
}

static void scroll_fit(int *scroll, int sel, int visible, int n) {
    if (sel < *scroll) *scroll = sel;
    if (sel >= *scroll + visible) *scroll = sel - visible + 1;
    *scroll = EF_CLAMP(*scroll, 0, EF_MAX(0, n - visible));
}

static void value_bar(App *a, int id, int x, int y, int w, Col c) {
    const EfParamInfo *pi = &g_param_info[id];
    float n = EF_CLAMP((get_param(a, id) - pi->min) / (pi->max - pi->min), 0.0f, 1.0f);
    for (int i = 0; i < w; i += 4) draw_fill(x + i, y, 2, 6, col(C_GRID, 255));
    bool bip = pi->min < 0 && pi->max > 0;
    int from = bip ? w / 2 : 0, to = (int)(n * w);
    for (int i = EF_MIN(from, to); i < EF_MAX(from, to); i += 4) draw_fill(x + i, y, 2, 6, c);
}

static void draw_param_page(App *a, const char *title, const char *sub) {
    top_bar(a, title, sub);
    int n; const int *ids = page_params(a->scr, &n);
    int *s = &a->sel[a->scr];
    *s = EF_CLAMP(*s, 0, n - 1);
    const int rows = 11, rh = 28, y0 = 36;
    scroll_fit(&a->scroll[a->scr], *s, rows, n);
    Col pc = col(a->scr == SCR_GRAIN ? C_BLUE : (a->scr == SCR_FX ? C_GREEN : C_ORANGE), 255);
    for (int r = 0; r < rows && a->scroll[a->scr] + r < n; r++) {
        int i = a->scroll[a->scr] + r, id = ids[i];
        int y = y0 + r * rh;
        bool sel = i == *s;
        Col tc = list_row(10, y, 620, rh - 2, sel);
        draw_text_spaced(F_TINY, 22, y + 7, tc, AL_LEFT, 2, g_param_info[id].label);
        if (a->locks[id]) draw_lock_icon(22 + draw_text_width(F_TINY, g_param_info[id].label) + 2 * (int)strlen(g_param_info[id].label) + 6, y + 7, sel ? col(C_BG, 255) : col(C_WARN, 255));
        char val[32]; ef_param_format(id, get_param(a, id), val, sizeof val);
        draw_text(F_SMALL, 618, y + 5, tc, AL_RIGHT, val);
        if (g_param_info[id].kind == PF_CONT) value_bar(a, id, 250, y + 10, 240, sel ? col(C_BG, 255) : pc);
    }
    if (n > rows) draw_textf(F_TINY, 630, y0 + rows * rh + 2, col(C_DIM, 255), AL_RIGHT, "%d/%d", *s + 1, n);
    int id = ids[*s];
    int hy = y0 + rows * rh + 12;
    draw_text_wrap(F_SMALL, 20, hy, col(C_DIM, 255), 600, 16, g_param_info[id].help);
    const char *mh = ef_param_mode_help(id, (int)get_param(a, P_MODE));
    if (mh) draw_text_wrap(F_SMALL, 20, hy + 34, col(C_ORANGE, 255), 600, 16, mh);
    static const char *const chips[] = {G_LEFTRIGHT, "ADJUST", "L2", "FINE", "Y", "LOCK", "X", "DICE", "L1/R1", "PAGE", "B", "BACK"};
    hints(a, chips, 6);
}

static void draw_settings(App *a) {
    top_bar(a, "SETTINGS", "saved automatically");
    static const char *const names[SETTINGS_N] = {"MAX GRAINS", "ECO MODE", "SWAP A / B", "RESTORE SESSION", "SHOW CPU", "VISUALISER"};
    static const char *const help[SETTINGS_N] = {
        "Upper limit of simultaneous grains. Lower it if the CPU goes above ~70%.",
        "Cheaper interpolation, no shimmer and no reverb modulation: saves CPU.",
        "Swap confirm/back if your device reports A and B the other way round.",
        "Reload the last session (sample + sound) at launch.",
        "Show the audio CPU load in the top bar.",
        "Main screen view. SELECT on the main screen cycles through them too.",
    };
    EfSettings *s = &a->S;
    char vals[SETTINGS_N][32];
    snprintf(vals[0], 32, "%d", s->max_voices);
    snprintf(vals[1], 32, "%s", s->eco ? "ON" : "OFF");
    snprintf(vals[2], 32, "%s", s->swap_ab ? "SWAPPED" : "NORMAL");
    snprintf(vals[3], 32, "%s", s->autoload_session ? "ON" : "OFF");
    snprintf(vals[4], 32, "%s", s->show_cpu ? "ON" : "OFF");
    snprintf(vals[5], 32, "%s", g_vis_names[s->visualiser % VIS_COUNT]);
    int sel = a->sel[SCR_SETTINGS];
    for (int i = 0; i < SETTINGS_N; i++) {
        int y = 40 + i * 34;
        Col tc = list_row(10, y, 620, 32, i == sel);
        draw_text_spaced(F_TINY, 22, y + 10, tc, AL_LEFT, 2, names[i]);
        draw_text(F_BODY, 618, y + 6, tc, AL_RIGHT, vals[i]);
    }
    draw_text_wrap(F_SMALL, 20, 260, col(C_DIM, 255), 600, 16, help[sel]);
    draw_text_spaced(F_TINY, 20, 330, col(C_FAINT, 255), AL_LEFT, 2, "YOUR FILES");
    draw_text_clipped(F_TINY, 20, 346, col(C_DIM, 255), 600, g_paths.data_dir);
    static const char *const chips[] = {G_UPDOWN, "SELECT", G_LEFTRIGHT, "CHANGE", "B", "BACK"};
    hints(a, chips, 3);
}

static void draw_samples(App *a) {
    top_bar(a, "SAMPLES", NULL);
    static const char *const tabs[2] = {"FACTORY", "MY SAMPLES"};
    for (int i = 0; i < 2; i++) {
        int x = 14 + i * 130;
        bool cur = i == a->tab;
        draw_text_spaced(F_TINY, x, 36, cur ? col(C_TEXT, 255) : col(C_FAINT, 255), AL_LEFT, 3, tabs[i]);
        if (cur) draw_fill(x, 50, draw_text_width(F_TINY, tabs[i]) + 3 * ((int)strlen(tabs[i]) - 1), 2, col(C_TEXT, 255));
    }
    draw_text(F_TINY, 290, 36, col(C_FAINT, 255), AL_LEFT, G_LEFTRIGHT " / L1 R1");
    int *s = &a->sel[SCR_SAMPLES];
    const int lx = 10, lw = 300, rows = 11, rh = 34, y0 = 60;
    if (a->nfiles == 0) {
        draw_text_wrap(F_SMALL, 20, 70, col(C_TEXT, 255), 290, 18, a->tab == 1 ? "No samples yet." : "Factory samples are missing from the install.");
        if (a->tab == 1) draw_text_wrap(F_SMALL, 20, 96, col(C_DIM, 255), 290, 18,
                                        "Copy WAV files (mono or stereo, 16/24/32-bit or float, any rate) onto the SD card, into the folder shown below. Then open SAMPLES again.");
    }
    scroll_fit(&a->scroll[SCR_SAMPLES], *s, rows, a->nfiles);
    for (int r = 0; r < rows && a->scroll[SCR_SAMPLES] + r < a->nfiles; r++) {
        int i = a->scroll[SCR_SAMPLES] + r;
        int y = y0 + r * rh;
        bool sel = i == *s;
        Col tc = list_row(lx, y, lw, rh - 2, sel);
        bool active = !strcmp(a->files[i].path, a->sample_path);
        if (active) draw_fill(lx + 6, y + 12, 6, 6, sel ? col(C_BG, 255) : col(C_ORANGE, 255));
        draw_text_clipped(F_SMALL, lx + 18, y + 2, tc, lw - 24, a->files[i].name);
        if (a->file_secs[i] >= 0) draw_textf(F_TINY, lx + 18, y + 18, sel ? col(0x404040, 255) : col(C_DIM, 255), AL_LEFT, "%.1fs %s %s %s %dk", (double)a->file_secs[i], G_MIDDOT,
                                             a->file_ch[i] == 1 ? "mono" : (a->file_ch[i] == 2 ? "stereo" : "multi"), G_MIDDOT, a->file_rate[i] / 1000);
        else draw_text(F_TINY, lx + 18, y + 18, col(C_ERR, 255), AL_LEFT, "unsupported or damaged WAV");
    }
    /* right: waveform of the selected file and what A will do */
    const int px = 326, pw = 304, py = 60, ph = 200;
    draw_rect(px, py, pw, ph, col(C_GRID, 255));
    if (a->nfiles) {
        const char *sp = a->files[EF_CLAMP(*s, 0, a->nfiles - 1)].path;
        bool have = a->preview_sample && !strcmp(a->preview_path, sp);
        if (have) {
            draw_waveform(a->preview_sample, px + 8, py + 30, pw - 16, ph - 50, 0.0f, 1.0f, col(C_WHITE, 255), col(C_WHITE, 255));
            if (atomic_load(&a->E.preview_playing) && a->E.preview == a->preview_sample) {
                float p = atomic_load(&a->E.preview_progress);
                draw_fill(px + 8 + (int)(p * (pw - 16)), py + 26, 1, ph - 42, col(C_ORANGE, 255));
                draw_text_spaced(F_TINY, px + pw - 10, py + 8, col(C_ORANGE, 255), AL_RIGHT, 2, "LISTENING");
            } else draw_text_spaced(F_TINY, px + pw - 10, py + 8, col(C_DIM, 255), AL_RIGHT, 2, "X LISTEN");
        } else draw_text(F_TINY, px + pw / 2, py + ph / 2, col(C_FAINT, 255), AL_CENTER, G_ELLIPSIS);
        draw_text_spaced(F_TINY, px + 10, py + 8, col(C_DIM, 255), AL_LEFT, 2, "PREVIEW");
    }
    draw_text_wrap(F_SMALL, px, py + ph + 12, col(C_DIM, 255), pw, 16,
                   "A plays the sample through the current sound. Your settings stay as they are.");
    draw_text_spaced(F_TINY, px, 384, col(C_FAINT, 255), AL_LEFT, 2, "YOUR SAMPLES FOLDER");
    draw_text_wrap(F_TINY, px, 400, col(C_DIM, 255), pw, 13, g_paths.samples);
    static const char *const chips[] = {"A", "LOAD", "X", "LISTEN", "Y", "DELETE", G_LEFTRIGHT, "TAB", "B", "BACK"};
    hints(a, chips, 5);
}

static void draw_edit(App *a) {
    top_bar(a, "EDIT SAMPLE", "non-destructive until RENDER");
    EfSample *s = a->E.cur;
    const int x = 10, y = 40, w = 620, h = 220;
    draw_rect(x, y, w, h, col(C_GRID, 255));
    if (!s) { draw_text(F_BODY, 320, 150, col(C_DIM, 255), AL_CENTER, "No sample loaded"); return; }
    draw_waveform(s, x + 4, y + 10, w - 8, h - 20, a->region_a, a->region_b, col(C_WHITE, 255), col(0x3a3a3a, 255));
    float pos = a->region_a + (a->region_b - a->region_a) * get_param(a, P_POSITION);
    float mk[3] = {a->region_a, a->region_b, pos};
    static const char *const names[3] = {"START", "END", "POSITION"};
    static const uint32_t mc[3] = {C_BLUE, C_GREEN, C_ORANGE};
    for (int i = 0; i < 3; i++) {
        int mx = x + 4 + (int)(mk[i] * (w - 8));
        bool sel = a->edit_marker == i;
        Col c = col(mc[i], sel ? 255 : 120);
        draw_fill(mx - (sel ? 1 : 0), y, sel ? 3 : 1, h, c);
        int lx = EF_CLAMP(mx, x + 36, x + w - 36);
        draw_text_spaced(F_TINY, lx, y + h + 4 + (i == 2 ? 14 : 0), c, AL_CENTER, 2, names[i]);
    }
    if (atomic_load(&a->E.preview_playing) && a->E.preview == s) {
        float p = atomic_load(&a->E.preview_progress);
        int px = x + 4 + (int)((a->region_a + (a->region_b - a->region_a) * p) * (w - 8));
        draw_fill(px, y, 1, h, col(C_WHITE, 255));
    }
    float len = (a->region_b - a->region_a) * a->sample_secs;
    draw_textf(F_BIG, 20, 296, col(C_TEXT, 255), AL_LEFT, "%.2fs", (double)len);
    draw_textf(F_TINY, 180, 312, col(C_DIM, 255), AL_LEFT, "of %.2fs  %s  editing %s", (double)a->sample_secs, G_MIDDOT, names[a->edit_marker]);
    draw_textf(F_SMALL, 20, 346, a->reverse ? col(C_ORANGE, 255) : col(C_DIM, 255), AL_LEFT, "REVERSE %s (X)", a->reverse ? "ON" : "OFF");
    draw_textf(F_SMALL, 230, 346, a->normalize ? col(C_ORANGE, 255) : col(C_DIM, 255), AL_LEFT, "NORMALIZE %s (Y)", a->normalize ? "ON" : "OFF");
    draw_text_wrap(F_SMALL, 20, 374, col(C_DIM, 255), 600, 16, "Grains only play between START and END. R1 renders the region into a NEW file in your samples folder and loads it. The original file is never modified.");
    static const char *const chips[] = {G_UPDOWN, "MARKER", G_LEFTRIGHT, "MOVE", "A", "LISTEN", "R1", "RENDER", "L1", "RESET", "B", "BACK"};
    hints(a, chips, 6);
}

static void draw_presets(App *a) {
    top_bar(a, "SOUNDS", a->preset_name);
    int *s = &a->sel[SCR_PRESETS];
    *s = EF_CLAMP(*s, 0, EF_MAX(0, a->npresets - 1));
    const int rows = 12, rh = 30, y0 = 36, lw = 300;
    scroll_fit(&a->scroll[SCR_PRESETS], *s, rows, a->npresets);
    for (int r = 0; r < rows && a->scroll[SCR_PRESETS] + r < a->npresets; r++) {
        int i = a->scroll[SCR_PRESETS] + r;
        int y = y0 + r * rh;
        Col tc = list_row(10, y, lw, rh - 2, i == *s);
        draw_textf(F_TINY, 18, y + 8, i == *s ? col(0x404040, 255) : col(C_FAINT, 255), AL_LEFT, "%02d", i + 1);
        draw_text_clipped(F_SMALL, 44, y + 6, tc, lw - 90, a->presets[i].name);
        if (!a->presets[i].factory) draw_text(F_TINY, 10 + lw - 8, y + 8, i == *s ? col(C_BG, 255) : col(C_ORANGE, 255), AL_RIGHT, "MINE");
    }
    if (!a->npresets) return;
    /* details of the selected sound (cached: user sounds are files) */
    if (a->sound_cache_sel != *s) {
        EfPresetEntry *pe = &a->presets[*s];
        char err[64];
        ef_params_defaults(&a->sound_cache);
        a->sound_cache_sample[0] = 0;
        if (pe->factory) ef_preset_apply_factory(pe->factory_idx, &a->sound_cache, a->sound_cache_sample, sizeof a->sound_cache_sample);
        else ef_preset_apply_file(pe->path, &a->sound_cache, a->sound_cache_sample, sizeof a->sound_cache_sample, err, sizeof err);
        a->sound_cache_sel = *s;
    }
    const int px = 326, pw = 304;
    EfParams *P = &a->sound_cache;
    int m = EF_CLAMP((int)ef_param_get(P, P_MODE), 0, MODE_COUNT - 1);
    draw_text_spaced(F_TINY, px, 40, col(C_DIM, 255), AL_LEFT, 2, "SCENE");
    draw_text(F_BODY, px, 54, col(C_TEXT, 255), AL_LEFT, g_mode_names[m]);
    draw_text_wrap(F_TINY, px, 76, col(C_DIM, 255), pw, 13, g_mode_blurb[m]);
    draw_text_spaced(F_TINY, px, 104, col(C_DIM, 255), AL_LEFT, 2, "SAMPLE");
    draw_text_clipped(F_SMALL, px, 118, col(C_TEXT, 255), pw, a->sound_cache_sample[0] ? ef_basename(a->sound_cache_sample) : "keeps the current sample");
    static const int show[6] = {P_TEXTURE, P_SCATTER, P_HARMONY, P_SPACE, P_SHIMMER, P_DLY_MIX};
    for (int i = 0; i < 6; i++) {
        int id = show[i], y = 150 + i * 22;
        const EfParamInfo *pi = &g_param_info[id];
        float n = (ef_param_get(P, id) - pi->min) / (pi->max - pi->min);
        draw_text_spaced(F_TINY, px, y, col(C_DIM, 255), AL_LEFT, 2, pi->label);
        for (int k = 0; k < 160; k += 4) draw_fill(px + 120 + k, y + 3, 2, 6, k < n * 160 ? col(k_slot_col[i % 4], 255) : col(C_GRID, 255));
    }
    draw_text_wrap(F_SMALL, px, 296, col(C_DIM, 255), pw, 16, "A loads the sound with its sample. X puts it on the sample you are playing now.");
    static const char *const chips[] = {"A", "LOAD", "X", "ON MY SAMPLE", "Y", "SAVE CURRENT", "R1", "DELETE", "B", "BACK"};
    hints(a, chips, 5);
}

static void draw_projects(App *a) {
    top_bar(a, "PROJECTS", "sample + sound + region");
    int *s = &a->sel[SCR_PROJECTS];
    int n = a->nprojects + 1;
    const int rows = 12, rh = 32, y0 = 36;
    scroll_fit(&a->scroll[SCR_PROJECTS], *s, rows, n);
    for (int r = 0; r < rows && a->scroll[SCR_PROJECTS] + r < n; r++) {
        int i = a->scroll[SCR_PROJECTS] + r;
        int y = y0 + r * rh;
        Col tc = list_row(10, y, 620, rh - 2, i == *s);
        if (i == 0) draw_text_spaced(F_TINY, 22, y + 9, i == *s ? tc : col(C_ORANGE, 255), AL_LEFT, 2, "+ SAVE CURRENT SESSION AS A NEW PROJECT");
        else draw_text_clipped(F_SMALL, 22, y + 7, tc, 590, a->projects[i - 1].name);
    }
    static const char *const chips[] = {"A", "SAVE / LOAD", "Y", "DELETE", "B", "BACK"};
    hints(a, chips, 3);
}

static void draw_text_page(App *a, const char *title, const char *text, int *scroll) {
    top_bar(a, title, NULL);
    int line = 0, y = 40;
    const char *p = text ? text : "";
    int skip = *scroll;
    int total = 0;
    for (const char *q = p; *q; q++) if (*q == '\n') total++;
    if (skip > total - 24) skip = *scroll = EF_MAX(0, total - 24);
    while (*p && y < 440) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (line >= skip) {
            char buf[160];
            size_t l = EF_MIN(len, sizeof buf - 1);
            memcpy(buf, p, l); buf[l] = 0;
            if (buf[0] == '#') { draw_fill(14, y + 4, 5, 5, col(C_ORANGE, 255)); draw_text_spaced(F_TINY, 26, y + 1, col(C_TEXT, 255), AL_LEFT, 2, buf + 1); }
            else {
                Col c = col(0xc8c8c8, 255);
                if (buf[0] == '!') { c = col(C_ERR, 255); buf[0] = ' '; }
                else if (buf[0] == '~') { c = col(C_DIM, 255); buf[0] = ' '; }
                draw_text_clipped(F_SMALL, 18, y, c, 604, buf);
            }
            y += 16;
        }
        line++;
        if (!nl) break;
        p = nl + 1;
    }
}

static const char *k_help_text =
    "#START\n"
    "Press A to play. Every sound is made of tiny slices of a sample (grains)\n"
    "that overlap, drift and bloom into an ambient texture.\n"
    "Try the SOUNDS (START > SOUNDS) - each one is a finished patch.\n"
    "\n"
    "#MAIN SCREEN\n"
    "Left/Right  pick a knob          Up/Down  turn it (hold = faster)\n"
    "L1 / R1     knob page: GRAIN, MOTION, SPACE, COLOUR\n"
    "A play/stop   X dice (random sound)   B undo dice   Y next scene\n"
    "L2 freeze     R2 samples    SELECT next visualiser    START menu\n"
    "SELECT+START quit (your session is saved automatically)\n"
    "\n"
    "#ANALOG STICKS = EFFECTS\n"
    "Left stick   left/right TONE (dark - bright), up/down REVERB\n"
    "Right stick  left/right ECHO, up/down SHIMMER\n"
    "Push to move the effect, let go and it stays there.\n"
    "\n"
    "#SCENES (Y)\n"
    "CLOUD   long overlapping grains melt into a pad\n"
    "DUST    tiny glinting specks scattered in space\n"
    "TAPE    three tape loops at half, normal and double speed\n"
    "BLOOM   slow swells with octave and fifth overtones\n"
    "PULSE   soft notes on a slow euclidean clock\n"
    "DRIFT   everything wanders slowly and keeps evolving\n"
    "\n"
    "#KNOBS\n"
    "SIZE grain length  DENSITY grains per second  SCATTER spread\n"
    "POSITION where in the sample  DRIFT wander  EVOLVE self-animation\n"
    "HARMONY octave/fifth layers  TAPE warble and warmth  WIDTH stereo\n"
    "\n"
    "#YOUR OWN SAMPLES\n"
    "Copy WAV files to the SD card into ports/echofield/userdata/samples\n"
    "(the exact folder is shown in SAMPLES). Mono or stereo, any rate.\n"
    "Sustained sounds work best: pads, voices, strings, field recordings.\n"
    "\n"
    "#VISUALISERS (SELECT)\n"
    "ORBIT     the sample as a ring; grains orbit it, coloured by pitch:\n"
    "~          green = played pitch, white = octave up, blue = below,\n"
    "~          orange = a fifth. The inner ring is the spectrum.\n"
    "FIELD     the sample as a dot matrix with the grains on top\n"
    "SPECTRUM  dot-matrix analyser of the output\n"
    "SCOPE     stereo XY scope: wide figure = wide stereo\n";

static void draw_menu(App *a) {
    draw_fill(0, 0, SCREEN_W, SCREEN_H, col(0x000000, 215));
    int n = EF_ARRAY_LEN(k_menu);
    int x = 60, w = 520, rh = 34, y0 = 48;
    draw_text_spaced(F_TINY, x, 24, col(C_DIM, 255), AL_LEFT, 3, "MENU");
    for (int i = 0; i < n; i++) {
        bool sel = a->sel[SCR_MENU] == i;
        int y = y0 + i * rh;
        Col tc = list_row(x, y, w, rh - 4, sel);
        draw_textf(F_TINY, x + 10, y + 9, sel ? col(0x404040, 255) : col(C_FAINT, 255), AL_LEFT, "%02d", i + 1);
        draw_text(F_BODY, x + 40, y + 5, tc, AL_LEFT, k_menu[i]);
        draw_text_clipped(F_TINY, x + 210, y + 9, sel ? col(0x404040, 255) : col(C_DIM, 255), w - 220, k_menu_desc[i]);
    }
    static const char *const chips[] = {G_UPDOWN, "SELECT", "A", "OPEN", "B", "CLOSE"};
    hints(a, chips, 3);
}

static void draw_overlays(App *a) {
    if (a->toast[0] && ef_now_s() < a->toast_until) {
        int w = EF_MIN(600, draw_text_width(F_SMALL, a->toast) + 32);
        int x = (SCREEN_W - w) / 2, y = a->scr == SCR_MAIN ? 40 : SCREEN_H - 60;
        draw_fill(x, y, w, 24, col(C_WHITE, 245));
        draw_fill(x, y, 4, 24, a->toast_col.r == 0xe8 && a->toast_col.g == 0xe8 ? col(C_ORANGE, 255) : a->toast_col);
        draw_text_clipped(F_SMALL, x + 16, y + 4, col(C_BG, 255), w - 26, a->toast);
    }
    if (a->confirm_open) {
        draw_fill(0, 0, SCREEN_W, SCREEN_H, col(0x000000, 200));
        int x = 90, y = 150, w = 460, h = 160;
        draw_fill(x, y, w, h, col(C_WHITE, 255));
        draw_fill(x, y, w, 4, col(C_ORANGE, 255));
        draw_text_spaced(F_TINY, x + 18, y + 16, col(0x606060, 255), AL_LEFT, 2, "CONFIRM");
        draw_text_wrap(F_BODY, x + 18, y + 40, col(C_BG, 255), w - 36, 21, a->confirm_msg);
        const char *yes = a->S.swap_ab ? "B" : "A", *no = a->S.swap_ab ? "A" : "B";
        int bx = x + 18, by = y + h - 32;
        draw_fill(bx, by, 22, 18, col(C_BG, 255)); draw_text(F_TINY, bx + 7, by + 3, col(C_WHITE, 255), AL_LEFT, yes);
        bx += 28 + draw_text(F_SMALL, bx + 28, by + 1, col(C_BG, 255), AL_LEFT, "YES") + 24;
        draw_fill(bx, by, 22, 18, col(C_BG, 255)); draw_text(F_TINY, bx + 7, by + 3, col(C_WHITE, 255), AL_LEFT, no);
        draw_text(F_SMALL, bx + 28, by + 1, col(C_BG, 255), AL_LEFT, "NO");
    }
}

void ui_frame(App *a, double dt) {
    draw_blend(SDL_BLENDMODE_BLEND);
    draw_fill(0, 0, SCREEN_W, SCREEN_H, col(C_BG, 255));
    EfScreen s = a->scr == SCR_MENU ? a->menu_return : a->scr;
    switch (s) {
    case SCR_SAMPLES: draw_samples(a); break;
    case SCR_EDIT: draw_edit(a); break;
    case SCR_GRAIN: draw_param_page(a, "GRAIN", "every grain control"); break;
    case SCR_FX: draw_param_page(a, "EFFECTS", "tape > filter > saturate > echo > reverb"); break;
    case SCR_ORACLE: draw_param_page(a, "DRIFT ORACLE", "generative harmony"); break;
    case SCR_SETTINGS: draw_settings(a); break;
    case SCR_PRESETS: draw_presets(a); break;
    case SCR_PROJECTS: draw_projects(a); break;
    case SCR_DIAG: draw_text_page(a, "DIAGNOSTICS", a->diag_text, &a->scroll[SCR_DIAG]);
        { static const char *const c[] = {G_UPDOWN, "SCROLL", "X", "REFRESH", "A", "SAVE REPORT", "B", "BACK"}; hints(a, c, 4); } break;
    case SCR_HELP: draw_text_page(a, "HELP", k_help_text, &a->scroll[SCR_HELP]);
        { static const char *const c[] = {G_UPDOWN, "SCROLL", "B", "BACK"}; hints(a, c, 2); } break;
    default: draw_main(a); break;
    }
    if (a->scr == SCR_MENU) draw_menu(a);
    draw_overlays(a);
    (void)dt;
}
