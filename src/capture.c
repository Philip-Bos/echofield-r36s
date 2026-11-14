/* ECHOFIELD - USB audio discovery, diagnostics and real capture through ALSA.
 *
 * libasound.so.2 is loaded at run time (SDL_LoadObject) so ECHOFIELD still starts,
 * plays and explains the problem when ALSA is missing. Discovery reads /proc/asound
 * and /sys/bus/usb directly, which lets the UI distinguish:
 *   1. a USB device physically connected,
 *   2. that device exposing a USB-audio capture (IN) streaming interface,
 *   3. the kernel driver (snd-usb-audio) creating an ALSA card for it,
 *   4. the capture stream opening with a negotiated format,
 *   5. real non-silent samples arriving.
 * The capture thread never touches the disk; a separate writer thread streams the
 * take to a temp WAV for crash safety. The thread never substitutes another device.
 */
#include "capture.h"
#include "sample.h"
#include "compat/sdl_min.h"

/* ------------------------------------------------------------------ ALSA (dlopen) */
typedef struct snd_pcm snd_pcm_t;
typedef struct snd_pcm_hw_params snd_pcm_hw_params_t;
typedef unsigned long snd_pcm_uframes_t;
typedef long snd_pcm_sframes_t;
enum { SND_PCM_STREAM_CAPTURE = 1, SND_PCM_NONBLOCK = 1, SND_PCM_ACCESS_RW_INTERLEAVED = 3 };
enum { FMT_S16_LE = 2, FMT_S24_LE = 6, FMT_S32_LE = 10, FMT_FLOAT_LE = 14, FMT_S24_3LE = 32 };

static struct {
    void *h;
    int (*open)(snd_pcm_t **, const char *, int, int);
    int (*close)(snd_pcm_t *);
    int (*hw_malloc)(snd_pcm_hw_params_t **);
    void (*hw_free)(snd_pcm_hw_params_t *);
    int (*hw_any)(snd_pcm_t *, snd_pcm_hw_params_t *);
    int (*hw_set_access)(snd_pcm_t *, snd_pcm_hw_params_t *, int);
    int (*hw_test_format)(snd_pcm_t *, snd_pcm_hw_params_t *, int);
    int (*hw_set_format)(snd_pcm_t *, snd_pcm_hw_params_t *, int);
    int (*hw_get_channels_min)(const snd_pcm_hw_params_t *, unsigned *);
    int (*hw_get_channels_max)(const snd_pcm_hw_params_t *, unsigned *);
    int (*hw_set_channels)(snd_pcm_t *, snd_pcm_hw_params_t *, unsigned);
    int (*hw_test_rate)(snd_pcm_t *, snd_pcm_hw_params_t *, unsigned, int);
    int (*hw_set_rate_near)(snd_pcm_t *, snd_pcm_hw_params_t *, unsigned *, int *);
    int (*hw_set_period_size_near)(snd_pcm_t *, snd_pcm_hw_params_t *, snd_pcm_uframes_t *, int *);
    int (*hw_set_buffer_size_near)(snd_pcm_t *, snd_pcm_hw_params_t *, snd_pcm_uframes_t *);
    int (*hw_params)(snd_pcm_t *, snd_pcm_hw_params_t *);
    int (*prepare)(snd_pcm_t *);
    int (*start)(snd_pcm_t *);
    int (*wait)(snd_pcm_t *, int);
    snd_pcm_sframes_t (*readi)(snd_pcm_t *, void *, snd_pcm_uframes_t);
    int (*recover)(snd_pcm_t *, int, int);
    const char *(*strerror)(int);
    const char *(*version)(void);
} A;

static bool alsa_load(EfCapture *c) {
    if (A.h) return true;
    const char *names[] = {"libasound.so.2", "libasound.so"};
    for (int i = 0; i < 2 && !A.h; i++) A.h = SDL_LoadObject(names[i]);
    if (!A.h) { snprintf(c->alsa_error, sizeof c->alsa_error, "libasound.so.2 not found: %s", SDL_GetError()); return false; }
#define LD(field, sym) do { *(void **)&A.field = SDL_LoadFunction(A.h, sym); if (!A.field) { snprintf(c->alsa_error, sizeof c->alsa_error, "libasound missing %s", sym); SDL_UnloadObject(A.h); A.h = NULL; return false; } } while (0)
    LD(open, "snd_pcm_open"); LD(close, "snd_pcm_close");
    LD(hw_malloc, "snd_pcm_hw_params_malloc"); LD(hw_free, "snd_pcm_hw_params_free");
    LD(hw_any, "snd_pcm_hw_params_any"); LD(hw_set_access, "snd_pcm_hw_params_set_access");
    LD(hw_test_format, "snd_pcm_hw_params_test_format"); LD(hw_set_format, "snd_pcm_hw_params_set_format");
    LD(hw_get_channels_min, "snd_pcm_hw_params_get_channels_min"); LD(hw_get_channels_max, "snd_pcm_hw_params_get_channels_max");
    LD(hw_set_channels, "snd_pcm_hw_params_set_channels"); LD(hw_test_rate, "snd_pcm_hw_params_test_rate");
    LD(hw_set_rate_near, "snd_pcm_hw_params_set_rate_near"); LD(hw_set_period_size_near, "snd_pcm_hw_params_set_period_size_near");
    LD(hw_set_buffer_size_near, "snd_pcm_hw_params_set_buffer_size_near"); LD(hw_params, "snd_pcm_hw_params");
    LD(prepare, "snd_pcm_prepare"); LD(start, "snd_pcm_start"); LD(wait, "snd_pcm_wait");
    LD(readi, "snd_pcm_readi"); LD(recover, "snd_pcm_recover"); LD(strerror, "snd_strerror");
    LD(version, "snd_asoundlib_version");
#undef LD
    ef_strlcpy(c->alsa_version, A.version(), sizeof c->alsa_version);
    return true;
}

