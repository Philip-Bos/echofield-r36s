/* ECHOFIELD - parameter metadata, formatting and contextual help */
#include "params.h"

const char *const g_mode_names[MODE_COUNT] = {"CLOUD", "DUST", "TAPE", "BLOOM", "PULSE", "DRIFT"};
const char *const g_mode_blurb[MODE_COUNT] = {
    "long overlapping grains melt into a pad",
    "tiny glinting specks scattered in space",
    "three slow tape loops at half, normal, double speed",
    "slow swells with octave and fifth overtones",
    "soft notes on a slow euclidean clock",
    "everything wanders slowly and keeps evolving",
};
const char *const g_note_names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char *const g_scale_names[] = {"Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian",
                                     "Pent Major", "Pent Minor", "Hirajoshi", "In-Sen", "Whole Tone", "Glass"};
const EfScale g_scales[] = {
    {7, {0, 2, 4, 5, 7, 9, 11}}, {7, {0, 2, 3, 5, 7, 8, 10}}, {7, {0, 2, 3, 5, 7, 9, 10}},
    {7, {0, 1, 3, 5, 7, 8, 10}}, {7, {0, 2, 4, 6, 7, 9, 11}}, {7, {0, 2, 4, 5, 7, 9, 10}},
    {5, {0, 2, 4, 7, 9}}, {5, {0, 3, 5, 7, 10}}, {5, {0, 2, 3, 7, 8}}, {5, {0, 1, 5, 7, 10}},
    {6, {0, 2, 4, 6, 8, 10}}, {6, {0, 2, 4, 7, 9, 11}},
};
const int g_scale_count = 12;
const char *const g_window_names[] = {"AUTO", "HANN", "TUKEY", "PERC", "SWELL", "GAUSS"};
static const char *const k_filter_names[] = {"LOWPASS", "BANDPASS", "HIGHPASS"};
static const char *const k_bool_names[] = {"OFF", "ON"};

#define CONT(k, l, mn, mx, d, st, rnd, h) {k, l, PF_CONT, mn, mx, d, st, NULL, rnd, h}
#define ENUMP(k, l, mx, d, names, rnd, h) {k, l, PF_ENUM, 0, mx, d, 1, names, rnd, h}
#define BOOLP(k, l, d, h) {k, l, PF_BOOL, 0, 1, d, 1, k_bool_names, false, h}

