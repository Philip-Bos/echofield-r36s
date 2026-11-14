/* ECHOFIELD - presets, projects, settings and the constrained randomizer.
 * File format: UTF-8 text, "key=value" lines, '#' comments, a [params] section.
 * Every file carries format= and version= so future releases can migrate. */
#include "store.h"

/* ---------------------------------------------------------------- factory presets */
typedef struct { const char *name; const char *sample; const char *body; } Factory;   /* sample: file in assets/samples */
static const Factory k_factory[] = {
    {"Felt Clouds", "felt_piano.wav", "mode=0\ntexture=0.78\nscatter=0.50\ndrift=0.35\nmotion=0.30\nspace=0.72\nharmony=0.35\nshimmer=0.25\ndly_mix=0.20\ncutoff=0.80\nwidth=0.90\n"},
    {"Glass Choir", "bowed_glass.wav", "mode=3\ntexture=0.70\nscatter=0.40\ndrift=0.30\nmotion=0.35\nspace=0.80\nharmony=0.50\nshimmer=0.45\nrev_size=0.90\nwidth=1.00\ncutoff=0.78\n"},
    {"Dust Lights", "music_box.wav", "mode=1\ntexture=0.45\nscatter=0.70\ndrift=0.30\nmotion=0.40\nspace=0.80\nharmony=0.30\nshimmer=0.30\ndly_mix=0.32\ndly_time=640\nwidth=1.00\n"},
    {"Warm Tide", "warm_pad.wav", "mode=5\ntexture=0.70\nscatter=0.45\ndrift=0.55\nmotion=0.40\nspace=0.70\ntape=0.25\ncutoff=0.70\nharmony=0.25\n"},
    {"Slow Tapes", "string_swell.wav", "mode=2\ntexture=0.60\nscatter=0.25\ndrift=0.40\nmotion=0.35\nspace=0.62\ntape=0.50\nharmony=0.15\ntape_wow=0.6\ncutoff=0.75\n"},
    {"Deep Water", "sub_bloom.wav", "mode=0\ntexture=0.85\nscatter=0.40\ndrift=0.40\nmotion=0.25\nspace=0.80\ntape=0.30\ncutoff=0.55\nharmony=0.40\nshimmer=0.15\n"},
    {"Rain Cathedral", "rain_field.wav", "mode=0\ntexture=0.88\nscatter=0.60\ndrift=0.40\nmotion=0.30\nspace=0.90\nharmony=0.45\nshimmer=0.50\nrev_size=0.95\ncutoff=0.72\n"},
    {"Kalimba Steps", "kalimba_drops.wav", "mode=4\ntexture=0.40\nscatter=0.60\ndrift=0.30\nmotion=0.30\nspace=0.62\ndly_mix=0.35\ndly_time=750\ndly_fb=0.55\nharmony=0.20\n"},
    {"Hymn", "vox_choir.wav", "mode=0\ntexture=0.85\nscatter=0.45\ndrift=0.35\nmotion=0.30\nspace=0.78\nharmony=0.40\nshimmer=0.32\nwidth=0.95\n"},
    {"Breath Choir", "breath_choir.wav", "mode=3\ntexture=0.75\nscatter=0.50\ndrift=0.40\nmotion=0.40\nspace=0.75\nharmony=0.35\nshimmer=0.25\n"},
    {"Night Current", "low_drone.wav", "mode=5\ntexture=0.65\nscatter=0.40\ndrift=0.60\nmotion=0.40\npitch=-12\nspace=0.60\ntape=0.30\nwidth=0.70\noracle_on=1\noracle_scale=1\noracle_activity=0.30\noracle_drone=1\noracle_glide=0.7\n"},
    {"Glass Rain", "glass_tines.wav", "mode=1\ntexture=0.35\nscatter=0.55\ndrift=0.20\nmotion=0.50\npitch=12\nspace=0.70\nwidth=0.95\nreverse=0.20\nshimmer=0.35\ndly_mix=0.25\ndly_fb=0.45\ndly_time=280\ngrain_tone=0.40\n"},
    {"Broken Constellation", "wire_plucks.wav", "mode=3\ntexture=0.60\nscatter=0.70\ndrift=0.50\nmotion=0.50\nspace=0.75\ntape=0.10\nwidth=1.00\nshimmer=0.50\noracle_on=1\noracle_scale=4\noracle_activity=0.40\noracle_ambig=0.35\n"},
    {"Distant Broadcast", "dust_rhythm.wav", "mode=2\ntexture=0.50\nscatter=0.20\ndrift=0.40\nmotion=0.40\nspace=0.55\ntape=0.80\nwidth=0.60\ncutoff=0.70\nreso=0.30\ntape_age=0.70\ntape_noise=0.35\ntape_wow=0.70\n"},
    {"Init", "", ""},
};
const int g_factory_count = EF_ARRAY_LEN(k_factory);
const char *ef_factory_name(int i) { return (i >= 0 && i < g_factory_count) ? k_factory[i].name : "?"; }

