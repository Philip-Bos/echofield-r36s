/* ECHOFIELD - experimental granular sampler for the R36S (ArkOS / PortMaster)
 *
 * usage: echofield [--windowed] [--data DIR] [--diag] [--bench SECONDS] [--screenshots DIR]
 *        echofield --render OUT.wav SECONDS SOUND   (offline render of factory sound SOUND)
 */
#include "app.h"

static volatile int g_signal_quit;
static void on_signal(int sig) { (void)sig; g_signal_quit = 1; }

static App g_app;

static int map_button(int b) {
    switch (b) {
    case SDL_CONTROLLER_BUTTON_A: return B_A;
    case SDL_CONTROLLER_BUTTON_B: return B_B;
    case SDL_CONTROLLER_BUTTON_X: return B_X;
    case SDL_CONTROLLER_BUTTON_Y: return B_Y;
    case SDL_CONTROLLER_BUTTON_BACK: return B_SELECT;
    case SDL_CONTROLLER_BUTTON_GUIDE: return B_SELECT;
    case SDL_CONTROLLER_BUTTON_START: return B_START;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return B_L3;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return B_R3;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return B_L1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return B_R1;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return B_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return B_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return B_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return B_RIGHT;
    }
    return -1;
}

static int map_key(int sym) {
    switch (sym) {
    case SDLK_UP: return B_UP; case SDLK_DOWN: return B_DOWN; case SDLK_LEFT: return B_LEFT; case SDLK_RIGHT: return B_RIGHT;
    case 'z': return B_A; case 'x': return B_B; case 's': return B_X; case 'a': return B_Y;
    case 'q': return B_L1; case 'w': return B_R1; case '1': return B_L2; case '2': return B_R2;
    case SDLK_RETURN: return B_START; case SDLK_RSHIFT: case SDLK_BACKSPACE: case SDLK_TAB: return B_SELECT;
    }
    return -1;
}

static void open_controller(App *a) {
    if (a->pad) return;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            a->pad = SDL_GameControllerOpen(i);
            if (a->pad) { ef_log("controller: %s", SDL_GameControllerName(a->pad)); break; }
        }
    }
    const char *kb = getenv("ECHOFIELD_KEYBOARD");
    /* with a pad present, keyboard events would double input from gptokeyb-like tools */
    a->keyboard_ok = !a->pad || (kb && kb[0] == '1');
}

static void handle_events(App *a) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: a->quit = true; break;
        case SDL_CONTROLLERDEVICEADDED: open_controller(a); break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (a->pad && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(a->pad)) == e.cdevice.which) {
                SDL_GameControllerClose(a->pad); a->pad = NULL; open_controller(a);
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP: {
            int b = map_button(e.cbutton.button);
            if (b >= 0) ui_button(a, (EfButton)b, e.type == SDL_CONTROLLERBUTTONDOWN);
            break;
        }
        case SDL_CONTROLLERAXISMOTION: {
            int ax = e.caxis.axis;
            float v = e.caxis.value / 32767.0f;
            if (ax <= SDL_CONTROLLER_AXIS_RIGHTY) a->axis[ax] = v;
            else {   /* L2/R2 are digital on the R36S but arrive as trigger axes */
                EfButton b = ax == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? B_L2 : B_R2;
                if (v > 0.6f && !a->down[b]) ui_button(a, b, true);
                else if (v < 0.3f && a->down[b]) ui_button(a, b, false);
            }
            break;
        }
        case SDL_KEYDOWN: case SDL_KEYUP: {
            if (!a->keyboard_ok) break;
            if (e.key.keysym.sym == SDLK_ESCAPE && e.type == SDL_KEYDOWN) { a->quit = true; break; }
            if (e.key.repeat) break;
            int b = map_key(e.key.keysym.sym);
            if (b >= 0) ui_button(a, (EfButton)b, e.type == SDL_KEYDOWN);
            break;
        }
        default: break;
        }
    }
}

