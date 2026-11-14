/* ECHOFIELD automated tests (desktop / CI). Uses only synthetic fixtures and
 * simulated device trees - these tests do NOT prove USB capture on hardware. */
#include "common.h"
#include "sample.h"
#include "engine.h"
#include "store.h"
#include "capture.h"
#include "compat/sdl_min.h"

static int g_fail, g_pass;
static char g_tmp[EF_PATH_MAX];
#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define SECTION(name) printf("[%s]\n", name)

static void tmp_path(char *out, size_t n, const char *name) { snprintf(out, n, "%s/%s", g_tmp, name); }

static bool all_finite(const float *b, long n, float *maxabs) {
    float m = 0;
    for (long i = 0; i < n; i++) {
        float v = b[i];
        if (!(v == v) || v > 1e30f || v < -1e30f) return false;
        if (fabsf(v) > m) m = fabsf(v);
    }
    if (maxabs) *maxabs = m;
    return true;
}

static EfSample *make_tone(int frames, int ch, int rate, float hz) {
    EfSample *s = ef_sample_alloc(frames, ch, rate);
    for (int i = 0; i < frames; i++) {
        s->L[i] = 0.5f * sinf(EF_TWOPI * hz * i / rate);
        if (ch == 2) s->R[i] = 0.5f * sinf(EF_TWOPI * hz * 1.5f * i / rate);
    }
    ef_sample_analyze(s);
    return s;
}

/* write raw WAV with arbitrary encoding for decoder tests */
static void write_raw_wav(const char *path, int fmt, int ch, int rate, int bits, int frames, bool extensible) {
    FILE *f = fopen(path, "wb");
    int bps = bits / 8, data = frames * ch * bps;
    int fmtlen = extensible ? 40 : 16;
    uint8_t h[80] = {0};
    int o = 0;
#define P4(s) do { memcpy(h + o, s, 4); o += 4; } while (0)
#define U32(v) do { uint32_t _v = (uint32_t)(v); h[o] = _v & 255; h[o+1] = (_v >> 8) & 255; h[o+2] = (_v >> 16) & 255; h[o+3] = _v >> 24; o += 4; } while (0)
#define U16(v) do { uint16_t _v = (uint16_t)(v); h[o] = _v & 255; h[o+1] = _v >> 8; o += 2; } while (0)
    P4("RIFF"); U32(4 + 8 + fmtlen + 8 + 8 + data + 8 + 4); P4("WAVE");
    P4("LIST"); U32(4); P4("INFO");                         /* unknown chunk before fmt */
    P4("fmt "); U32(fmtlen); U16(extensible ? 0xFFFE : fmt); U16(ch); U32(rate); U32(rate * ch * bps); U16(ch * bps); U16(bits);
    if (extensible) { U16(22); U16(bits); U32(3); U16(fmt); memset(h + o, 0, 14); o += 14; }
    P4("data"); U32(data);
    fwrite(h, 1, (size_t)o, f);
    for (int i = 0; i < frames; i++) for (int c = 0; c < ch; c++) {
        double v = 0.5 * sin(2 * M_PI * 440.0 * i / rate) * (c == 0 ? 1.0 : -1.0);
        uint8_t b[8];
        if (fmt == 3 && bits == 32) { float fv = (float)v; memcpy(b, &fv, 4); }
        else if (fmt == 3 && bits == 64) { memcpy(b, &v, 8); }
        else if (bits == 8) b[0] = (uint8_t)(128 + (int)(v * 127));
        else if (bits == 16) { int16_t x = (int16_t)(v * 32767); memcpy(b, &x, 2); }
        else if (bits == 24) { int32_t x = (int32_t)(v * 8388607); b[0] = x & 255; b[1] = (x >> 8) & 255; b[2] = (x >> 16) & 255; }
        else { int32_t x = (int32_t)(v * 2147483647.0); memcpy(b, &x, 4); }
        fwrite(b, 1, (size_t)bps, f);
    }
    fclose(f);
}