/* ---------------------------------------------------------------- parsing helpers */
typedef void (*kv_fn)(void *ctx, const char *section, const char *key, const char *val);
static void parse_kv(const char *text, kv_fn fn, void *ctx) {
    char section[32] = "";
    const char *p = text;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char line[512];
        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, p, len); line[len] = 0;
        ef_trim(line);
        if (line[0] && line[0] != '#' && line[0] != ';') {
            if (line[0] == '[') {
                char *e = strchr(line, ']');
                if (e) { *e = 0; ef_strlcpy(section, line + 1, sizeof section); }
            } else {
                char *eq = strchr(line, '=');
                if (eq) { *eq = 0; char *k = line, *v = eq + 1; ef_trim(k); ef_trim(v); fn(ctx, section, k, v); }
            }
        }
        p = nl ? nl + 1 : NULL;
    }
}

typedef struct { EfParams *p; int n; } ParamCtx;
static void param_kv(void *vctx, const char *section, const char *k, const char *v) {
    ParamCtx *c = vctx;
    if (section[0] && strcmp(section, "params")) return;
    int id = ef_param_find(k);
    if (id < 0 || id == P_PLAY) return;    /* unknown keys are ignored (forward compatible) */
    char *end;
    double d = strtod(v, &end);
    if (end == v) return;
    ef_param_set(c->p, id, (float)d);
    c->n++;
}

int ef_parse_params(const char *text, EfParams *p, bool reset_defaults) {
    if (reset_defaults) {
        float play = ef_param_get(p, P_PLAY);
        ef_params_defaults(p);
        ef_param_set(p, P_PLAY, play);
    }
    ParamCtx c = {p, 0};
    parse_kv(text, param_kv, &c);
    return c.n;
}

static void append(char **buf, size_t *len, size_t *cap, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static void append(char **buf, size_t *len, size_t *cap, const char *fmt, ...) {
    char tmp[1024];
    va_list ap; va_start(ap, fmt); int n = vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof tmp) n = sizeof tmp - 1;
    if (*len + (size_t)n + 1 > *cap) {
        size_t nc = (*cap + (size_t)n + 1) * 2;
        char *nb = realloc(*buf, nc);
        if (!nb) return;
        *buf = nb; *cap = nc;
    }
    memcpy(*buf + *len, tmp, (size_t)n + 1);
    *len += (size_t)n;
}

static void append_params(char **b, size_t *l, size_t *c, const EfParams *p) {
    append(b, l, c, "[params]\n");
    for (int i = 0; i < P_COUNT; i++) {
        if (i == P_PLAY) continue;
        append(b, l, c, "%s=%.4f\n", g_param_info[i].key, (double)ef_param_get((EfParams *)p, i));
    }
}

/* ---------------------------------------------------------------- presets */
bool ef_preset_apply_factory(int idx, EfParams *p, char *sample_ref, size_t n) {
    if (idx < 0 || idx >= g_factory_count) return false;
    ef_parse_params(k_factory[idx].body, p, true);
    if (sample_ref && n) {
        if (k_factory[idx].sample[0]) ef_path_join(sample_ref, n, g_paths.factory_samples, k_factory[idx].sample);
        else sample_ref[0] = 0;
    }
    return true;
}