const EfParamInfo g_param_info[P_COUNT] = {
    [P_POSITION] = CONT("position", "POSITION", 0, 1, 0.35f, 0.01f, true, "Where in the sample the grains are read from. DRIFT and EVOLVE move it around for you."),
    [P_TEXTURE] = CONT("texture", "SIZE", 0, 1, 0.72f, 0.02f, true, "Grain size and overlap together: low = grainy and fragmented, high = long, smooth and sustained."),
    [P_SCATTER] = CONT("scatter", "SCATTER", 0, 1, 0.45f, 0.02f, true, "Randomness of grain position, timing and size. Low = focused, high = smeared cloud."),
    [P_DRIFT] = CONT("drift", "DRIFT", 0, 1, 0.35f, 0.02f, true, "Slow wandering of pitch and position. Gives life and detune."),
    [P_MOTION] = CONT("motion", "EVOLVE", 0, 1, 0.35f, 0.02f, true, "How much the texture animates itself over time: slow LFOs, wandering, pattern changes."),
    [P_PITCH] = CONT("pitch", "PITCH", -24, 24, 0, 1.0f, true, "Transpose in semitones."),
    [P_SPACE] = CONT("space", "REVERB", 0, 1, 0.65f, 0.02f, true, "Reverb amount. Left stick up/down."),
    [P_TAPE] = CONT("tape", "TAPE", 0, 1, 0.12f, 0.02f, true, "Tape stage intensity: wow & flutter, saturation and darkening. Details in EFFECTS."),
    [P_WIDTH] = CONT("width", "WIDTH", 0, 1, 0.85f, 0.02f, true, "Stereo spread of grains and of the whole output."),
    [P_LEVEL] = CONT("level", "VOLUME", 0, 1, 0.75f, 0.02f, false, "Master output level. A soft limiter prevents hard clipping."),
    [P_HARMONY] = CONT("harmony", "HARMONY", 0, 1, 0.30f, 0.02f, true, "Adds grains an octave or a fifth above and below: turns any sound into a choir of itself."),

    [P_SIZE_MUL] = CONT("size_mul", "GRAIN SIZE", -2, 2, 0, 0.25f, true, "Multiplies the grain length chosen by TEXTURE and the mode (x0.25 .. x4)."),
    [P_DENS_MUL] = CONT("dens_mul", "DENSITY", -2, 2, 0, 0.25f, true, "Multiplies how many grains are born per second (x0.25 .. x4)."),
    [P_SCAN] = CONT("scan", "SCAN", -1, 1, 0, 0.05f, true, "Moves the read position automatically. 1 = forward at real-time speed, negative = backwards."),
    [P_REVERSE] = CONT("reverse", "REVERSE", 0, 1, 0.10f, 0.05f, true, "Chance that a grain plays backwards."),
    [P_PITCH_RAND] = CONT("pitch_rand", "PITCH RAND", 0, 12, 0, 0.25f, true, "Random pitch offset per grain, in semitones."),
    [P_SIZE_RAND] = CONT("size_rand", "SIZE RAND", 0, 1, 0.3f, 0.05f, true, "Random variation of each grain's length."),
    [P_WINDOW] = ENUMP("window", "ENVELOPE", 5, 0, g_window_names, true, "Grain envelope. AUTO lets the mode choose. HANN smooth, TUKEY flat, PERC plucked, SWELL reversed-attack, GAUSS soft."),
    [P_GRAIN_TONE] = CONT("grain_tone", "GRAIN TONE", -1, 1, -0.1f, 0.05f, true, "Per-grain colour: negative = darker grains, positive = thinner, airier grains."),

    [P_FILTER_TYPE] = ENUMP("filter_type", "FILTER", 2, 0, k_filter_names, false, "Master filter type."),
    [P_CUTOFF] = CONT("cutoff", "TONE", 0, 1, 0.82f, 0.02f, true, "Master filter: low = dark and muffled, high = open and bright. Left stick left/right."),
    [P_RESO] = CONT("reso", "RESONANCE", 0, 1, 0.10f, 0.05f, true, "Filter resonance (kept below self-oscillation)."),
    [P_DRIVE] = CONT("drive", "SATURATE", 0, 1, 0.05f, 0.05f, true, "Soft saturation after the filter: warmth and harmonics."),
    [P_DLY_TIME] = CONT("dly_time", "DELAY TIME", 20, 1500, 520, 10, true, "Delay time in milliseconds (changes glide smoothly)."),
    [P_DLY_FB] = CONT("dly_fb", "DELAY FDBK", 0, 0.92f, 0.45f, 0.02f, true, "Delay feedback. Hard-limited and saturated so it can never run away."),
    [P_DLY_MIX] = CONT("dly_mix", "ECHO", 0, 1, 0.18f, 0.02f, true, "Amount of stereo echo. Right stick left/right."),
    [P_DLY_PINGPONG] = BOOLP("dly_pingpong", "PING-PONG", 1, "Echoes bounce between left and right."),
    [P_REV_SIZE] = CONT("rev_size", "REVERB SIZE", 0, 1, 0.78f, 0.05f, true, "Reverb decay length (SPACE sets how much you hear)."),
    [P_REV_DAMP] = CONT("rev_damp", "REVERB DAMP", 0, 1, 0.50f, 0.05f, true, "High-frequency absorption in the reverb tail."),
    [P_SHIMMER] = CONT("shimmer", "SHIMMER", 0, 1, 0.22f, 0.05f, true, "Octave-up sparkle inside the reverb tail. Right stick up/down."),
    [P_TAPE_WOW] = CONT("tape_wow", "WOW", 0, 1, 0.4f, 0.05f, true, "Slow pitch wobble of the tape stage (scaled by TAPE)."),
    [P_TAPE_FLUTTER] = CONT("tape_flutter", "FLUTTER", 0, 1, 0.25f, 0.05f, true, "Fast pitch jitter of the tape stage (scaled by TAPE)."),
    [P_TAPE_AGE] = CONT("tape_age", "AGE", 0, 1, 0.25f, 0.05f, true, "Wear: lost highs, slight grit and dropouts (scaled by TAPE)."),
    [P_TAPE_NOISE] = CONT("tape_noise", "HISS", 0, 1, 0, 0.05f, false, "Optional tape hiss. Off by default."),

    [P_OR_ON] = BOOLP("oracle_on", "ORACLE", 0, "Turn the DRIFT ORACLE generative layer on or off."),
    [P_OR_ROOT] = ENUMP("oracle_root", "ROOT", 11, 0, g_note_names, false, "Root note. The sample's own pitch is treated as C."),
    [P_OR_SCALE] = ENUMP("oracle_scale", "SCALE", 11, 1, g_scale_names, false, "Scale the oracle chooses notes from."),
    [P_OR_ACTIVITY] = CONT("oracle_activity", "ACTIVITY", 0, 1, 0.35f, 0.05f, true, "How often the oracle changes notes and triggers chance events."),
    [P_OR_AMBIG] = CONT("oracle_ambig", "AMBIGUITY", 0, 1, 0.2f, 0.05f, true, "Low = root/fifth heavy and stable. High = colour tones, suspensions, passing notes."),
    [P_OR_RANGE] = {"oracle_range", "RANGE", PF_INT, 0, 2, 1, 1, NULL, false, "How many octaves notes may spread over."},
    [P_OR_GLIDE] = CONT("oracle_glide", "GLIDE", 0, 1, 0.4f, 0.05f, true, "How slowly the texture glides to new notes."),
    [P_OR_DRONE] = BOOLP("oracle_drone", "DRONE", 0, "Drone mode: root + fifth bed with a sub-octave, very slow changes."),
    [P_OR_HOLD] = BOOLP("oracle_hold", "HOLD", 0, "Freeze the oracle on its current chord."),

    [P_MODE] = ENUMP("mode", "MODE", MODE_COUNT - 1, 0, g_mode_names, false, "Scene: how grains are born and move (Y)."),
    [P_PLAY] = BOOLP("play", "PLAY", 1, "Sound on/off (A)."),
    [P_FREEZE] = BOOLP("freeze", "FREEZE", 0, "Freeze: the sound holds still where it is (L2)."),
};

