/* ECHOFIELD - granular engine, modes, DRIFT ORACLE and effects (UI independent) */
#ifndef EF_ENGINE_H
#define EF_ENGINE_H
#include "common.h"
#include "params.h"
#include "sample.h"
#include "ring.h"

#define EF_MAX_GRAINS 96
#define EF_SUBBLOCK 128
#define EF_WIN_N 1024
#define EF_SCOPE_N 1024        /* output history for the visualisers (power of two) */
enum { WIN_HANN, WIN_TUKEY, WIN_PERC, WIN_SWELL, WIN_GAUSS, WIN_COUNT };

typedef struct {
    bool active;
    double pos, inc;           /* read position (frames) and increment (negative = reverse) */
    int len, age, delay;       /* delay = start offset inside the current sub-block */
    float wpos, wstep;
    float gl, gr;              /* pan * amplitude */
    uint8_t win;
    float tone_a, tone_mix;    /* per-grain one-pole colour */
    float zl, zr;
    float roff;                /* right-channel read offset (frames): stereo from mono */
    float vis_pitch;
} EfGrain;

typedef struct {
    bool on;
    float timer;
    int chord[4], nchord;
    float glide_cur, glide_target;
    float bloom, scatter_burst, rev_swell, octave_flick, hush;   /* event envelopes (seconds left) */
    char event[24];
    float event_show;
    int bass_deg;
    int last_scale;
    bool last_drone;
} EfOracle;

typedef struct {
    /* tape */
    float *tape_buf; int tape_len, tape_w;
    float wow_ph, flut_ph, flut_noise, wow_noise, tape_lpz[2], age_gain, age_timer, hiss_z[2];
    /* filter (TPT SVF) */
    float svf_ic1[2], svf_ic2[2], cut_smooth;
    /* delay */
    float *dly[2]; int dly_len, dly_w; float dly_time_smooth, dly_lp[2];
    /* reverb (8-line FDN) */
    float *fdn[8]; int fdn_len[8], fdn_w[8], fdn_cap[8]; float fdn_lp[8];
    float *ap[4]; int ap_len[4], ap_w[4];
    float rev_ph1, rev_ph2;
    float *shim; int shim_len, shim_w; float shim_ph, shim_lp, shim_fb[2];
    /* output */
    float dc_x[2], dc_y[2];
    float lim_gain;
    float agc_ms, agc_gain;    /* auto level: smoothed mean square and applied gain */
    EfRng rng;
} EfFx;

typedef struct { float x, pitch, amp, pan; } EfGrainVis;
typedef struct {
    int n;
    EfGrainVis g[EF_MAX_GRAINS];
    float center, scatter, head;  /* 0..1 of whole sample */
} EfVisFrame;

typedef struct EfEngine {
    int sr;
    EfParams *P;
    /* sample pointers: changed by the UI only while holding the audio-device lock */
    EfSample *cur, *next, *retired;
    int fade_left, fade_len;
    float fade_gain;
    /* region (fractions of the sample) and non-destructive edit flags */
    _Atomic float region_a, region_b, sample_gain;
    _Atomic int region_reverse;
    /* performance offsets from analog sticks (UI thread) */
    _Atomic float perf_pos, perf_pitch, perf_texture, perf_scatter;
    /* settings */
    _Atomic int max_voices, eco;
    _Atomic float monitor_gain;
    _Atomic int monitor_on;
    EfRing *monitor;           /* capture ring (stereo, at capture rate) */
    _Atomic int monitor_rate;
    double mon_frac;
    /* preview player (sample browser / editor / recorder audition) */
    EfSample *preview;
    double preview_pos;
    int preview_a, preview_b;
    bool preview_loop;
    float preview_fade;
    _Atomic int preview_playing;
    /* grains */
    EfGrain grains[EF_MAX_GRAINS];
    double spawn_wait;
    float win[WIN_COUNT][EF_WIN_N + 2];
    /* mode state */
    double scan;               /* accumulated scan offset (fraction of region) */
    float walk_pos, walk_pitch, walk_v1, walk_v2, walk_size;
    float lfo1, lfo2_cur, lfo2_tgt, lfo2_t;
    double tape_head;          /* fraction of region */
    float tape_speed, tape_stop;
    int stutter_left; float stutter_pos;
    int tape_k;                /* next tape head to spawn from */
    double pulse_acc; int pulse_step; float step_pos[16]; int step_oct[16]; int pulse_rot;
    float play_gain;
    /* smoothed macro values */
    float sm[P_COUNT];
    EfOracle oracle;
    EfFx fx;
    EfRng rng;
    /* scratch (allocated once) */
    float busL[EF_SUBBLOCK], busR[EF_SUBBLOCK];
    /* metering & stats (read by UI) */
    _Atomic float peak_l, peak_r, cpu_load;
    _Atomic int clip_count, limit_active, active_grains, spawn_skipped;
    _Atomic int vis_idx;
    EfVisFrame vis[2];
    _Atomic float preview_progress;
    char oracle_event_ui[24];
    _Atomic int oracle_event_seq;
    _Atomic int oracle_note;   /* current bass note semitone (for UI) */
    float scope[2][EF_SCOPE_N]; /* last output samples; UI reads them racily (visual only) */
    _Atomic int scope_w;
} EfEngine;

bool ef_engine_init(EfEngine *e, int sample_rate, EfParams *params);
void ef_engine_free(EfEngine *e);
/* real-time: render interleaved stereo float */
void ef_engine_render(EfEngine *e, float *out, int frames);

/* The following must be called with the audio device locked (or before audio starts). */
void ef_engine_set_sample(EfEngine *e, EfSample *s);       /* crossfades; old goes to e->retired */
EfSample *ef_engine_take_retired(EfEngine *e);
void ef_engine_preview(EfEngine *e, EfSample *s, int a, int b, bool loop);   /* s=NULL stops */

void ef_engine_reset_state(EfEngine *e);
int ef_engine_count_active(const EfEngine *e);
float ef_window_value(int win, float x);   /* for tests */
#endif
