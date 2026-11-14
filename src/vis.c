/* ECHOFIELD - main screen visualisers.
 * All four read the engine's double-buffered grain snapshot and its output scope
 * buffer; nothing here touches audio state. Colours follow the knob palette:
 * grains are blue below the played pitch, green at it, orange a fifth away and
 * white an octave or more above, so HARMONY is visible as colour.
 */
#include "app.h"
#include "vis.h"

const char *const g_vis_names[VIS_COUNT] = {"ORBIT", "FIELD", "SPECTRUM", "SCOPE"};

/* ------------------------------------------------------------------ spectrum analysis */
#define FFT_N 1024
#define BANDS 48
static float g_band[BANDS], g_peak[BANDS], g_level;

static void fft(float *re, float *im, int n) {
    for (int i = 1, j = 0; i < n; i++) {   /* bit reversal */
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -EF_TWOPI / len, wr = cosf(ang), wi = sinf(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k, b = a + len / 2;
                float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr; im[a] += ti;
                float nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
            }
        }
    }
}

static void analyse(App *a) {
    static float re[FFT_N], im[FFT_N], win[FFT_N];
    static bool init;
    if (!init) { for (int i = 0; i < FFT_N; i++) win[i] = 0.5f - 0.5f * cosf(EF_TWOPI * i / (FFT_N - 1)); init = true; }
    int w = atomic_load_explicit(&a->E.scope_w, memory_order_acquire);
    float sum = 0;
    for (int i = 0; i < FFT_N; i++) {
        int k = (w + i) & (EF_SCOPE_N - 1);
        float m = 0.5f * (a->E.scope[0][k] + a->E.scope[1][k]);
        sum += m * m;
        re[i] = m * win[i]; im[i] = 0.0f;
    }
    float rms = sqrtf(sum / FFT_N);
    g_level = g_level * 0.8f + EF_CLAMP(rms * 4.0f, 0.0f, 1.0f) * 0.2f;
    fft(re, im, FFT_N);
    float binhz = (float)a->sr / FFT_N;
    for (int b = 0; b < BANDS; b++) {
        float f0 = 40.0f * powf(16000.0f / 40.0f, (float)b / BANDS), f1 = 40.0f * powf(16000.0f / 40.0f, (float)(b + 1) / BANDS);
        int i0 = EF_CLAMP((int)(f0 / binhz), 1, FFT_N / 2 - 1), i1 = EF_CLAMP((int)(f1 / binhz), i0 + 1, FFT_N / 2);
        float mx = 0;
        for (int i = i0; i < i1; i++) { float m = re[i] * re[i] + im[i] * im[i]; if (m > mx) mx = m; }
        float db = 10.0f * log10f(mx + 1e-12f) - 20.0f * log10f(FFT_N / 4.0f);
        db += 3.0f * log2f(f0 / 1000.0f + 1.0f);              /* gentle tilt: highs read fairly */
        float v = EF_CLAMP((db + 66.0f) / 60.0f, 0.0f, 1.0f);
        g_band[b] = v > g_band[b] ? g_band[b] + (v - g_band[b]) * 0.6f : g_band[b] * 0.88f + v * 0.12f;
        g_peak[b] = EF_MAX(g_band[b], g_peak[b] - 0.012f);
    }
}

float vis_level(void) { return g_level; }

static Col band_col(int b, int n, uint8_t al) {
    static const uint32_t pal[4] = {C_BLUE, C_GREEN, C_WHITE, C_ORANGE};
    return col(pal[EF_CLAMP(b * 4 / n, 0, 3)], al);
}

static Col grain_col(float pitch, float base, uint8_t al) {
    float d = pitch - base;
    if (d > 9.5f) return col(C_WHITE, al);
    if (d < -3.5f) return col(C_BLUE, al);
    float r = fmodf(fabsf(d) + 0.5f, 12.0f);
    if (r > 4.5f && r < 8.5f) return col(C_ORANGE, al);
    return col(C_GREEN, al);
}

