/* ECHOFIELD - USB audio discovery, diagnostics and capture (ALSA, loaded at run time) */
#ifndef EF_CAPTURE_H
#define EF_CAPTURE_H
#include "common.h"
#include "ring.h"

typedef struct {
    int card;
    char id[32];             /* /proc/asound/cardN/id */
    char shortname[64];
    char longname[128];
    char driver[32];
    bool is_usb;
    char usbid[16];          /* "vvvv:pppp" */
    char usb_sysname[32];    /* USB device behind /sys/class/sound/cardN/device, e.g. "1-1" */
    int capture_dev;         /* first PCM device with capture, -1 if none */
    int playback_dev;
    /* from /proc/asound/cardN/stream0 (USB only) */
    char cap_format[24];
    int cap_channels;
    int cap_rate_min, cap_rate_max;
    bool stream_info;
    bool stream_has_capture, stream_has_playback;
} EfAlsaCard;

typedef struct {
    char sysname[32];        /* e.g. "1-1" */
    char vidpid[16];
    char product[64], manufacturer[64];
    bool is_hub;
    bool audio_control, as_in, as_out, midi;
    bool any_audio_iface_bound;
    char driver[32];         /* driver bound to the first audio interface */
    int alsa_card;           /* -1 if no ALSA card matched */
    bool looks_like_fm1;
} EfUsbDev;

typedef struct {
    EfAlsaCard cards[12]; int ncards;
    EfUsbDev usb[24]; int nusb;
    bool proc_asound_ok;
    bool sys_usb_ok;
    bool usb_audio_driver;   /* snd_usb_audio loaded (or built in) */
    bool usb_audio_module_file; /* module present on disk but maybe not loaded */
    char kernel[96];
} EfInventory;

typedef enum {
    IN_READY = 0,            /* ALSA capture device present */
    IN_PLAYBACK_ONLY,        /* audio device without any capture path */
    IN_NO_DRIVER,            /* USB audio interface present but no ALSA card */
    IN_MIDI_ONLY,
    IN_TEST,                 /* developer test device (ALSA name override) */
} EfInputStatus;

typedef struct {
    char key[48];            /* stable identity used to re-find the device: usb vid:pid or card id */
    char label[96];
    char detail[128];
    bool usb, internal, fm1;
    int card, dev;
    char pcm_override[96];   /* test devices only */
    EfInputStatus status;
    int usb_index;           /* into inventory usb[] or -1 */
    int card_index;          /* into inventory cards[] or -1 */
} EfInput;

int ef_inputs_build(const EfInventory *inv, EfInput *out, int max);
void ef_inventory_scan(EfInventory *inv);
void ef_capture_set_sysroot(const char *root);   /* tests: prefix for /proc and /sys */
bool ef_usb_parse_descriptors(const uint8_t *d, int len, EfUsbDev *u);

/* ---------- capture engine ---------- */
typedef enum { CAP_IDLE, CAP_OPENING, CAP_RUNNING, CAP_ERROR, CAP_LOST } EfCapState;
typedef enum { REC_OFF, REC_RUNNING, REC_STOPPING, REC_DONE } EfRecState;

typedef struct EfCapture {
    bool alsa_loaded;
    char alsa_version[32];
    char alsa_error[128];
    /* requested device (UI thread writes, guarded by req_seq) */
    EfInput req;
    void *req_mtx;            /* SDL_mutex guarding req */
    _Atomic int req_seq;
    _Atomic int want_open;
    /* status (capture thread writes) */
    _Atomic int state;
    char error[256];
    char opened_name[96];
    char attempts[512];
    int rate, dev_channels, period;
    char format_name[16];
    _Atomic float gain_db;
    _Atomic float peak[2];       /* max since last UI read (post gain) */
    _Atomic float raw_peak;      /* max since last UI read (pre gain) */
    _Atomic int clip_src, clip_post;
    _Atomic int signal_seen;     /* real non-silent samples arrived since open */
    _Atomic uint32_t last_signal_ms;
    _Atomic int64_t frames_in;
    _Atomic int xruns;
    _Atomic int reconnects;
    EfRing ring;                 /* monitoring path to the audio callback */
    /* recording */
    float *rec_buf;              /* interleaved stereo */
    long rec_cap;
    _Atomic long rec_frames;
    _Atomic int rec_state;
    _Atomic int rec_interrupted;
    _Atomic int rec_full;
    int rec_channels;            /* 1 if the device is mono */
    int rec_rate;
    char rec_temp_path[EF_PATH_MAX];
    _Atomic long rec_flushed;
    void *writer_thread;
    _Atomic int writer_quit;
    _Atomic int writer_error;
    /* thread */
    void *thread;
    _Atomic int quit;
} EfCapture;

bool ef_capture_init(EfCapture *c);
void ef_capture_shutdown(EfCapture *c);
void ef_capture_open(EfCapture *c, const EfInput *in);   /* async */
void ef_capture_close(EfCapture *c);                     /* async */
bool ef_capture_rec_start(EfCapture *c, float max_seconds, const char *temp_path);
void ef_capture_rec_stop(EfCapture *c);                  /* request; poll rec_state for REC_DONE */
void ef_capture_rec_finish(EfCapture *c);                /* after DONE: join writer, close temp file */
void ef_capture_rec_release(EfCapture *c);               /* free the take buffer */
const char *ef_capture_state_name(int st);
int ef_recover_recordings(const char *dir, char *msg, size_t n);   /* repair orphaned temp takes */
#endif
