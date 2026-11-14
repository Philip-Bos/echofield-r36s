/* ECHOFIELD - shared definitions */
#ifndef EF_COMMON_H
#define EF_COMMON_H
#include "compat/minilibc.h"
#include <stdatomic.h>

#define EF_VERSION "2.0.0"
#define EF_PROJECT_FORMAT 1
#define EF_PRESET_FORMAT 1

#define EF_MIN(a, b) ((a) < (b) ? (a) : (b))
#define EF_MAX(a, b) ((a) > (b) ? (a) : (b))
#define EF_CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#define EF_ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define EF_PATH_MAX 512

static inline float ef_lerp(float a, float b, float t) { return a + (b - a) * t; }
static inline float ef_db_to_lin(float db) { return powf(10.0f, db * 0.05f); }
static inline float ef_lin_to_db(float l) { return l > 1e-9f ? 20.0f * log10f(l) : -180.0f; }
static inline long lrint_ef(float x) { return (long)(x >= 0.0f ? x + 0.5f : x - 0.5f); }
static inline float ef_semis_to_ratio(float s) { return exp2f(s * (1.0f / 12.0f)); }

/* fast, deterministic RNG (xorshift32); one instance per thread */
typedef struct { uint32_t s; } EfRng;
static inline uint32_t ef_rng_u32(EfRng *r) {
    uint32_t x = r->s ? r->s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    r->s = x; return x;
}
static inline float ef_rng_f(EfRng *r) { return (ef_rng_u32(r) >> 8) * (1.0f / 16777216.0f); }   /* [0,1) */
static inline float ef_rng_bi(EfRng *r) { return ef_rng_f(r) * 2.0f - 1.0f; }                      /* [-1,1) */
static inline float ef_rng_tri(EfRng *r) { return (ef_rng_f(r) + ef_rng_f(r)) - 1.0f; }            /* triangular */

/* logging (util.c) */
void ef_log_open(const char *path);
void ef_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void ef_log_close(void);

/* paths and files (util.c) */
typedef struct {
    char app_dir[EF_PATH_MAX];      /* read-only install dir (assets) */
    char data_dir[EF_PATH_MAX];     /* writable user data root */
    char recordings[EF_PATH_MAX];
    char samples[EF_PATH_MAX];
    char presets[EF_PATH_MAX];
    char projects[EF_PATH_MAX];
    char factory_samples[EF_PATH_MAX];
    bool data_writable;
} EfPaths;
extern EfPaths g_paths;
bool ef_paths_init(const char *override_data);
bool ef_dir_exists(const char *p);
bool ef_file_exists(const char *p);
bool ef_mkdirs(const char *p);
bool ef_dir_writable(const char *p);
long ef_file_size(const char *p);
char *ef_read_text_file(const char *path, long max_bytes);   /* malloc'd, NUL terminated */
bool ef_write_text_file_atomic(const char *path, const char *text);
void ef_path_join(char *out, size_t n, const char *a, const char *b);
const char *ef_basename(const char *p);
void ef_strlcpy(char *d, const char *s, size_t n);
void ef_timestamp(char *out, size_t n, const char *fmt_kind); /* "file" or "human" */
double ef_now_s(void);
bool ef_has_suffix_ci(const char *s, const char *suf);
void ef_trim(char *s);

/* file listing */
typedef struct { char name[128]; char path[EF_PATH_MAX]; long size; } EfFileEntry;
int ef_list_files(const char *dir, const char *suffix, EfFileEntry *out, int max); /* sorted by name */

/* denormal protection for the calling thread */
void ef_fpu_flush_denormals(void);

#endif