typedef struct { char *sample; size_t n; int version; char format[48]; } MetaCtx;
static void meta_kv(void *vctx, const char *section, const char *k, const char *v) {
    MetaCtx *m = vctx;
    if (section[0]) return;
    if (!strcmp(k, "sample") && m->sample) ef_strlcpy(m->sample, v, m->n);
    if (!strcmp(k, "version")) m->version = atoi(v);
    if (!strcmp(k, "format")) ef_strlcpy(m->format, v, sizeof m->format);
}

bool ef_preset_apply_file(const char *path, EfParams *p, char *sample_ref, size_t n, char *err, size_t en) {
    char *txt = ef_read_text_file(path, 256 * 1024);
    if (!txt) { snprintf(err, en, "cannot read preset"); return false; }
    MetaCtx m = {sample_ref, n, 0, ""};
    if (sample_ref && n) sample_ref[0] = 0;
    parse_kv(txt, meta_kv, &m);
    if (strcmp(m.format, "echofield-preset")) { free(txt); snprintf(err, en, "not an ECHOFIELD preset"); return false; }
    if (m.version > EF_PRESET_FORMAT) ef_log("preset %s is from a newer version (%d); loading known values", path, m.version);
    ef_parse_params(txt, p, true);
    free(txt);
    return true;
}

bool ef_preset_save(const EfParams *p, const char *name, const char *sample_path, char *out_path, size_t n) {
    char *b = NULL; size_t l = 0, c = 0;
    char ts[32]; ef_timestamp(ts, sizeof ts, "human");
    append(&b, &l, &c, "# ECHOFIELD preset\nformat=echofield-preset\nversion=%d\nname=%s\ncreated=%s\nsample=%s\n",
           EF_PRESET_FORMAT, name, ts, sample_path ? sample_path : "");
    append_params(&b, &l, &c, p);
    if (!b) return false;
    char fname[96], path[EF_PATH_MAX];
    snprintf(fname, sizeof fname, "%s.efpreset", name);
    for (char *q = fname; *q; q++) if (*q == '/' || *q == '\\' || *q == ':') *q = '_';
    ef_path_join(path, sizeof path, g_paths.presets, fname);
    bool ok = ef_write_text_file_atomic(path, b);
    free(b);
    if (ok && out_path) ef_strlcpy(out_path, path, n);
    return ok;
}

static void name_kv(void *vctx, const char *section, const char *k, const char *v) {
    if (!section[0] && !strcmp(k, "name")) ef_strlcpy(vctx, v, 64);
}

int ef_presets_list(EfPresetEntry *out, int max) {
    int n = 0;
    for (int i = 0; i < g_factory_count && n < max; i++) {
        memset(&out[n], 0, sizeof out[n]);
        ef_strlcpy(out[n].name, k_factory[i].name, sizeof out[n].name);
        out[n].factory = true; out[n].factory_idx = i;
        n++;
    }
    EfFileEntry *files = malloc(sizeof(EfFileEntry) * 256);
    if (!files) return n;
    int nf = ef_list_files(g_paths.presets, ".efpreset", files, 256);
    for (int i = 0; i < nf && n < max; i++) {
        memset(&out[n], 0, sizeof out[n]);
        ef_strlcpy(out[n].path, files[i].path, sizeof out[n].path);
        out[n].factory_idx = -1;
        char *txt = ef_read_text_file(files[i].path, 256 * 1024);
        if (txt) { parse_kv(txt, name_kv, out[n].name); free(txt); }
        if (!out[n].name[0]) ef_strlcpy(out[n].name, files[i].name, sizeof out[n].name);
        n++;
    }
    free(files);
    return n;
}