static const EfVisFrame *frame(App *a) {
    int vi = atomic_load_explicit(&a->E.vis_idx, memory_order_acquire);
    return &a->E.vis[vi];
}

/* fade the persistence texture and point the renderer at it (returns false without one) */
static bool trail_begin(App *a, int w, int h) {
    if (!a->trail) return false;
    SDL_SetRenderTarget(a->ren, a->trail);
    draw_blend(SDL_BLENDMODE_BLEND);
    draw_fill(0, 0, w, h, col(0x000000, 60));
    return true;
}
static void trail_end(App *a, int x, int y, int w, int h) {
    SDL_SetRenderTarget(a->ren, NULL);
    SDL_Rect src = {0, 0, w, h}, dst = {x, y, w, h};
    SDL_RenderCopy(a->ren, a->trail, &src, &dst);
    draw_blend(SDL_BLENDMODE_BLEND);
}

/* ------------------------------------------------------------------ ORBIT */
static void vis_orbit(App *a, int x, int y, int w, int h) {
    const EfVisFrame *v = frame(a);
    EfSample *s = a->E.cur;
    int cx = x + w / 2, cy = y + h / 2;
    int R = (int)(EF_MIN(w, h) * 0.33f);
    float base = ef_param_get(&a->P, P_PITCH);
    /* guide rings */
    draw_circle(cx, cy, R + 58, col(C_GRID, 255));
    for (int i = 0; i < 64; i++) {   /* dotted outer scale */
        float an = -EF_PI / 2 + EF_TWOPI * i / 64;
        int rr = R + 64 + (i % 8 == 0 ? 4 : 0);
        draw_fill(cx + (int)(cosf(an) * rr), cy + (int)(sinf(an) * rr), i % 8 == 0 ? 2 : 1, i % 8 == 0 ? 2 : 1, col(i % 8 == 0 ? C_DIM : C_FAINT, 255));
    }
    /* inner spectrum ring */
    for (int b = 0; b < BANDS; b++) {
        float an = -EF_PI / 2 + EF_TWOPI * (b + 0.5f) / BANDS;
        float r0 = R * 0.42f, r1 = r0 + 4 + g_band[b] * R * 0.42f;
        Col c = band_col(b, BANDS, 230);
        for (int t = -1; t <= 1; t++) {
            float aa = an + t * 0.012f;
            draw_line(cx + (int)(cosf(aa) * r0), cy + (int)(sinf(aa) * r0), cx + (int)(cosf(aa) * r1), cy + (int)(sinf(aa) * r1), c);
        }
    }
    /* the sample wrapped around the ring */
    if (s) {
        const int N = 180;
        float c0 = v->center - v->scatter * 0.5f, c1 = v->center + v->scatter * 0.5f;
        for (int i = 0; i < N; i++) {
            float f = (float)i / N;
            int ov = i * EF_OVERVIEW_COLS / N;
            float amp = EF_MAX(fabsf(s->ov_max[ov]), fabsf(s->ov_min[ov])) / EF_MAX(0.05f, s->peak);
            float an = -EF_PI / 2 + EF_TWOPI * f;
            float r0 = R - 2 - amp * 18, r1 = R + 2 + amp * 18;
            bool in_region = f >= a->region_a && f <= a->region_b;
            bool in_band = (f >= c0 && f <= c1) || (f + 1 >= c0 && f + 1 <= c1) || (f - 1 >= c0 && f - 1 <= c1);
            Col c = !in_region ? col(C_GRID, 255) : (in_band ? col(C_WHITE, 255) : col(0x6a6a6a, 255));
            draw_line(cx + (int)(cosf(an) * r0), cy + (int)(sinf(an) * r0), cx + (int)(cosf(an) * r1), cy + (int)(sinf(an) * r1), c);
        }
        /* playhead */
        float an = -EF_PI / 2 + EF_TWOPI * v->center;
        draw_line(cx + (int)(cosf(an) * (R * 0.9f)), cy + (int)(sinf(an) * (R * 0.9f)), cx + (int)(cosf(an) * (R + 50)), cy + (int)(sinf(an) * (R + 50)), col(C_ORANGE, 255));
        draw_disc(cx + (int)(cosf(an) * (R + 50)), cy + (int)(sinf(an) * (R + 50)), 3, col(C_ORANGE, 255));
        if (v->head >= 0) {
            float ah = -EF_PI / 2 + EF_TWOPI * v->head;
            draw_disc(cx + (int)(cosf(ah) * (R + 24)), cy + (int)(sinf(ah) * (R + 24)), 3, col(C_WHITE, 255));
        }
    }
    /* grains orbit just outside the ring, further out when pitched higher */
    int tw = w, th = h;
    bool tr = trail_begin(a, tw, th);
    int ox = tr ? w / 2 : cx, oy = tr ? h / 2 : cy;
    for (int i = 0; i < v->n && i < EF_MAX_GRAINS; i++) {
        const EfGrainVis *g = &v->g[i];
        float an = -EF_PI / 2 + EF_TWOPI * EF_CLAMP(g->x, 0.0f, 1.0f);
        float rr = R + 30 + EF_CLAMP(g->pitch - base, -24.0f, 24.0f) * 1.1f + (g->pan - 0.5f) * 10;
        float amp = EF_CLAMP(g->amp * 1.4f, 0.0f, 1.0f);
        draw_glow_dot(ox + (int)(cosf(an) * rr), oy + (int)(sinf(an) * rr), 2 + (int)(amp * 6), grain_col(g->pitch, base, (uint8_t)(90 + amp * 165)));
    }
    if (tr) trail_end(a, x, y, tw, th);
    /* centre: scene + state */
    int mode = EF_CLAMP((int)ef_param_get(&a->P, P_MODE), 0, MODE_COUNT - 1);
    draw_text_spaced(F_TINY, cx, cy - 8, col(C_TEXT, 255), AL_CENTER, 2, g_mode_names[mode]);
    if (ef_param_get(&a->P, P_FREEZE) > 0.5f) draw_text_spaced(F_TINY, cx, cy + 6, col(C_BLUE, 255), AL_CENTER, 2, "FROZEN");
    else draw_textf(F_TINY, cx, cy + 6, col(C_DIM, 255), AL_CENTER, "%d", v->n);
}