static bool save_screenshot(App *a, const char *path) {
    uint32_t *px = malloc(SCREEN_W * SCREEN_H * 4);
    if (!px) return false;
    SDL_Rect rc = {0, 0, SCREEN_W, SCREEN_H};
    bool ok = false;
    if (SDL_RenderReadPixels(a->ren, &rc, SDL_PIXELFORMAT_ARGB8888, px, SCREEN_W * 4) == 0) {
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(px, SCREEN_W, SCREEN_H, 32, SCREEN_W * 4, SDL_PIXELFORMAT_ARGB8888);
        if (s) { ok = SDL_SaveBMP_RW(s, SDL_RWFromFile(path, "wb"), 1) == 0; SDL_FreeSurface(s); }
    }
    free(px);
    return ok;
}

static void load_default_sample(App *a, const char *preferred) {
    char p[EF_PATH_MAX];
    ef_path_join(p, sizeof p, g_paths.factory_samples, "felt_piano.wav");
    if (preferred && preferred[0] && ef_file_exists(preferred)) app_load_sample_async(a, preferred);
    else if (ef_file_exists(p)) app_load_sample_async(a, p);
    else {
        EfFileEntry f[4];
        if (ef_list_files(g_paths.factory_samples, ".wav", f, 4) > 0) app_load_sample_async(a, f[0].path);
    }
}

