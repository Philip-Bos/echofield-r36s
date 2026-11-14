/* ECHOFIELD - granular engine, synthesis modes, DRIFT ORACLE and effects chain.
 *
 * Real-time rules followed in this file: no allocation, no locks, no I/O and no
 * unbounded loops inside ef_engine_render(). All buffers are allocated in
 * ef_engine_init(). Grain parameters are sampled at grain birth, continuous
 * parameters are smoothed, so rapid control changes never click.
 */
#include "engine.h"

/* ------------------------------------------------------------------ helpers */
static inline float tanh_fast(float x) {
    x = EF_CLAMP(x, -3.0f, 3.0f);
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
static inline float smooth_to(float cur, float tgt, float a) { return cur + (tgt - cur) * a; }
static inline float onepole_coef(float hz, float sr) { return 1.0f - expf(-EF_TWOPI * hz / sr); }

float ef_window_value(int win, float x) {
    x = EF_CLAMP(x, 0.0f, 1.0f);
    switch (win) {
    default:
    case WIN_HANN: return 0.5f - 0.5f * cosf(EF_TWOPI * x);
    case WIN_TUKEY: {
        const float a = 0.3f;
        if (x < a * 0.5f) return 0.5f - 0.5f * cosf(EF_TWOPI * x / a);
        if (x > 1.0f - a * 0.5f) return 0.5f - 0.5f * cosf(EF_TWOPI * (1.0f - x) / a);
        return 1.0f;
    }
    case WIN_PERC: {
        const float att = 0.03f;
        float v = x < att ? sinf(0.5f * EF_PI * x / att) : expf(-5.0f * (x - att) / (1.0f - att));
        if (x > 0.92f) v *= 0.5f + 0.5f * cosf(EF_PI * (x - 0.92f) / 0.08f);  /* reach exactly 0 */
        return v;
    }
    case WIN_SWELL: return ef_window_value(WIN_PERC, 1.0f - x);
    case WIN_GAUSS: {
        float g = expf(-0.5f * ((x - 0.5f) / 0.16f) * ((x - 0.5f) / 0.16f));
        float e = expf(-0.5f * (0.5f / 0.16f) * (0.5f / 0.16f));
        return (g - e) / (1.0f - e);
    }
    }
}

/* ------------------------------------------------------------------ init */
bool ef_engine_init(EfEngine *e, int sr, EfParams *params) {
    memset(e, 0, sizeof *e);
    e->sr = sr;
    e->P = params;
    e->rng.s = 0x1234567u;
    e->fx.rng.s = 0x89abcdefu;
    atomic_store(&e->region_a, 0.0f);
    atomic_store(&e->region_b, 1.0f);
    atomic_store(&e->sample_gain, 1.0f);
    atomic_store(&e->max_voices, 40);
    atomic_store(&e->monitor_gain, 0.8f);
    for (int w = 0; w < WIN_COUNT; w++) {
        for (int i = 0; i <= EF_WIN_N; i++) e->win[w][i] = ef_window_value(w, (float)i / EF_WIN_N);
        e->win[w][EF_WIN_N + 1] = 0.0f;
    }
    EfFx *f = &e->fx;
    f->tape_len = 4096;
    f->tape_buf = calloc((size_t)f->tape_len * 2, sizeof(float));
    f->dly_len = sr * 2 + 8;
    f->dly[0] = calloc((size_t)f->dly_len, sizeof(float));
    f->dly[1] = calloc((size_t)f->dly_len, sizeof(float));
    static const int base[8] = {1499, 1889, 2381, 2803, 3191, 3607, 4027, 4513};
    for (int i = 0; i < 8; i++) {
        f->fdn_len[i] = (int)((float)base[i] * sr / 48000.0f);
        f->fdn_cap[i] = f->fdn_len[i] + 64;
        f->fdn[i] = calloc((size_t)f->fdn_cap[i], sizeof(float));
        if (!f->fdn[i]) return false;
    }
    static const int apb[4] = {142, 107, 379, 277};
    for (int i = 0; i < 4; i++) {
        f->ap_len[i] = (int)((float)apb[i] * sr / 48000.0f) + 1;
        f->ap[i] = calloc((size_t)f->ap_len[i], sizeof(float));
        if (!f->ap[i]) return false;
    }
    f->shim_len = 4096;
    f->shim = calloc((size_t)f->shim_len, sizeof(float));
    if (!f->tape_buf || !f->dly[0] || !f->dly[1] || !f->shim) return false;
    f->lim_gain = 1.0f;
    f->agc_gain = 1.0f;
    f->age_gain = 1.0f;
    f->cut_smooth = 1.0f;
    f->dly_time_smooth = 380.0f;
    ef_engine_reset_state(e);
    for (int i = 0; i < P_COUNT; i++) e->sm[i] = ef_param_get(params, i);
    return true;
}

void ef_engine_reset_state(EfEngine *e) {
    for (int i = 0; i < EF_MAX_GRAINS; i++) e->grains[i].active = false;
    e->spawn_wait = 0;
    e->scan = 0;
    e->tape_head = 0;
    e->tape_speed = 1.0f;
    e->tape_stop = 0.0f;
    e->play_gain = 1.0f;
    for (int i = 0; i < 16; i++) { e->step_pos[i] = ef_rng_f(&e->rng); e->step_oct[i] = 0; }
    e->oracle.timer = 0.0f;
    e->oracle.nchord = 1;
    e->oracle.chord[0] = 0;
    e->fade_gain = 1.0f;
}

void ef_engine_free(EfEngine *e) {
    EfFx *f = &e->fx;
    free(f->tape_buf); free(f->dly[0]); free(f->dly[1]); free(f->shim);
    for (int i = 0; i < 8; i++) free(f->fdn[i]);
    for (int i = 0; i < 4; i++) free(f->ap[i]);
    if (e->cur) ef_sample_free(e->cur);
    if (e->next) ef_sample_free(e->next);
    if (e->retired) ef_sample_free(e->retired);
    memset(e, 0, sizeof *e);
}

void ef_engine_set_sample(EfEngine *e, EfSample *s) {
    if (e->next && e->next != s) {          /* replaced before it was used: retire it too */
        if (!e->retired) e->retired = e->next; else ef_sample_free(e->next);
    }
    e->next = s;
    if (!e->cur) { e->cur = s; e->next = NULL; e->fade_gain = 0.0f; }
    else { e->fade_len = e->sr / 50; e->fade_left = e->fade_len; }   /* 20 ms fade out */
}

EfSample *ef_engine_take_retired(EfEngine *e) { EfSample *r = e->retired; e->retired = NULL; return r; }

void ef_engine_preview(EfEngine *e, EfSample *s, int a, int b, bool loop) {
    e->preview = s;
    if (s) {
        e->preview_a = EF_CLAMP(a, 0, s->frames - 1);
        e->preview_b = EF_CLAMP(b, e->preview_a + 1, s->frames);
        e->preview_pos = e->preview_a;
        e->preview_loop = loop;
        e->preview_fade = 0.0f;
        atomic_store(&e->preview_playing, 1);
    } else atomic_store(&e->preview_playing, 0);
}

int ef_engine_count_active(const EfEngine *e) {
    int n = 0;
    for (int i = 0; i < EF_MAX_GRAINS; i++) n += e->grains[i].active;
    return n;
}

/* ------------------------------------------------------------------ DRIFT ORACLE */
static int scale_semi(const EfScale *sc, int deg) {
    int oct = deg >= 0 ? deg / sc->n : -((-deg + sc->n - 1) / sc->n);
    int d = deg - oct * sc->n;
    return sc->deg[d] + 12 * oct;
}

static void oracle_event(EfEngine *e, const char *name) {
    ef_strlcpy(e->oracle.event, name, sizeof e->oracle.event);
    ef_strlcpy(e->oracle_event_ui, name, sizeof e->oracle_event_ui);
    atomic_fetch_add(&e->oracle_event_seq, 1);
}

static void oracle_update(EfEngine *e, float dt) {
    EfOracle *o = &e->oracle;
    o->on = e->sm[P_OR_ON] > 0.5f;
    float glide = e->sm[P_OR_GLIDE];
    float tc = 0.05f + glide * glide * 4.0f;
    o->glide_cur = smooth_to(o->glide_cur, o->on ? o->glide_target : 0.0f, 1.0f - expf(-dt / tc));
    o->bloom = EF_MAX(0.0f, o->bloom - dt);
    o->scatter_burst = EF_MAX(0.0f, o->scatter_burst - dt);
    o->rev_swell = EF_MAX(0.0f, o->rev_swell - dt);
    o->octave_flick = EF_MAX(0.0f, o->octave_flick - dt);
    o->hush = EF_MAX(0.0f, o->hush - dt);
    if (!o->on) return;
    bool hold = e->sm[P_OR_HOLD] > 0.5f || e->sm[P_FREEZE] > 0.5f;
    if (hold) return;
    int si = EF_CLAMP((int)e->sm[P_OR_SCALE], 0, g_scale_count - 1);
    const EfScale *sc = &g_scales[si];
    float act = e->sm[P_OR_ACTIVITY], amb = e->sm[P_OR_AMBIG];
    bool drone = e->sm[P_OR_DRONE] > 0.5f;
    int range = (int)e->sm[P_OR_RANGE];
    o->timer -= dt;
    if (si != o->last_scale || drone != o->last_drone) { o->timer = 0.0f; o->last_scale = si; o->last_drone = drone; }   /* react at once */
    if (o->timer <= 0.0f) {
        /* weighted choice of the next bass degree: root and fifth favoured, ambiguity flattens */
        float w[12]; float tot = 0;
        for (int d = 0; d < sc->n; d++) {
            int semi = sc->deg[d];
            float base = semi == 0 ? 4.0f : (semi == 7 ? 3.0f : ((semi == 3 || semi == 4) ? 2.2f : (semi == 5 ? 1.6f : 1.0f)));
            if (drone) base = semi == 0 ? 6.0f : ((semi == 7 || semi == 5) ? 2.5f : 0.15f);
            w[d] = powf(base, 1.0f - amb * 0.85f);
            if (d == o->bass_deg) w[d] *= 0.5f;   /* encourage movement */
            tot += w[d];
        }
        float r = ef_rng_f(&e->rng) * tot;
        int pick = 0;
        for (int d = 0; d < sc->n; d++) { r -= w[d]; if (r <= 0) { pick = d; break; } }
        o->bass_deg = pick;
        int oct = 0;
        if (range > 0 && ef_rng_f(&e->rng) < 0.25f * range) oct = (ef_rng_f(&e->rng) < 0.6f ? 0 : 1);
        if (range > 1 && ef_rng_f(&e->rng) < 0.15f) oct = -1;
        o->glide_target = (float)(scale_semi(sc, pick) + 12 * oct);
        /* chord built from scale thirds, relative to the bass */
        int b = scale_semi(sc, pick);
        o->nchord = 0;
        o->chord[o->nchord++] = 0;
        if (drone) { o->chord[o->nchord++] = 7; o->chord[o->nchord++] = 12; }
        else {
            int third = scale_semi(sc, pick + 2) - b;
            if (ef_rng_f(&e->rng) < amb * 0.4f) third = scale_semi(sc, pick + (ef_rng_f(&e->rng) < 0.5f ? 1 : 3)) - b; /* sus */
            o->chord[o->nchord++] = third;
            o->chord[o->nchord++] = scale_semi(sc, pick + 4) - b;
            if (amb > 0.5f && ef_rng_f(&e->rng) < amb - 0.4f) o->chord[o->nchord++] = scale_semi(sc, pick + 6) - b; /* 7th */
        }
        atomic_store(&e->oracle_note, scale_semi(sc, pick) + (int)e->sm[P_OR_ROOT]);
        float mean = ef_lerp(14.0f, 1.2f, act) * (drone ? 3.0f : 1.0f);
        o->timer = mean * (0.5f + ef_rng_f(&e->rng));
    }
    /* chance events */
    float p = powf(act, 1.5f) * 0.35f * dt * (drone ? 0.3f : 1.0f);
    if (ef_rng_f(&e->rng) < p) {
        int k = (int)(ef_rng_f(&e->rng) * 6.0f);
        switch (k) {
        case 0: o->bloom = 2.0f + 2.0f * ef_rng_f(&e->rng); oracle_event(e, "BLOOM"); break;
        case 1: o->scatter_burst = 1.5f; oracle_event(e, "SCATTER"); break;
        case 2: o->rev_swell = 2.0f; oracle_event(e, "REVERSE SWELL"); break;
        case 3: o->octave_flick = 1.5f; oracle_event(e, "OCTAVE FLICKER"); break;
        case 4: e->scan += (ef_rng_f(&e->rng) - 0.5f) * 0.5f; oracle_event(e, "LEAP"); break;
        default: o->hush = 2.0f; oracle_event(e, "HUSH"); break;
        }
    }
}

/* semitone offset for a newly born grain (relative to PITCH) */
static float oracle_grain_note(EfEngine *e) {
    EfOracle *o = &e->oracle;
    if (!o->on) return 0.0f;
    float root = e->sm[P_OR_ROOT];
    float n = root + o->glide_cur;
    float r = ef_rng_f(&e->rng);
    int idx = r < 0.4f ? 0 : 1 + (int)(ef_rng_f(&e->rng) * (float)(o->nchord - 1));
    idx = EF_CLAMP(idx, 0, o->nchord - 1);
    n += (float)o->chord[idx];
    int range = (int)e->sm[P_OR_RANGE];
    if (range > 0 && ef_rng_f(&e->rng) < 0.2f * range) n += 12.0f;
    if (e->sm[P_OR_DRONE] > 0.5f && ef_rng_f(&e->rng) < 0.2f) n -= 12.0f;
    if (ef_rng_f(&e->rng) < e->sm[P_OR_AMBIG] * 0.12f) n += ef_rng_f(&e->rng) < 0.5f ? -1.0f : 1.0f;   /* passing tone */
    if (o->octave_flick > 0 && ef_rng_f(&e->rng) < 0.4f) n += 12.0f;
    return n;
}

/* ------------------------------------------------------------------ mode controls */
typedef struct {
    float size_s, density, jitter, center, scatter;
    float pitch_rand, rev_prob, pan_spread, amp, tone, tone_rand;
    int win;
    bool clocked, tape;
    float tape_speed;
} Ctl;

static void euclid(int k, int n, int rot, bool *out) {
    for (int i = 0; i < n; i++) out[i] = false;
    if (k <= 0) return;
    for (int i = 0; i < n; i++) out[(i + rot) % n] = ((i * k) % n) < k;
}

static void compute_ctl(EfEngine *e, Ctl *c, float dt) {
    float *s = e->sm;
    int mode = (int)s[P_MODE];
    bool frozen = s[P_FREEZE] > 0.5f;
    float T = EF_CLAMP(s[P_TEXTURE] + atomic_load(&e->perf_texture), 0.0f, 1.0f);
    float S = EF_CLAMP(s[P_SCATTER] + atomic_load(&e->perf_scatter), 0.0f, 1.0f);
    float D = s[P_DRIFT], M = s[P_MOTION], W = s[P_WIDTH];
    float size_mul = exp2f(s[P_SIZE_MUL]), dens_mul = exp2f(s[P_DENS_MUL]);
    EfOracle *o = &e->oracle;

    /* modulators */
    if (!frozen) {
        e->lfo1 += dt * (0.03f + 0.3f * M);
        if (e->lfo1 > 1.0f) e->lfo1 -= 1.0f;
        e->lfo2_t += dt * (0.1f + 1.5f * M);
        if (e->lfo2_t >= 1.0f) { e->lfo2_t -= 1.0f; e->lfo2_tgt = ef_rng_bi(&e->rng); }
        e->lfo2_cur = smooth_to(e->lfo2_cur, e->lfo2_tgt, EF_MIN(1.0f, dt * (0.5f + 3.0f * M)));
        /* random walks (bounded, mean-reverting) */
        float wr = (mode == MODE_DRIFT ? 0.25f + 1.2f * M : 0.08f + 0.4f * M) * dt;
        e->walk_v1 += ef_rng_bi(&e->rng) * wr * 2.0f - e->walk_v1 * wr * 2.0f;
        e->walk_pos = EF_CLAMP(e->walk_pos + e->walk_v1 * wr * 4.0f - e->walk_pos * wr * 0.3f, -1.0f, 1.0f);
        e->walk_v2 += ef_rng_bi(&e->rng) * wr * 2.0f - e->walk_v2 * wr * 2.0f;
        e->walk_pitch = EF_CLAMP(e->walk_pitch + e->walk_v2 * wr * 4.0f - e->walk_pitch * wr * 0.3f, -1.0f, 1.0f);
        e->walk_size = smooth_to(e->walk_size, ef_rng_bi(&e->rng), dt * 0.3f);
        if (e->cur) {   /* SCAN = 1 moves through the region at real-time speed */
            float rsec = (atomic_load(&e->region_b) - atomic_load(&e->region_a)) * e->cur->frames / (float)e->sr;
            e->scan += (double)(s[P_SCAN] * dt / EF_MAX(0.05f, fabsf(rsec)));
            e->scan -= floor(e->scan);
        }
    }
    float lfo1 = sinf(EF_TWOPI * e->lfo1);
    float pos = EF_CLAMP(s[P_POSITION] + atomic_load(&e->perf_pos), 0.0f, 1.0f);

    /* a few cents of random detune on every grain: chorus-like thickness */
    c->jitter = 0.5f; c->pitch_rand = s[P_PITCH_RAND] + 0.08f + D * 0.3f; c->rev_prob = s[P_REVERSE];
    c->pan_spread = W; c->amp = 1.0f; c->tone = s[P_GRAIN_TONE]; c->tone_rand = 0.0f; c->win = WIN_HANN;
    c->clocked = false; c->tape = false; c->tape_speed = 1.0f;
    float wander = D * 0.3f * e->walk_pos;

    switch (mode) {
    default:
    case MODE_CLOUD:
        c->size_s = 0.12f * powf(1500.0f / 120.0f, T);
        c->density = (4.0f + 8.0f * T) / c->size_s * (1.0f + 0.4f * M * e->lfo2_cur);
        c->jitter = 0.4f + 0.6f * S;
        c->center = pos + 0.15f * M * lfo1 + wander;
        c->scatter = 0.03f + 0.6f * powf(S, 1.3f);
        c->rev_prob += S * 0.1f;
        break;
    case MODE_SHARDS:
        c->size_s = 0.02f * powf(200.0f / 20.0f, T);
        c->density = (6.0f + 24.0f * (1.0f - T * 0.5f)) * (0.5f + S);
        c->jitter = 0.7f + 0.3f * S;
        c->center = pos + 0.1f * M * lfo1 + wander;
        c->scatter = 0.1f + 0.85f * S;
        c->rev_prob += 0.2f + 0.3f * S;
        c->win = WIN_GAUSS;
        c->amp = 0.85f;
        c->tone = EF_CLAMP(s[P_GRAIN_TONE] + 0.25f, -1.0f, 1.0f);
        c->tone_rand = 0.3f;
        c->pan_spread = EF_MIN(1.0f, W * 1.3f);
        break;
    case MODE_TAPE: {
        float target = ef_semis_to_ratio(s[P_PITCH] + atomic_load(&e->perf_pitch)) * ef_lerp(1.0f, 0.3f, powf(T, 1.2f));
        target *= 1.0f + 0.03f * M * e->lfo2_cur + 0.02f * D * e->walk_pitch;
        if (!frozen && e->tape_stop <= 0.0f && ef_rng_f(&e->rng) < M * 0.02f * dt) e->tape_stop = 1.6f;
        if (e->tape_stop > 0.0f) {   /* motor stop and restart */
            e->tape_stop -= dt;
            float ph = 1.6f - e->tape_stop;
            target *= ph < 0.8f ? (1.0f - ph / 0.8f) : ((ph - 0.8f) / 0.8f);
            target = EF_MAX(target, 0.02f);
        }
        e->tape_speed = smooth_to(e->tape_speed, target, EF_MIN(1.0f, dt * 6.0f));
        c->tape = true;
        c->tape_speed = e->tape_speed;
        c->size_s = 0.18f * (1.0f + T);
        c->density = 4.0f / c->size_s;
        c->jitter = 0.15f;
        c->center = (float)e->tape_head;
        c->scatter = 0.002f + S * 0.03f;
        c->pitch_rand = s[P_PITCH_RAND];
        c->pan_spread = W * 0.8f;   /* the three heads sit across the stereo field */
        break;
    }
    case MODE_GHOST:
        c->size_s = 0.6f * powf(2.5f / 0.6f, T);
        c->density = (3.0f + 3.0f * T) / c->size_s * (1.0f + 0.5f * M * e->lfo2_cur);
        c->jitter = 0.9f;
        c->center = pos + 0.2f * M * lfo1 + wander;
        c->scatter = 0.08f + 0.5f * S;
        c->rev_prob += 0.25f;
        c->tone = EF_CLAMP(s[P_GRAIN_TONE] + 0.15f, -1.0f, 1.0f);
        c->tone_rand = 0.25f;
        c->win = (ef_rng_f(&e->rng) < 0.5f) ? WIN_HANN : WIN_GAUSS;
        c->amp = 0.95f;
        break;
    case MODE_PULSE:
        c->clocked = true;
        c->size_s = 0.06f * powf(1200.0f / 60.0f, T);
        c->density = 0.0f;
        c->center = pos + wander;
        c->scatter = S;
        c->win = T > 0.6f ? WIN_HANN : WIN_PERC;
        c->jitter = 0.0f;
        break;
    case MODE_DRIFT:
        c->size_s = 0.1f * powf(12.0f, T) * (1.0f + 0.5f * e->walk_size);
        c->density = (2.0f + 4.0f * T) * (1.0f + 0.5f * lfo1) / c->size_s;
        c->jitter = 0.8f;
        c->center = pos + e->walk_pos * (0.1f + 0.5f * S);
        c->scatter = 0.03f + 0.3f * S;
        c->pitch_rand = s[P_PITCH_RAND] + D * 0.5f;
        c->rev_prob += 0.15f;
        c->win = WIN_GAUSS;
        break;
    }
    c->size_s *= size_mul;
    c->density *= dens_mul;
    /* oracle event envelopes */
    if (o->on) {
        if (o->bloom > 0) c->density *= 2.5f;
        if (o->hush > 0) c->density *= 0.3f;
        if (o->scatter_burst > 0) c->scatter = EF_MIN(1.0f, c->scatter + 0.4f);
        if (o->rev_swell > 0) c->rev_prob += 0.6f;
        c->jitter = EF_MIN(1.0f, c->jitter + s[P_OR_ACTIVITY] * 0.3f);
    }
    int wsel = (int)s[P_WINDOW];
    if (wsel > 0) c->win = wsel - 1;
    c->rev_prob = EF_CLAMP(c->rev_prob, 0.0f, 1.0f);
    c->size_s = EF_CLAMP(c->size_s, 0.004f, 2.5f);
    c->density = EF_CLAMP(c->density, 0.0f, 400.0f);
    c->scatter = EF_CLAMP(c->scatter, 0.0f, 1.0f);
    /* scan + wrap center into 0..1 */
    if (!c->tape) {
        float cc = c->center + (float)e->scan;
        cc -= floorf(cc);
        c->center = cc;
    }
}

/* ------------------------------------------------------------------ grains */
static void spawn_grain(EfEngine *e, const Ctl *c, int offset, float amp_scale, float extra_pitch, float pos_override) {
    EfSample *smp = e->cur;
    if (!smp || smp->frames < 64) return;
    int maxv = EF_CLAMP(atomic_load_explicit(&e->max_voices, memory_order_relaxed), 4, EF_MAX_GRAINS);
    int slot = -1, active = 0;
    for (int i = 0; i < EF_MAX_GRAINS; i++) {
        if (e->grains[i].active) active++;
        else if (slot < 0) slot = i;
    }
    if (slot < 0 || active >= maxv) { atomic_fetch_add_explicit(&e->spawn_skipped, 1, memory_order_relaxed); return; }
    EfGrain *g = &e->grains[slot];
    float ra = atomic_load_explicit(&e->region_a, memory_order_relaxed);
    float rb = atomic_load_explicit(&e->region_b, memory_order_relaxed);
    if (rb < ra) { float t = ra; ra = rb; rb = t; }
    double rs = (double)ra * smp->frames, re = (double)rb * smp->frames;
    if (re - rs < 64.0) { re = EF_MIN((double)smp->frames, rs + 64.0); rs = re - 64.0; if (rs < 0) rs = 0; }
    double rlen = re - rs;

    float size = c->size_s * (1.0f + ef_rng_bi(&e->rng) * e->sm[P_SIZE_RAND] * 0.7f * (c->tape ? 0.3f : 1.0f));
    int len = (int)(size * e->sr);
    float pitch = e->sm[P_PITCH] + atomic_load_explicit(&e->perf_pitch, memory_order_relaxed);
    /* HARMONY: some grains sound an octave or a fifth away (a choir made of the sample) */
    float harm = 0.0f;
    if (ef_rng_f(&e->rng) < e->sm[P_HARMONY] * 0.65f) {
        static const float iv[8] = {12, 12, -12, 7, 19, 24, -5, 7};
        harm = iv[ef_rng_u32(&e->rng) & 7];
        amp_scale *= 0.7f;
    }
    /* TAPE: three loop heads a third apart, at normal, half and double speed */
    int head = 0;
    if (c->tape) { head = e->tape_k; e->tape_k = (e->tape_k + 1) % 3; }
    static const float head_ratio[3] = {1.0f, 0.5f, 2.0f}, head_amp[3] = {1.0f, 0.8f, 0.4f};
    double inc;
    if (c->tape) {
        inc = c->tape_speed * head_ratio[head] * ef_semis_to_ratio(harm);
        amp_scale *= head_amp[head];
    } else {
        float st = pitch + harm + oracle_grain_note(e) + extra_pitch + ef_rng_bi(&e->rng) * c->pitch_rand
                 + e->walk_pitch * e->sm[P_DRIFT] * 2.0f;
        st = EF_CLAMP(st, -48.0f, 48.0f);
        g->vis_pitch = st;
        inc = (double)ef_semis_to_ratio(st);
    }
    if (c->tape) g->vis_pitch = 12.0f * log2f(EF_MAX(0.01f, (float)inc));
    if (len < 64) len = 64;
    double span = len * inc;
    if (span > rlen * 0.98) { len = (int)(rlen * 0.98 / inc); span = len * inc; }
    if (len < 32) return;
    bool rev = ef_rng_f(&e->rng) < c->rev_prob;
    if (atomic_load_explicit(&e->region_reverse, memory_order_relaxed)) rev = !rev;
    float center = pos_override >= 0.0f ? pos_override : c->center + ef_rng_bi(&e->rng) * c->scatter * 0.5f;
    if (c->tape) center = c->center + head / 3.0f + ef_rng_bi(&e->rng) * c->scatter;
    center -= floorf(center);
    if (atomic_load_explicit(&e->region_reverse, memory_order_relaxed)) center = 1.0f - center;
    double cpos = rs + center * rlen;
    double start;
    if (!rev) { start = cpos - span * 0.5; start = EF_CLAMP(start, rs, re - span); }
    else { start = cpos + span * 0.5; start = EF_CLAMP(start, rs + span, re - 1.0); }
    g->pos = start;
    g->inc = rev ? -inc : inc;
    g->len = len; g->age = 0; g->delay = offset;
    g->win = (uint8_t)EF_CLAMP(c->win, 0, WIN_COUNT - 1);
    g->wpos = 0.0f;
    g->wstep = (float)EF_WIN_N / (float)len;
    float pan = 0.5f + ef_rng_bi(&e->rng) * 0.5f * c->pan_spread;
    if (c->tape) pan = EF_CLAMP(0.5f + (head == 1 ? -0.35f : (head == 2 ? 0.35f : 0.0f)) * c->pan_spread + ef_rng_bi(&e->rng) * 0.1f, 0.0f, 1.0f);
    /* mono sources: the right ear hears the grain a few ms later -> real stereo image */
    g->roff = smp->channels == 2 ? 0.0f : ef_rng_bi(&e->rng) * 0.018f * e->sr * c->pan_spread;
    float overlap = EF_MAX(1.0f, c->density * c->size_s);
    float amp = c->amp * amp_scale * 0.75f / sqrtf(overlap) * atomic_load_explicit(&e->sample_gain, memory_order_relaxed);
    if (e->cur->channels == 2) { g->gl = amp * sqrtf(1.0f - pan) * 1.41421f * 0.85f; g->gr = amp * sqrtf(pan) * 1.41421f * 0.85f; }
    else { g->gl = amp * sqrtf(1.0f - pan) * 1.41421f; g->gr = amp * sqrtf(pan) * 1.41421f; }
    float tone = EF_CLAMP(c->tone + ef_rng_bi(&e->rng) * c->tone_rand, -1.0f, 1.0f);
    if (fabsf(tone) < 0.02f) { g->tone_a = 1.0f; g->tone_mix = 0.0f; }
    else if (tone < 0) { g->tone_a = onepole_coef(18000.0f * powf(0.02f, -tone), (float)e->sr); g->tone_mix = -1.0f; }
    else { g->tone_a = onepole_coef(150.0f * powf(30.0f, tone), (float)e->sr); g->tone_mix = 1.0f; }
    g->zl = g->zr = 0.0f;
    g->active = true;
}

static inline float hermite(const float *d, double pos, int frames) {
    if (pos < 1.0) pos = 1.0;
    if (pos > frames - 3) pos = frames - 3;
    int i = (int)pos;
    float t = (float)(pos - i);
    float xm1 = d[i - 1], x0 = d[i], x1 = d[i + 1], x2 = d[i + 2];
    float c1 = 0.5f * (x1 - xm1);
    float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}
static inline float linear(const float *d, double pos, int frames) {
    if (pos < 0.0) pos = 0.0;
    if (pos > frames - 2) pos = frames - 2;
    int i = (int)pos;
    float t = (float)(pos - i);
    return d[i] + (d[i + 1] - d[i]) * t;
}

static void render_grains(EfEngine *e, int n) {
    EfSample *s = e->cur;
    if (!s) return;
    bool eco = atomic_load_explicit(&e->eco, memory_order_relaxed) != 0;
    bool stereo = s->channels == 2;
    for (int k = 0; k < EF_MAX_GRAINS; k++) {
        EfGrain *g = &e->grains[k];
        if (!g->active) continue;
        const float *win = e->win[g->win];
        int i = g->delay;
        g->delay = 0;
        for (; i < n; i++) {
            int wi = (int)g->wpos;
            float wf = g->wpos - wi;
            float w = win[wi] + (win[wi + 1] - win[wi]) * wf;
            float l, r;
            if (eco) { l = linear(s->L, g->pos, s->frames); r = stereo ? linear(s->R, g->pos, s->frames) : (g->roff != 0.0f ? linear(s->L, g->pos + g->roff, s->frames) : l); }
            else { l = hermite(s->L, g->pos, s->frames); r = stereo ? hermite(s->R, g->pos, s->frames) : (g->roff != 0.0f ? hermite(s->L, g->pos + g->roff, s->frames) : l); }
            if (g->tone_mix != 0.0f) {
                g->zl += g->tone_a * (l - g->zl);
                g->zr += g->tone_a * (r - g->zr);
                if (g->tone_mix < 0) { l = g->zl; r = g->zr; } else { l -= g->zl; r -= g->zr; }
            }
            e->busL[i] += l * w * g->gl;
            e->busR[i] += r * w * g->gr;
            g->pos += g->inc;
            g->wpos += g->wstep;
            if (++g->age >= g->len || g->wpos >= (float)EF_WIN_N) { g->active = false; break; }
        }
    }
}

static void schedule_grains(EfEngine *e, const Ctl *c, int n, float dt) {
    if (!e->cur) return;
    float gate = e->sm[P_PLAY] > 0.5f ? 1.0f : 0.0f;
    if (gate <= 0.0f) return;
    if (c->clocked) {
        /* PULSE: euclidean pattern on a 16th-note clock */
        float bpm = 30.0f + 90.0f * e->sm[P_MOTION];
        double step_samples = (double)e->sr * 60.0 / bpm / 4.0;
        int k = EF_CLAMP((int)lrint_ef(5.0f + e->sm[P_DENS_MUL] * 2.5f), 1, 16);
        if (e->oracle.on && e->oracle.bloom > 0) k = EF_MIN(16, k + 3);
        bool pat[16];
        euclid(k, 16, e->pulse_rot, pat);
        for (int i = 0; i < n; i++) {
            e->pulse_acc -= 1.0;
            if (e->pulse_acc <= 0.0) {
                e->pulse_acc += step_samples * (1.0 + ((e->pulse_step & 1) ? 0.08 * e->sm[P_DRIFT] : -0.08 * e->sm[P_DRIFT]));
                int st = e->pulse_step;
                e->pulse_step = (e->pulse_step + 1) & 15;
                if (st == 0 && e->sm[P_FREEZE] < 0.5f && ef_rng_f(&e->rng) < e->sm[P_MOTION] * 0.35f) {   /* mutate */
                    int j = (int)(ef_rng_f(&e->rng) * 16) & 15;
                    e->step_pos[j] = ef_rng_f(&e->rng);
                    e->step_oct[j] = ef_rng_f(&e->rng) < e->sm[P_DRIFT] * 0.5f ? (ef_rng_f(&e->rng) < 0.5f ? 12 : 7) : 0;
                    if (ef_rng_f(&e->rng) < e->sm[P_MOTION] * 0.3f) e->pulse_rot = (e->pulse_rot + 1) & 15;
                }
                if (pat[st]) {
                    float p = c->center + (e->step_pos[st] - 0.5f) * c->scatter;
                    float acc = (st % 4 == 0) ? 1.0f : 0.7f;
                    spawn_grain(e, c, i, acc * 1.1f, (float)e->step_oct[st], p);
                }
            }
        }
        return;
    }
    if (c->density <= 0.0f) return;
    double mean = (double)e->sr / c->density;
    for (int i = 0; i < n; i++) {
        e->spawn_wait -= 1.0;
        if (e->spawn_wait <= 0.0) {
            float j = c->jitter;
            e->spawn_wait += mean * (1.0 - j + j * 2.0 * ef_rng_f(&e->rng));
            if (e->spawn_wait < 1.0) e->spawn_wait = 1.0;
            /* SHARDS stutter: repeat a position for a few grains */
            if ((int)e->sm[P_MODE] == MODE_SHARDS) {
                if (e->stutter_left > 0) { e->stutter_left--; spawn_grain(e, c, i, 1.0f, 0.0f, e->stutter_pos); continue; }
                if (ef_rng_f(&e->rng) < e->sm[P_MOTION] * 0.05f) {
                    e->stutter_left = 2 + (int)(ef_rng_f(&e->rng) * 4.0f);
                    e->stutter_pos = c->center + ef_rng_bi(&e->rng) * c->scatter * 0.5f;
                }
                float shard_pitch = 0.0f;
                if (ef_rng_f(&e->rng) < e->sm[P_SCATTER] * 0.35f) {
                    static const float jumps[5] = {12, 19, 24, 7, -12};
                    shard_pitch = jumps[(int)(ef_rng_f(&e->rng) * 5) % 5];
                }
                spawn_grain(e, c, i, 1.0f, shard_pitch, -1.0f);
            } else if ((int)e->sm[P_MODE] == MODE_GHOST) {
                static const float partial[4] = {0, 12, 7, -12};
                float r = ef_rng_f(&e->rng);
                int k = r < 0.55f ? 0 : (r < 0.75f ? 1 : (r < 0.9f ? 2 : 3));
                spawn_grain(e, c, i, 0.5f + 0.5f * ef_rng_f(&e->rng), partial[k], -1.0f);
            } else spawn_grain(e, c, i, 1.0f, 0.0f, -1.0f);
        }
    }
    (void)dt;
}

/* ------------------------------------------------------------------ effects */
static void fx_process(EfEngine *e, float *L, float *R, int n) {
    EfFx *f = &e->fx;
    float *s = e->sm;
    float sr = (float)e->sr;
    bool eco = atomic_load_explicit(&e->eco, memory_order_relaxed) != 0;
    int mode = (int)s[P_MODE];
    float tape = s[P_TAPE];
    if (mode == MODE_TAPE) tape = EF_MAX(tape, 0.35f);
    float wow_d = tape * s[P_TAPE_WOW] * 0.0025f * sr;
    float flut_d = tape * s[P_TAPE_FLUTTER] * 0.00015f * sr;
    float sat = 1.0f + 3.0f * tape;
    float sat_norm = 1.0f / tanh_fast(sat);
    float tone_a = onepole_coef(16000.0f * (1.0f - 0.8f * tape * (0.3f + 0.7f * s[P_TAPE_AGE])), sr);
    float hiss = s[P_TAPE_NOISE] * 0.012f * (0.3f + tape);
    float age = tape * s[P_TAPE_AGE];
    /* filter */
    int ftype = (int)s[P_FILTER_TYPE];
    float k_res = 2.0f - 1.94f * s[P_RESO];
    float drive = 1.0f + 6.0f * s[P_DRIVE];
    float drive_norm = 1.0f / sqrtf(drive);
    /* delay */
    float fb = EF_MIN(s[P_DLY_FB], 0.92f) * 0.98f;
    float dmix = s[P_DLY_MIX];
    bool ping = s[P_DLY_PINGPONG] > 0.5f;
    float dlp = onepole_coef(5000.0f, sr);
    /* reverb */
    float space = s[P_SPACE];
    float rev_wet = powf(space, 1.2f) * 0.9f;
    if (mode == MODE_GHOST || mode == MODE_SHARDS) rev_wet = EF_MIN(1.0f, rev_wet + 0.2f);
    float dry = 1.0f - 0.35f * space;
    float t60 = 0.6f * powf(24.0f, s[P_REV_SIZE]) * (0.7f + 0.6f * space);
    float g[8];
    for (int i = 0; i < 8; i++) g[i] = powf(10.0f, -3.0f * f->fdn_len[i] / (t60 * sr));
    float damp_a = onepole_coef(12000.0f * powf(0.12f, s[P_REV_DAMP]), sr);
    float shimmer = eco ? 0.0f : s[P_SHIMMER];
    float width = 0.4f + 0.8f * s[P_WIDTH];   /* side gain stays near 1: wide but mono-safe */
    float master = 1.6f * s[P_LEVEL] * s[P_LEVEL];
    float dc_r = 1.0f - 20.0f * EF_TWOPI / sr;
    float cut_target = s[P_CUTOFF];
    float lim_rel = 1.0f - expf(-1.0f / (0.15f * sr));
    float peak_l = 0, peak_r = 0;
    bool limited = false;
    int clipped = 0;
    float g_coef = 0, cut_hz;

    for (int i = 0; i < n; i++) {
        float l = L[i], r = R[i];
        /* DC blocker */
        float yl = l - f->dc_x[0] + dc_r * f->dc_y[0]; f->dc_x[0] = l; f->dc_y[0] = yl; l = yl;
        float yr = r - f->dc_x[1] + dc_r * f->dc_y[1]; f->dc_x[1] = r; f->dc_y[1] = yr; r = yr;

        /* --- TAPE: modulated short delay (wow/flutter), saturation, tone, wear, hiss --- */
        f->wow_ph += (0.55f + 0.1f * f->wow_noise) / sr; if (f->wow_ph >= 1.0f) { f->wow_ph -= 1.0f; f->wow_noise = ef_rng_bi(&f->rng); }
        f->flut_ph += 7.3f / sr; if (f->flut_ph >= 1.0f) f->flut_ph -= 1.0f;
        if ((i & 31) == 0) f->flut_noise = smooth_to(f->flut_noise, ef_rng_bi(&f->rng), 0.3f);
        float dly = 0.006f * sr + wow_d * (1.0f + sinf(EF_TWOPI * f->wow_ph)) + flut_d * (1.0f + 0.6f * sinf(EF_TWOPI * f->flut_ph) + 0.4f * f->flut_noise);
        f->tape_buf[f->tape_w * 2] = l; f->tape_buf[f->tape_w * 2 + 1] = r;
        float rp = (float)f->tape_w - dly;
        while (rp < 0) rp += (float)f->tape_len;
        int i0 = (int)rp & (f->tape_len - 1); float fr = rp - (int)rp; int i1 = (i0 + 1) & (f->tape_len - 1);
        float tl = f->tape_buf[i0 * 2] + (f->tape_buf[i1 * 2] - f->tape_buf[i0 * 2]) * fr;
        float tr = f->tape_buf[i0 * 2 + 1] + (f->tape_buf[i1 * 2 + 1] - f->tape_buf[i0 * 2 + 1]) * fr;
        f->tape_w = (f->tape_w + 1) & (f->tape_len - 1);
        if (tape > 0.001f) {
            tl = tanh_fast(tl * sat) * sat_norm; tr = tanh_fast(tr * sat) * sat_norm;
            f->tape_lpz[0] += tone_a * (tl - f->tape_lpz[0]); f->tape_lpz[1] += tone_a * (tr - f->tape_lpz[1]);
            tl = f->tape_lpz[0]; tr = f->tape_lpz[1];
            if (age > 0.01f) {
                f->age_timer -= 1.0f / sr;
                if (f->age_timer <= 0.0f) { f->age_timer = 0.05f; if (ef_rng_f(&f->rng) < age * 0.012f) f->age_gain = 0.35f; }
                f->age_gain += (1.0f - f->age_gain) * 0.0006f;
                tl *= f->age_gain; tr *= f->age_gain;
            }
            float mixw = EF_MIN(1.0f, tape * 4.0f);   /* fade the stage in from 0 */
            l = l + (tl - l) * mixw; r = r + (tr - r) * mixw;
        }
        if (hiss > 0.0f) {
            f->hiss_z[0] += 0.3f * (ef_rng_bi(&f->rng) - f->hiss_z[0]);
            f->hiss_z[1] += 0.3f * (ef_rng_bi(&f->rng) - f->hiss_z[1]);
            l += f->hiss_z[0] * hiss; r += f->hiss_z[1] * hiss;
        }

        /* --- FILTER: TPT state-variable, cutoff smoothed per sample, coefs every 8 --- */
        f->cut_smooth += (cut_target - f->cut_smooth) * 0.002f;
        if ((i & 7) == 0 || g_coef == 0) {
            cut_hz = 30.0f * powf(600.0f, f->cut_smooth);
            cut_hz = EF_MIN(cut_hz, sr * 0.45f);
            g_coef = tanf(EF_PI * cut_hz / sr);
        }
        float a1 = 1.0f / (1.0f + g_coef * (g_coef + k_res));
        float in2[2] = {l, r};
        for (int c = 0; c < 2; c++) {
            float v3 = in2[c] - f->svf_ic2[c];
            float v1 = a1 * f->svf_ic1[c] + g_coef * a1 * v3;
            float v2 = f->svf_ic2[c] + g_coef * v1;
            f->svf_ic1[c] = 2.0f * v1 - f->svf_ic1[c];
            f->svf_ic2[c] = 2.0f * v2 - f->svf_ic2[c];
            float hp = in2[c] - k_res * v1 - v2;
            in2[c] = ftype == 0 ? v2 : (ftype == 1 ? v1 * k_res : hp);
        }
        l = in2[0]; r = in2[1];

        /* --- SATURATION --- */
        l = tanh_fast(l * drive) * drive_norm;
        r = tanh_fast(r * drive) * drive_norm;

        /* --- DELAY: smoothed time, filtered + saturated feedback, ping-pong --- */
        f->dly_time_smooth += (s[P_DLY_TIME] - f->dly_time_smooth) * 0.0004f;
        float dsamp = EF_CLAMP(f->dly_time_smooth * 0.001f * sr, 1.0f, (float)(f->dly_len - 4));
        float drp = (float)f->dly_w - dsamp;
        if (drp < 0) drp += (float)f->dly_len;
        int d0 = (int)drp; float dfr = drp - d0; if (d0 >= f->dly_len) d0 -= f->dly_len; int d1 = d0 + 1; if (d1 >= f->dly_len) d1 = 0;
        float el = f->dly[0][d0] + (f->dly[0][d1] - f->dly[0][d0]) * dfr;
        float er = f->dly[1][d0] + (f->dly[1][d1] - f->dly[1][d0]) * dfr;
        f->dly_lp[0] += dlp * (el - f->dly_lp[0]); f->dly_lp[1] += dlp * (er - f->dly_lp[1]);
        float fbl = tanh_fast(f->dly_lp[0] * fb), fbr = tanh_fast(f->dly_lp[1] * fb);
        if (ping) { f->dly[0][f->dly_w] = (l + r) * 0.5f + fbr; f->dly[1][f->dly_w] = fbl; }
        else { f->dly[0][f->dly_w] = l + fbl; f->dly[1][f->dly_w] = r + fbr; }
        if (++f->dly_w >= f->dly_len) f->dly_w = 0;
        l += el * dmix; r += er * dmix;

        /* --- REVERB: diffusers -> 8-line FDN (Householder) with damping, optional shimmer --- */
        float rin = (l + r) * 0.5f + f->shim_fb[0] * shimmer * 0.16f;
        for (int a = 0; a < 4; a++) {
            float *buf = f->ap[a];
            float d = buf[f->ap_w[a]];
            float v = rin + 0.6f * d;
            buf[f->ap_w[a]] = v;
            rin = d - 0.6f * v;
            if (++f->ap_w[a] >= f->ap_len[a]) f->ap_w[a] = 0;
        }
        float y[8], sum = 0;
        if (!eco) { f->rev_ph1 += 0.31f / sr; if (f->rev_ph1 > 1) f->rev_ph1 -= 1; f->rev_ph2 += 0.47f / sr; if (f->rev_ph2 > 1) f->rev_ph2 -= 1; }
        for (int q = 0; q < 8; q++) {
            float mod = 0.0f;
            if (!eco && q == 1) mod = 12.0f * (1.0f + sinf(EF_TWOPI * f->rev_ph1));
            if (!eco && q == 5) mod = 12.0f * (1.0f + sinf(EF_TWOPI * f->rev_ph2));
            float rpos = (float)f->fdn_w[q] - (float)f->fdn_len[q] - mod;
            while (rpos < 0) rpos += (float)f->fdn_cap[q];
            int r0 = (int)rpos; float rf = rpos - r0; if (r0 >= f->fdn_cap[q]) r0 -= f->fdn_cap[q]; int r1 = r0 + 1; if (r1 >= f->fdn_cap[q]) r1 = 0;
            float v = f->fdn[q][r0] + (f->fdn[q][r1] - f->fdn[q][r0]) * rf;
            f->fdn_lp[q] += damp_a * (v - f->fdn_lp[q]);
            y[q] = f->fdn_lp[q] * g[q];
            sum += y[q];
        }
        sum *= 0.25f;   /* 2/N */
        for (int q = 0; q < 8; q++) {
            float inj = (q & 1) ? (q & 2 ? -rin : rin) : (q & 2 ? rin : -rin);
            float v = y[q] - sum + inj * 0.5f;
            f->fdn[q][f->fdn_w[q]] = v;
            if (++f->fdn_w[q] >= f->fdn_cap[q]) f->fdn_w[q] = 0;
        }
        /* output taps chosen so the direct injection arrives with the same polarity on
           both sides: decorrelated but never anti-phase (the R36S speaker is mono) */
        float wl = (y[2] - y[0] + y[6] - y[4]) * 0.6f;
        float wr = (y[1] - y[3] + y[5] - y[7]) * 0.6f;
        if (shimmer > 0.001f) {   /* +12 st pitch shifter, two crossfaded taps */
            const int W = 2048;
            f->shim[f->shim_w] = (wl + wr) * 0.25f;   /* loop gain well below 1 even with a long tail */
            f->shim_ph += 1.0f / W; if (f->shim_ph >= 1.0f) f->shim_ph -= 1.0f;
            float out = 0;
            for (int t = 0; t < 2; t++) {
                float ph = f->shim_ph + t * 0.5f; if (ph >= 1.0f) ph -= 1.0f;
                float dd = (1.0f - ph) * W + 2.0f;
                float rp2 = (float)f->shim_w - dd; while (rp2 < 0) rp2 += (float)f->shim_len;
                int s0 = (int)rp2 & (f->shim_len - 1); float sf = rp2 - (int)rp2; int s1 = (s0 + 1) & (f->shim_len - 1);
                float v = f->shim[s0] + (f->shim[s1] - f->shim[s0]) * sf;
                out += v * sinf(EF_PI * ph);
            }
            f->shim_w = (f->shim_w + 1) & (f->shim_len - 1);
            f->shim_lp += 0.25f * (out - f->shim_lp);
            f->shim_fb[0] = tanh_fast(f->shim_lp * 0.9f);
        } else f->shim_fb[0] = 0.0f;
        l = l * dry + wl * rev_wet;
        r = r * dry + wr * rev_wet;

        /* --- WIDTH (M/S) and master --- */
        float m = (l + r) * 0.5f, sd = (l - r) * 0.5f * width;
        l = (m + sd) * master; r = (m - sd) * master;
        L[i] = l; R[i] = r;
    }

    /* monitor input and preview are added by the caller before limiting */
    (void)peak_l; (void)peak_r; (void)limited; (void)clipped; (void)lim_rel;
}

/* Slow automatic level: brings sparse and dense sounds to a similar loudness
 * (target about -18 dBFS RMS, at most +12 dB / -6 dB), ahead of the limiter. */
static void auto_level(EfEngine *e, float *L, float *R, int n) {
    EfFx *f = &e->fx;
    float ms = 0;
    for (int i = 0; i < n; i++) ms += 0.5f * (L[i] * L[i] + R[i] * R[i]);
    ms /= (float)EF_MAX(1, n);
    float blk = (float)n / e->sr;
    f->agc_ms += (ms - f->agc_ms) * (1.0f - expf(-blk / 2.5f));
    float target = f->agc_gain;
    if (f->agc_ms > 1e-6f) target = EF_CLAMP(0.126f / sqrtf(f->agc_ms), 0.5f, 4.0f);   /* only when there is sound */
    float tc = target < f->agc_gain ? 0.4f : 2.5f;                                  /* duck faster than it rises */
    float g0 = f->agc_gain;
    f->agc_gain += (target - f->agc_gain) * (1.0f - expf(-blk / tc));
    for (int i = 0; i < n; i++) {
        float g = g0 + (f->agc_gain - g0) * (float)i / n;
        L[i] *= g; R[i] *= g;
    }
}

static void limiter(EfEngine *e, float *L, float *R, int n) {
    EfFx *f = &e->fx;
    float rel = 1.0f - expf(-1.0f / (0.15f * e->sr));
    const float thr = 0.89f;
    float pl = 0, pr = 0;
    bool lim = false;
    int clipped = 0;
    for (int i = 0; i < n; i++) {
        float l = L[i], r = R[i];
        if (!(l == l)) l = 0.0f;           /* NaN guard: never emit NaN */
        if (!(r == r)) r = 0.0f;
        float pk = EF_MAX(fabsf(l), fabsf(r));
        if (pk > 1.0f) clipped++;
        float need = pk > thr ? thr / pk : 1.0f;
        if (need < f->lim_gain) f->lim_gain = need;
        else f->lim_gain += (1.0f - f->lim_gain) * rel;
        if (f->lim_gain < 0.999f) lim = true;
        l *= f->lim_gain; r *= f->lim_gain;
        /* soft knee safety clip */
        if (fabsf(l) > 0.9f) l = (l > 0 ? 1.0f : -1.0f) * (0.9f + 0.1f * tanh_fast((fabsf(l) - 0.9f) * 10.0f));
        if (fabsf(r) > 0.9f) r = (r > 0 ? 1.0f : -1.0f) * (0.9f + 0.1f * tanh_fast((fabsf(r) - 0.9f) * 10.0f));
        L[i] = EF_CLAMP(l, -1.0f, 1.0f); R[i] = EF_CLAMP(r, -1.0f, 1.0f);
        if (fabsf(L[i]) > pl) pl = fabsf(L[i]);
        if (fabsf(R[i]) > pr) pr = fabsf(R[i]);
    }
    /* meters: keep the max until the UI reads & resets */
    if (pl > atomic_load_explicit(&e->peak_l, memory_order_relaxed)) atomic_store_explicit(&e->peak_l, pl, memory_order_relaxed);
    if (pr > atomic_load_explicit(&e->peak_r, memory_order_relaxed)) atomic_store_explicit(&e->peak_r, pr, memory_order_relaxed);
    if (lim) atomic_store_explicit(&e->limit_active, 1, memory_order_relaxed);
    if (clipped) atomic_fetch_add_explicit(&e->clip_count, clipped, memory_order_relaxed);
}

/* ------------------------------------------------------------------ monitor + preview */
static void mix_monitor(EfEngine *e, float *L, float *R, int n) {
    EfRing *ring = e->monitor;
    if (!ring || !atomic_load_explicit(&e->monitor_on, memory_order_relaxed)) {
        if (ring) ef_ring_reset_consumer(ring);   /* drop stale input so monitoring restarts in sync */
        return;
    }
    int in_rate = atomic_load_explicit(&e->monitor_rate, memory_order_relaxed);
    if (in_rate <= 0) return;
    float gain = atomic_load_explicit(&e->monitor_gain, memory_order_relaxed);
    int fill = ef_ring_fill(ring);
    int target = in_rate / 25;   /* ~40 ms cushion */
    if (fill > target * 4) { ef_ring_advance(ring, fill - target); fill = target; e->mon_frac = 0; }   /* resync after a stall */
    double ratio = (double)in_rate / e->sr;
    ratio *= 1.0 + EF_CLAMP((fill - target) / (double)(target * 20 + 1), -0.005, 0.005);   /* drift correction */
    for (int i = 0; i < n; i++) {
        fill = ef_ring_fill(ring);
        if (fill < 2) break;                         /* underrun: silence, never block */
        float l0, r0, l1, r1;
        ef_ring_peek(ring, 0, &l0, &r0);
        ef_ring_peek(ring, 1, &l1, &r1);
        float t = (float)e->mon_frac;
        L[i] += (l0 + (l1 - l0) * t) * gain;
        R[i] += (r0 + (r1 - r0) * t) * gain;
        e->mon_frac += ratio;
        int adv = (int)e->mon_frac;
        if (adv > 0) { if (adv > fill - 1) adv = fill - 1; ef_ring_advance(ring, adv); e->mon_frac -= adv; }
    }
}

static void mix_preview(EfEngine *e, float *L, float *R, int n) {
    EfSample *s = e->preview;
    if (!s || !atomic_load_explicit(&e->preview_playing, memory_order_relaxed)) return;
    double inc = (double)s->rate / e->sr;
    for (int i = 0; i < n; i++) {
        if (e->preview_pos >= e->preview_b - 1) {
            if (e->preview_loop) e->preview_pos = e->preview_a;
            else { atomic_store(&e->preview_playing, 0); break; }
        }
        float fade = 1.0f;
        double left = e->preview_b - e->preview_pos;
        if (left < 256) fade = (float)(left / 256.0);
        e->preview_fade = EF_MIN(1.0f, e->preview_fade + 1.0f / 256.0f);
        fade *= e->preview_fade;
        L[i] += linear(s->L, e->preview_pos, s->frames) * fade * 0.9f;
        R[i] += linear(s->R, e->preview_pos, s->frames) * fade * 0.9f;
        e->preview_pos += inc;
    }
    atomic_store_explicit(&e->preview_progress, (float)((e->preview_pos - e->preview_a) / EF_MAX(1, e->preview_b - e->preview_a)), memory_order_relaxed);
}

/* ------------------------------------------------------------------ main render */
static void write_vis(EfEngine *e, const Ctl *c) {
    int idx = 1 - atomic_load_explicit(&e->vis_idx, memory_order_relaxed);
    EfVisFrame *v = &e->vis[idx];
    v->n = 0;
    EfSample *s = e->cur;
    if (s) {
        for (int k = 0; k < EF_MAX_GRAINS; k++) {
            EfGrain *g = &e->grains[k];
            if (!g->active) continue;
            EfGrainVis *gv = &v->g[v->n++];
            gv->x = (float)(g->pos / s->frames);
            gv->pitch = g->vis_pitch;
            gv->amp = e->win[g->win][(int)EF_MIN(g->wpos, (float)EF_WIN_N)] * (g->gl + g->gr);
            gv->pan = (g->gr) / (g->gl + g->gr + 1e-6f);
        }
        float ra = atomic_load(&e->region_a), rb = atomic_load(&e->region_b);
        float cc = atomic_load(&e->region_reverse) ? 1.0f - c->center : c->center;
        v->center = ra + (rb - ra) * cc;
        v->scatter = (rb - ra) * c->scatter;
        v->head = c->tape ? ra + (rb - ra) * (float)e->tape_head : -1.0f;
    }
    atomic_store_explicit(&e->vis_idx, idx, memory_order_release);
}

void ef_engine_render(EfEngine *e, float *out, int frames) {
    ef_fpu_flush_denormals();
    int done = 0;
    float dt = (float)EF_SUBBLOCK / e->sr;
    float a_smooth = 1.0f - expf(-(float)EF_SUBBLOCK / (0.04f * e->sr));
    Ctl c;
    memset(&c, 0, sizeof c);
    while (done < frames) {
        int n = EF_MIN(EF_SUBBLOCK, frames - done);
        /* parameter smoothing (continuous params) */
        for (int i = 0; i < P_COUNT; i++) {
            float t = ef_param_get(e->P, i);
            if (g_param_info[i].kind == PF_CONT && i != P_DLY_TIME && i != P_CUTOFF) e->sm[i] = smooth_to(e->sm[i], t, a_smooth);
            else e->sm[i] = t;
        }
        /* pending sample swap: fade out, swap, fade in */
        if (e->next) {
            if (e->fade_left > 0) { e->fade_left -= n; e->fade_gain = EF_MAX(0.0f, (float)e->fade_left / EF_MAX(1, e->fade_len)); }
            if (e->fade_left <= 0 && !e->retired) {   /* wait (silent) until the UI has freed the last retired sample */
                e->retired = e->cur;
                e->cur = e->next; e->next = NULL;
                for (int g = 0; g < EF_MAX_GRAINS; g++) e->grains[g].active = false;
                e->fade_gain = 0.0f;
                e->tape_head = 0;
            }
        } else if (e->fade_gain < 1.0f) e->fade_gain = EF_MIN(1.0f, e->fade_gain + (float)n / (e->sr * 0.02f));

        oracle_update(e, dt * n / EF_SUBBLOCK);
        compute_ctl(e, &c, dt * n / EF_SUBBLOCK);
        if (c.tape && e->cur && e->sm[P_FREEZE] < 0.5f) {
            float ra = atomic_load(&e->region_a), rb = atomic_load(&e->region_b);
            double rlen = EF_MAX(64.0, (double)(rb - ra) * e->cur->frames);
            e->tape_head += c.tape_speed * n / rlen;
            e->tape_head -= floor(e->tape_head);
        }
        memset(e->busL, 0, sizeof(float) * n);
        memset(e->busR, 0, sizeof(float) * n);
        if (!e->next || e->fade_left > 0) schedule_grains(e, &c, n, dt);
        render_grains(e, n);
        float pg_target = e->sm[P_PLAY] > 0.5f ? 1.0f : 0.0f;
        for (int i = 0; i < n; i++) {
            e->play_gain += (pg_target - e->play_gain) * 0.002f;
            float g = e->play_gain * e->fade_gain;
            e->busL[i] *= g; e->busR[i] *= g;
        }
        fx_process(e, e->busL, e->busR, n);
        auto_level(e, e->busL, e->busR, n);
        mix_monitor(e, e->busL, e->busR, n);
        mix_preview(e, e->busL, e->busR, n);
        limiter(e, e->busL, e->busR, n);
        int sw = atomic_load_explicit(&e->scope_w, memory_order_relaxed);
        for (int i = 0; i < n; i++) {
            out[(done + i) * 2] = e->busL[i]; out[(done + i) * 2 + 1] = e->busR[i];
            e->scope[0][sw] = e->busL[i]; e->scope[1][sw] = e->busR[i];
            sw = (sw + 1) & (EF_SCOPE_N - 1);
        }
        atomic_store_explicit(&e->scope_w, sw, memory_order_release);
        done += n;
    }
    atomic_store_explicit(&e->active_grains, ef_engine_count_active(e), memory_order_relaxed);
    write_vis(e, &c);
}
