/* ECHOFIELD - main screen visualisers */
#ifndef EF_VIS_H
#define EF_VIS_H
#include "common.h"

struct App;
enum { VIS_ORBIT, VIS_FIELD, VIS_SPECTRUM, VIS_SCOPE, VIS_COUNT };
extern const char *const g_vis_names[VIS_COUNT];

/* draws visualiser `kind` into the rectangle; call once per frame */
void vis_draw(struct App *a, int kind, int x, int y, int w, int h);
/* overall output loudness 0..1 from the last frame (for small UI accents) */
float vis_level(void);
#endif