/* ---------------------------------------------------------------- projects */
bool ef_project_save(const char *path, const EfProject *pr, const EfParams *p) {
    char *b = NULL; size_t l = 0, c = 0;
    char ts[32]; ef_timestamp(ts, sizeof ts, "human");
    append(&b, &l, &c, "# ECHOFIELD project\nformat=echofield-project\nversion=%d\nname=%s\nsaved=%s\nsample=%s\n",
           EF_PROJECT_FORMAT, pr->name, ts, pr->sample_path);
    append(&b, &l, &c, "region_a=%.6f\nregion_b=%.6f\nreverse=%d\nnormalize=%d\nlocks=", (double)pr->region_a, (double)pr->region_b, pr->reverse, pr->normalize);
    bool first = true;
    for (int i = 0; i < P_COUNT; i++) if (pr->locks[i]) { append(&b, &l, &c, "%s%s", first ? "" : ",", g_param_info[i].key); first = false; }
    append(&b, &l, &c, "\n");
    append_params(&b, &l, &c, p);
    if (!b) return false;
    bool ok = ef_write_text_file_atomic(path, b);
    free(b);
    return ok;
}

typedef struct { EfProject *pr; char format[48]; int version; } ProjCtx;
static void proj_kv(void *vctx, const char *section, const char *k, const char *v) {
    ProjCtx *c = vctx;
    if (section[0]) return;
    EfProject *pr = c->pr;
    if (!strcmp(k, "format")) ef_strlcpy(c->format, v, sizeof c->format);
    else if (!strcmp(k, "version")) c->version = atoi(v);
    else if (!strcmp(k, "name")) ef_strlcpy(pr->name, v, sizeof pr->name);
    else if (!strcmp(k, "sample")) ef_strlcpy(pr->sample_path, v, sizeof pr->sample_path);
    else if (!strcmp(k, "region_a")) pr->region_a = (float)strtod(v, NULL);
    else if (!strcmp(k, "region_b")) pr->region_b = (float)strtod(v, NULL);
    else if (!strcmp(k, "reverse")) pr->reverse = atoi(v) != 0;
    else if (!strcmp(k, "normalize")) pr->normalize = atoi(v) != 0;
    else if (!strcmp(k, "locks")) {
        char tmp[1024]; ef_strlcpy(tmp, v, sizeof tmp);
        char *s = tmp;
        while (s && *s) {
            char *comma = strchr(s, ',');
            if (comma) *comma = 0;
            ef_trim(s);
            int id = ef_param_find(s);
            if (id >= 0) pr->locks[id] = true;
            s = comma ? comma + 1 : NULL;
        }
    }
}

bool ef_project_load(const char *path, EfProject *pr, EfParams *p, char *err, size_t en) {
    char *txt = ef_read_text_file(path, 512 * 1024);
    if (!txt) { snprintf(err, en, "cannot read project file"); return false; }
    memset(pr, 0, sizeof *pr);
    pr->region_a = 0.0f; pr->region_b = 1.0f;
    ProjCtx c = {pr, "", 0};
    parse_kv(txt, proj_kv, &c);
    if (strcmp(c.format, "echofield-project")) { free(txt); snprintf(err, en, "not an ECHOFIELD project"); return false; }
    /* migration hook: version 1 is the first format; newer files load known keys only */
    if (c.version > EF_PROJECT_FORMAT) ef_log("project %s has newer format %d", path, c.version);
    pr->region_a = EF_CLAMP(pr->region_a, 0.0f, 1.0f);
    pr->region_b = EF_CLAMP(pr->region_b, 0.0f, 1.0f);
    if (pr->region_b - pr->region_a < 0.001f) { pr->region_a = 0; pr->region_b = 1; }
    ef_parse_params(txt, p, true);
    free(txt);
    return true;
}

/* ---------------------------------------------------------------- settings */
void ef_settings_defaults(EfSettings *s) {
    memset(s, 0, sizeof *s);
    s->input_gain_db = 0.0f;
    s->monitor = false;          /* off by default: avoids feedback loops */
    s->monitor_gain = 0.8f;
    s->max_voices = 40;
    s->eco = false;
    s->swap_ab = false;
    s->rec_max_seconds = 120;
    s->rec_normalize = false;
    s->autoload_session = true;
    s->show_cpu = false;
    s->visualiser = 0;
}

