/* ECHOFIELD - diagnostics report and offline CPU benchmark */
#include "app.h"

typedef struct { char *buf; size_t len, cap; } Sb;
static void sb(Sb *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sb(Sb *s, const char *fmt, ...) {
    char tmp[1024];
    va_list ap; va_start(ap, fmt); int n = vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof tmp) n = sizeof tmp - 1;
    if (s->len + (size_t)n + 2 > s->cap) {
        size_t nc = (s->cap + (size_t)n + 2) * 2;
        char *nb = realloc(s->buf, nc);
        if (!nb) return;
        s->buf = nb; s->cap = nc;
    }
    memcpy(s->buf + s->len, tmp, (size_t)n);
    s->len += (size_t)n;
    s->buf[s->len++] = '\n';
    s->buf[s->len] = 0;
}

static void os_release(char *out, size_t n) {
    out[0] = 0;
    char *t = ef_read_text_file("/etc/os-release", 16384);
    if (!t) { ef_strlcpy(out, "unknown", n); return; }
    char *p = strstr(t, "PRETTY_NAME=");
    if (p) {
        p += 12; if (*p == '"') p++;
        char *e = p; while (*e && *e != '"' && *e != '\n') e++;
        *e = 0;
        ef_strlcpy(out, p, n);
    }
    free(t);
}

static void report_system(Sb *s) {
    struct utsname u;
    char osr[128];
    os_release(osr, sizeof osr);
#if defined(__aarch64__)
    const char *built = "aarch64";
#elif defined(__x86_64__)
    const char *built = "x86_64";
#else
    const char *built = "other";
#endif
    sb(s, "#SYSTEM");
    sb(s, "ECHOFIELD %s (binary built for %s)", EF_VERSION, built);
    if (uname(&u) == 0) sb(s, "kernel: %s %s  machine: %s", u.sysname, u.release, u.machine);
    sb(s, "os: %s", osr);
    SDL_version v; SDL_GetVersion(&v);
    sb(s, "SDL: %d.%d.%d   cpus: %d   ram: %d MB", v.major, v.minor, v.patch, SDL_GetCPUCount(), SDL_GetSystemRAM());
    const char *cfw = getenv("CFW_NAME"), *dev = getenv("DEVICE_NAME"), *arch = getenv("DEVICE_ARCH");
    sb(s, "PortMaster env: CFW_NAME=%s DEVICE_NAME=%s DEVICE_ARCH=%s", cfw ? cfw : "-", dev ? dev : "-", arch ? arch : "-");
    sb(s, "controller mapping from launcher: %s", getenv("SDL_GAMECONTROLLERCONFIG") ? "yes" : "no");
    sb(s, "data dir: %s (%s)", g_paths.data_dir, g_paths.data_writable ? "writable" : "NOT WRITABLE");
    sb(s, "app dir: %s", g_paths.app_dir);
}

static void report_inventory(Sb *s, const EfInventory *inv) {
    sb(s, "");
    sb(s, "#ALSA CARDS (/proc/asound)%s", inv->proc_asound_ok ? "" : "  - NOT AVAILABLE");
    for (int i = 0; i < inv->ncards; i++) {
        const EfAlsaCard *c = &inv->cards[i];
        sb(s, "card %d [%s] %s %s", c->card, c->id, c->shortname, c->is_usb ? "(USB)" : "(internal)");
        sb(s, "~   capture pcm: %d  playback pcm: %d  usbid: %s", c->capture_dev, c->playback_dev, c->usbid[0] ? c->usbid : "-");
        if (c->stream_info) sb(s, "~   USB streams: playback %s, capture %s %s %dch %d-%d Hz", c->stream_has_playback ? "yes" : "no",
                               c->stream_has_capture ? "yes" : "NO", c->cap_format, c->cap_channels, c->cap_rate_min, c->cap_rate_max);
    }
    sb(s, "");
    sb(s, "#USB DEVICES (/sys/bus/usb)%s", inv->sys_usb_ok ? "" : "  - NOT AVAILABLE");
    if (inv->nusb == 0) sb(s, "~ none (check OTG adapter / power / powered hub)");
    for (int i = 0; i < inv->nusb; i++) {
        const EfUsbDev *u = &inv->usb[i];
        sb(s, "%s %s %s [%s]%s", u->sysname, u->manufacturer, u->product, u->vidpid, u->looks_like_fm1 ? "  <- FM-1" : "");
        sb(s, "~   audio-control:%s capture(IN):%s playback(OUT):%s midi:%s driver:%s alsa-card:%d",
           u->audio_control ? "y" : "n", u->as_in ? "YES" : "no", u->as_out ? "y" : "n", u->midi ? "y" : "n",
           u->driver[0] ? u->driver : "-", u->alsa_card >= 0 ? inv->cards[u->alsa_card].card : -1);
        if (u->midi && !u->as_in) sb(s, "!   this device exposes no audio capture interface: it cannot be recorded over USB");
    }
    sb(s, "snd-usb-audio driver active: %s   module file present: %s", inv->usb_audio_driver ? "yes" : "no", inv->usb_audio_module_file ? "yes" : "no/unknown");
    EfInput ins[16];
    int n = ef_inputs_build(inv, ins, 16);
    sb(s, "");
    sb(s, "#RECORDABLE INPUTS");
    for (int i = 0; i < n; i++) sb(s, "%s %s - %s", ins[i].status == IN_READY ? "[ok]" : (ins[i].status == IN_TEST ? "[test]" : "[--]"), ins[i].label, ins[i].detail);
    if (!n) sb(s, "~ none");
}