static void test_wav(void) {
    SECTION("wav decode/encode");
    struct { int fmt, ch, bits; bool ext; } cases[] = {
        {1, 1, 8, false}, {1, 1, 16, false}, {1, 2, 16, false}, {1, 2, 24, false}, {1, 2, 32, false},
        {3, 1, 32, false}, {3, 2, 64, false}, {1, 2, 24, true}, {3, 2, 32, true}, {1, 6, 16, false},
    };
    for (int i = 0; i < EF_ARRAY_LEN(cases); i++) {
        char p[EF_PATH_MAX]; snprintf(p, sizeof p, "%s/case%d.wav", g_tmp, i);
        write_raw_wav(p, cases[i].fmt, cases[i].ch, 44100, cases[i].bits, 4410, cases[i].ext);
        EfWavInfo info;
        EfSample *s = ef_wav_load(p, &info);
        CHECK(s != NULL, "case %d load failed: %s", i, info.error);
        if (!s) continue;
        CHECK(s->frames == 4410, "case %d frames %d", i, s->frames);
        CHECK(s->channels == (cases[i].ch >= 2 ? 2 : 1), "case %d channels", i);
        float pk; float want = cases[i].ch == 6 ? 0.1667f : 0.5f;   /* 6ch fold: (+v -v -v)/3 */
        CHECK(all_finite(s->L, s->frames, &pk) && fabsf(pk - want) < 0.05f, "case %d peak %f", i, (double)pk);
        if (cases[i].ch == 2) CHECK(fabsf(s->L[100] + s->R[100]) < 0.02f, "case %d stereo channels preserved", i);
        ef_sample_free(s);
    }
    /* unsupported encodings must fail cleanly */
    char p[EF_PATH_MAX]; tmp_path(p, sizeof p, "adpcm.wav");
    write_raw_wav(p, 2, 1, 44100, 16, 100, false);
    EfWavInfo info; EfSample *bad = ef_wav_load(p, &info);
    CHECK(bad == NULL && strstr(info.error, "unsupported"), "ADPCM rejected (%s)", info.error);
    tmp_path(p, sizeof p, "garbage.wav");
    FILE *f = fopen(p, "wb"); fputs("this is not audio at all", f); fclose(f);
    CHECK(ef_wav_load(p, &info) == NULL, "garbage rejected");
    tmp_path(p, sizeof p, "missing.wav");
    CHECK(ef_wav_load(p, &info) == NULL, "missing file rejected");
    /* truncated data chunk: loads what exists */
    tmp_path(p, sizeof p, "trunc.wav");
    write_raw_wav(p, 1, 1, 22050, 16, 1000, false);
    f = fopen(p, "r+b"); fseek(f, 0, SEEK_END); long len = ftell(f); fclose(f);
    char *buf = ef_read_text_file(p, 1 << 20);
    f = fopen(p, "wb"); fwrite(buf, 1, (size_t)(len - 500), f); fclose(f); free(buf);
    EfSample *tr = ef_wav_load(p, &info);
    CHECK(tr && tr->frames == 750, "truncated file loads available frames (%d)", tr ? tr->frames : -1);
    ef_sample_free(tr);

    /* round trip 16/24/float */
    EfSample *s = make_tone(1000, 2, 48000, 300);
    int bits[3] = {16, 24, 32};
    for (int b = 0; b < 3; b++) {
        snprintf(p, sizeof p, "%s/rt%d.wav", g_tmp, bits[b]);
        CHECK(ef_wav_write_sample(p, s, 0, s->frames, false, 1.0f, bits[b]), "write %d", bits[b]);
        EfSample *r = ef_wav_load(p, &info);
        CHECK(r && r->frames == 1000 && r->rate == 48000 && r->channels == 2, "roundtrip %d meta", bits[b]);
        float err = 0;
        for (int i = 0; r && i < 1000; i++) err = EF_MAX(err, fabsf(r->L[i] - s->L[i]) + fabsf(r->R[i] - s->R[i]));
        CHECK(r && err < (bits[b] == 16 ? 1e-3f : 1e-5f), "roundtrip %d error %g", bits[b], (double)err);
        ef_sample_free(r);
    }
    /* trimming + reverse render */
    snprintf(p, sizeof p, "%s/trim.wav", g_tmp);
    CHECK(ef_wav_write_sample(p, s, 100, 300, true, 1.0f, 32), "trim write");
    EfSample *t = ef_wav_load(p, &info);
    CHECK(t && t->frames == 200 && fabsf(t->L[0] - s->L[299]) < 1e-6f && fabsf(t->L[199] - s->L[100]) < 1e-6f, "trim+reverse content");
    ef_sample_free(t);
    EfSample *rd = ef_sample_render(s, 100, 300, false, 0.5f);
    CHECK(rd && rd->frames == 200 && fabsf(rd->L[0] - 0.5f * s->L[100]) < 1e-6f, "render region with gain");
    ef_sample_free(rd);
    /* mono <-> stereo */
    EfSample *m = ef_sample_make_mono(s);
    CHECK(m && m->channels == 1 && m->R == m->L && fabsf(m->L[10] - 0.5f * (s->L[10] + s->R[10])) < 1e-6f, "stereo->mono");
    EfSample *st = ef_sample_make_stereo(m);
    CHECK(st && st->channels == 2 && st->L != st->R && st->R[10] == m->L[10], "mono->stereo");
    ef_sample_free(m); ef_sample_free(st);
    /* resample */
    EfSample *rs = ef_sample_resample(s, 44100);
    CHECK(rs && abs(rs->frames - 918) <= 1 && rs->rate == 44100, "resample length %d", rs ? rs->frames : 0);
    float pk2; CHECK(rs && all_finite(rs->L, rs->frames, &pk2) && pk2 > 0.45f && pk2 < 0.56f, "resample amplitude");
    ef_sample_free(rs);
    ef_sample_free(s);
    /* streaming writer + repair */
    snprintf(p, sizeof p, "%s/stream.wav", g_tmp);
    EfWavStream ws;
    float chunk[200];
    for (int i = 0; i < 200; i++) chunk[i] = (float)i / 200.0f;
    CHECK(ef_wav_stream_open(&ws, p, 2, 48000), "stream open");
    ef_wav_stream_append(&ws, chunk, 100);
    ef_wav_stream_append(&ws, chunk, 100);
    fclose(ws.f);   /* simulate a crash: header never patched */
    EfSample *broken = ef_wav_load(p, &info);
    CHECK(broken && broken->frames == 200, "unpatched stream still loads (size tolerant)");
    ef_sample_free(broken);
    CHECK(ef_wav_repair(p), "repair");
    EfWavInfo pi; CHECK(ef_wav_probe(p, &pi) && pi.frames == 200 && pi.format == 3, "repaired header frames=%ld", pi.frames);
}

static void render_n(EfEngine *e, float *buf, int frames) {
    for (int done = 0; done < frames; done += 512) ef_engine_render(e, buf + done * 2, EF_MIN(512, frames - done));
}

static double rms(const float *b, int n) { double a = 0; for (int i = 0; i < n * 2; i++) a += b[i] * b[i]; return sqrt(a / (n * 2)); }