static void settings_kv(void *vctx, const char *section, const char *k, const char *v) {
    EfSettings *s = vctx;
    if (!strcmp(k, "input_key")) ef_strlcpy(s->input_key, v, sizeof s->input_key);
    else if (!strcmp(k, "input_gain_db")) s->input_gain_db = EF_CLAMP((float)strtod(v, NULL), -12.0f, 30.0f);
    else if (!strcmp(k, "monitor")) s->monitor = atoi(v) != 0;
    else if (!strcmp(k, "monitor_gain")) s->monitor_gain = EF_CLAMP((float)strtod(v, NULL), 0.0f, 1.5f);
    else if (!strcmp(k, "max_voices")) s->max_voices = EF_CLAMP(atoi(v), 8, 96);
    else if (!strcmp(k, "eco")) s->eco = atoi(v) != 0;
    else if (!strcmp(k, "swap_ab")) s->swap_ab = atoi(v) != 0;
    else if (!strcmp(k, "rec_max_seconds")) s->rec_max_seconds = EF_CLAMP(atoi(v), 10, 600);
    else if (!strcmp(k, "rec_normalize")) s->rec_normalize = atoi(v) != 0;
    else if (!strcmp(k, "autoload_session")) s->autoload_session = atoi(v) != 0;
    else if (!strcmp(k, "show_cpu")) s->show_cpu = atoi(v) != 0;
    else if (!strcmp(k, "visualiser")) s->visualiser = EF_CLAMP(atoi(v), 0, 7);
    (void)section;
}

bool ef_settings_load(EfSettings *s, const char *path) {
    ef_settings_defaults(s);
    char *txt = ef_read_text_file(path, 64 * 1024);
    if (!txt) return false;
    parse_kv(txt, settings_kv, s);
    free(txt);
    return true;
}

bool ef_settings_save(const EfSettings *s, const char *path) {
    char buf[1024];
    snprintf(buf, sizeof buf,
             "# ECHOFIELD settings\nformat=echofield-settings\nversion=1\ninput_key=%s\ninput_gain_db=%.1f\nmonitor=%d\nmonitor_gain=%.2f\n"
             "max_voices=%d\neco=%d\nswap_ab=%d\nrec_max_seconds=%d\nrec_normalize=%d\nautoload_session=%d\nshow_cpu=%d\nvisualiser=%d\n",
             s->input_key, (double)s->input_gain_db, s->monitor, (double)s->monitor_gain, s->max_voices, s->eco, s->swap_ab,
             s->rec_max_seconds, s->rec_normalize, s->autoload_session, s->show_cpu, s->visualiser);
    return ef_write_text_file_atomic(path, buf);
}

/* ---------------------------------------------------------------- randomizer */
static float rr(EfRng *r, float lo, float hi) { return lo + (hi - lo) * ef_rng_f(r); }
static float tri(EfRng *r, float c, float spread, float lo, float hi) { return EF_CLAMP(c + ef_rng_tri(r) * spread, lo, hi); }
static float skew_low(EfRng *r, float lo, float hi) { float u = ef_rng_f(r); return lo + (hi - lo) * u * u; }

