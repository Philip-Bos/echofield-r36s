/* ECHOFIELD - WAV decoding/encoding, resampling, sample utilities */
#include "sample.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = v & 0xff; p[1] = v >> 8; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; p[3] = v >> 24; }

EfSample *ef_sample_alloc(int frames, int channels, int rate) {
    if (frames < 1 || channels < 1 || channels > 2 || rate < 1000) return NULL;
    EfSample *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->L = calloc((size_t)frames + 4, sizeof(float));      /* +4 guard frames for interpolation */
    if (!s->L) { free(s); return NULL; }
    if (channels == 2) {
        s->R = calloc((size_t)frames + 4, sizeof(float));
        if (!s->R) { free(s->L); free(s); return NULL; }
    } else s->R = s->L;
    s->frames = frames; s->channels = channels; s->rate = rate;
    return s;
}

void ef_sample_free(EfSample *s) {
    if (!s) return;
    if (s->R && s->R != s->L) free(s->R);
    free(s->L);
    free(s);
}

void ef_sample_analyze(EfSample *s) {
    float pk = 0.0f;
    for (int c = 0; c < EF_OVERVIEW_COLS; c++) {
        int a = (int)((long)s->frames * c / EF_OVERVIEW_COLS);
        int b = (int)((long)s->frames * (c + 1) / EF_OVERVIEW_COLS);
        if (b <= a) b = a + 1;
        if (b > s->frames) b = s->frames;
        float mn = 0.0f, mx = 0.0f;
        for (int i = a; i < b; i++) {
            float v = s->channels == 2 ? 0.5f * (s->L[i] + s->R[i]) : s->L[i];
            if (v < mn) mn = v;
            if (v > mx) mx = v;
            float al = fabsf(s->L[i]), ar = fabsf(s->R[i]);
            if (al > pk) pk = al;
            if (ar > pk) pk = ar;
        }
        s->ov_min[c] = mn; s->ov_max[c] = mx;
    }
    s->peak = pk;
}

bool ef_wav_probe(const char *path, EfWavInfo *info) {
    memset(info, 0, sizeof *info);
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(info->error, sizeof info->error, "cannot open file"); return false; }
    uint8_t h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
        fclose(f); snprintf(info->error, sizeof info->error, "not a RIFF/WAVE file"); return false;
    }
    bool have_fmt = false;
    long data_bytes = -1;
    for (int guard = 0; guard < 64; guard++) {
        uint8_t ch[8];
        if (fread(ch, 1, 8, f) != 8) break;
        uint32_t sz = rd32(ch + 4);
        if (!memcmp(ch, "fmt ", 4)) {
            uint8_t fm[40] = {0};
            uint32_t n = sz < sizeof fm ? sz : sizeof fm;
            if (fread(fm, 1, n, f) != n) break;
            if (sz > n) fseek(f, (long)(sz - n), SEEK_CUR);
            int tag = rd16(fm);
            info->channels = rd16(fm + 2);
            info->rate = (int)rd32(fm + 4);
            info->bits = rd16(fm + 14);
            if (tag == 0xFFFE && sz >= 26) tag = rd16(fm + 24);   /* extensible: subformat GUID */
            info->format = tag;
            have_fmt = true;
        } else if (!memcmp(ch, "data", 4)) {
            data_bytes = sz;
            break;
        } else {
            fseek(f, (long)(sz + (sz & 1)), SEEK_CUR);
        }
        if (sz & 1 && !memcmp(ch, "fmt ", 4)) fseek(f, 1, SEEK_CUR);
    }
    fclose(f);
    if (!have_fmt) { snprintf(info->error, sizeof info->error, "missing fmt chunk"); return false; }
    if (data_bytes < 0) { snprintf(info->error, sizeof info->error, "missing data chunk"); return false; }
    if (info->format != 1 && info->format != 3) {
        snprintf(info->error, sizeof info->error, "unsupported encoding (tag %d); use PCM or float WAV", info->format);
        return false;
    }
    bool okbits = (info->format == 1 && (info->bits == 8 || info->bits == 16 || info->bits == 24 || info->bits == 32)) ||
                  (info->format == 3 && (info->bits == 32 || info->bits == 64));
    if (!okbits) { snprintf(info->error, sizeof info->error, "unsupported bit depth %d", info->bits); return false; }
    if (info->channels < 1 || info->channels > 16) { snprintf(info->error, sizeof info->error, "unsupported channel count %d", info->channels); return false; }
    if (info->rate < 4000 || info->rate > 384000) { snprintf(info->error, sizeof info->error, "unsupported sample rate %d", info->rate); return false; }
    int bpf = info->channels * info->bits / 8;
    /* 0xFFFFFFFF or 0 sizes come from streamed/unfinalized files: caller may repair */
    info->frames = data_bytes / bpf;
    return true;
}

