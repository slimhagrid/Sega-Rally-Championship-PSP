/* Sega Rally PSP - the race HUD (M5).  The original draws it with the VDP2 text layers NBG2 / NBG3: name tables of 8x8 cells (256 colours, character data CGDATAUS.DAT,
 * palettes cl_*), rewritten by the display routines every pass and positioned by the layer scroll registers.  hud_draw() paints the visible cells of both layers over the
 * picture (docs/hud.md).  Data: tools/conv_hud.py -> hud.dat. */
#ifndef HUD_H
#define HUD_H
#include "render.h"

typedef struct {
    unsigned char *atlas;               /* 256 x 256 T8: 32 x 24 cells of the VDP2 VRAM region 0x18000 .. 0x24000 */
    unsigned short *clut;               /* 8 palettes x 256 entries RGBA5551, dot 0 transparent */
} Hud;

#define HUD_CELLS 4096                  /* one 64 x 64 page */

int  hud_load(Hud *h, const char *path);
void hud_free(Hud *h);

/* nt2: NBG2 (one page, all four planes), nt3a / nt3b: NBG3 planes A / B and C / D (nt3b may be NULL = empty), scr = scroll x2, y2, x3, y3 in pixels (0 .. 1023).
 * NBG3 first, NBG2 over it (equal priority: the lower layer number wins) */
void hud_set_clip(int on, int x0, int y0, int x1, int y1);       /* draw only the cells inside this rectangle of the picture (dots), until it is switched off */
void hud_draw(const Hud *h, const unsigned short *nt2, const unsigned short *nt3a, const unsigned short *nt3b, const int scr[4], const RenderRect *rect);

/* The end-of-race screens (goal, game over) are drawn by NBG0: two-word name cells (palette << 16 | character number, the cell at VDP2 VRAM 0x20 * character), 4 pages of 64 x 64 cells as the planes A B / C D
 * of a 1024 x 1024 pixel layer (src/hudgen.c HudGen.nt0), character data from the stage's result files (tools/conv_hudend.py -> hude_<cr|pr>_<stage>.dat), the palettes of hud.dat. */
typedef struct {
    unsigned char *atlas;               /* 256 x 512 T8 (48 rows used): 32 x 48 cells of the VDP2 VRAM region 0 .. 0x18000 */
} HudEnd;

int  hud_end_load(HudEnd *e, const char *path);
void hud_end_free(HudEnd *e);
void hud_draw_end(const Hud *h, const HudEnd *e, const unsigned *cells, int scx, int scy, const RenderRect *rect);     /* cells = 4 * HUD_CELLS words, scroll 0 .. 1023 */

#endif
