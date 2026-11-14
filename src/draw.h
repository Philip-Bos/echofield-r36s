/* ECHOFIELD - minimal 2D drawing layer over SDL_Renderer */
#ifndef EF_DRAW_H
#define EF_DRAW_H
#include "common.h"
#include "compat/sdl_min.h"

#define SCREEN_W 640
#define SCREEN_H 480

enum { F_SMALL, F_BODY, F_BIG, F_LOGO, F_TINY, F_COUNT };
enum { AL_LEFT, AL_CENTER, AL_RIGHT };

/* glyph codes for symbols in strings */
#define G_RIGHT "\x01"
#define G_LEFT "\x02"
#define G_DOT "\x03"
#define G_DIAMOND "\x04"
#define G_CHECK "\x05"
#define G_CROSS "\x06"
#define G_UP "\x07"
#define G_DOWN "\x08"
#define G_RING "\x0b"
#define G_NOTE "\x0c"
#define G_ELLIPSIS "\x0e"
#define G_LOOP "\x0f"
#define G_MIDDOT "\x10"
#define G_UPDOWN "\x11"
#define G_LEFTRIGHT "\x12"

typedef struct { uint8_t r, g, b, a; } Col;
static inline Col col(uint32_t rgb, uint8_t a) { Col c = {(uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb, a}; return c; }
static inline Col col_mix(Col a, Col b, float t) {
    Col c = {(uint8_t)(a.r + (b.r - a.r) * t), (uint8_t)(a.g + (b.g - a.g) * t), (uint8_t)(a.b + (b.b - a.b) * t), (uint8_t)(a.a + (b.a - a.a) * t)};
    return c;
}

/* palette: black screen, white type, four instrument colours (one per knob) */
#define C_BG 0x050505
#define C_PANEL 0x101010
#define C_PANEL2 0x1b1b1b
#define C_GRID 0x262626
#define C_TEXT 0xf0f0f0
#define C_DIM 0x8c8c8c
#define C_FAINT 0x454545
#define C_REC 0xff4a1c
#define C_OK 0x2fd27a
#define C_WARN 0xffb000
#define C_ERR 0xff3b30
#define C_BLUE 0x2e86ff
#define C_GREEN 0x2fd27a
#define C_WHITE 0xe8e8e8
#define C_ORANGE 0xff5f1f

bool draw_init(SDL_Renderer *r);
void draw_shutdown(void);
SDL_Renderer *draw_renderer(void);
void draw_fill(int x, int y, int w, int h, Col c);
void draw_rect(int x, int y, int w, int h, Col c);
void draw_line(int x1, int y1, int x2, int y2, Col c);
void draw_blend(SDL_BlendMode m);
int draw_text(int font, int x, int y, Col c, int align, const char *s);
int draw_textf(int font, int x, int y, Col c, int align, const char *fmt, ...) __attribute__((format(printf, 6, 7)));
int draw_text_width(int font, const char *s);
int draw_font_height(int font);
void draw_text_clipped(int font, int x, int y, Col c, int maxw, const char *s);
/* wraps on spaces; returns number of lines */
int draw_text_wrap(int font, int x, int y, Col c, int maxw, int line_h, const char *s);
void draw_glow_dot(int x, int y, int r, Col c);
/* geometry for knobs and visualisers (angles in radians, 0 = right, clockwise) */
void draw_circle(int cx, int cy, int r, Col c);
void draw_disc(int cx, int cy, int r, Col c);
void draw_arc(int cx, int cy, int r, float a0, float a1, int thick, Col c);
void draw_lines(const SDL_Point *p, int n, Col c);
void draw_points(const SDL_Point *p, int n, Col c);
/* text with extra letter spacing (labels) */
int draw_text_spaced(int font, int x, int y, Col c, int align, int spacing, const char *s);
void draw_lock_icon(int x, int y, Col c);
void draw_button_chip(int x, int y, const char *btn, const char *label, Col accent, int *out_w);
#endif