/* ------------------------------------------------------------------ FIELD */
static void vis_field(App *a, int x, int y, int w, int h) {
    const EfVisFrame *v = frame(a);
    EfSample *s = a->E.cur;
    const int pad = 24, gx = x + pad, gw = w - 2 * pad, gy = y + 18, gh = h - 46;
    int mid = gy + gh / 2;
    float base = ef_param_get(&a->P, P_PITCH);
    /* scatter band + playhead */
    float c0 = v->center - v->scatter * 0.5f, c1 = v->center + v->scatter * 0.5f;
    int bx0 = gx + (int)(EF_CLAMP(c0, 0.0f, 1.0f) * gw), bx1 = gx + (int)(EF_CLAMP(c1, 0.0f, 1.0f) * gw);
    draw_fill(bx0, gy, EF_MAX(1, bx1 - bx0), gh, col(0xffffff, 14));
    /* dot-matrix waveform */
    if (s) {
        static SDL_Point dim[4096], lit[4096];
        int nd = 0, nl = 0;
        const int step = 6;
        for (int cxp = 0; cxp < gw && nd < 4000 && nl < 4000; cxp += step) {
            float f = (float)cxp / gw;
            int ov = EF_CLAMP((int)(f * EF_OVERVIEW_COLS), 0, EF_OVERVIEW_COLS - 1);
            float amp = EF_MAX(fabsf(s->ov_max[ov]), fabsf(s->ov_min[ov])) / EF_MAX(0.05f, s->peak);
            int dots = 1 + (int)(amp * (gh / 2 - 6) / step);
            bool on = f >= a->region_a && f <= a->region_b && gx + cxp >= bx0 && gx + cxp <= bx1;
            for (int d = 0; d < dots && nd < 4000 && nl < 4000; d++) {
                SDL_Point p1 = {gx + cxp, mid - d * step}, p2 = {gx + cxp, mid + d * step};
                if (on) { lit[nl++] = p1; lit[nl++] = p2; } else { dim[nd++] = p1; dim[nd++] = p2; }
            }
        }
        /* one batched call per colour: per-dot draw calls are slow on older SDL */
        static SDL_Rect rc[4096];
        for (int i = 0; i < nd; i++) { rc[i].x = dim[i].x - 1; rc[i].y = dim[i].y - 1; rc[i].w = rc[i].h = 2; }
        SDL_SetRenderDrawColor(draw_renderer(), 0x50, 0x50, 0x50, 255);
        SDL_RenderFillRects(draw_renderer(), rc, nd);
        for (int i = 0; i < nl; i++) { rc[i].x = lit[i].x - 1; rc[i].y = lit[i].y - 1; rc[i].w = rc[i].h = 2; }
        SDL_SetRenderDrawColor(draw_renderer(), 0xe8, 0xe8, 0xe8, 255);
        SDL_RenderFillRects(draw_renderer(), rc, nl);
        /* region brackets */
        int ra = gx + (int)(a->region_a * gw), rb = gx + (int)(a->region_b * gw);
        draw_fill(ra, gy - 6, 1, gh + 12, col(C_DIM, 255)); draw_fill(ra, gy - 6, 6, 1, col(C_DIM, 255));
        draw_fill(rb, gy - 6, 1, gh + 12, col(C_DIM, 255)); draw_fill(rb - 5, gy - 6, 6, 1, col(C_DIM, 255));
    }
    int px = gx + (int)(EF_CLAMP(v->center, 0.0f, 1.0f) * gw);
    draw_fill(px, gy - 8, 1, gh + 16, col(C_ORANGE, 255));
    draw_disc(px, gy - 10, 3, col(C_ORANGE, 255));
    if (v->head >= 0) draw_fill(gx + (int)(v->head * gw), gy, 1, gh, col(C_WHITE, 160));
    /* grains */
    bool tr = trail_begin(a, w, h);
    int ox = tr ? 0 : x, oy = tr ? 0 : y;
    for (int i = 0; i < v->n && i < EF_MAX_GRAINS; i++) {
        const EfGrainVis *g = &v->g[i];
        int gxp = ox + pad + (int)(EF_CLAMP(g->x, 0.0f, 1.0f) * gw);
        float pn = EF_CLAMP((g->pitch - base) / 24.0f, -1.0f, 1.0f);
        int gyp = oy + 18 + gh / 2 - (int)(pn * (gh / 2 - 8)) + (int)((g->pan - 0.5f) * 20.0f);
        float amp = EF_CLAMP(g->amp * 1.4f, 0.0f, 1.0f);
        draw_glow_dot(gxp, gyp, 3 + (int)(amp * 7), grain_col(g->pitch, base, (uint8_t)(80 + amp * 175)));
    }
    if (tr) trail_end(a, x, y, w, h);
    /* time ruler */
    for (int i = 0; i <= 16; i++) draw_fill(gx + gw * i / 16, gy + gh + 12, 1, i % 4 == 0 ? 5 : 2, col(i % 4 == 0 ? C_DIM : C_FAINT, 255));
    if (s) draw_textf(F_TINY, gx + gw, gy + gh + 18, col(C_DIM, 255), AL_RIGHT, "%.1fs", (double)a->sample_secs);
}