static void test_engine(void) {
    SECTION("granular engine");
    for (int w = 0; w < WIN_COUNT; w++) {
        CHECK(fabsf(ef_window_value(w, 0.0f)) < 1e-3f && fabsf(ef_window_value(w, 1.0f)) < 1e-3f, "window %d starts/ends at zero", w);
        float mx = 0; for (int i = 0; i <= 100; i++) mx = EF_MAX(mx, ef_window_value(w, i / 100.0f));
        CHECK(mx > 0.9f && mx <= 1.0001f, "window %d peak %f", w, (double)mx);
    }
    static EfParams P;
    ef_params_defaults(&P);
    static EfEngine E;
    CHECK(ef_engine_init(&E, 48000, &P), "engine init");
    int N = 48000 * 2;
    float *buf = malloc(sizeof(float) * 48000 * 8 * 2);
    /* no sample: silence, no crash */
    render_n(&E, buf, 4800);
    float mx; CHECK(all_finite(buf, 4800 * 2, &mx) && mx < 1e-3f, "silence without sample");
    EfSample *s = make_tone(48000, 2, 48000, 220);
    ef_engine_set_sample(&E, s);
    for (int mode = 0; mode < MODE_COUNT; mode++) {
        ef_param_set(&P, P_MODE, (float)mode);
        render_n(&E, buf, N);
        bool fin = all_finite(buf, N * 2, &mx);
        double r = rms(buf + 24000 * 2, 24000);
        CHECK(fin && mx <= 1.0f, "mode %s finite & bounded (max %f)", g_mode_names[mode], (double)mx);
        CHECK(r > 0.003, "mode %s produces sound (rms %f)", g_mode_names[mode], r);
        CHECK(atomic_load(&E.active_grains) <= atomic_load(&E.max_voices), "mode %s voice limit", g_mode_names[mode]);
    }
    /* grain voice limit under extreme density */
    ef_param_set(&P, P_MODE, MODE_CLOUD);
    ef_param_set(&P, P_DENS_MUL, 2.0f); ef_param_set(&P, P_SIZE_MUL, 2.0f); ef_param_set(&P, P_TEXTURE, 1.0f);
    atomic_store(&E.max_voices, 24);
    render_n(&E, buf, 48000 * 4);
    CHECK(ef_engine_count_active(&E) <= 24, "max voices respected (%d)", ef_engine_count_active(&E));
    CHECK(atomic_load(&E.spawn_skipped) > 0, "excess spawns are skipped, not allocated");
    atomic_store(&E.max_voices, 40);
    ef_params_defaults(&P);
    /* boundaries: grains stay inside region, reverse works */
    atomic_store(&E.region_a, 0.25f); atomic_store(&E.region_b, 0.5f);
    ef_param_set(&P, P_SCATTER, 1.0f); ef_param_set(&P, P_REVERSE, 1.0f); ef_param_set(&P, P_PITCH, 24);
    ef_param_set(&P, P_TEXTURE, 1.0f); ef_param_set(&P, P_SIZE_MUL, 2.0f);
    bool inside = true, saw_rev = false, saw_fwd = false;
    render_n(&E, buf, 48000 * 4);   /* let grains born before the region change finish */
    for (int k = 0; k < 200; k++) {
        ef_engine_render(&E, buf, 256);
        for (int g = 0; g < EF_MAX_GRAINS; g++) {
            EfGrain *gr = &E.grains[g];
            if (!gr->active) continue;
            if (gr->pos < 0.25 * 48000 - 2 || gr->pos > 0.5 * 48000 + 2) inside = false;
            if (gr->inc < 0) saw_rev = true; else saw_fwd = true;
        }
    }
    CHECK(inside, "grains stay inside the region at max pitch/scatter/size");
    CHECK(saw_rev, "reverse grains present at REVERSE=1");
    ef_param_set(&P, P_REVERSE, 0.0f); ef_param_set(&P, P_MODE, MODE_DRIFT);
    (void)saw_fwd;
    /* pitch: grain increment follows PITCH */
    ef_params_defaults(&P);
    ef_param_set(&P, P_DRIFT, 0); ef_param_set(&P, P_PITCH, 12); ef_param_set(&P, P_PITCH_RAND, 0);
    ef_param_set(&P, P_SCATTER, 0); ef_param_set(&P, P_REVERSE, 0); ef_param_set(&P, P_HARMONY, 0);
    atomic_store(&E.region_a, 0.0f); atomic_store(&E.region_b, 1.0f);
    render_n(&E, buf, 48000);
    double inc = 0; int cnt = 0;
    for (int g = 0; g < EF_MAX_GRAINS; g++) if (E.grains[g].active) { inc += fabs(E.grains[g].inc); cnt++; }
    CHECK(cnt > 0 && fabs(inc / cnt - 2.0) < 0.15, "PITCH +12 doubles grain rate (%.3f)", cnt ? inc / cnt : 0.0);
    /* HARMONY: a share of grains lands an octave/fifth away from the played pitch */
    ef_param_set(&P, P_PITCH, 0); ef_param_set(&P, P_HARMONY, 1.0f);
    int harm = 0, plain = 0;
    for (int k = 0; k < 40; k++) {
        render_n(&E, buf, 4800);
        for (int g = 0; g < EF_MAX_GRAINS; g++) if (E.grains[g].active) {
            double r = fabs(E.grains[g].inc);
            if (r > 1.35 || r < 0.8) harm++; else plain++;
        }
    }
    CHECK(harm > 0 && plain > 0 && harm < plain * 3, "HARMONY mixes layered and plain grains (%d / %d)", harm, plain);
    ef_param_set(&P, P_HARMONY, 0.0f);
    /* mono material gets a per-grain right-channel offset (stereo image) */
    bool roff = false;
    for (int g = 0; g < EF_MAX_GRAINS; g++) if (E.grains[g].active && E.grains[g].roff != 0.0f) roff = true;
    CHECK(E.cur && (E.cur->channels == 2 || roff), "mono source is decorrelated into stereo");
    int sw0 = atomic_load(&E.scope_w);
    ef_engine_render(&E, buf, 300);
    CHECK(atomic_load(&E.scope_w) == ((sw0 + 300) & (EF_SCOPE_N - 1)), "scope buffer follows the output");
    /* rapid parameter changes stay stable */
    EfRng rng = {77};
    bool stable = true;
    for (int k = 0; k < 400; k++) {
        for (int i = 0; i < P_COUNT; i++) if (i != P_PLAY) ef_param_set(&P, i, g_param_info[i].min + ef_rng_f(&rng) * (g_param_info[i].max - g_param_info[i].min));
        ef_engine_render(&E, buf, 256);
        if (!all_finite(buf, 512, &mx) || mx > 1.0f) stable = false;
    }
    CHECK(stable, "random parameter storm: finite and bounded");
    /* delay feedback limit: max feedback, impulse, then must decay (no runaway) */
    ef_params_defaults(&P);
    ef_param_set(&P, P_PLAY, 0); ef_param_set(&P, P_DLY_FB, 0.92f); ef_param_set(&P, P_DLY_MIX, 1.0f);
    ef_param_set(&P, P_DLY_TIME, 50); ef_param_set(&P, P_SPACE, 1.0f); ef_param_set(&P, P_REV_SIZE, 1.0f); ef_param_set(&P, P_SHIMMER, 1.0f);
    render_n(&E, buf, 48000);
    E.fx.dly[0][E.fx.dly_w] = 1.0f;   /* inject an impulse */
    render_n(&E, buf, 48000 * 2);
    double early = rms(buf, 24000);
    render_n(&E, buf, 48000 * 8);
    double late = rms(buf + 48000 * 6 * 2, 48000);
    CHECK(all_finite(buf, 48000 * 8 * 2, &mx), "max feedback stays finite");
    CHECK(late < early + 1e-4, "max feedback decays (early %.5f late %.5f)", early, late);
    /* sample swap crossfade: retire old sample */
    ef_params_defaults(&P);
    EfSample *s2 = make_tone(24000, 1, 48000, 330);
    ef_engine_set_sample(&E, s2);
    render_n(&E, buf, 4800);
    CHECK(E.cur == s2, "sample swapped after fade");
    EfSample *old = ef_engine_take_retired(&E);
    CHECK(old == s, "old sample retired for UI-thread free");
    ef_sample_free(old);
    /* tiny sample + zero-length region guards */
    EfSample *tiny = make_tone(100, 1, 48000, 1000);
    ef_engine_set_sample(&E, tiny);
    render_n(&E, buf, 4800);
    ef_sample_free(ef_engine_take_retired(&E));
    atomic_store(&E.region_a, 0.5f); atomic_store(&E.region_b, 0.5f);
    render_n(&E, buf, 9600);
    CHECK(all_finite(buf, 9600 * 2, &mx), "zero-length region and 100-frame sample are safe");
    atomic_store(&E.region_a, 0.0f); atomic_store(&E.region_b, 1.0f);
    /* oracle: scale-constrained notes */
    ef_params_defaults(&P);
    ef_param_set(&P, P_OR_ON, 1); ef_param_set(&P, P_OR_SCALE, 7); ef_param_set(&P, P_OR_ACTIVITY, 1.0f); ef_param_set(&P, P_OR_AMBIG, 0.0f);
    EfSample *s3 = make_tone(48000, 1, 48000, 200);
    ef_engine_set_sample(&E, s3);
    render_n(&E, buf, 4800);
    ef_sample_free(ef_engine_take_retired(&E));
    bool in_scale = true; int changes = 0; float last = -999;
    for (int k = 0; k < 60; k++) {
        render_n(&E, buf, 48000);
        float t = E.oracle.glide_target;
        if (t != last) changes++;
        last = t;
        int semi = ((int)lrint_ef(t) % 12 + 12) % 12;
        bool ok = false;
        for (int d = 0; d < g_scales[7].n; d++) if (g_scales[7].deg[d] == semi) ok = true;
        if (!ok) in_scale = false;
    }
    CHECK(in_scale, "oracle bass notes stay in the selected scale");
    CHECK(changes >= 5, "oracle evolves at high activity (%d changes/min)", changes);
    /* monitor ring: mixing input */
    EfRing ring; ef_ring_init(&ring, 4096);
    E.monitor = &ring; atomic_store(&E.monitor_rate, 44100); atomic_store(&E.monitor_on, 1);
    ef_param_set(&P, P_PLAY, 0); ef_param_set(&P, P_SPACE, 0); ef_param_set(&P, P_DLY_MIX, 0);
    float in[2 * 2000];
    for (int i = 0; i < 2000; i++) in[i * 2] = in[i * 2 + 1] = 0.3f * sinf(i * 0.05f);
    render_n(&E, buf, 9600);
    ef_ring_write(&ring, in, 2000);
    ef_engine_render(&E, buf, 512);
    CHECK(rms(buf, 512) > 0.05, "monitor input reaches output");
    ef_ring_write(&ring, in, 2000); ef_ring_write(&ring, in, 2000); ef_ring_write(&ring, in, 2000);
    CHECK(ef_ring_write(&ring, in, 2000) < 2000 && atomic_load(&ring.overruns) > 0, "ring overrun counted, never blocks");
    atomic_store(&E.monitor_on, 0);
    E.monitor = NULL;
    ef_ring_free(&ring);
    free(buf);
    ef_engine_free(&E);
}

