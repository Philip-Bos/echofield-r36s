/* ECHOFIELD - drawing primitives and bitmap text */
#include "draw.h"
#include "font_data.h"

static SDL_Renderer *R;
static SDL_Texture *g_font_tex[F_COUNT];
static const EfFontData *g_font[F_COUNT] = {&k_font_small, &k_font_body, &k_font_big, &k_font_logo, &k_font_tiny};
static SDL_Texture *g_dot_tex;

SDL_Renderer *draw_renderer(void) { return R; }

bool draw_init(SDL_Renderer *r) {
    R = r;
    for (int f = 0; f < F_COUNT; f++) {
        const EfFontData *fd = g_font[f];
        uint32_t *px = malloc(sizeof(uint32_t) * fd->atlas_w * fd->atlas_h);
        if (!px) return false;
        for (int i = 0; i < fd->atlas_w * fd->atlas_h; i++) px[i] = ((uint32_t)fd->atlas[i] << 24) | 0xFFFFFF;
        g_font_tex[f] = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, fd->atlas_w, fd->atlas_h);
        if (!g_font_tex[f]) { free(px); ef_log("font texture: %s", SDL_GetError()); return false; }
        SDL_UpdateTexture(g_font_tex[f], NULL, px, fd->atlas_w * 4);
        SDL_SetTextureBlendMode(g_font_tex[f], SDL_BLENDMODE_BLEND);
        free(px);
    }
    /* soft round particle used for grains */
    enum { D = 32 };
    uint32_t dot[D * D];
    for (int y = 0; y < D; y++) for (int x = 0; x < D; x++) {
        float dx = (x + 0.5f - D / 2.0f) / (D / 2.0f), dy = (y + 0.5f - D / 2.0f) / (D / 2.0f);
        float d = sqrtf(dx * dx + dy * dy);
        float a = d >= 1.0f ? 0.0f : (1.0f - d) * (1.0f - d);
        if (d < 0.25f) a = 1.0f;
        dot[y * D + x] = ((uint32_t)(a * 255.0f) << 24) | 0xFFFFFF;
    }
    g_dot_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, D, D);
    if (g_dot_tex) { SDL_UpdateTexture(g_dot_tex, NULL, dot, D * 4); SDL_SetTextureBlendMode(g_dot_tex, SDL_BLENDMODE_ADD); }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    return true;
}

void draw_shutdown(void) {
    for (int f = 0; f < F_COUNT; f++) if (g_font_tex[f]) SDL_DestroyTexture(g_font_tex[f]);
    if (g_dot_tex) SDL_DestroyTexture(g_dot_tex);
}

void draw_blend(SDL_BlendMode m) { SDL_SetRenderDrawBlendMode(R, m); }
void draw_fill(int x, int y, int w, int h, Col c) {
    SDL_Rect rc = {x, y, w, h};
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(R, &rc);
}
void draw_rect(int x, int y, int w, int h, Col c) {
    SDL_Rect rc = {x, y, w, h};
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a);
    SDL_RenderDrawRect(R, &rc);
}
void draw_line(int x1, int y1, int x2, int y2, Col c) {
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLine(R, x1, y1, x2, y2);
}

int draw_font_height(int font) { return g_font[font]->height; }

int draw_text_width(int font, const char *s) {
    const EfFontData *fd = g_font[font];
    int w = 0;
    for (; *s; s++) w += fd->glyphs[(unsigned char)*s & 127].adv;
    return w;
}

int draw_text(int font, int x, int y, Col c, int align, const char *s) {
    const EfFontData *fd = g_font[font];
    int w = draw_text_width(font, s);
    if (align == AL_CENTER) x -= w / 2;
    else if (align == AL_RIGHT) x -= w;
    SDL_Texture *t = g_font_tex[font];
    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t, c.a);
    int pen = x;
    for (; *s; s++) {
        const EfGlyph *g = &fd->glyphs[(unsigned char)*s & 127];
        if (g->w > 0 && g->h > 0) {
            SDL_Rect src = {g->x, g->y, g->w, g->h};
            SDL_Rect dst = {pen + g->xoff, y + g->yoff, g->w, g->h};
            SDL_RenderCopy(R, t, &src, &dst);
        }
        pen += g->adv;
    }
    return w;
}

int draw_textf(int font, int x, int y, Col c, int align, const char *fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    return draw_text(font, x, y, c, align, buf);
}