EfSample *ef_wav_load(const char *path, EfWavInfo *info) {
    EfWavInfo tmp;
    if (!info) info = &tmp;
    if (!ef_wav_probe(path, info)) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(info->error, sizeof info->error, "cannot open file"); return NULL; }
    /* seek to data chunk */
    fseek(f, 12, SEEK_SET);
    uint32_t data_sz = 0;
    for (int guard = 0; guard < 64; guard++) {
        uint8_t ch[8];
        if (fread(ch, 1, 8, f) != 8) { fclose(f); snprintf(info->error, sizeof info->error, "truncated file"); return NULL; }
        uint32_t sz = rd32(ch + 4);
        if (!memcmp(ch, "data", 4)) { data_sz = sz; break; }
        fseek(f, (long)(sz + (sz & 1)), SEEK_CUR);
    }
    long start = ftell(f);
    fseek(f, 0, SEEK_END);
    long avail = ftell(f) - start;
    fseek(f, start, SEEK_SET);
    if ((long)data_sz > avail || data_sz == 0) data_sz = (uint32_t)(avail > 0 ? avail : 0);   /* tolerate bad sizes */
    int bps = info->bits / 8, nch = info->channels;
    long frames = (long)data_sz / (bps * nch);
    const long max_frames = 384000L * 60 * 10;   /* 10 minutes at 384k: hard sanity limit */
    if (frames < 1) { fclose(f); snprintf(info->error, sizeof info->error, "no audio data"); return NULL; }
    if (frames > max_frames) { fclose(f); snprintf(info->error, sizeof info->error, "file too long"); return NULL; }
    int outch = nch >= 2 ? 2 : 1;
    EfSample *s = ef_sample_alloc((int)frames, outch, info->rate);
    if (!s) { fclose(f); snprintf(info->error, sizeof info->error, "out of memory"); return NULL; }
    info->frames = frames;
    /* decode in chunks */
    enum { CH = 4096 };
    uint8_t *buf = malloc((size_t)CH * bps * nch);
    if (!buf) { ef_sample_free(s); fclose(f); snprintf(info->error, sizeof info->error, "out of memory"); return NULL; }
    long pos = 0;
    while (pos < frames) {
        long want = EF_MIN((long)CH, frames - pos);
        size_t got = fread(buf, (size_t)bps * nch, (size_t)want, f);
        if (got == 0) break;
        for (size_t i = 0; i < got; i++) {
            float acc[2] = {0, 0};
            for (int c = 0; c < nch; c++) {
                const uint8_t *p = buf + (i * nch + c) * bps;
                float v;
                if (info->format == 3) {
                    if (bps == 4) { uint32_t u = rd32(p); float fv; memcpy(&fv, &u, 4); v = fv; }
                    else { uint64_t u = (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); double dv; memcpy(&dv, &u, 8); v = (float)dv; }
                    if (!(v == v)) v = 0.0f;                     /* NaN guard */
                    v = EF_CLAMP(v, -8.0f, 8.0f);                 /* inf guard */
                } else if (bps == 1) v = ((int)p[0] - 128) / 128.0f;
                else if (bps == 2) v = (int16_t)rd16(p) / 32768.0f;
                else if (bps == 3) { int32_t x = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8; v = x / 8388608.0f; }
                else v = (int32_t)rd32(p) / 2147483648.0f;
                if (outch == 1) acc[0] += v;
                else if (nch == 2) acc[c] = v;
                else acc[c & 1] += v * (2.0f / nch);              /* fold >2 channels to stereo */
            }
            s->L[pos + (long)i] = acc[0];
            if (outch == 2) s->R[pos + (long)i] = acc[1];
        }
        pos += (long)got;
    }
    free(buf);
    fclose(f);
    if (pos < frames) { s->frames = (int)EF_MAX(1, pos); }
    ef_strlcpy(s->path, path, sizeof s->path);
    const char *b = ef_basename(path);
    ef_strlcpy(s->name, b, sizeof s->name);
    char *dot = strrchr(s->name, '.');
    if (dot) *dot = 0;
    ef_sample_analyze(s);
    return s;
}

