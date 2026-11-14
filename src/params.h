/* ECHOFIELD - parameter table shared by UI, engine, presets and randomizer */
#ifndef EF_PARAMS_H
#define EF_PARAMS_H
#include "common.h"

typedef enum {
    /* PERFORM macros (main screen) */
    P_POSITION, P_TEXTURE, P_SCATTER, P_DRIFT, P_MOTION,
    P_PITCH, P_SPACE, P_TAPE, P_WIDTH, P_LEVEL, P_HARMONY,
    /* GRAIN (advanced) */
    P_SIZE_MUL, P_DENS_MUL, P_SCAN, P_REVERSE, P_PITCH_RAND, P_SIZE_RAND, P_WINDOW, P_GRAIN_TONE,
    /* EFFECTS */
    P_FILTER_TYPE, P_CUTOFF, P_RESO, P_DRIVE,
    P_DLY_TIME, P_DLY_FB, P_DLY_MIX, P_DLY_PINGPONG,
    P_REV_SIZE, P_REV_DAMP, P_SHIMMER,
    P_TAPE_WOW, P_TAPE_FLUTTER, P_TAPE_AGE, P_TAPE_NOISE,
    /* DRIFT ORACLE */
    P_OR_ON, P_OR_ROOT, P_OR_SCALE, P_OR_ACTIVITY, P_OR_AMBIG, P_OR_RANGE, P_OR_GLIDE, P_OR_DRONE, P_OR_HOLD,
    /* global state */
    P_MODE, P_PLAY, P_FREEZE,
    P_COUNT
} EfParamId;

typedef enum { PF_CONT = 0, PF_INT = 1, PF_ENUM = 2, PF_BOOL = 3 } EfParamKind;

typedef struct {
    const char *key;        /* stable name used in preset/project files */
    const char *label;      /* UI label */
    EfParamKind kind;
    float min, max, def;
    float step;             /* coarse step for D-pad */
    const char *const *enum_names;
    bool randomizable;
    const char *help;
} EfParamInfo;

extern const EfParamInfo g_param_info[P_COUNT];

enum { MODE_CLOUD, MODE_SHARDS, MODE_TAPE, MODE_GHOST, MODE_PULSE, MODE_DRIFT, MODE_COUNT };
extern const char *const g_mode_names[MODE_COUNT];
extern const char *const g_mode_blurb[MODE_COUNT];
extern const char *const g_scale_names[];
extern const int g_scale_count;
extern const char *const g_note_names[12];
extern const char *const g_window_names[];

typedef struct { int n; int deg[12]; } EfScale;
extern const EfScale g_scales[];

/* Lock-free parameter store: written by the UI thread, read by the audio thread. */
typedef struct { _Atomic float v[P_COUNT]; } EfParams;

static inline float ef_param_get(EfParams *p, int id) { return atomic_load_explicit(&p->v[id], memory_order_relaxed); }
void ef_param_set(EfParams *p, int id, float v);          /* clamps/quantizes */
void ef_params_defaults(EfParams *p);
void ef_param_format(int id, float v, char *out, size_t n);
const char *ef_param_mode_help(int id, int mode);          /* mode-specific meaning or NULL */
int ef_param_find(const char *key);

#endif
