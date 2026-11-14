/* ECHOFIELD - presets, projects, settings, constrained randomizer */
#ifndef EF_STORE_H
#define EF_STORE_H
#include "common.h"
#include "params.h"

typedef struct {
    char name[64];
    char path[EF_PATH_MAX];
    bool factory;
    int factory_idx;
} EfPresetEntry;

typedef struct {
    char name[64];
    char sample_path[EF_PATH_MAX];
    float region_a, region_b;
    bool reverse, normalize;
    bool locks[P_COUNT];
} EfProject;

typedef struct {
    char input_key[48];
    float input_gain_db;
    bool monitor;
    float monitor_gain;
    int max_voices;
    bool eco;
    bool swap_ab;
    int rec_max_seconds;
    bool rec_normalize;
    bool autoload_session;
    bool show_cpu;
    int visualiser;            /* main screen view, cycled with SELECT */
} EfSettings;

extern const int g_factory_count;
const char *ef_factory_name(int i);
int ef_presets_list(EfPresetEntry *out, int max);
/* Applies a preset. sample_ref receives the referenced sample path ("" if none). */
bool ef_preset_apply_factory(int idx, EfParams *p, char *sample_ref, size_t n);
bool ef_preset_apply_file(const char *path, EfParams *p, char *sample_ref, size_t n, char *err, size_t en);
bool ef_preset_save(const EfParams *p, const char *name, const char *sample_path, char *out_path, size_t n);

bool ef_project_save(const char *path, const EfProject *pr, const EfParams *p);
bool ef_project_load(const char *path, EfProject *pr, EfParams *p, char *err, size_t en);

void ef_settings_defaults(EfSettings *s);
bool ef_settings_load(EfSettings *s, const char *path);
bool ef_settings_save(const EfSettings *s, const char *path);

/* constrained, weighted randomization of unlocked parameters */
void ef_randomize(EfParams *p, const bool locks[P_COUNT], EfRng *rng);
void ef_random_name(EfRng *rng, char *out, size_t n);
/* shared helper: parse "key=value" text with a [params] section into params */
int ef_parse_params(const char *text, EfParams *p, bool reset_defaults);
#endif
