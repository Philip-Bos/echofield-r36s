/*
 * minilibc.h - the complete C library surface used by ECHOFIELD.
 *
 * Why this exists: ECHOFIELD is cross-compiled for the R36S (aarch64, ArkOS,
 * glibc 2.30) without an aarch64 sysroot. Every declaration here uses only
 * types whose layout is identical on x86_64 and aarch64 LP64 Linux/glibc, so the
 * same header is used for the desktop test build and the device build. Opaque
 * types (FILE, DIR) are never dereferenced. Anything struct-layout-sensitive
 * (pthread, stat, sigaction, open flags) is deliberately avoided; threads,
 * mutexes and dynamic loading go through SDL2 instead.
 *
 * Freestanding compiler headers (stdint/stddef/stdarg/stdbool/stdatomic/float)
 * come from clang itself and are always correct for the target.
 */
#ifndef EF_MINILIBC_H
#define EF_MINILIBC_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <float.h>
#ifndef INT_MAX
#define INT_MAX 2147483647
#endif

typedef long ssize_t;
typedef long time_t;
typedef unsigned int mode_t;

/* ---- stdio ---- */
typedef struct _IO_FILE FILE;
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
size_t fread(void *p, size_t sz, size_t n, FILE *f);
size_t fwrite(const void *p, size_t sz, size_t n, FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
int fflush(FILE *f);
int fileno(FILE *f);
int fprintf(FILE *f, const char *fmt, ...);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int printf(const char *fmt, ...);
int snprintf(char *s, size_t n, const char *fmt, ...);
int vsnprintf(char *s, size_t n, const char *fmt, va_list ap);
int sscanf(const char *s, const char *fmt, ...);
char *fgets(char *s, int n, FILE *f);
int fputs(const char *s, FILE *f);
int rename(const char *a, const char *b);
int remove(const char *p);
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EOF (-1)

/* ---- stdlib ---- */
void *malloc(size_t n);
void *calloc(size_t n, size_t sz);
void *realloc(void *p, size_t n);
void free(void *p);
char *getenv(const char *k);
int setenv(const char *k, const char *v, int overwrite);
int unsetenv(const char *k);
int abs(int x);
int atoi(const char *s);
long strtol(const char *s, char **end, int base);
double strtod(const char *s, char **end);
void exit(int code) __attribute__((noreturn));
void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *));

/* ---- string ---- */
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);
char *strncpy(char *d, const char *s, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *h, const char *n);
char *strdup(const char *s);
char *strerror(int e);

/* ---- ctype (functions, not the table macros) ---- */
int tolower(int c);
int toupper(int c);
int isspace(int c);
int isalnum(int c);
int isdigit(int c);

/* ---- math (libm) ---- */
float sinf(float), cosf(float), tanf(float), expf(float), logf(float), log10f(float);
float powf(float, float), tanhf(float), sqrtf(float), floorf(float), ceilf(float);
float fabsf(float), fmodf(float, float), exp2f(float), log2f(float), roundf(float);
double sin(double), cos(double), exp(double), log(double), log10(double), pow(double, double);
double sqrt(double), floor(double), ceil(double), fabs(double), fmod(double, double);
/* let the compiler inline the trivial ones */
#define fabsf(x) __builtin_fabsf(x)
#define fabs(x) __builtin_fabs(x)
#define sqrtf(x) __builtin_sqrtf(x)
#define floorf(x) __builtin_floorf(x)
#define floor(x) __builtin_floor(x)
#define ceilf(x) __builtin_ceilf(x)
#define M_PI 3.14159265358979323846
#define EF_PI 3.14159265358979323846f
#define EF_TWOPI 6.28318530717958647692f

/* ---- time ---- */
struct timespec { long tv_sec; long tv_nsec; };
struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
    long tm_gmtoff;
    const char *tm_zone;
};
time_t time(time_t *t);
struct tm *localtime_r(const time_t *t, struct tm *out);
int clock_gettime(int clk, struct timespec *ts);
#define CLOCK_MONOTONIC 1
int usleep(unsigned int usec);

/* ---- filesystem / unistd ---- */
typedef struct __dirstream DIR;
struct dirent { /* identical on x86_64 and aarch64 glibc */
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[256];
};
#define DT_DIR 4
#define DT_REG 8
#define DT_LNK 10
#define DT_UNKNOWN 0
DIR *opendir(const char *p);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);
int mkdir(const char *p, mode_t m);
int access(const char *p, int mode);
#define F_OK 0
#define W_OK 2
#define R_OK 4
ssize_t readlink(const char *p, char *buf, size_t n);
int symlink(const char *target, const char *linkpath);
int fsync(int fd);
ssize_t write(int fd, const void *buf, size_t n);
char *getcwd(char *buf, size_t n);
int chdir(const char *p);

/* ---- misc ---- */
int *__errno_location(void);
#define errno (*__errno_location())
typedef void (*sighandler_t)(int);
sighandler_t signal(int sig, sighandler_t h);
#define SIGINT 2
#define SIGTERM 15
#define SIGHUP 1

struct utsname { char sysname[65], nodename[65], release[65], version[65], machine[65], domainname[65]; };
int uname(struct utsname *u);

/* errno values (asm-generic, same on x86_64 and aarch64) */
#define EPERM 1
#define ENOENT 2
#define EIO 5
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EBUSY 16
#define ENODEV 19
#define EINVAL 22
#define EPIPE 32
#define ESTRPIPE 86
#define EBADFD 77
#define ENOTSUP 95

#endif