void ef_randomize(EfParams *p, const bool locks[P_COUNT], EfRng *r) {
    float v[P_COUNT];
    for (int i = 0; i < P_COUNT; i++) v[i] = ef_param_get(p, i);
    static const float pitches[] = {0, 0, 0, 0, -12, -12, 12, 12, -5, 7, 7, -7, 5, 19, -24};
    v[P_POSITION] = rr(r, 0.05f, 0.95f);
    v[P_TEXTURE] = tri(r, 0.65f, 0.4f, 0.15f, 1.0f);
    v[P_SCATTER] = tri(r, 0.35f, 0.4f, 0.03f, 0.85f);
    v[P_DRIFT] = skew_low(r, 0.0f, 0.75f);
    v[P_MOTION] = skew_low(r, 0.0f, 0.85f);
    v[P_PITCH] = pitches[ef_rng_u32(r) % EF_ARRAY_LEN(pitches)];
    v[P_SPACE] = rr(r, 0.45f, 0.9f);
    v[P_TAPE] = skew_low(r, 0.0f, 0.75f);
    v[P_WIDTH] = rr(r, 0.6f, 1.0f);
    v[P_HARMONY] = skew_low(r, 0.0f, 0.7f);
    v[P_SIZE_MUL] = tri(r, 0.0f, 1.0f, -1.5f, 1.5f);
    v[P_DENS_MUL] = tri(r, 0.0f, 1.0f, -1.5f, 1.5f);
    v[P_SCAN] = ef_rng_f(r) < 0.7f ? 0.0f : tri(r, 0.0f, 0.35f, -0.5f, 0.5f);
    v[P_REVERSE] = skew_low(r, 0.0f, 0.45f);
    v[P_PITCH_RAND] = ef_rng_f(r) < 0.7f ? 0.0f : skew_low(r, 0.1f, 3.0f);
    v[P_SIZE_RAND] = rr(r, 0.0f, 0.6f);
    v[P_WINDOW] = ef_rng_f(r) < 0.6f ? 0.0f : (float)(1 + ef_rng_u32(r) % 5);
    v[P_GRAIN_TONE] = tri(r, 0.0f, 0.5f, -0.7f, 0.7f);
    v[P_CUTOFF] = 0.95f - skew_low(r, 0.0f, 0.4f);
    v[P_RESO] = skew_low(r, 0.0f, 0.6f);
    v[P_DRIVE] = skew_low(r, 0.0f, 0.5f);
    v[P_DLY_TIME] = rr(r, 120.0f, 900.0f);
    v[P_DLY_FB] = rr(r, 0.1f, 0.6f);
    v[P_DLY_MIX] = skew_low(r, 0.0f, 0.4f);
    v[P_REV_SIZE] = rr(r, 0.3f, 1.0f);
    v[P_REV_DAMP] = rr(r, 0.2f, 0.8f);
    v[P_SHIMMER] = ef_rng_f(r) < 0.4f ? 0.0f : rr(r, 0.1f, 0.55f);
    v[P_TAPE_WOW] = rr(r, 0.1f, 0.9f);
    v[P_TAPE_FLUTTER] = rr(r, 0.0f, 0.7f);
    v[P_TAPE_AGE] = rr(r, 0.0f, 0.8f);
    v[P_OR_ACTIVITY] = rr(r, 0.1f, 0.7f);
    v[P_OR_AMBIG] = skew_low(r, 0.0f, 0.6f);
    v[P_OR_GLIDE] = rr(r, 0.1f, 0.8f);
    /* musical constraints: never land on near-silence or mud */
    if ((int)v[P_FILTER_TYPE] == 0 && v[P_CUTOFF] < 0.45f) v[P_CUTOFF] = 0.45f;
    if (v[P_SIZE_MUL] + v[P_DENS_MUL] < -1.8f) v[P_DENS_MUL] = -1.8f - v[P_SIZE_MUL];
    if (v[P_DLY_FB] > 0.5f && v[P_DLY_MIX] > 0.3f) v[P_DLY_FB] = 0.5f;
    for (int i = 0; i < P_COUNT; i++) {
        if (!g_param_info[i].randomizable || (locks && locks[i])) continue;
        ef_param_set(p, i, v[i]);
    }
}

void ef_random_name(EfRng *r, char *out, size_t n) {
    static const char *const adj[] = {"Amber", "Hollow", "Quiet", "Static", "Velvet", "Iron", "Paper", "Salt", "Low",
                                      "Silver", "Ash", "Drowned", "Lunar", "Rust", "Faint", "Opal", "Cold", "Slow"};
    static const char *const noun[] = {"Harbor", "Signal", "Orchard", "Lantern", "Relay", "Tide", "Choir", "Engine",
                                       "Meadow", "Archive", "Static", "Garden", "Wire", "Comet", "Shore", "Field"};
    snprintf(out, n, "%s %s %02u", adj[ef_rng_u32(r) % EF_ARRAY_LEN(adj)], noun[ef_rng_u32(r) % EF_ARRAY_LEN(noun)], ef_rng_u32(r) % 100);
}