static void write_header(uint8_t *h, int channels, int rate, int bits, uint32_t data_bytes, bool isfloat) {
    memcpy(h, "RIFF", 4); wr32(h + 4, 36 + data_bytes); memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4); wr32(h + 16, 16);
    wr16(h + 20, isfloat ? 3 : 1); wr16(h + 22, (uint16_t)channels);
    wr32(h + 24, (uint32_t)rate); wr32(h + 28, (uint32_t)(rate * channels * bits / 8));
    wr16(h + 32, (uint16_t)(channels * bits / 8)); wr16(h + 34, (uint16_t)bits);
    memcpy(h + 36, "data", 4); wr32(h + 40, data_bytes);
}

static void encode(uint8_t *p, float v, int bits) {
    if (bits == 32) { memcpy(p, &v, 4); return; }
    v = EF_CLAMP(v, -1.0f, 1.0f);
    if (bits == 16) { int x = (int)lrint_ef(v * 32767.0f); wr16(p, (uint16_t)(int16_t)x); }
    else { int x = (int)lrint_ef(v * 8388607.0f); p[0] = x & 0xff; p[1] = (x >> 8) & 0xff; p[2] = (x >> 16) & 0xff; }
}

bool ef_wav_write_interleaved(const char *path, const float *data, long frames, int channels, int rate, int bits) {
    char tmp[EF_PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.part", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    int bps = bits / 8;
    uint8_t h[44];
    write_header(h, channels, rate, bits, (uint32_t)(frames * channels * bps), bits == 32);
    bool ok = fwrite(h, 1, 44, f) == 44;
    uint8_t buf[4096 * 8];
    long n = frames * channels, i = 0;
    while (ok && i < n) {
        int k = 0;
        while (i < n && k + bps <= (int)sizeof buf) { encode(buf + k, data[i++], bits); k += bps; }
        ok = fwrite(buf, 1, (size_t)k, f) == (size_t)k;
    }
    fflush(f); fsync(fileno(f));
    ok = (fclose(f) == 0) && ok;
    if (!ok) { remove(tmp); return false; }
    return rename(tmp, path) == 0;
}

bool ef_wav_write_sample(const char *path, const EfSample *s, int start, int end, bool reverse, float gain, int bits) {
    start = EF_CLAMP(start, 0, s->frames - 1);
    end = EF_CLAMP(end, start + 1, s->frames);
    long n = end - start;
    int ch = s->channels;
    float *tmp = malloc((size_t)n * ch * sizeof(float));
    if (!tmp) return false;
    for (long i = 0; i < n; i++) {
        long src = reverse ? (end - 1 - i) : (start + i);
        tmp[i * ch] = s->L[src] * gain;
        if (ch == 2) tmp[i * ch + 1] = s->R[src] * gain;
    }
    bool ok = ef_wav_write_interleaved(path, tmp, n, ch, s->rate, bits);
    free(tmp);
    return ok;
}

bool ef_wav_stream_open(EfWavStream *w, const char *path, int channels, int rate) {
    memset(w, 0, sizeof *w);
    w->f = fopen(path, "wb");
    if (!w->f) return false;
    w->channels = channels; w->rate = rate;
    uint8_t h[44];
    write_header(h, channels, rate, 32, 0, true);   /* sizes patched on close / repair */
    return fwrite(h, 1, 44, w->f) == 44;
}

bool ef_wav_stream_append(EfWavStream *w, const float *d, long frames) {
    if (!w->f || frames <= 0) return w->f != NULL;
    size_t n = (size_t)frames * w->channels;
    if (fwrite(d, sizeof(float), n, w->f) != n) return false;   /* host is little endian */
    w->frames += frames;
    fflush(w->f);
    return true;
}

bool ef_wav_stream_close(EfWavStream *w) {
    if (!w->f) return false;
    uint32_t bytes = (uint32_t)(w->frames * w->channels * 4);
    uint8_t b[4];
    bool ok = true;
    wr32(b, 36 + bytes); ok &= fseek(w->f, 4, SEEK_SET) == 0 && fwrite(b, 1, 4, w->f) == 4;
    wr32(b, bytes); ok &= fseek(w->f, 40, SEEK_SET) == 0 && fwrite(b, 1, 4, w->f) == 4;
    fflush(w->f); fsync(fileno(w->f));
    ok = (fclose(w->f) == 0) && ok;
    w->f = NULL;
    return ok;
}

/* Fix the header of a float32 stream file whose sizes were never written (crash/kill). */
bool ef_wav_repair(const char *path) {
    FILE *f = fopen(path, "r+b");
    if (!f) return false;
    uint8_t h[44];
    if (fread(h, 1, 44, f) != 44 || memcmp(h, "RIFF", 4) || memcmp(h + 36, "data", 4)) { fclose(f); return false; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    int ch = rd16(h + 22);
    if (ch < 1) { fclose(f); return false; }
    long data = len - 44;
    data -= data % (ch * 4);
    if (data <= 0) { fclose(f); return false; }
    wr32(h + 4, (uint32_t)(36 + data));
    wr32(h + 40, (uint32_t)data);
    fseek(f, 0, SEEK_SET);
    bool ok = fwrite(h, 1, 44, f) == 44;
    ok = (fclose(f) == 0) && ok;
    return ok;
}

EfSample *ef_sample_from_interleaved(const float *d, long frames, int channels, int rate) {
    int outch = channels >= 2 ? 2 : 1;
    EfSample *s = ef_sample_alloc((int)frames, outch, rate);
    if (!s) return NULL;
    for (long i = 0; i < frames; i++) {
        s->L[i] = d[i * channels];
        if (outch == 2) s->R[i] = d[i * channels + 1];
    }
    ef_sample_analyze(s);
    return s;
}

/* Windowed-sinc resampler (offline; runs on the worker thread). */
EfSample *ef_sample_resample(const EfSample *s, int new_rate) {
    if (new_rate == s->rate) {
        EfSample *c = ef_sample_alloc(s->frames, s->channels, s->rate);
        if (!c) return NULL;
        memcpy(c->L, s->L, sizeof(float) * s->frames);
        if (s->channels == 2) memcpy(c->R, s->R, sizeof(float) * s->frames);
        memcpy(c->name, s->name, sizeof c->name); memcpy(c->path, s->path, sizeof c->path);
        ef_sample_analyze(c);
        return c;
    }
    double ratio = (double)new_rate / s->rate;
    long n = (long)((double)s->frames * ratio);
    if (n < 1) n = 1;
    EfSample *o = ef_sample_alloc((int)n, s->channels, new_rate);
    if (!o) return NULL;
    const int HALF = 8;
    double cutoff = ratio < 1.0 ? ratio * 0.95 : 0.95;   /* anti-alias when downsampling */
    for (int ch = 0; ch < s->channels; ch++) {
        const float *in = ch ? s->R : s->L;
        float *out = ch ? o->R : o->L;
        for (long i = 0; i < n; i++) {
            double t = i / ratio;
            long c = (long)t;
            double frac = t - c;
            double acc = 0, wsum = 0;
            for (int k = -HALF + 1; k <= HALF; k++) {
                long idx = c + k;
                double x = (k - frac);
                double sx = x * cutoff;
                double sinc = fabs(sx) < 1e-9 ? 1.0 : sin(M_PI * sx) / (M_PI * sx);
                double w = 0.42 + 0.5 * cos(M_PI * x / HALF) + 0.08 * cos(2 * M_PI * x / HALF);   /* Blackman */
                double coef = sinc * w;
                wsum += coef;
                if (idx >= 0 && idx < s->frames) acc += in[idx] * coef;
            }
            out[i] = (float)(wsum > 1e-9 ? acc / wsum : acc);
        }
    }
    memcpy(o->name, s->name, sizeof o->name); memcpy(o->path, s->path, sizeof o->path);
    ef_sample_analyze(o);
    return o;
}

EfSample *ef_sample_render(const EfSample *s, int start, int end, bool reverse, float gain) {
    start = EF_CLAMP(start, 0, s->frames - 1);
    end = EF_CLAMP(end, start + 1, s->frames);
    int n = end - start;
    EfSample *o = ef_sample_alloc(n, s->channels, s->rate);
    if (!o) return NULL;
    for (int i = 0; i < n; i++) {
        int src = reverse ? end - 1 - i : start + i;
        o->L[i] = s->L[src] * gain;
        if (s->channels == 2) o->R[i] = s->R[src] * gain;
    }
    ef_strlcpy(o->name, s->name, sizeof o->name);
    ef_sample_analyze(o);
    return o;
}

EfSample *ef_sample_make_mono(const EfSample *s) {
    EfSample *o = ef_sample_alloc(s->frames, 1, s->rate);
    if (!o) return NULL;
    for (int i = 0; i < s->frames; i++) o->L[i] = s->channels == 2 ? 0.5f * (s->L[i] + s->R[i]) : s->L[i];
    ef_strlcpy(o->name, s->name, sizeof o->name);
    ef_sample_analyze(o);
    return o;
}

EfSample *ef_sample_make_stereo(const EfSample *s) {
    EfSample *o = ef_sample_alloc(s->frames, 2, s->rate);
    if (!o) return NULL;
    memcpy(o->L, s->L, sizeof(float) * s->frames);
    memcpy(o->R, s->R, sizeof(float) * s->frames);
    ef_strlcpy(o->name, s->name, sizeof o->name);
    ef_sample_analyze(o);
    return o;
}