static void test_store(void) {
    SECTION("presets / projects / settings / randomizer");
    static EfParams P, Q;
    ef_params_defaults(&P);
    char ref[EF_PATH_MAX];
    for (int i = 0; i < g_factory_count; i++) CHECK(ef_preset_apply_factory(i, &P, ref, sizeof ref), "factory %d", i);
    int glass = -1;
    for (int i = 0; i < g_factory_count; i++) if (!strcmp(ef_factory_name(i), "Glass Rain")) glass = i;
    ef_preset_apply_factory(glass, &P, ref, sizeof ref);
    CHECK(glass >= 0 && (int)ef_param_get(&P, P_MODE) == MODE_SHARDS && fabsf(ef_param_get(&P, P_PITCH) - 12) < 1e-4f, "Glass Rain values applied");
    CHECK(!strcmp(ef_basename(ref), "glass_tines.wav"), "factory sound names its sample (%s)", ref);
    char path[EF_PATH_MAX];
    CHECK(ef_preset_save(&P, "Test Preset", "/x/y.wav", path, sizeof path), "save preset");
    ef_params_defaults(&Q);
    char err[128];
    CHECK(ef_preset_apply_file(path, &Q, ref, sizeof ref, err, sizeof err), "load preset: %s", err);
    bool same = true;
    for (int i = 0; i < P_COUNT; i++) if (i != P_PLAY && fabsf(ef_param_get(&P, i) - ef_param_get(&Q, i)) > 1e-3f) same = false;
    CHECK(same, "preset round trip identical");
    CHECK(!strcmp(ref, "/x/y.wav"), "preset sample reference kept");
    EfPresetEntry list[64];
    int n = ef_presets_list(list, 64);
    CHECK(n == g_factory_count + 1 && !strcmp(list[n - 1].name, "Test Preset"), "preset listing (%d)", n);
    EfProject pr = {0};
    ef_strlcpy(pr.name, "proj", sizeof pr.name);
    ef_strlcpy(pr.sample_path, "/nonexistent/sample.wav", sizeof pr.sample_path);
    pr.region_a = 0.1f; pr.region_b = 0.7f; pr.reverse = true; pr.locks[P_SPACE] = true; pr.locks[P_PITCH] = true;
    char pp[EF_PATH_MAX]; tmp_path(pp, sizeof pp, "p.efproj");
    CHECK(ef_project_save(pp, &pr, &P), "save project");
    EfProject pr2; ef_params_defaults(&Q);
    CHECK(ef_project_load(pp, &pr2, &Q, err, sizeof err), "load project: %s", err);
    CHECK(fabsf(pr2.region_a - 0.1f) < 1e-5f && fabsf(pr2.region_b - 0.7f) < 1e-5f && pr2.reverse && pr2.locks[P_SPACE] && pr2.locks[P_PITCH] && !pr2.locks[P_TEXTURE], "project fields");
    CHECK(!strcmp(pr2.sample_path, "/nonexistent/sample.wav") && !ef_file_exists(pr2.sample_path), "missing sample reference detectable");
    char bad[EF_PATH_MAX]; tmp_path(bad, sizeof bad, "bad.efproj");
    ef_write_text_file_atomic(bad, "format=something-else\n");
    CHECK(!ef_project_load(bad, &pr2, &Q, err, sizeof err), "foreign file rejected");
    char fut[EF_PATH_MAX]; tmp_path(fut, sizeof fut, "future.efproj");
    ef_write_text_file_atomic(fut, "format=echofield-project\nversion=99\nnew_thing=5\n[params]\ntexture=0.9\nfuture_param=3\n");
    CHECK(ef_project_load(fut, &pr2, &Q, err, sizeof err) && fabsf(ef_param_get(&Q, P_TEXTURE) - 0.9f) < 1e-4f, "newer format loads known keys");
    EfSettings s, s2;
    ef_settings_defaults(&s);
    s.input_gain_db = 6; s.monitor = true; ef_strlcpy(s.input_key, "1234:abcd", sizeof s.input_key); s.max_voices = 32;
    char sp[EF_PATH_MAX]; tmp_path(sp, sizeof sp, "settings.ini");
    CHECK(ef_settings_save(&s, sp) && ef_settings_load(&s2, sp), "settings io");
    CHECK(s2.input_gain_db == 6 && s2.monitor && !strcmp(s2.input_key, "1234:abcd") && s2.max_voices == 32, "settings values");
    /* randomizer respects locks and ranges */
    EfRng rng = {42};
    bool locks[P_COUNT] = {0};
    locks[P_TEXTURE] = true;
    ef_params_defaults(&P);
    ef_param_set(&P, P_TEXTURE, 0.123f);
    bool ok = true, changed = false, level_same = true;
    for (int k = 0; k < 500; k++) {
        float before = ef_param_get(&P, P_SPACE);
        ef_randomize(&P, locks, &rng);
        if (fabsf(ef_param_get(&P, P_TEXTURE) - 0.123f) > 1e-6f) ok = false;
        if (ef_param_get(&P, P_SPACE) != before) changed = true;
        if (fabsf(ef_param_get(&P, P_LEVEL) - g_param_info[P_LEVEL].def) > 1e-6f) level_same = false;
        for (int i = 0; i < P_COUNT; i++) {
            float v = ef_param_get(&P, i);
            if (v < g_param_info[i].min || v > g_param_info[i].max || !(v == v)) ok = false;
        }
        if ((int)ef_param_get(&P, P_FILTER_TYPE) == 0 && ef_param_get(&P, P_CUTOFF) < 0.45f) ok = false;
    }
    CHECK(ok, "randomize: locked param untouched, all values in range, no muffled LP");
    CHECK(changed && level_same, "randomize changes unlocked macros but never LEVEL");
}