/* Offline render of a factory sound with its sample: checks the sound without hardware. */
static int render_offline(const char *out, float seconds, int sound) {
    static EfParams P;
    static EfEngine E;
    char ref[EF_PATH_MAX] = "";
    ef_params_defaults(&P);
    if (!ef_preset_apply_factory(sound, &P, ref, sizeof ref)) { printf("no factory sound %d\n", sound); return 1; }
    ef_param_set(&P, P_PLAY, 1);
    if (!ef_engine_init(&E, 48000, &P)) return 1;
    if (!ref[0]) ef_path_join(ref, sizeof ref, g_paths.factory_samples, "felt_piano.wav");
    EfWavInfo info;
    EfSample *s = ef_wav_load(ref, &info);
    if (!s) { printf("%s: %s\n", ref, info.error); return 1; }
    if (s->rate != 48000) { EfSample *r = ef_sample_resample(s, 48000); ef_sample_free(s); s = r; }
    ef_engine_set_sample(&E, s);
    long frames = (long)(seconds * 48000);
    float *buf = malloc(sizeof(float) * 2 * frames);
    if (!buf) return 1;
    for (long i = 0; i < frames; i += 512) ef_engine_render(&E, buf + i * 2, (int)EF_MIN(512L, frames - i));
    bool ok = ef_wav_write_interleaved(out, buf, frames, 2, 48000, 16);
    printf("%s: %s (%s, %.0f s)\n", out, ok ? "written" : "FAILED", ef_factory_name(sound), (double)seconds);
    free(buf);
    ef_engine_free(&E);
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    App *a = &g_app;
    const char *data_override = NULL, *shots = NULL;
    bool windowed = false, diag = false;
    int bench = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--windowed")) windowed = true;
        else if (!strcmp(argv[i], "--diag")) diag = true;
        else if (!strcmp(argv[i], "--bench") && i + 1 < argc) bench = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--data") && i + 1 < argc) data_override = argv[++i];
        else if (!strcmp(argv[i], "--screenshots") && i + 1 < argc) shots = argv[++i];
        else if (!strcmp(argv[i], "--version")) { printf("ECHOFIELD %s\n", EF_VERSION); return 0; }
        else if (!strcmp(argv[i], "--render") && i + 3 < argc) {
            SDL_Init(SDL_INIT_TIMER);
            ef_paths_init(NULL);
            return render_offline(argv[i + 1], (float)strtod(argv[i + 2], NULL), atoi(argv[i + 3]));
        }
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);

    if (bench > 0) { SDL_Init(SDL_INIT_TIMER); return diag_bench(bench); }

    ef_paths_init(data_override);
    char logp[EF_PATH_MAX];
    ef_path_join(logp, sizeof logp, g_paths.data_dir, "echofield.log");
    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) { printf("SDL_Init failed: %s\n", SDL_GetError()); return 1; }
    ef_log_open(g_paths.data_writable ? logp : NULL);
    ef_log("ECHOFIELD %s starting; data dir %s (%s)", EF_VERSION, g_paths.data_dir, g_paths.data_writable ? "writable" : "read-only!");

    if (diag) { SDL_InitSubSystem(SDL_INIT_AUDIO); diag_print_cli(); SDL_Quit(); return 0; }

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) { ef_log("video init failed: %s", SDL_GetError()); return 1; }
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) ef_log("controller init failed: %s", SDL_GetError());
    bool audio_sys = SDL_InitSubSystem(SDL_INIT_AUDIO) == 0;
    if (!audio_sys) ef_log("audio init failed: %s", SDL_GetError());

    a->win = SDL_CreateWindow("ECHOFIELD", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W, SCREEN_H,
                              SDL_WINDOW_SHOWN | (windowed ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP));
    if (!a->win) { ef_log("window failed: %s", SDL_GetError()); return 1; }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    a->ren = SDL_CreateRenderer(a->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    if (!a->ren) a->ren = SDL_CreateRenderer(a->win, -1, SDL_RENDERER_SOFTWARE);
    if (!a->ren) { ef_log("renderer failed: %s", SDL_GetError()); return 1; }
    SDL_RenderSetLogicalSize(a->ren, SCREEN_W, SCREEN_H);
    SDL_ShowCursor(0);
    if (!draw_init(a->ren)) return 1;
    if (SDL_RenderTargetSupported(a->ren)) {
        a->trail = SDL_CreateTexture(a->ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, SCREEN_W, 300);
        if (a->trail) {
            SDL_SetTextureBlendMode(a->trail, SDL_BLENDMODE_ADD);
            SDL_SetRenderTarget(a->ren, a->trail);
            SDL_SetRenderDrawColor(a->ren, 0, 0, 0, 255);
            SDL_RenderClear(a->ren);
            SDL_SetRenderTarget(a->ren, NULL);
        }
    }
    open_controller(a);

    ui_init(a);
    ef_settings_load(&a->S, a->settings_path);
    ef_params_defaults(&a->P);
    char msg[128] = "";
    ef_recover_recordings(g_paths.recordings, msg, sizeof msg);   /* takes from older versions show up in MY SAMPLES */

    /* audio out first: its rate becomes the engine rate */
    int sr = 48000;
    if (audio_sys && ef_audio_open(&a->A, NULL, 48000, 512)) { sr = a->A.spec.freq; a->audio_ok = true; }
    else { ef_strlcpy(a->audio_error, a->A.error[0] ? a->A.error : "SDL audio unavailable", sizeof a->audio_error); }
    a->sr = sr;
    if (!ef_engine_init(&a->E, sr, &a->P)) { ef_log("engine init failed (out of memory)"); return 1; }
    ef_capture_init(&a->C);
    a->E.monitor = &a->C.ring;
    app_apply_settings(a);
    ef_audio_lock(&a->A);
    a->A.engine = &a->E;
    ef_audio_unlock(&a->A);
    char first[EF_PATH_MAX] = "";
    if (!app_restore_session(a)) {
        ef_preset_apply_factory(0, &a->P, first, sizeof first);
        ef_strlcpy(a->preset_name, ef_factory_name(0), sizeof a->preset_name);
        ui_toast(a, col(C_ORANGE, 255), "Welcome. Move the sticks. START > SOUNDS for more.");
    }
    ef_param_set(&a->P, P_PLAY, 1);
    if (!a->job_busy && !a->E.cur) load_default_sample(a, first);
    if (!a->audio_ok) ui_toast(a, col(C_ERR, 255), "%s", a->audio_error);
    if (a->audio_ok) SDL_PauseAudioDevice(a->A.dev, 0);

    if (shots) {   /* documentation / visual test mode: render each screen to BMP */
        static const struct { EfScreen s; int vis, page; const char *name; } list[] = {
            {SCR_MAIN, 0, 0, "main"}, {SCR_MAIN, 1, 1, "main_field"}, {SCR_MAIN, 2, 2, "main_spectrum"}, {SCR_MAIN, 3, 3, "main_scope"},
            {SCR_SAMPLES, 0, 0, "samples"}, {SCR_PRESETS, 0, 0, "sounds"}, {SCR_EDIT, 0, 0, "edit"}, {SCR_FX, 0, 0, "effects"},
            {SCR_SETTINGS, 0, 0, "settings"}, {SCR_HELP, 0, 0, "help"}, {SCR_MENU, 0, 0, "menu"}};
        double t0 = ef_now_s();
        while (a->job_busy && ef_now_s() - t0 < 10) { ui_update(a, 0.03); SDL_Delay(20); }
        for (int i = 0; i < EF_ARRAY_LEN(list); i++) {
            a->S.visualiser = list[i].vis;
            a->page = list[i].page;
            if (list[i].s == SCR_MENU) { a->menu_return = SCR_MAIN; a->scr = SCR_MENU; }
            else ui_open_screen(a, list[i].s);
            a->toast_until = 0;
            for (int f = 0; f < 60; f++) { handle_events(a); ui_update(a, 1 / 30.0); ui_frame(a, 1 / 30.0); SDL_RenderPresent(a->ren); SDL_Delay(33); }
            ui_frame(a, 1 / 30.0);
            char p[EF_PATH_MAX]; snprintf(p, sizeof p, "%s/%02d_%s.bmp", shots, i, list[i].name);
            ef_log("screenshot %s: %s", p, save_screenshot(a, p) ? "ok" : SDL_GetError());
            SDL_RenderPresent(a->ren);
        }
        a->quit = true;
    }

    /* --script: scripted button presses for end-to-end tests without hardware buttons,
     * e.g. "R2,wait:1500,R2,wait:800,A,wait:1500,shot:/tmp/x.bmp,quit" */
    const char *script = getenv("ECHOFIELD_SCRIPT");
    char sbuf[1024] = "";
    char *scur = NULL;
    double script_wait = 0;
    if (script) { ef_strlcpy(sbuf, script, sizeof sbuf); scur = sbuf; }
    static const char *const bnames[B_COUNT] = {"UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y", "L1", "R1", "L2", "R2", "START", "SELECT", "L3", "R3"};

    double last = ef_now_s(), fps_t = last;
    int frames = 0;
    while (!a->quit && !g_signal_quit) {
        handle_events(a);
        ui_poll_repeat(a);
        double now = ef_now_s();
        double dt = now - last;
        last = now;
        atomic_store(&a->E.monitor_rate, a->C.rate);
        ui_update(a, dt);
        ui_frame(a, dt);
        while (scur && *scur && now >= script_wait) {
            char *comma = strchr(scur, ',');
            if (comma) *comma = 0;
            if (!strncmp(scur, "wait:", 5)) script_wait = now + atoi(scur + 5) / 1000.0;
            else if (!strncmp(scur, "shot:", 5)) ef_log("script shot %s: %s", scur + 5, save_screenshot(a, scur + 5) ? "ok" : "failed");
            else if (!strcmp(scur, "quit")) a->quit = true;
            else for (int b2 = 0; b2 < B_COUNT; b2++) if (!strcmp(scur, bnames[b2])) { ui_button(a, (EfButton)b2, true); ui_button(a, (EfButton)b2, false); ef_log("script: %s", scur); }
            scur = comma ? comma + 1 : NULL;
        }
        SDL_RenderPresent(a->ren);
        frames++;
        if (now - fps_t > 1.0) { a->fps = frames / (float)(now - fps_t); frames = 0; fps_t = now; }
        double spent = ef_now_s() - now;
        double target = 1.0 / 30.0;   /* 30 fps leaves the CPU to the audio */
        if (spent < target) SDL_Delay((Uint32)((target - spent) * 1000.0));
    }
    ef_log("shutting down%s", g_signal_quit ? " (signal)" : "");
    app_save_session(a);
    ef_capture_shutdown(&a->C);
    ef_audio_close(&a->A);
    app_shutdown_ui(a);
    ef_engine_free(&a->E);
    if (a->trail) SDL_DestroyTexture(a->trail);
    draw_shutdown();
    if (a->pad) SDL_GameControllerClose(a->pad);
    SDL_DestroyRenderer(a->ren);
    SDL_DestroyWindow(a->win);
    ef_log("bye");
    ef_log_close();
    SDL_Quit();
    return 0;
}