/* ------------------------------------------------------------------ discovery */
static char g_root[EF_PATH_MAX] = "";
void ef_capture_set_sysroot(const char *root) { ef_strlcpy(g_root, root ? root : "", sizeof g_root); }
static void rp(char *out, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void rp(char *out, size_t n, const char *fmt, ...) {
    char tmp[EF_PATH_MAX];
    va_list ap; va_start(ap, fmt); vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    snprintf(out, n, "%s%s", g_root, tmp);
}
static bool read_line_file(const char *path, char *out, size_t n) {
    FILE *f = fopen(path, "r");
    if (!f) { if (n) out[0] = 0; return false; }
    if (!fgets(out, (int)n, f)) out[0] = 0;
    fclose(f);
    ef_trim(out);
    return true;
}

static void parse_stream0(EfAlsaCard *c, const char *text) {
    /* sections "Playback:" and "Capture:" each with Format/Channels/Rates lines */
    const char *cap = strstr(text, "\nCapture:");
    if (!cap && !strncmp(text, "Capture:", 8)) cap = text;
    c->stream_info = true;
    c->stream_has_playback = strstr(text, "Playback:") != NULL;
    c->stream_has_capture = cap != NULL;
    if (!cap) return;
    const char *f = strstr(cap, "Format:");
    if (f) { sscanf(f + 7, " %23s", c->cap_format); }
    const char *ch = strstr(cap, "Channels:");
    if (ch) c->cap_channels = atoi(ch + 9);
    const char *r = strstr(cap, "Rates:");
    if (r) {
        r += 6;
        int mn = 0, mx = 0;
        const char *p = r;
        while (*p && *p != '\n') {
            while (*p == ' ' || *p == ',' || *p == '-') p++;
            if (!isdigit((unsigned char)*p)) break;
            int v = (int)strtol(p, (char **)&p, 10);
            if (!mn || v < mn) mn = v;
            if (v > mx) mx = v;
            while (*p == ' ') p++;
            if (*p == '(') break;
        }
        c->cap_rate_min = mn; c->cap_rate_max = mx;
    }
}

bool ef_usb_parse_descriptors(const uint8_t *d, int len, EfUsbDev *u) {
    int i = 0, cls = -1, sub = -1;
    bool any = false;
    while (i + 2 <= len) {
        int bl = d[i], ty = d[i + 1];
        if (bl < 2 || i + bl > len) break;
        if (ty == 4 && bl >= 9) {           /* interface */
            cls = d[i + 5]; sub = d[i + 6];
            if (cls == 1 && sub == 1) u->audio_control = true;
            if (cls == 1 && sub == 3) u->midi = true;
            any = true;
        } else if (ty == 5 && bl >= 7) {    /* endpoint */
            int addr = d[i + 2], attr = d[i + 3];
            if (cls == 1 && sub == 2 && (attr & 3) == 1) {    /* isochronous audio streaming */
                int usage = (attr >> 4) & 3;
                if (addr & 0x80) { if (usage != 1) u->as_in = true; }   /* skip feedback endpoints */
                else u->as_out = true;
            }
        } else if (ty == 9 && bl >= 9 && i == 18) { /* config after device descriptor */ }
        else if (ty == 1 && bl == 18 && d[i + 4] == 9) u->is_hub = true;  /* device class 9 = hub */
        i += bl;
    }
    return any;
}

static void scan_usb(EfInventory *inv) {
    char path[EF_PATH_MAX];
    rp(path, sizeof path, "/sys/bus/usb/devices");
    DIR *d = opendir(path);
    if (!d) return;
    inv->sys_usb_ok = true;
    struct dirent *e;
    while ((e = readdir(d)) && inv->nusb < EF_ARRAY_LEN(inv->usb)) {
        if (e->d_name[0] == '.' || strchr(e->d_name, ':')) continue;   /* skip interfaces */
        if (!strncmp(e->d_name, "usb", 3)) continue;                     /* root hubs */
        EfUsbDev *u = &inv->usb[inv->nusb];
        memset(u, 0, sizeof *u);
        u->alsa_card = -1;
        ef_strlcpy(u->sysname, e->d_name, sizeof u->sysname);
        char f[EF_PATH_MAX], vid[16] = "", pid[16] = "";
        snprintf(f, sizeof f, "%s/%s/idVendor", path, e->d_name); read_line_file(f, vid, sizeof vid);
        snprintf(f, sizeof f, "%s/%s/idProduct", path, e->d_name); read_line_file(f, pid, sizeof pid);
        if (!vid[0]) continue;
        snprintf(u->vidpid, sizeof u->vidpid, "%s:%s", vid, pid);
        snprintf(f, sizeof f, "%s/%s/product", path, e->d_name); read_line_file(f, u->product, sizeof u->product);
        snprintf(f, sizeof f, "%s/%s/manufacturer", path, e->d_name); read_line_file(f, u->manufacturer, sizeof u->manufacturer);
        if (!u->product[0]) snprintf(u->product, sizeof u->product, "USB device %s", u->vidpid);
        snprintf(f, sizeof f, "%s/%s/descriptors", path, e->d_name);
        FILE *df = fopen(f, "rb");
        if (df) {
            uint8_t buf[4096];
            int n = (int)fread(buf, 1, sizeof buf, df);
            fclose(df);
            ef_usb_parse_descriptors(buf, n, u);
        }
        char cls[8] = "";
        snprintf(f, sizeof f, "%s/%s/bDeviceClass", path, e->d_name); read_line_file(f, cls, sizeof cls);
        if (!strcmp(cls, "09")) u->is_hub = true;
        /* driver bound to audio interfaces: <dev>:<cfg>.<n>/driver */
        DIR *d2 = opendir(path);
        if (d2) {
            struct dirent *e2;
            size_t ln = strlen(e->d_name);
            while ((e2 = readdir(d2))) {
                if (strncmp(e2->d_name, e->d_name, ln) || e2->d_name[ln] != ':') continue;
                char ic[8] = "";
                snprintf(f, sizeof f, "%s/%s/bInterfaceClass", path, e2->d_name); read_line_file(f, ic, sizeof ic);
                if (strcmp(ic, "01")) continue;
                char link[EF_PATH_MAX];
                snprintf(f, sizeof f, "%s/%s/driver", path, e2->d_name);
                ssize_t k = readlink(f, link, sizeof link - 1);
                if (k > 0) { link[k] = 0; u->any_audio_iface_bound = true; ef_strlcpy(u->driver, ef_basename(link), sizeof u->driver); }
                else if (ef_dir_exists(f)) { u->any_audio_iface_bound = true; ef_strlcpy(u->driver, "bound", sizeof u->driver); }
            }
            closedir(d2);
        }
        char lp[160];
        snprintf(lp, sizeof lp, "%s %s", u->manufacturer, u->product);
        for (char *q = lp; *q; q++) *q = (char)toupper((unsigned char)*q);
        u->looks_like_fm1 = strstr(lp, "FM-1") || strstr(lp, "FM1") || strstr(lp, "FM 1") || strstr(lp, "M-VAVE");
        if (!u->is_hub) inv->nusb++;
    }
    closedir(d);
}

static void scan_alsa(EfInventory *inv) {
    char path[EF_PATH_MAX];
    rp(path, sizeof path, "/proc/asound/cards");
    char *txt = ef_read_text_file(path, 65536);
    if (!txt) return;
    inv->proc_asound_ok = true;
    char *line = txt;
    while (line && *line && inv->ncards < EF_ARRAY_LEN(inv->cards)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        int num; char id[64] = "";
        char *lb = strchr(line, '['), *rb = strchr(line, ']');
        if (lb && rb && rb > lb && sscanf(line, " %d", &num) == 1) {
            EfAlsaCard *c = &inv->cards[inv->ncards];
            memset(c, 0, sizeof *c);
            c->card = num; c->capture_dev = -1; c->playback_dev = -1;
            size_t idl = (size_t)(rb - lb - 1);
            if (idl >= sizeof id) idl = sizeof id - 1;
            memcpy(id, lb + 1, idl); id[idl] = 0; ef_trim(id);
            ef_strlcpy(c->id, id, sizeof c->id);
            const char *rest = rb + 1;
            if (*rest == ':') rest++;
            char tmp[160]; ef_strlcpy(tmp, rest, sizeof tmp); ef_trim(tmp);
            char *dash = strstr(tmp, " - ");
            if (dash) { *dash = 0; ef_strlcpy(c->driver, tmp, sizeof c->driver); ef_strlcpy(c->shortname, dash + 3, sizeof c->shortname); }
            else ef_strlcpy(c->shortname, tmp, sizeof c->shortname);
            ef_trim(c->driver); ef_trim(c->shortname);
            if (nl && nl[1]) {   /* long name on the following line */
                char *l2 = nl + 1, *nl2 = strchr(l2, '\n');
                if (nl2) *nl2 = 0;
                ef_strlcpy(c->longname, l2, sizeof c->longname); ef_trim(c->longname);
                nl = nl2;
            }
            char f[EF_PATH_MAX];
            rp(f, sizeof f, "/proc/asound/card%d/usbid", num);
            if (read_line_file(f, c->usbid, sizeof c->usbid) && c->usbid[0]) c->is_usb = true;
            if (!strcasecmp(c->driver, "USB-Audio")) c->is_usb = true;
            /* sysfs parent of the card: a USB interface ("1-1:1.0") names the USB device
               directly, which still works when usbid is missing or the ids differ */
            rp(f, sizeof f, "/sys/class/sound/card%d/device", num);
            char link[EF_PATH_MAX];
            ssize_t k = readlink(f, link, sizeof link - 1);
            if (k > 0) {
                link[k] = 0;
                const char *b = ef_basename(link);
                const char *colon = strchr(b, ':'), *dash = strchr(b, '-');
                if (isdigit((unsigned char)b[0]) && colon && dash && dash < colon) {
                    size_t sl = EF_MIN((size_t)(colon - b), sizeof c->usb_sysname - 1);
                    memcpy(c->usb_sysname, b, sl); c->usb_sysname[sl] = 0;
                    c->is_usb = true;
                }
            }
            rp(f, sizeof f, "/proc/asound/card%d/stream0", num);
            char *st = ef_read_text_file(f, 65536);
            if (st) { parse_stream0(c, st); free(st); }
            inv->ncards++;
        }
        line = nl ? nl + 1 : NULL;
    }
    free(txt);
    /* PCM devices: "01-00: USB Audio : USB Audio : playback 1 : capture 1" */
    rp(path, sizeof path, "/proc/asound/pcm");
    txt = ef_read_text_file(path, 65536);
    if (txt) {
        for (char *l = txt; l && *l;) {
            char *nl = strchr(l, '\n');
            if (nl) *nl = 0;
            int cn, dn;
            if (sscanf(l, "%d-%d", &cn, &dn) == 2) {
                for (int i = 0; i < inv->ncards; i++) if (inv->cards[i].card == cn) {
                    if (strstr(l, "capture") && inv->cards[i].capture_dev < 0) inv->cards[i].capture_dev = dn;
                    if (strstr(l, "playback") && inv->cards[i].playback_dev < 0) inv->cards[i].playback_dev = dn;
                }
            }
            l = nl ? nl + 1 : NULL;
        }
        free(txt);
    } else {
        /* fall back to pcmXc directories */
        for (int i = 0; i < inv->ncards; i++) for (int dn = 0; dn < 4; dn++) {
            char f[EF_PATH_MAX];
            rp(f, sizeof f, "/proc/asound/card%d/pcm%dc", inv->cards[i].card, dn);
            if (inv->cards[i].capture_dev < 0 && ef_dir_exists(f)) inv->cards[i].capture_dev = dn;
            rp(f, sizeof f, "/proc/asound/card%d/pcm%dp", inv->cards[i].card, dn);
            if (inv->cards[i].playback_dev < 0 && ef_dir_exists(f)) inv->cards[i].playback_dev = dn;
        }
    }
}

void ef_inventory_scan(EfInventory *inv) {
    memset(inv, 0, sizeof *inv);
    scan_alsa(inv);
    scan_usb(inv);
    char p[EF_PATH_MAX];
    rp(p, sizeof p, "/sys/module/snd_usb_audio");
    inv->usb_audio_driver = ef_dir_exists(p);
    for (int i = 0; i < inv->ncards; i++) if (inv->cards[i].is_usb) inv->usb_audio_driver = true;
    if (!g_root[0]) {
        struct utsname un;
        if (uname(&un) == 0) {
            snprintf(inv->kernel, sizeof inv->kernel, "%s %s", un.release, un.machine);
            snprintf(p, sizeof p, "/lib/modules/%s/kernel/sound/usb/snd-usb-audio.ko", un.release);
            inv->usb_audio_module_file = ef_file_exists(p);
        }
    }
    /* link USB devices to ALSA cards: sysfs parent first, usbid as fallback */
    for (int u = 0; u < inv->nusb; u++) for (int i = 0; i < inv->ncards; i++) {
        const EfAlsaCard *c = &inv->cards[i];
        if (!c->is_usb) continue;
        bool match = c->usb_sysname[0] ? !strcmp(c->usb_sysname, inv->usb[u].sysname)
                                       : c->usbid[0] && !strcasecmp(c->usbid, inv->usb[u].vidpid);
        if (match) inv->usb[u].alsa_card = i;
    }
}

/* Build the list the recorder shows. Internal devices are listed but never auto-selected. */
int ef_inputs_build(const EfInventory *inv, EfInput *out, int max) {
    int n = 0;
    for (int i = 0; i < inv->ncards && n < max; i++) {
        const EfAlsaCard *c = &inv->cards[i];
        int ui = -1;
        for (int u = 0; u < inv->nusb; u++) if (inv->usb[u].alsa_card == i) ui = u;
        EfInput *in = &out[n];
        memset(in, 0, sizeof *in);
        in->card = c->card; in->dev = c->capture_dev; in->card_index = i; in->usb_index = ui;
        in->usb = c->is_usb; in->internal = !c->is_usb;
        in->fm1 = ui >= 0 && inv->usb[ui].looks_like_fm1;
        const char *nm = ui >= 0 ? inv->usb[ui].product : (c->shortname[0] ? c->shortname : c->id);
        snprintf(in->label, sizeof in->label, "%s %s", c->is_usb ? "[USB]" : "[INTERNAL]", nm);
        snprintf(in->key, sizeof in->key, "%s", c->is_usb && c->usbid[0] ? c->usbid : c->id);
        if (c->capture_dev >= 0) {
            in->status = IN_READY;
            if (c->stream_info && c->cap_channels)
                snprintf(in->detail, sizeof in->detail, "capture: %s, %d ch, %d-%d Hz (card %d)", c->cap_format, c->cap_channels, c->cap_rate_min, c->cap_rate_max, c->card);
            else snprintf(in->detail, sizeof in->detail, "capture PCM hw:%d,%d", c->card, c->capture_dev);
        } else {
            in->status = IN_PLAYBACK_ONLY;
            snprintf(in->detail, sizeof in->detail, "playback only - this device sends no audio to the R36S");
        }
        n++;
    }
    /* USB devices with audio/MIDI interfaces but no ALSA card */
    for (int u = 0; u < inv->nusb && n < max; u++) {
        const EfUsbDev *d = &inv->usb[u];
        if (d->alsa_card >= 0) continue;
        if (!d->audio_control && !d->as_in && !d->as_out && !d->midi) continue;
        EfInput *in = &out[n];
        memset(in, 0, sizeof *in);
        in->card = -1; in->dev = -1; in->usb = true; in->usb_index = u; in->card_index = -1;
        in->fm1 = d->looks_like_fm1;
        snprintf(in->label, sizeof in->label, "[USB] %s", d->product);
        snprintf(in->key, sizeof in->key, "%s", d->vidpid);
        if (d->as_in) { in->status = IN_NO_DRIVER; snprintf(in->detail, sizeof in->detail, "has USB-audio capture, but no ALSA card (driver %s)", d->driver[0] ? d->driver : "not bound"); }
        else if (d->as_out) { in->status = IN_PLAYBACK_ONLY; snprintf(in->detail, sizeof in->detail, "USB-audio playback only (no capture endpoint)"); }
        else { in->status = IN_MIDI_ONLY; snprintf(in->detail, sizeof in->detail, "USB-MIDI only: no audio streaming interface"); }
        n++;
    }
    /* optional developer test input (explicitly labelled, never used on its own) */
    const char *t = getenv("ECHOFIELD_TEST_PCM");
    if (t && t[0] && n < max) {
        EfInput *in = &out[n];
        memset(in, 0, sizeof *in);
        in->status = IN_TEST; in->card = -1; in->dev = 0; in->usb_index = -1; in->card_index = -1;
        snprintf(in->label, sizeof in->label, "[TEST] %s", t);
        snprintf(in->key, sizeof in->key, "test:%s", t);
        ef_strlcpy(in->pcm_override, t, sizeof in->pcm_override);
        snprintf(in->detail, sizeof in->detail, "developer ALSA PCM (simulation), not hardware");
        n++;
    }
    /* order: FM-1 first, then other USB, then internal */
    for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) {
        int ra = out[a].fm1 ? 0 : (out[a].usb ? 1 : 2), rb = out[b].fm1 ? 0 : (out[b].usb ? 1 : 2);
        if (rb < ra) { EfInput t2 = out[a]; out[a] = out[b]; out[b] = t2; }
    }
    return n;
}