char *diag_report(App *a) {
    Sb s = {0};
    report_system(&s);
    sb(&s, "");
    sb(&s, "#VIDEO / INPUT");
    sb(&s, "video driver: %s", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "-");
    int ow = 0, oh = 0;
    if (a->ren) SDL_GetRendererOutputSize(a->ren, &ow, &oh);
    sb(&s, "renderer output: %dx%d (logical 640x480)  trails: %s  fps: %.0f", ow, oh, a->trail ? "on" : "off", (double)a->fps);
    sb(&s, "controller: %s   joysticks: %d   keyboard input: %s", a->pad ? SDL_GameControllerName(a->pad) : "NONE", SDL_NumJoysticks(), a->keyboard_ok ? "on" : "off");
    sb(&s, "");
    sb(&s, "#AUDIO OUTPUT");
    if (a->audio_ok) sb(&s, "driver: %s  rate: %d Hz  buffer: %d frames  format: f32 stereo", SDL_GetCurrentAudioDriver(), a->A.spec.freq, a->A.spec.samples);
    else sb(&s, "!output FAILED: %s", a->audio_error);
    sb(&s, "dsp load: %.0f%% avg, %.0f%% peak   grains: %d / %d   skipped spawns: %d", (double)(atomic_load(&a->A.load) * 100),
       (double)(atomic_load(&a->A.load_peak) * 100), atomic_load(&a->E.active_grains), atomic_load(&a->E.max_voices), atomic_load(&a->E.spawn_skipped));
    sb(&s, "");
    sb(&s, "#CAPTURE");
    if (a->C.alsa_loaded) sb(&s, "ALSA library: %s (loaded at run time)", a->C.alsa_version);
    else sb(&s, "!ALSA library missing: %s", a->C.alsa_error);
    sb(&s, "state: %s  device: %s", ef_capture_state_name(atomic_load(&a->C.state)), a->C.opened_name[0] ? a->C.opened_name : "-");
    if (atomic_load(&a->C.state) == CAP_RUNNING)
        sb(&s, "format: %s %d ch %d Hz period %d   frames in: %lld   overruns: %d   reconnects: %d", a->C.format_name, a->C.dev_channels, a->C.rate,
           a->C.period, (long long)atomic_load(&a->C.frames_in), atomic_load(&a->C.xruns), atomic_load(&a->C.reconnects));
    if (a->C.error[0]) sb(&s, "!last error: %s", a->C.error);
    if (a->C.attempts[0]) sb(&s, "~open attempts: %s", a->C.attempts);
    sb(&s, "signal seen: %s   monitor ring overruns: %lld", atomic_load(&a->C.signal_seen) ? "yes" : "no", (long long)atomic_load(&a->C.ring.overruns));
    EfInventory *inv = malloc(sizeof *inv);
    if (inv) { ef_inventory_scan(inv); report_inventory(&s, inv); free(inv); }
    /* written by the launcher, which can load/bind the driver and read dmesg as root */
    sb(&s, "");
    sb(&s, "#KERNEL USB AUDIO (checked at launch)");
    const char *kr = getenv("ECHOFIELD_KERNEL_REPORT");
    char *kt = kr && kr[0] ? ef_read_text_file(kr, 16384) : NULL;
    if (kt) {
        for (char *l = kt; l && *l;) {
            char *nl = strchr(l, '\n');
            if (nl) *nl = 0;
            bool bad = strstr(l, "COULD NOT") || strstr(l, "FAILED") || strstr(l, "not shipped") || strstr(l, "error") || strstr(l, "failed");
            sb(&s, "%s%s", bad ? "!" : "~", l);
            l = nl ? nl + 1 : NULL;
        }
        free(kt);
    } else sb(&s, "~ no launcher report (start ECHOFIELD from PORTS to get one)");
    sb(&s, "");
    sb(&s, "#MISSING / WARNINGS");
    int warn = 0;
    if (!a->C.alsa_loaded) { sb(&s, "!libasound.so.2 - USB recording impossible"); warn++; }
    if (!a->audio_ok) { sb(&s, "!audio output unavailable"); warn++; }
    if (!a->pad) { sb(&s, "!no game controller detected (SDL_GAMECONTROLLERCONFIG?)"); warn++; }
    if (!g_paths.data_writable) { sb(&s, "!no writable storage: recordings cannot be saved"); warn++; }
    if (!warn) sb(&s, "~ none");
    return s.buf;
}

