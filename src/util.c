/* ECHOFIELD - logging, paths, file helpers */
#include "common.h"
#include "compat/sdl_min.h"

EfPaths g_paths;
static FILE *g_log;
static SDL_mutex *g_log_mtx;

void ef_log_open(const char *path) {
    if (!g_log_mtx) g_log_mtx = SDL_CreateMutex();
    if (g_log) fclose(g_log);
    g_log = path ? fopen(path, "w") : NULL;
}

void ef_log(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 2) n = sizeof buf - 2;
    buf[n++] = '\n'; buf[n] = 0;
    if (g_log_mtx) SDL_LockMutex(g_log_mtx);
    write(2, buf, (size_t)n);                 /* console / PortMaster log.txt */
    if (g_log) { fputs(buf, g_log); fflush(g_log); }
    if (g_log_mtx) SDL_UnlockMutex(g_log_mtx);
}

void ef_log_close(void) { if (g_log) fclose(g_log); g_log = NULL; }

void ef_strlcpy(char *d, const char *s, size_t n) {
    if (!n) return;
    size_t i = 0;
    if (s) for (; i + 1 < n && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}

void ef_path_join(char *out, size_t n, const char *a, const char *b) {
    size_t la = strlen(a);
    if (la && a[la - 1] == '/') snprintf(out, n, "%s%s", a, b);
    else snprintf(out, n, "%s/%s", a, b);
}

const char *ef_basename(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }

bool ef_dir_exists(const char *p) { DIR *d = opendir(p); if (!d) return false; closedir(d); return true; }
bool ef_file_exists(const char *p) { return access(p, F_OK) == 0; }

bool ef_mkdirs(const char *p) {
    char tmp[EF_PATH_MAX];
    ef_strlcpy(tmp, p, sizeof tmp);
    for (char *s = tmp + 1; *s; s++) {
        if (*s == '/') { *s = 0; if (!ef_dir_exists(tmp)) mkdir(tmp, 0755); *s = '/'; }
    }
    if (!ef_dir_exists(tmp)) mkdir(tmp, 0755);
    return ef_dir_exists(tmp);
}

bool ef_dir_writable(const char *p) {
    char probe[EF_PATH_MAX];
    snprintf(probe, sizeof probe, "%s/.ef_write_probe", p);
    FILE *f = fopen(probe, "w");
    if (!f) return false;
    bool ok = fputs("ok", f) >= 0;
    ok = (fclose(f) == 0) && ok;
    remove(probe);
    return ok;
}

long ef_file_size(const char *p) {
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

char *ef_read_text_file(const char *path, long max_bytes) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > max_bytes) { fclose(f); return NULL; }
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    return buf;
}

/* write to tmp then rename, so a crash never leaves a half-written file */
bool ef_write_text_file_atomic(const char *path, const char *text) {
    char tmp[EF_PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return false;
    size_t n = strlen(text);
    bool ok = fwrite(text, 1, n, f) == n;
    fflush(f);
    fsync(fileno(f));
    ok = (fclose(f) == 0) && ok;
    if (!ok) { remove(tmp); return false; }
    return rename(tmp, path) == 0;
}

void ef_timestamp(char *out, size_t n, const char *kind) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    if (kind && kind[0] == 'h')
        snprintf(out, n, "%04d-%02d-%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    else
        snprintf(out, n, "%04d%02d%02d_%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

double ef_now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

bool ef_has_suffix_ci(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcasecmp(s + a - b, suf) == 0;
}

void ef_trim(char *s) {
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    size_t i = 0;
    while (s[i] && isspace((unsigned char)s[i])) i++;
    if (i) memmove(s, s + i, n - i + 1);
}

static int cmp_entry(const void *a, const void *b) {
    return strcasecmp(((const EfFileEntry *)a)->name, ((const EfFileEntry *)b)->name);
}

int ef_list_files(const char *dir, const char *suffix, EfFileEntry *out, int max) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        if (e->d_name[0] == '.') continue;          /* hidden + in-progress temp files */
        if (e->d_type == DT_DIR) continue;
        if (suffix && !ef_has_suffix_ci(e->d_name, suffix)) continue;
        ef_strlcpy(out[n].name, e->d_name, sizeof out[n].name);
        ef_path_join(out[n].path, sizeof out[n].path, dir, e->d_name);
        if (e->d_type == DT_UNKNOWN && ef_dir_exists(out[n].path)) continue;
        out[n].size = ef_file_size(out[n].path);
        if (out[n].size < 0) continue;
        n++;
    }
    closedir(d);
    qsort(out, (size_t)n, sizeof *out, cmp_entry);
    return n;
}

/* Choose a writable data root. Never assumes the install dir is writable. */
static bool try_data_dir(const char *p) {
    if (!p || !p[0]) return false;
    if (!ef_mkdirs(p)) return false;
    if (!ef_dir_writable(p)) return false;
    ef_strlcpy(g_paths.data_dir, p, sizeof g_paths.data_dir);
    return true;
}

bool ef_paths_init(const char *override_data) {
    if (!getcwd(g_paths.app_dir, sizeof g_paths.app_dir)) ef_strlcpy(g_paths.app_dir, ".", sizeof g_paths.app_dir);
    const char *envapp = getenv("ECHOFIELD_APPDIR");
    if (envapp && envapp[0]) ef_strlcpy(g_paths.app_dir, envapp, sizeof g_paths.app_dir);
    ef_path_join(g_paths.factory_samples, sizeof g_paths.factory_samples, g_paths.app_dir, "assets/samples");

    char cand[4][EF_PATH_MAX];
    int nc = 0;
    if (override_data) ef_strlcpy(cand[nc++], override_data, EF_PATH_MAX);
    const char *env = getenv("ECHOFIELD_DATA");
    if (env && env[0]) ef_strlcpy(cand[nc++], env, EF_PATH_MAX);
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (xdg && xdg[0]) snprintf(cand[nc++], EF_PATH_MAX, "%s/echofield", xdg);
    if (home && home[0]) snprintf(cand[nc++], EF_PATH_MAX, "%s/.local/share/echofield", home);
    g_paths.data_writable = false;
    for (int i = 0; i < nc && !g_paths.data_writable; i++) g_paths.data_writable = try_data_dir(cand[i]);
    if (!g_paths.data_writable) g_paths.data_writable = try_data_dir("/tmp/echofield");
    if (!g_paths.data_writable) ef_strlcpy(g_paths.data_dir, "/tmp", sizeof g_paths.data_dir);

    ef_path_join(g_paths.recordings, EF_PATH_MAX, g_paths.data_dir, "recordings");
    ef_path_join(g_paths.samples, EF_PATH_MAX, g_paths.data_dir, "samples");
    ef_path_join(g_paths.presets, EF_PATH_MAX, g_paths.data_dir, "presets");
    ef_path_join(g_paths.projects, EF_PATH_MAX, g_paths.data_dir, "projects");
    ef_mkdirs(g_paths.recordings);
    ef_mkdirs(g_paths.samples);
    ef_mkdirs(g_paths.presets);
    ef_mkdirs(g_paths.projects);
    return g_paths.data_writable;
}

void ef_fpu_flush_denormals(void) {
#if defined(__aarch64__)
    uint64_t fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1ull << 24);  /* FZ: flush-to-zero */
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
#elif defined(__x86_64__)
    unsigned int csr;
    __asm__ volatile("stmxcsr %0" : "=m"(csr));
    csr |= 0x8040;         /* FTZ | DAZ */
    __asm__ volatile("ldmxcsr %0" : : "m"(csr));
#endif
}