static void mkfile(const char *root, const char *rel, const char *content) {
    char p[EF_PATH_MAX]; snprintf(p, sizeof p, "%s%s", root, rel);
    char d[EF_PATH_MAX]; ef_strlcpy(d, p, sizeof d); *strrchr(d, '/') = 0; ef_mkdirs(d);
    FILE *f = fopen(p, "wb"); fputs(content, f); fclose(f);
}
static void mkbin(const char *root, const char *rel, const uint8_t *b, int n) {
    char p[EF_PATH_MAX]; snprintf(p, sizeof p, "%s%s", root, rel);
    char d[EF_PATH_MAX]; ef_strlcpy(d, p, sizeof d); *strrchr(d, '/') = 0; ef_mkdirs(d);
    FILE *f = fopen(p, "wb"); fwrite(b, 1, (size_t)n, f); fclose(f);
}

/* minimal UAC1 configuration: AC interface, AS-OUT (playback), optional AS-IN (capture), MIDI */
static int make_desc(uint8_t *d, bool with_in, bool with_out, bool midi) {
    int o = 0;
    uint8_t dev[18] = {18, 1, 0x00, 0x02, 0, 0, 0, 64, 0x4a, 0x4c, 0x01, 0x00, 0, 1, 1, 2, 3, 1};
    memcpy(d + o, dev, 18); o += 18;
    uint8_t cfg[9] = {9, 2, 0, 0, 4, 1, 0, 0x80, 50}; memcpy(d + o, cfg, 9); o += 9;
    uint8_t ac[9] = {9, 4, 0, 0, 0, 1, 1, 0, 0}; memcpy(d + o, ac, 9); o += 9;
    if (with_out) {
        uint8_t i0[9] = {9, 4, 1, 0, 0, 1, 2, 0, 0}, i1[9] = {9, 4, 1, 1, 1, 1, 2, 0, 0};
        uint8_t ep[7] = {7, 5, 0x01, 0x09, 0xc0, 0, 1};
        memcpy(d + o, i0, 9); o += 9; memcpy(d + o, i1, 9); o += 9; memcpy(d + o, ep, 7); o += 7;
        uint8_t fb[7] = {7, 5, 0x81, 0x11, 4, 0, 1};   /* feedback IN endpoint: must NOT count as capture */
        memcpy(d + o, fb, 7); o += 7;
    }
    if (with_in) {
        uint8_t i0[9] = {9, 4, 2, 0, 0, 1, 2, 0, 0}, i1[9] = {9, 4, 2, 1, 1, 1, 2, 0, 0};
        uint8_t ep[7] = {7, 5, 0x82, 0x05, 0xc0, 0, 1};
        memcpy(d + o, i0, 9); o += 9; memcpy(d + o, i1, 9); o += 9; memcpy(d + o, ep, 7); o += 7;
    }
    if (midi) { uint8_t m[9] = {9, 4, 3, 0, 2, 1, 3, 0, 0}; memcpy(d + o, m, 9); o += 9; }
    return o;
}