void draw_text_clipped(int font, int x, int y, Col c, int maxw, const char *s) {
    char buf[256];
    ef_strlcpy(buf, s, sizeof buf);
    if (draw_text_width(font, buf) > maxw) {
        size_t n = strlen(buf);
        while (n > 1 && draw_text_width(font, buf) + draw_text_width(font, G_ELLIPSIS) > maxw) buf[--n] = 0;
        size_t l = strlen(buf);
        if (l + 1 < sizeof buf) { buf[l] = G_ELLIPSIS[0]; buf[l + 1] = 0; }
    }
    draw_text(font, x, y, c, AL_LEFT, buf);
}

int draw_text_wrap(int font, int x, int y, Col c, int maxw, int line_h, const char *s) {
    char line[256];
    int lines = 0;
    const char *p = s;
    while (*p) {
        int n = 0, last_space = -1;
        line[0] = 0;
        while (p[n] && p[n] != '\n' && n < 250) {
            line[n] = p[n]; line[n + 1] = 0;
            if (p[n] == ' ') last_space = n;
            if (draw_text_width(font, line) > maxw) {
                if (last_space > 0) n = last_space;
                break;
            }
            n++;
        }
        line[n] = 0;
        draw_text(font, x, y + lines * line_h, c, AL_LEFT, line);
        lines++;
        p += n;
        while (*p == ' ' || *p == '\n') p++;
    }
    return lines;
}

void draw_glow_dot(int x, int y, int r, Col c) {
    if (!g_dot_tex) { draw_fill(x - 1, y - 1, 3, 3, c); return; }
    SDL_SetTextureColorMod(g_dot_tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(g_dot_tex, c.a);
    SDL_Rect dst = {x - r, y - r, r * 2, r * 2};
    SDL_RenderCopy(R, g_dot_tex, NULL, &dst);
}

void draw_lines(const SDL_Point *p, int n, Col c) {
    if (n < 2) return;
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLines(R, p, n);
}

void draw_points(const SDL_Point *p, int n, Col c) {
    if (n < 1) return;
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a);
    SDL_RenderDrawPoints(R, p, n);
}

void draw_arc(int cx, int cy, int r, float a0, float a1, int thick, Col c) {
    SDL_Point pts[130];
    float span = a1 - a0;
    int n = EF_CLAMP((int)(fabsf(span) * r / 4.0f) + 2, 2, 128);
    for (int t = 0; t < thick; t++) {
        float rr = (float)r - t;
        for (int i = 0; i < n; i++) {
            float a = a0 + span * i / (n - 1);
            pts[i].x = cx + (int)lrint_ef(cosf(a) * rr); pts[i].y = cy + (int)lrint_ef(sinf(a) * rr);
        }
        draw_lines(pts, n, c);
    }
}

void draw_circle(int cx, int cy, int r, Col c) { draw_arc(cx, cy, r, 0.0f, EF_TWOPI, 1, c); }

void draw_disc(int cx, int cy, int r, Col c) {
    SDL_Rect rows[160];
    int n = 0;
    for (int dy = -r; dy <= r && n < 160; dy++) {
        int w = (int)sqrtf((float)(r * r - dy * dy));
        rows[n].x = cx - w; rows[n].y = cy + dy; rows[n].w = 2 * w + 1; rows[n].h = 1; n++;
    }
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a);
    SDL_RenderFillRects(R, rows, n);
}

int draw_text_spaced(int font, int x, int y, Col c, int align, int spacing, const char *s) {
    int n = (int)strlen(s);
    int w = draw_text_width(font, s) + spacing * EF_MAX(0, n - 1);
    if (align == AL_CENTER) x -= w / 2;
    else if (align == AL_RIGHT) x -= w;
    char ch[2] = {0, 0};
    for (int i = 0; i < n; i++) { ch[0] = s[i]; x += draw_text(font, x, y, c, AL_LEFT, ch) + spacing; }
    return w;
}

void draw_lock_icon(int x, int y, Col c) {
    draw_rect(x + 2, y, 6, 6, c);
    draw_fill(x, y + 5, 10, 7, c);
}

/* button hint: the button name inverted (light box, dark text), then a dim label */
void draw_button_chip(int x, int y, const char *btn, const char *label, Col accent, int *out_w) {
    (void)accent;
    int bw = draw_text_width(F_TINY, btn) + 8;
    draw_fill(x, y + 1, bw, 15, col(C_WHITE, 255));
    draw_text(F_TINY, x + 4, y + 2, col(C_BG, 255), AL_LEFT, btn);
    int lw = draw_text(F_TINY, x + bw + 5, y + 2, col(C_DIM, 255), AL_LEFT, label);
    if (out_w) *out_w = bw + 5 + lw;
}