/* ------------------------------------------------------------------ SPECTRUM (dot matrix) */
static void vis_spectrum(App *a, int x, int y, int w, int h) {
    const int cols = BANDS, rows = 22;
    int cw = (w - 40) / cols, rh = (h - 40) / rows;
    int x0 = x + (w - cw * cols) / 2, y0 = y + 14;
    /* cells grouped by colour: 0-3 band colours, 4 peak, 5 unlit -> six draw calls */
    static SDL_Rect grp[6][BANDS * 24];
    int ng[6] = {0};
    for (int b = 0; b < cols; b++) {
        int lit = (int)(g_band[b] * rows + 0.5f), pk = EF_MIN(rows - 1, (int)(g_peak[b] * rows));
        int ci = EF_CLAMP(b * 4 / cols, 0, 3);
        for (int r = 0; r < rows; r++) {
            SDL_Rect rc = {x0 + b * cw + 1, y0 + (rows - 1 - r) * rh + 1, cw - 3, rh - 3};
            int g = r < lit ? ci : (r == pk && pk > 0 ? 4 : 5);
            grp[g][ng[g]++] = rc;
        }
    }
    for (int g = 0; g < 6; g++) {
        Col c = g < 4 ? band_col(g * cols / 4, cols, 255) : (g == 4 ? col(C_WHITE, 255) : col(0x1c1c1c, 255));
        SDL_SetRenderDrawColor(draw_renderer(), c.r, c.g, c.b, 255);
        SDL_RenderFillRects(draw_renderer(), grp[g], ng[g]);
    }
    static const struct { float hz; const char *t; } marks[] = {{100, "100"}, {1000, "1K"}, {10000, "10K"}};
    for (int i = 0; i < 3; i++) {
        float f = logf(marks[i].hz / 40.0f) / logf(16000.0f / 40.0f);
        draw_text(F_TINY, x0 + (int)(f * cols * cw), y0 + rows * rh + 4, col(C_DIM, 255), AL_CENTER, marks[i].t);
    }
    (void)a;
}