static void test_discovery(void) {
    SECTION("USB / ALSA discovery (simulated /proc and /sys trees)");
    uint8_t d[512];
    EfUsbDev u;
    memset(&u, 0, sizeof u); ef_usb_parse_descriptors(d, make_desc(d, true, true, true), &u);
    CHECK(u.as_in && u.as_out && u.midi && u.audio_control, "full duplex + MIDI descriptor parsed");
    memset(&u, 0, sizeof u); ef_usb_parse_descriptors(d, make_desc(d, false, true, true), &u);
    CHECK(!u.as_in && u.as_out, "playback-only device not reported as capture (feedback EP ignored)");
    memset(&u, 0, sizeof u); ef_usb_parse_descriptors(d, 30, &u);  /* truncated */
    CHECK(!u.as_in, "truncated descriptors safe");

    /* scenario A: FM-1 with capture, driver bound, ALSA card present */
    char root[EF_PATH_MAX]; tmp_path(root, sizeof root, "sysA");
    mkfile(root, "/proc/asound/cards", " 0 [rockchiprk817 ]: rockchip_rk817 - rockchip,rk817-codec\n                      rockchip,rk817-codec\n 1 [FM1            ]: USB-Audio - FM-1\n                      M-VAVE FM-1 at usb-ff300000.usb-1, full speed\n");
    mkfile(root, "/proc/asound/pcm", "00-00: ff070000.i2s-rk817-hifi rk817-hifi-0 :  : playback 1 : capture 1\n01-00: USB Audio : USB Audio : playback 1 : capture 1\n");
    mkfile(root, "/proc/asound/card1/usbid", "4c4a:4155\n");
    mkfile(root, "/proc/asound/card1/stream0", "M-VAVE FM-1 at usb-ff300000.usb-1, full speed : USB Audio\n\nPlayback:\n  Status: Stop\n  Interface 1\n    Altset 1\n    Format: S16_LE\n    Channels: 2\n    Endpoint: 1 OUT (ADAPTIVE)\n    Rates: 48000\n\nCapture:\n  Status: Stop\n  Interface 2\n    Altset 1\n    Format: S16_LE\n    Channels: 2\n    Endpoint: 2 IN (ASYNC)\n    Rates: 44100, 48000\n");
    mkfile(root, "/sys/bus/usb/devices/1-1/idVendor", "4c4a\n");
    mkfile(root, "/sys/bus/usb/devices/1-1/idProduct", "4155\n");
    mkfile(root, "/sys/bus/usb/devices/1-1/product", "FM-1\n");
    mkfile(root, "/sys/bus/usb/devices/1-1/manufacturer", "M-VAVE\n");
    mkbin(root, "/sys/bus/usb/devices/1-1/descriptors", d, make_desc(d, true, true, true));
    mkfile(root, "/sys/bus/usb/devices/1-1:1.0/bInterfaceClass", "01\n");
    char drv[EF_PATH_MAX]; snprintf(drv, sizeof drv, "%s/sys/bus/usb/drivers/snd-usb-audio", root); ef_mkdirs(drv);
    char lnk[EF_PATH_MAX]; snprintf(lnk, sizeof lnk, "%s/sys/bus/usb/devices/1-1:1.0/driver", root);
    symlink(drv, lnk);
    snprintf(drv, sizeof drv, "%s/sys/module/snd_usb_audio", root); ef_mkdirs(drv);
    ef_capture_set_sysroot(root);
    static EfInventory inv;
    ef_inventory_scan(&inv);
    CHECK(inv.ncards == 2 && inv.cards[1].is_usb && inv.cards[1].capture_dev == 0, "cards parsed (%d)", inv.ncards);
    CHECK(inv.cards[1].cap_channels == 2 && inv.cards[1].cap_rate_min == 44100 && inv.cards[1].cap_rate_max == 48000 && !strcmp(inv.cards[1].cap_format, "S16_LE"), "stream0 capture caps parsed");
    CHECK(inv.nusb == 1 && inv.usb[0].looks_like_fm1 && inv.usb[0].as_in && inv.usb[0].alsa_card == 1, "FM-1 usb device linked to card");
    CHECK(!strcmp(inv.usb[0].driver, "snd-usb-audio"), "bound driver detected (%s)", inv.usb[0].driver);
    EfInput ins[16];
    int n = ef_inputs_build(&inv, ins, 16);
    CHECK(n == 2 && ins[0].fm1 && ins[0].status == IN_READY && ins[1].internal, "FM-1 listed first, internal last");

    /* scenario B: FM-1 connected (stock firmware) - MIDI + playback, no capture */
    tmp_path(root, sizeof root, "sysB");
    mkfile(root, "/proc/asound/cards", " 0 [rockchiprk817 ]: rockchip_rk817 - rockchip,rk817-codec\n                      rockchip,rk817-codec\n 1 [FM1            ]: USB-Audio - FM-1\n                      M-VAVE FM-1 at usb-1, full speed\n");
    mkfile(root, "/proc/asound/pcm", "00-00: rk817 : : playback 1 : capture 1\n01-00: USB Audio : USB Audio : playback 1\n");
    mkfile(root, "/proc/asound/card1/usbid", "4c4a:4155\n");
    mkfile(root, "/proc/asound/card1/stream0", "FM-1\n\nPlayback:\n  Status: Stop\n  Interface 1\n    Format: S16_LE\n    Channels: 2\n    Rates: 48000\n");
    mkfile(root, "/sys/bus/usb/devices/1-1/idVendor", "4c4a\n");
    mkfile(root, "/sys/bus/usb/devices/1-1/idProduct", "4155\n");
    mkfile(root, "/sys/bus/usb/devices/1-1/product", "FM-1\n");
    mkbin(root, "/sys/bus/usb/devices/1-1/descriptors", d, make_desc(d, false, true, true));
    ef_capture_set_sysroot(root);
    ef_inventory_scan(&inv);
    n = ef_inputs_build(&inv, ins, 16);
    CHECK(n >= 1 && ins[0].fm1 && ins[0].status == IN_PLAYBACK_ONLY, "playback-only FM-1 flagged, not offered as capture");

    /* scenario C: USB audio interface present but kernel has no snd-usb-audio */
    tmp_path(root, sizeof root, "sysC");
    mkfile(root, "/proc/asound/cards", " 0 [rockchiprk817 ]: rockchip_rk817 - rockchip,rk817-codec\n                      rockchip,rk817-codec\n");
    mkfile(root, "/proc/asound/pcm", "00-00: rk817 : : playback 1 : capture 1\n");
    mkfile(root, "/sys/bus/usb/devices/1-1/idVendor", "4c4a\n");
    mkfile(root, "/sys/bus/usb/devices/1-1/idProduct", "4155\n");
    mkfile(root, "/sys/bus/usb/devices/1-1/product", "FM-1\n");
    mkbin(root, "/sys/bus/usb/devices/1-1/descriptors", d, make_desc(d, true, true, false));
    ef_capture_set_sysroot(root);
    ef_inventory_scan(&inv);
    n = ef_inputs_build(&inv, ins, 16);
    CHECK(n == 2 && ins[0].status == IN_NO_DRIVER && !inv.usb_audio_driver, "capture-capable device without driver diagnosed");

    /* scenario E: class-compliant device whose card has no usbid file - linked via sysfs */
    tmp_path(root, sizeof root, "sysE");
    mkfile(root, "/proc/asound/cards", " 0 [rockchiprk817 ]: rockchip_rk817 - rockchip,rk817-codec\n                      rockchip,rk817-codec\n 1 [Sloop          ]: USB-Audio - Sloop\n                      Sloop at usb-ff300000.usb-1.2, full speed\n");
    mkfile(root, "/proc/asound/pcm", "00-00: rk817 : : playback 1 : capture 1\n01-00: USB Audio : USB Audio : playback 1 : capture 1\n");
    mkfile(root, "/sys/bus/usb/devices/1-1.2/idVendor", "1209\n");
    mkfile(root, "/sys/bus/usb/devices/1-1.2/idProduct", "5100\n");
    mkfile(root, "/sys/bus/usb/devices/1-1.2/product", "Sloop\n");
    mkbin(root, "/sys/bus/usb/devices/1-1.2/descriptors", d, make_desc(d, true, true, false));
    char card[EF_PATH_MAX]; snprintf(card, sizeof card, "%s/sys/class/sound/card1", root); ef_mkdirs(card);
    snprintf(lnk, sizeof lnk, "%s/device", card);
    symlink("../../../devices/platform/ff300000.usb/usb1/1-1/1-1.2/1-1.2:1.0", lnk);
    snprintf(card, sizeof card, "%s/sys/class/sound/card0", root); ef_mkdirs(card);
    snprintf(lnk, sizeof lnk, "%s/device", card);
    symlink("../../../devices/platform/rk817-sound", lnk);
    ef_capture_set_sysroot(root);
    ef_inventory_scan(&inv);
    CHECK(inv.nusb == 1 && inv.usb[0].alsa_card == 1 && !strcmp(inv.cards[1].usb_sysname, "1-1.2"), "card linked via sysfs without usbid");
    CHECK(!inv.cards[0].is_usb, "internal card not mistaken for USB");
    n = ef_inputs_build(&inv, ins, 16);
    CHECK(n == 2 && ins[0].status == IN_READY && ins[0].usb_index == 0 && !strcmp(ins[0].label, "[USB] Sloop"), "class-compliant device ready for capture");

    /* scenario D: nothing connected, no /proc/asound at all */
    tmp_path(root, sizeof root, "sysD");
    ef_mkdirs(root);
    ef_capture_set_sysroot(root);
    ef_inventory_scan(&inv);
    n = ef_inputs_build(&inv, ins, 16);
    CHECK(n == 0 && !inv.proc_asound_ok, "missing devices handled (no inputs)");
    ef_capture_set_sysroot(NULL);
}

