/* ECHOFIELD - application state shared by UI, main loop and diagnostics */
#ifndef EF_APP_H
#define EF_APP_H
#include "common.h"
#include "params.h"
#include "engine.h"
#include "capture.h"
#include "store.h"
#include "audio.h"
#include "draw.h"
#include "vis.h"

typedef enum {
    SCR_MAIN, SCR_MENU, SCR_SAMPLES, SCR_EDIT, SCR_GRAIN, SCR_FX,
    SCR_ORACLE, SCR_PRESETS, SCR_PROJECTS, SCR_SETTINGS, SCR_DIAG, SCR_HELP, SCR_COUNT
} EfScreen;

typedef enum {
    B_UP, B_DOWN, B_LEFT, B_RIGHT, B_A, B_B, B_X, B_Y, B_L1, B_R1, B_L2, B_R2,
    B_START, B_SELECT, B_L3, B_R3, B_COUNT
} EfButton;

typedef enum { JOB_NONE, JOB_LOAD, JOB_PREVIEW, JOB_RENDER } EfJobKind;

typedef struct {
    EfJobKind kind;
    char path[EF_PATH_MAX];
    char path2[EF_PATH_MAX];
    EfSample *src;               /* input for save/render jobs (owned by job) */
    int a, b; bool reverse; float gain;
    int target_rate;
    bool play;                   /* JOB_PREVIEW: start playing when loaded */
    /* results */
    EfSample *result;
    char error[160];
    bool ok;
    _Atomic int done;
    void *thread;
} EfJob;

typedef struct App {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *trail;           /* persistence layer for grain particles (optional) */
    SDL_GameController *pad;
    bool keyboard_ok;
    EfParams P;
    EfEngine E;
    EfCapture C;
    EfAudioOut A;
    EfSettings S;
    EfRng rng;
    bool audio_ok;
    char audio_error[160];
    int sr;
    char settings_path[EF_PATH_MAX];
    char session_path[EF_PATH_MAX];

    /* active sample (mirrors the engine; engine owns the pointer) */
    char sample_name[96];
    char sample_path[EF_PATH_MAX];
    float sample_secs;
    int sample_channels, sample_src_rate;
    float region_a, region_b;
    bool reverse, normalize;
    char preset_name[64];
    bool locks[P_COUNT];
    float undo[P_COUNT];
    bool has_undo;
    bool dirty;
    double last_autosave;

    /* navigation */
    EfScreen scr;
    EfScreen menu_return;
    int sel[SCR_COUNT];
    int scroll[SCR_COUNT];
    bool help_open;
    int edit_marker;              /* 0 start, 1 end, 2 position */

    /* buttons */
    bool down[B_COUNT];
    double down_t[B_COUNT];
    double repeat_t[B_COUNT];
    bool select_used;             /* SELECT combined with another button */
    float axis[6];

    /* toasts and confirm dialogs */
    char toast[160];
    double toast_until;
    Col toast_col;
    bool confirm_open;
    char confirm_msg[200];
    int confirm_action, confirm_arg;

    /* jobs */
    EfJob job;
    bool job_busy;
    char job_label[64];
    EfSample *preview_sample;     /* owned by UI, played by engine preview */
    char preview_path[EF_PATH_MAX];

    /* main screen */
    int page;                     /* knob page 0..3 (L1/R1) */
    double stick_t;               /* last time a stick moved an effect */
    float stick_vis[4];           /* stick deflection, for the on-screen indicator */

    /* browser */
    double browse_t;              /* when the selection last changed (auto-preview) */
    int sound_cache_sel;          /* SOUNDS screen: details of the selected entry */
    EfParams sound_cache;
    char sound_cache_sample[EF_PATH_MAX];
    int tab;
    EfFileEntry *files;
    int nfiles;
    float *file_secs;
    int *file_ch;
    int *file_rate;
    EfPresetEntry presets[160];
    int npresets;
    EfFileEntry *projects;
    int nprojects;

    /* meters */
    float out_meter[2];
    double out_clip_until, limit_until;
    int last_oracle_seq;
    double oracle_flash_until;
    char oracle_flash[24];

    /* diagnostics text */
    char *diag_text;
    int diag_lines;
    float fps;
    bool quit;
    bool screenshot_mode;
} App;

/* ui.c */
void ui_init(App *a);
void ui_open_screen(App *a, EfScreen s);
void ui_frame(App *a, double dt);
void ui_button(App *a, EfButton b, bool down);
void ui_poll_repeat(App *a);
void ui_update(App *a, double dt);
void ui_toast(App *a, Col c, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void app_load_sample_async(App *a, const char *path);
void app_save_session(App *a);
bool app_restore_session(App *a);
void app_apply_settings(App *a);
void app_shutdown_ui(App *a);

/* diag.c */
char *diag_report(App *a);   /* malloc'd text */
void diag_print_cli(void);
int diag_bench(int seconds);

#endif