/* ------------------------------------------------------------------ SCOPE (stereo XY) */
static void vis_scope(App *a, int x, int y, int w, int h) {
    int cx = x + w / 2, cy = y + h / 2;
    int R = (int)(EF_MIN(w, h) * 0.44f);
    draw_circle(cx, cy, R, col(C_GRID, 255));
    draw_circle(cx, cy, R / 2, col(C_GRID, 255));
    for (int i = -R; i <= R; i += 6) { draw_fill(cx + i, cy, 1, 1, col(C_FAINT, 255)); draw_fill(cx, cy + i, 1, 1, col(C_FAINT, 255)); }
    draw_text(F_TINY, cx - R - 6, cy - 6, col(C_DIM, 255), AL_RIGHT, "L");
    draw_text(F_TINY, cx + R + 6, cy - 6, col(C_DIM, 255), AL_LEFT, "R");
    static float gain = 4.0f;
    int wpos = atomic_load_explicit(&a->E.scope_w, memory_order_acquire);
    static SDL_Point pts[EF_SCOPE_N];
    float pk = 1e-4f;
    for (int i = 0; i < EF_SCOPE_N; i++) {
        int k = (wpos + i) & (EF_SCOPE_N - 1);
        float l = a->E.scope[0][k], r = a->E.scope[1][k];
        pk = EF_MAX(pk, EF_MAX(fabsf(l), fabsf(r)));
        float sx = (r - l) * 0.7071f * gain, sy = (l + r) * 0.7071f * gain;
        float m = sqrtf(sx * sx + sy * sy);
        if (m > 1.0f) { sx /= m; sy /= m; }
        pts[i].x = cx + (int)(sx * R); pts[i].y = cy - (int)(sy * R);
    }
    gain += (EF_CLAMP(0.7f / pk, 1.0f, 30.0f) - gain) * 0.05f;   /* slow auto-gain keeps the figure readable */
    draw_lines(pts, EF_SCOPE_N / 2, col(C_GREEN, 110));
    draw_lines(pts + EF_SCOPE_N / 2, EF_SCOPE_N / 2, col(C_WHITE, 200));
    float lv = g_level;
    draw_arc(cx, cy, R + 8, -EF_PI / 2, -EF_PI / 2 + EF_TWOPI * lv, 2, col(C_ORANGE, 255));
}

void vis_draw(App *a, int kind, int x, int y, int w, int h) {
    analyse(a);
    switch (kind) {
    default:
    case VIS_ORBIT: vis_orbit(a, x, y, w, h); break;
    case VIS_FIELD: vis_field(a, x, y, w, h); break;
    case VIS_SPECTRUM: vis_spectrum(a, x, y, w, h); break;
    case VIS_SCOPE: vis_scope(a, x, y, w, h); break;
    }
}