/* ------------------------------------------------------------------ capture thread */
const char *ef_capture_state_name(int st) {
    static const char *n[] = {"idle", "opening", "running", "error", "device lost"};
    return st >= 0 && st <= CAP_LOST ? n[st] : "?";
}

typedef struct { snd_pcm_t *pcm; int fmt, bps, ch, rate; snd_pcm_uframes_t period; } PcmOpen;

static const char *fmt_name(int f) {
    switch (f) { case FMT_S16_LE: return "S16_LE"; case FMT_S24_LE: return "S24_LE"; case FMT_S32_LE: return "S32_LE";
                 case FMT_FLOAT_LE: return "FLOAT_LE"; case FMT_S24_3LE: return "S24_3LE"; default: return "?"; }
}
static int fmt_bps(int f) { return f == FMT_S16_LE ? 2 : (f == FMT_S24_3LE ? 3 : 4); }

static int try_open_name(EfCapture *c, const char *name, PcmOpen *po, char *err, size_t en) {
    snd_pcm_t *pcm = NULL;
    int r = A.open(&pcm, name, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
    if (r < 0) { snprintf(err, en, "open %s: %s", name, A.strerror(r)); return r; }
    snd_pcm_hw_params_t *hw = NULL;
    if ((r = A.hw_malloc(&hw)) < 0) { A.close(pcm); snprintf(err, en, "hw alloc failed"); return r; }
    const char *stage = "hw_params_any";
    if ((r = A.hw_any(pcm, hw)) < 0) goto fail;
    stage = "set access";
    if ((r = A.hw_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) goto fail;
    static const int prefs[] = {FMT_S32_LE, FMT_S24_3LE, FMT_S24_LE, FMT_FLOAT_LE, FMT_S16_LE};
    int fmt = -1;
    for (int i = 0; i < 5; i++) if (A.hw_test_format(pcm, hw, prefs[i]) == 0) { fmt = prefs[i]; break; }
    stage = "no supported sample format (needs S16/S24/S32/FLOAT little endian)";
    if (fmt < 0) { r = -EINVAL; goto fail; }
    if ((r = A.hw_set_format(pcm, hw, fmt)) < 0) { stage = "set format"; goto fail; }
    unsigned cmin = 1, cmax = 2;
    A.hw_get_channels_min(hw, &cmin);
    A.hw_get_channels_max(hw, &cmax);
    unsigned ch = cmin > 2 ? cmin : EF_MIN(2u, cmax);
    stage = "set channels";
    if ((r = A.hw_set_channels(pcm, hw, ch)) < 0) goto fail;
    static const unsigned rates[] = {48000, 44100, 96000, 88200, 32000, 22050, 16000};
    unsigned rate = 0;
    for (int i = 0; i < 7; i++) if (A.hw_test_rate(pcm, hw, rates[i], 0) == 0) { rate = rates[i]; break; }
    if (!rate) rate = 48000;
    int dir = 0;
    stage = "set rate";
    if ((r = A.hw_set_rate_near(pcm, hw, &rate, &dir)) < 0) goto fail;
    snd_pcm_uframes_t period = rate >= 88200 ? 1024 : 512, buffer = period * 8;
    A.hw_set_period_size_near(pcm, hw, &period, &dir);
    A.hw_set_buffer_size_near(pcm, hw, &buffer);
    stage = "apply hw params";
    if ((r = A.hw_params(pcm, hw)) < 0) goto fail;
    A.hw_free(hw);
    A.prepare(pcm);
    A.start(pcm);
    po->pcm = pcm; po->fmt = fmt; po->bps = fmt_bps(fmt); po->ch = (int)ch; po->rate = (int)rate; po->period = period;
    (void)c;
    return 0;
fail:
    snprintf(err, en, "%s: %s: %s", name, stage, A.strerror(r));
    A.hw_free(hw);
    A.close(pcm);
    return r < 0 ? r : -EINVAL;
}

static bool device_still_present(const EfInput *in) {
    if (in->pcm_override[0]) return true;
    EfInventory *inv = malloc(sizeof *inv);
    if (!inv) return false;
    ef_inventory_scan(inv);
    bool ok = false;
    for (int i = 0; i < inv->ncards; i++) {
        const EfAlsaCard *cd = &inv->cards[i];
        const char *k = cd->is_usb && cd->usbid[0] ? cd->usbid : cd->id;
        if (!strcasecmp(k, in->key) && cd->capture_dev >= 0) ok = true;
    }
    free(inv);
    return ok;
}

/* Resolve the current ALSA name for a key (card numbers can change on replug). */
static void resolve_names(const EfInput *in, char names[2][96]) {
    names[0][0] = names[1][0] = 0;
    if (in->pcm_override[0]) { ef_strlcpy(names[0], in->pcm_override, 96); return; }
    EfInventory *inv = malloc(sizeof *inv);
    if (!inv) return;
    ef_inventory_scan(inv);
    for (int i = 0; i < inv->ncards; i++) {
        const EfAlsaCard *cd = &inv->cards[i];
        const char *k = cd->is_usb && cd->usbid[0] ? cd->usbid : cd->id;
        if (strcasecmp(k, in->key) || cd->capture_dev < 0) continue;
        snprintf(names[0], 96, "hw:CARD=%s,DEV=%d", cd->id, cd->capture_dev);
        snprintf(names[1], 96, "plughw:CARD=%s,DEV=%d", cd->id, cd->capture_dev);
        break;
    }
    free(inv);
}

static inline float decode(const uint8_t *p, int fmt) {
    switch (fmt) {
    case FMT_S16_LE: return (int16_t)(p[0] | (p[1] << 8)) / 32768.0f;
    case FMT_S24_3LE: { int32_t x = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8; return x / 8388608.0f; }
    case FMT_S24_LE: { int32_t x = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8; return x / 8388608.0f; }
    case FMT_S32_LE: { int32_t x = (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); return x / 2147483648.0f; }
    case FMT_FLOAT_LE: { float f; memcpy(&f, p, 4); return (f == f) ? EF_CLAMP(f, -4.0f, 4.0f) : 0.0f; }
    }
    return 0.0f;
}

static int capture_thread(void *arg) {
    EfCapture *c = arg;
    PcmOpen po = {0};
    uint8_t *raw = NULL;
    float *lr = NULL;
    int seen_seq = -1;
    uint32_t retry_at = 0;
    EfInput dev;
    memset(&dev, 0, sizeof dev);
    const int CHUNK = 2048;

    while (!atomic_load(&c->quit)) {
        int seq = atomic_load(&c->req_seq);
        bool want = atomic_load(&c->want_open) != 0;
        if (seq != seen_seq) {   /* new request: close current stream */
            if (po.pcm) { A.close(po.pcm); po.pcm = NULL; }
            seen_seq = seq;
            SDL_LockMutex(c->req_mtx); dev = c->req; SDL_UnlockMutex(c->req_mtx);
            retry_at = 0;
            atomic_store(&c->state, want ? CAP_OPENING : CAP_IDLE);
        }
        if (!want) {
            if (po.pcm) { A.close(po.pcm); po.pcm = NULL; }
            atomic_store(&c->state, CAP_IDLE);
            SDL_Delay(30);
            continue;
        }
        if (!po.pcm) {
            if (SDL_GetTicks() < retry_at) { SDL_Delay(30); continue; }
            if (!c->alsa_loaded) {
                snprintf(c->error, sizeof c->error, "ALSA unavailable: %s", c->alsa_error);
                atomic_store(&c->state, CAP_ERROR);
                retry_at = SDL_GetTicks() + 100000;
                continue;
            }
            int st = atomic_load(&c->state);
            if (st == CAP_LOST && !device_still_present(&dev)) { retry_at = SDL_GetTicks() + 1000; continue; }
            char names[2][96];
            resolve_names(&dev, names);
            c->attempts[0] = 0;
            int r = -ENODEV;
            if (!names[0][0]) {
                snprintf(c->error, sizeof c->error, "%s is not connected / has no ALSA capture device", dev.label);
            } else {
                for (int i = 0; i < 2 && names[i][0]; i++) {
                    char err[200];
                    r = try_open_name(c, names[i], &po, err, sizeof err);
                    if (r == 0) { ef_strlcpy(c->opened_name, names[i], sizeof c->opened_name); break; }
                    size_t al = strlen(c->attempts);
                    snprintf(c->attempts + al, sizeof c->attempts - al, "%s%s", al ? " | " : "", err);
                }
                if (r != 0) snprintf(c->error, sizeof c->error, "%s", c->attempts);
            }
            if (r != 0) {
                if (st != CAP_LOST) atomic_store(&c->state, CAP_ERROR);
                retry_at = SDL_GetTicks() + 1500;   /* keep retrying quietly: replug recovers */
                continue;
            }
            free(raw); free(lr);
            raw = malloc((size_t)CHUNK * po.ch * po.bps);
            lr = malloc((size_t)CHUNK * 2 * sizeof(float));
            if (!raw || !lr) { A.close(po.pcm); po.pcm = NULL; snprintf(c->error, sizeof c->error, "out of memory"); atomic_store(&c->state, CAP_ERROR); continue; }
            c->rate = po.rate; c->dev_channels = po.ch; c->period = (int)po.period;
            ef_strlcpy(c->format_name, fmt_name(po.fmt), sizeof c->format_name);
            atomic_store(&c->signal_seen, 0);
            if (st == CAP_LOST) atomic_fetch_add(&c->reconnects, 1);
            c->error[0] = 0;
            atomic_store(&c->state, CAP_RUNNING);
            ef_log("capture: opened %s  %s %d ch %d Hz period %d", c->opened_name, c->format_name, po.ch, po.rate, (int)po.period);
        }
        int w = A.wait(po.pcm, 100);
        (void)w;
        snd_pcm_sframes_t got = A.readi(po.pcm, raw, CHUNK);
        if (got == -EAGAIN) continue;
        if (got < 0) {
            int er = (int)got;
            if (er == -EPIPE || er == -ESTRPIPE) {
                atomic_fetch_add(&c->xruns, 1);
                if (A.recover(po.pcm, er, 1) >= 0) { A.start(po.pcm); continue; }
            }
            /* device gone (USB unplugged) or unrecoverable */
            ef_log("capture: stream failed: %s", A.strerror(er));
            A.close(po.pcm); po.pcm = NULL;
            snprintf(c->error, sizeof c->error, "stream stopped: %s (device unplugged?) - waiting for it to return", A.strerror(er));
            atomic_store(&c->state, CAP_LOST);
            if (atomic_load(&c->rec_state) == REC_RUNNING || atomic_load(&c->rec_state) == REC_STOPPING) {
                atomic_store(&c->rec_interrupted, 1);
                atomic_store(&c->rec_state, REC_DONE);
            }
            retry_at = SDL_GetTicks() + 1000;
            continue;
        }
        if (got == 0) continue;
        /* convert, meter, monitor, record */
        float gain = ef_db_to_lin(atomic_load(&c->gain_db));
        float rawpk = 0, pk[2] = {0, 0};
        int clip_src = 0, clip_post = 0;
        for (long i = 0; i < got; i++) {
            const uint8_t *p = raw + (size_t)i * po.ch * po.bps;
            float l = decode(p, po.fmt);
            float r = po.ch >= 2 ? decode(p + po.bps, po.fmt) : l;
            float al = fabsf(l), ar = fabsf(r);
            if (al > rawpk) rawpk = al;
            if (ar > rawpk) rawpk = ar;
            if (al >= 0.999f || ar >= 0.999f) clip_src++;
            l *= gain; r *= gain;
            if (fabsf(l) > 1.0f || fabsf(r) > 1.0f) clip_post++;
            if (fabsf(l) > pk[0]) pk[0] = fabsf(l);
            if (fabsf(r) > pk[1]) pk[1] = fabsf(r);
            lr[i * 2] = l; lr[i * 2 + 1] = r;
        }
        if (rawpk > 0.0003f) {   /* above -70 dBFS: real signal, not digital silence */
            atomic_store(&c->signal_seen, 1);
            atomic_store(&c->last_signal_ms, SDL_GetTicks());
        }
        if (rawpk > atomic_load(&c->raw_peak)) atomic_store(&c->raw_peak, rawpk);
        if (pk[0] > atomic_load(&c->peak[0])) atomic_store(&c->peak[0], pk[0]);
        if (pk[1] > atomic_load(&c->peak[1])) atomic_store(&c->peak[1], pk[1]);
        if (clip_src) atomic_fetch_add(&c->clip_src, clip_src);
        if (clip_post) atomic_fetch_add(&c->clip_post, clip_post);
        atomic_fetch_add(&c->frames_in, got);
        if (c->ring.buf) ef_ring_write(&c->ring, lr, (int)got);
        int rs = atomic_load(&c->rec_state);
        if (rs == REC_STOPPING) atomic_store(&c->rec_state, REC_DONE);
        else if (rs == REC_RUNNING && c->rec_buf) {
            long have = atomic_load(&c->rec_frames);
            long room = c->rec_cap - have;
            long n = EF_MIN((long)got, room);
            if (n > 0) memcpy(c->rec_buf + have * 2, lr, (size_t)n * 2 * sizeof(float));
            atomic_store(&c->rec_frames, have + EF_MAX(0, n));
            if (have + n >= c->rec_cap) { atomic_store(&c->rec_full, 1); atomic_store(&c->rec_state, REC_DONE); }
        }
    }
    if (po.pcm) A.close(po.pcm);
    free(raw); free(lr);
    return 0;
}

bool ef_capture_init(EfCapture *c) {
    memset(c, 0, sizeof *c);
    c->alsa_loaded = alsa_load(c);
    if (!c->alsa_loaded) ef_log("capture: %s", c->alsa_error);
    else ef_log("capture: ALSA %s loaded", c->alsa_version);
    ef_ring_init(&c->ring, 32768);
    c->req_mtx = SDL_CreateMutex();
    atomic_store(&c->gain_db, 0.0f);
    c->thread = SDL_CreateThread(capture_thread, "ef_capture", c);
    return c->thread != NULL;
}

void ef_capture_open(EfCapture *c, const EfInput *in) {
    SDL_LockMutex(c->req_mtx); c->req = *in; SDL_UnlockMutex(c->req_mtx);
    atomic_store(&c->want_open, 1);
    atomic_fetch_add(&c->req_seq, 1);
}

void ef_capture_close(EfCapture *c) {
    atomic_store(&c->want_open, 0);
    atomic_fetch_add(&c->req_seq, 1);
}

/* ---- disk writer: streams the growing take to a hidden temp WAV (crash safety) ---- */
static int writer_thread(void *arg) {
    EfCapture *c = arg;
    EfWavStream ws;
    if (!ef_wav_stream_open(&ws, c->rec_temp_path, c->rec_channels, c->rec_rate)) { atomic_store(&c->writer_error, 1); return 0; }
    float *tmp = malloc(sizeof(float) * 2 * 8192);
    for (;;) {
        bool quit = atomic_load(&c->writer_quit) != 0;
        long have = atomic_load(&c->rec_frames), done = atomic_load(&c->rec_flushed);
        while (tmp && done < have) {
            long n = EF_MIN(8192L, have - done);
            const float *src = c->rec_buf + done * 2;
            const float *wr = src;
            if (c->rec_channels == 1) { for (long i = 0; i < n; i++) tmp[i] = src[i * 2]; wr = tmp; }
            if (!ef_wav_stream_append(&ws, wr, n)) { atomic_store(&c->writer_error, 1); break; }
            done += n;
        }
        atomic_store(&c->rec_flushed, done);
        if (quit) break;
        SDL_Delay(250);
    }
    free(tmp);
    ef_wav_stream_close(&ws);
    return 0;
}

bool ef_capture_rec_start(EfCapture *c, float max_seconds, const char *temp_path) {
    if (atomic_load(&c->state) != CAP_RUNNING) return false;
    if (atomic_load(&c->rec_state) != REC_OFF) return false;
    long cap = (long)(max_seconds * c->rate);
    float *buf = malloc((size_t)cap * 2 * sizeof(float));
    if (!buf) return false;
    c->rec_buf = buf; c->rec_cap = cap;
    c->rec_channels = c->dev_channels >= 2 ? 2 : 1;
    c->rec_rate = c->rate;
    atomic_store(&c->rec_frames, 0);
    atomic_store(&c->rec_flushed, 0);
    atomic_store(&c->rec_interrupted, 0);
    atomic_store(&c->rec_full, 0);
    atomic_store(&c->writer_error, 0);
    atomic_store(&c->writer_quit, 0);
    c->rec_temp_path[0] = 0;
    if (temp_path) {
        ef_strlcpy(c->rec_temp_path, temp_path, sizeof c->rec_temp_path);
        c->writer_thread = SDL_CreateThread(writer_thread, "ef_recwriter", c);
    }
    atomic_store(&c->rec_state, REC_RUNNING);
    return true;
}

void ef_capture_rec_stop(EfCapture *c) {
    int expect = REC_RUNNING;
    atomic_compare_exchange_strong(&c->rec_state, &expect, REC_STOPPING);
    /* if the stream is not running the capture thread will not acknowledge: finish here */
    if (atomic_load(&c->state) != CAP_RUNNING) {
        expect = REC_STOPPING;
        atomic_compare_exchange_strong(&c->rec_state, &expect, REC_DONE);
    }
}

void ef_capture_rec_finish(EfCapture *c) {
    if (c->writer_thread) {
        atomic_store(&c->writer_quit, 1);
        SDL_WaitThread(c->writer_thread, NULL);
        c->writer_thread = NULL;
    }
}

void ef_capture_rec_release(EfCapture *c) {
    ef_capture_rec_finish(c);
    /* only free once the capture thread can no longer write (state DONE or OFF) */
    int st = atomic_load(&c->rec_state);
    if (st == REC_RUNNING || st == REC_STOPPING) { ef_capture_rec_stop(c); for (int i = 0; i < 100 && atomic_load(&c->rec_state) != REC_DONE; i++) SDL_Delay(5); }
    if (atomic_load(&c->rec_state) == REC_DONE || atomic_load(&c->rec_state) == REC_OFF) {
        free(c->rec_buf);
        c->rec_buf = NULL;
        c->rec_cap = 0;
        atomic_store(&c->rec_frames, 0);
        atomic_store(&c->rec_state, REC_OFF);
    }
}

void ef_capture_shutdown(EfCapture *c) {
    ef_capture_rec_stop(c);
    atomic_store(&c->quit, 1);
    if (c->thread) SDL_WaitThread(c->thread, NULL);
    c->thread = NULL;
    atomic_store(&c->state, CAP_IDLE);
    if (atomic_load(&c->rec_state) == REC_STOPPING) atomic_store(&c->rec_state, REC_DONE);
    ef_capture_rec_finish(c);   /* flushes everything recorded so far to the temp take */
    free(c->rec_buf); c->rec_buf = NULL;
    ef_ring_free(&c->ring);
}

int ef_recover_recordings(const char *dir, char *msg, size_t n) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int count = 0;
    struct dirent *e;
    char names[16][128];
    int nn = 0;
    while ((e = readdir(d)) && nn < 16) if (!strncmp(e->d_name, ".rec_", 5) && ef_has_suffix_ci(e->d_name, ".wav")) ef_strlcpy(names[nn++], e->d_name, 128);
    closedir(d);
    for (int i = 0; i < nn; i++) {
        char src[EF_PATH_MAX], dst[EF_PATH_MAX];
        ef_path_join(src, sizeof src, dir, names[i]);
        if (ef_wav_repair(src)) {
            snprintf(dst, sizeof dst, "%s/recovered_%s", dir, names[i] + 5);
            if (rename(src, dst) == 0) count++;
        } else remove(src);   /* empty take (nothing was recorded) */
    }
    if (count) snprintf(msg, n, "Recovered %d unfinished recording%s", count, count > 1 ? "s" : "");
    return count;
}