void diag_print_cli(void) {
    App *a = calloc(1, sizeof *a);
    if (!a) return;
    a->audio_ok = false;
    ef_strlcpy(a->audio_error, "not opened in --diag mode", sizeof a->audio_error);
    ef_capture_init(&a->C);
    char *t = diag_report(a);
    if (t) {
        printf("%s", t);
        char p[EF_PATH_MAX];
        ef_path_join(p, sizeof p, g_paths.data_dir, "diagnostics.txt");
        if (ef_write_text_file_atomic(p, t)) printf("\n(report written to %s)\n", p);
        free(t);
    }
    ef_capture_shutdown(&a->C);
    free(a);
}

/* Offline render speed per mode: real-time factor and estimated CPU share. */
int diag_bench(int seconds) {
    static EfParams P;
    static EfEngine E;
    ef_params_defaults(&P);
    if (!ef_engine_init(&E, 48000, &P)) return 1;
    EfSample *s = ef_sample_alloc(48000 * 4, 2, 48000);
    EfRng r = {123};
    for (int i = 0; i < s->frames; i++) { s->L[i] = 0.3f * sinf(i * 0.031f) + 0.1f * ef_rng_bi(&r); s->R[i] = 0.3f * sinf(i * 0.029f); }
    ef_sample_analyze(s);
    ef_engine_set_sample(&E, s);
    float *buf = malloc(sizeof(float) * 512 * 2);
    printf("ECHOFIELD DSP benchmark: %d s of audio per mode at 48 kHz, 512-frame blocks, max voices %d\n", seconds, atomic_load(&E.max_voices));
    for (int eco = 0; eco < 2; eco++) {
        atomic_store(&E.eco, eco);
        for (int m = 0; m < MODE_COUNT; m++) {
            ef_param_set(&P, P_MODE, (float)m);
            ef_param_set(&P, P_SHIMMER, eco ? 0.0f : 0.4f);
            int blocks = seconds * 48000 / 512;
            double t0 = ef_now_s();
            int maxg = 0;
            for (int b = 0; b < blocks; b++) { ef_engine_render(&E, buf, 512); maxg = EF_MAX(maxg, atomic_load(&E.active_grains)); }
            double el = ef_now_s() - t0;
            printf("  %-6s %s  %.1fx realtime  (~%4.1f%% of one core)  peak grains %d\n", g_mode_names[m], eco ? "eco " : "full",
                   seconds / el, 100.0 * el / seconds, maxg);
        }
    }
    /* worst case: every grain slot busy, shimmer + all effects on */
    atomic_store(&E.eco, 0);
    ef_param_set(&P, P_MODE, MODE_CLOUD); ef_param_set(&P, P_TEXTURE, 1.0f); ef_param_set(&P, P_DENS_MUL, 2.0f);
    ef_param_set(&P, P_SIZE_MUL, 1.0f); ef_param_set(&P, P_SHIMMER, 0.5f); ef_param_set(&P, P_TAPE, 1.0f); ef_param_set(&P, P_DLY_MIX, 0.5f);
    int caps[3] = {40, 64, 96};
    for (int k = 0; k < 3; k++) {
        atomic_store(&E.max_voices, caps[k]);
        int blocks = seconds * 48000 / 512, maxg = 0;
        double t0 = ef_now_s();
        for (int b = 0; b < blocks; b++) { ef_engine_render(&E, buf, 512); maxg = EF_MAX(maxg, atomic_load(&E.active_grains)); }
        double el = ef_now_s() - t0;
        printf("  STRESS cap %2d  %.1fx realtime  (~%4.1f%% of one core)  peak grains %d\n", caps[k], seconds / el, 100.0 * el / seconds, maxg);
    }
    free(buf);
    ef_engine_free(&E);
    return 0;
}
