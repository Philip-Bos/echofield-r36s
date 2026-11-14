/* ECHOFIELD - audio sample buffers and WAV I/O */
#ifndef EF_SAMPLE_H
#define EF_SAMPLE_H
#include "common.h"

#define EF_OVERVIEW_COLS 600

typedef struct EfSample {
    float *L, *R;          /* planar float; R == L for mono material */
    int frames;
    int rate;
    int channels;          /* 1 or 2 (source channels preserved up to stereo) */
    char name[96];
    char path[EF_PATH_MAX];
    float peak;
    float ov_min[EF_OVERVIEW_COLS], ov_max[EF_OVERVIEW_COLS];  /* waveform overview */
} EfSample;

typedef struct {
    int format;      /* 1 = PCM, 3 = float */
    int channels, rate, bits;
    long frames;
    char error[128];
} EfWavInfo;

/* Decode a RIFF/WAVE file (PCM 8/16/24/32, float 32/64, WAVE_FORMAT_EXTENSIBLE).
 * More than two channels are folded to stereo. Returns NULL + info->error on failure. */
EfSample *ef_wav_load(const char *path, EfWavInfo *info);
bool ef_wav_probe(const char *path, EfWavInfo *info);   /* header only */

/* Write. bits: 16, 24 (PCM) or 32 (float). Interleaved input. */
bool ef_wav_write_interleaved(const char *path, const float *data, long frames, int channels, int rate, int bits);
bool ef_wav_write_sample(const char *path, const EfSample *s, int start, int end, bool reverse, float gain, int bits);

/* Streaming float32 writer used by the recorder (crash-safe: header patched on finalize,
 * and ef_wav_repair() can fix a file whose header was never patched). */
typedef struct { FILE *f; int channels, rate; long frames; } EfWavStream;
bool ef_wav_stream_open(EfWavStream *w, const char *path, int channels, int rate);
bool ef_wav_stream_append(EfWavStream *w, const float *interleaved, long frames);
bool ef_wav_stream_close(EfWavStream *w);
bool ef_wav_repair(const char *path);

EfSample *ef_sample_alloc(int frames, int channels, int rate);
void ef_sample_free(EfSample *s);
EfSample *ef_sample_resample(const EfSample *s, int new_rate);  /* returns new sample */
EfSample *ef_sample_from_interleaved(const float *data, long frames, int channels, int rate);
EfSample *ef_sample_render(const EfSample *s, int start, int end, bool reverse, float gain);
void ef_sample_analyze(EfSample *s);   /* peak + overview */
EfSample *ef_sample_make_mono(const EfSample *s);
EfSample *ef_sample_make_stereo(const EfSample *s);

#endif