void ef_param_set(EfParams *p, int id, float v) {
    const EfParamInfo *pi = &g_param_info[id];
    if (!(v == v)) v = pi->def;
    v = EF_CLAMP(v, pi->min, pi->max);
    if (pi->kind != PF_CONT) v = (float)lrint_ef(v);
    atomic_store_explicit(&p->v[id], v, memory_order_relaxed);
}

void ef_params_defaults(EfParams *p) {
    for (int i = 0; i < P_COUNT; i++) atomic_store(&p->v[i], g_param_info[i].def);
}

int ef_param_find(const char *key) {
    for (int i = 0; i < P_COUNT; i++) if (!strcmp(g_param_info[i].key, key)) return i;
    return -1;
}

void ef_param_format(int id, float v, char *out, size_t n) {
    const EfParamInfo *pi = &g_param_info[id];
    if ((pi->kind == PF_ENUM || pi->kind == PF_BOOL) && pi->enum_names) {
        int i = (int)lrint_ef(v);
        i = EF_CLAMP(i, 0, (int)pi->max);
        snprintf(out, n, "%s", pi->enum_names[i]);
        return;
    }
    switch (id) {
    case P_PITCH: snprintf(out, n, "%+.1f st", (double)v); break;
    case P_PITCH_RAND: snprintf(out, n, "+/-%.2f st", (double)v); break;
    case P_LEVEL: { float g = 1.6f * v * v; if (g < 1e-4f) snprintf(out, n, "-inf dB"); else snprintf(out, n, "%+.1f dB", (double)ef_lin_to_db(g)); } break;
    case P_SIZE_MUL: case P_DENS_MUL: snprintf(out, n, "x%.2f", (double)exp2f(v)); break;
    case P_SCAN: snprintf(out, n, "%+.2fx", (double)v); break;
    case P_GRAIN_TONE: snprintf(out, n, v < -0.02f ? "dark %.0f" : (v > 0.02f ? "thin %.0f" : "neutral"), (double)fabsf(v * 100)); break;
    case P_CUTOFF: { float hz = 30.0f * powf(600.0f, v); if (hz >= 1000) snprintf(out, n, "%.1f kHz", (double)hz / 1000); else snprintf(out, n, "%.0f Hz", (double)hz); } break;
    case P_DLY_TIME: snprintf(out, n, "%.0f ms", (double)v); break;
    case P_OR_RANGE: snprintf(out, n, "%d oct", (int)v); break;
    default: snprintf(out, n, "%.0f%%", (double)(v * 100.0f)); break;
    }
}

/* What each macro does in each mode - shown in the contextual help overlay. */
const char *ef_param_mode_help(int id, int mode) {
    static const char *const tex[MODE_COUNT] = {
        "CLOUD: grains 120 ms - 1.5 s with 4-12x overlap.",
        "DUST: specks 20-200 ms; low = pinpricks, high = soft flakes.",
        "TAPE: tape speed; high = slow, heavy, stretched loops.",
        "BLOOM: swell length, from 0.6 s to several seconds.",
        "PULSE: note length, from a short pluck to a long swell.",
        "DRIFT: base grain length; it also breathes on its own.",
    };
    static const char *const mot[MODE_COUNT] = {
        "CLOUD: slow LFOs sway position and density.",
        "DUST: how often specks cluster and repeat.",
        "TAPE: varispeed drift and the odd tape stop.",
        "BLOOM: slow pan rotation and how often blooms arrive.",
        "PULSE: tempo (30-120 BPM) and pattern mutation.",
        "DRIFT: speed of the random walk through everything.",
    };
    static const char *const sca[MODE_COUNT] = {
        "CLOUD: spread of grain positions around the playhead.",
        "DUST: how widely specks are strewn through the sample.",
        "TAPE: head wobble around the loop position.",
        "BLOOM: how far apart the swells are taken from.",
        "PULSE: each step picks a different slice of the sample.",
        "DRIFT: width of the wandering region.",
    };
    if (mode < 0 || mode >= MODE_COUNT) return NULL;
    if (id == P_TEXTURE) return tex[mode];
    if (id == P_MOTION) return mot[mode];
    if (id == P_SCATTER) return sca[mode];
    return NULL;
}