/* ALSA path with the real libasound "file" plugin feeding a recorded fixture.
 * This exercises snd_pcm_open/hw_params/readi/format conversion/recording and
 * the error paths. It is a SIMULATION - not USB hardware. */
static void test_capture_alsa(void) {
    SECTION("capture via real libasound (file-plugin simulation)");
    char raw[EF_PATH_MAX], conf[EF_PATH_MAX];
    tmp_path(raw, sizeof raw, "fixture.raw");
    FILE *f = fopen(raw, "wb");
    const int frames = 48000 * 3;
    for (int i = 0; i < frames; i++) {
        /* the file plugin replays raw bytes in the negotiated format (S32_LE first choice) */
        int32_t s[2];
        s[0] = (int32_t)(0.366f * 2147483647.0f * sinf(EF_TWOPI * 440.0f * i / 48000.0f));
        s[1] = -s[0];
        fwrite(s, 4, 2, f);
    }
    fclose(f);
    tmp_path(conf, sizeof conf, "asound.conf");
    char txt[1024];
    snprintf(txt, sizeof txt,
             "pcm.effixture {\n type file\n slave.pcm \"efnull\"\n file \"/dev/null\"\n infile \"%s\"\n format \"raw\"\n}\n"
             "pcm.efnull {\n type null\n }\n", raw);
    ef_write_text_file_atomic(conf, txt);
    setenv("ALSA_CONFIG_PATH", conf, 1);
    static EfCapture cap;
    if (!ef_capture_init(&cap) || !cap.alsa_loaded) { printf("  SKIP: libasound not available (%s)\n", cap.alsa_error); return; }
    EfInput in;
    memset(&in, 0, sizeof in);
    ef_strlcpy(in.label, "fixture", sizeof in.label);
    ef_strlcpy(in.key, "test:effixture", sizeof in.key);
    ef_strlcpy(in.pcm_override, "effixture", sizeof in.pcm_override);
    in.status = IN_TEST;
    ef_capture_open(&cap, &in);
    for (int i = 0; i < 200 && atomic_load(&cap.state) != CAP_RUNNING && atomic_load(&cap.state) != CAP_ERROR; i++) SDL_Delay(10);
    CHECK(atomic_load(&cap.state) == CAP_RUNNING, "file-plugin capture opened (state %s: %s)", ef_capture_state_name(atomic_load(&cap.state)), cap.error);
    if (atomic_load(&cap.state) == CAP_RUNNING) {
        CHECK(cap.dev_channels == 2 && cap.rate > 0, "negotiated %s %d ch %d Hz", cap.format_name, cap.dev_channels, cap.rate);
        char tp[EF_PATH_MAX]; tmp_path(tp, sizeof tp, ".rec_test.wav");
        CHECK(ef_capture_rec_start(&cap, 1.0f, tp), "record start");
        for (int i = 0; i < 300 && atomic_load(&cap.rec_state) != REC_DONE; i++) SDL_Delay(10);
        CHECK(atomic_load(&cap.rec_state) == REC_DONE && atomic_load(&cap.rec_full), "record stops at max length");
        long nfr = atomic_load(&cap.rec_frames);
        CHECK(nfr == cap.rec_cap, "frames %ld == cap %ld", nfr, cap.rec_cap);
        float pk = 0; bool inverted = true;
        for (long i = 0; i < nfr; i++) { pk = EF_MAX(pk, fabsf(cap.rec_buf[i * 2])); if (fabsf(cap.rec_buf[i * 2] + cap.rec_buf[i * 2 + 1]) > 1e-3f) inverted = false; }
        CHECK(pk > 0.3f && pk < 0.4f && inverted, "recorded data matches fixture (peak %f, stereo kept)", (double)pk);
        CHECK(atomic_load(&cap.signal_seen) == 1, "signal detection fired");
        ef_capture_rec_finish(&cap);
        EfWavInfo wi;
        EfSample *ws = ef_wav_load(tp, &wi);
        CHECK(ws && ws->frames == nfr && ws->channels == 2, "temp take on disk is complete and valid");
        ef_sample_free(ws);
        ef_capture_rec_release(&cap);
        char msg[128] = "";
        int rec = ef_recover_recordings(g_tmp, msg, sizeof msg);
        CHECK(rec == 1, "orphaned temp take recovered (%s)", msg);
    }
    /* failure: a device that does not exist */
    EfInput bad = in;
    ef_strlcpy(bad.pcm_override, "nonexistent_pcm_xyz", sizeof bad.pcm_override);
    ef_capture_open(&cap, &bad);
    for (int i = 0; i < 200 && atomic_load(&cap.state) != CAP_ERROR; i++) SDL_Delay(10);
    CHECK(atomic_load(&cap.state) == CAP_ERROR && cap.error[0], "missing device -> clear error: %s", cap.error);
    CHECK(!ef_capture_rec_start(&cap, 1.0f, NULL), "cannot record without an open stream");
    /* shutdown while recording */
    ef_capture_open(&cap, &in);
    for (int i = 0; i < 200 && atomic_load(&cap.state) != CAP_RUNNING; i++) SDL_Delay(10);
    char tp2[EF_PATH_MAX]; tmp_path(tp2, sizeof tp2, ".rec_exit.wav");
    bool started = ef_capture_rec_start(&cap, 30.0f, tp2);
    SDL_Delay(100);
    ef_capture_shutdown(&cap);
    EfWavInfo wi2;
    CHECK(started && ef_wav_repair(tp2) && ef_wav_probe(tp2, &wi2) && wi2.frames > 0, "exit while recording leaves a recoverable take");
    unsetenv("ALSA_CONFIG_PATH");
}

int main(int argc, char **argv) {
    SDL_Init(SDL_INIT_TIMER);
    snprintf(g_tmp, sizeof g_tmp, "/tmp/echofield_test_%ld", (long)time(NULL));
    ef_mkdirs(g_tmp);
    char data[EF_PATH_MAX]; snprintf(data, sizeof data, "%s/data", g_tmp);
    ef_paths_init(data);
    test_wav();
    test_engine();
    test_store();
    test_discovery();
    test_capture_alsa();
    printf("\n%d passed, %d failed  (simulated environment: %s)\n", g_pass, g_fail, argc > 1 ? argv[1] : "desktop");
    return g_fail ? 1 : 0;
}
