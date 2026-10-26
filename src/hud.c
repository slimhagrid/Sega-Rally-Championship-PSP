/* Sega Rally PSP - race HUD, see hud.h */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspgu.h>
#include <string.h>
#include "hud.h"

typedef struct { float u, v; unsigned int col; float x, y, z; } Vtx;

#define CG_BASE   0x18000           /* VDP2 VRAM address of atlas cell 0 */
#define CG_CELLS  768
#define SCREEN_W  44                /* cells across the 352-pixel picture (+1 for the scroll offset) */
#define SCREEN_H  28

static unsigned char atlas_buf[256 * 256] __attribute__((aligned(64)));
static unsigned short clut_buf[8 * 256] __attribute__((aligned(64)));

int hud_load(Hud *h, const char *path)
{
    memset(h, 0, sizeof *h);
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return -1;
    unsigned char hdr[12];
    if (sceIoRead(fd, hdr, 12) != 12 || memcmp(hdr, "HUD1", 4) != 0) { sceIoClose(fd); return -3; }
    memset(atlas_buf, 0, sizeof atlas_buf);
    int ok = sceIoRead(fd, atlas_buf, 256 * 192) == 256 * 192 && sceIoRead(fd, clut_buf, sizeof clut_buf) == (int)sizeof clut_buf;
    sceIoClose(fd);
    if (!ok) return -3;
    h->atlas = atlas_buf;
    h->clut = clut_buf;
    sceKernelDcacheWritebackAll();
    return 0;
}

void hud_free(Hud *h)
{
    memset(h, 0, sizeof *h);
}

/* the name table entry shown at screen cell (c, r) of a layer scrolled by (scx, scy): the layer is 2 x 2 planes of one 64 x 64 page each (A B / C D), 1024 x 1024 pixels, wrapping */
static unsigned int cell_at(const unsigned short *top, const unsigned short *bottom, int scx, int scy, int c, int r)
{
    int row = ((scy >> 3) + r) & 127, col = ((scx >> 3) + c) & 63;
    const unsigned short *page = row < 64 ? top : bottom;
    return page ? page[(row & 63) * 64 + col] : 0;
}

/* a layer's cell lookup: the cell shown at screen cell (c, r) -> palette 0..7, atlas cell number, flips (bit 0 horizontal, bit 1 vertical); 0 = nothing there */
typedef int (*CellFn)(const void *ctx, int scx, int scy, int c, int r, int *pal, int *cell, int *flip);

typedef struct { const unsigned short *top, *bottom; int sup; } Layer1;               /* NBG2 / NBG3: one-word names */

static int cell1(const void *ctx, int scx, int scy, int c, int r, int *pal, int *cell, int *flip)
{
    const Layer1 *l = (const Layer1 *)ctx;
    unsigned int pn = cell_at(l->top, l->bottom, scx, scy, c, r);
    if (!pn) return 0;
    *pal = (int)((pn >> 12) & 7);
    *cell = (int)(((((pn & 0x3FF) | ((unsigned)l->sup << 10)) * 0x20u) - CG_BASE) >> 6);
    *flip = 0;
    return *cell >= 0 && *cell < CG_CELLS;
}

#define END_CELLS 1536                  /* cells of the NBG0 atlas */

static int cell0(const void *ctx, int scx, int scy, int c, int r, int *pal, int *cell, int *flip)
{
    const unsigned *cells = (const unsigned *)ctx;
    int row = ((scy >> 3) + r) & 127, col = ((scx >> 3) + c) & 127;
    unsigned int pn = cells[((((row >> 6) << 1) | (col >> 6)) ^ 1) * HUD_CELLS + (row & 63) * 64 + (col & 63)];      /* planes A B / C D are the maps 0x0B 0x0A / 0x0D 0x0C: the pages in address order are B A D C */
    if (!pn) return 0;
    *pal = (int)((pn >> 20) & 7);
    *cell = (int)((pn & 0x7FFF) >> 1);                                                    /* character numbers count 0x20 bytes, a 256-colour cell is 0x40 */
    *flip = (int)((pn >> 30) & 3);
    return *cell < END_CELLS;
}

/* one layer, palette by palette (a palette is a CLUT load); each cell is two triangles with shared edges so that the scaled picture has no gaps */
static void draw_layer(const Hud *h, CellFn fn, const void *ctx, int scx, int scy, const RenderRect *rect)
{
    const float sx = (float)rect->w / 352.0f, sy = (float)rect->h / 224.0f;
    const int ox = scx & 7, oy = scy & 7;
    for (int pal = 0; pal < 8; pal++) {
        int n = 0;
        for (int pass = 0; pass < 2; pass++) {
            Vtx *q = NULL;
            int k = 0;
            if (pass == 1) {
                if (!n) break;
                q = (Vtx *)sceGuGetMemory((unsigned)(6 * n) * sizeof(Vtx));
            }
            for (int r = 0; r <= SCREEN_H; r++)
                for (int c = 0; c <= SCREEN_W; c++) {
                    int cp, cell, flip;
                    if (!fn(ctx, scx, scy, c, r, &cp, &cell, &flip) || cp != pal) continue;
                    if (pass == 0) { n++; continue; }
                    float u0 = (float)((cell % 32) * 8), v0 = (float)((cell / 32) * 8);
                    float x0 = (float)(rect->x0 + (int)((c * 8 - ox) * sx + 0.5f)), x1 = (float)(rect->x0 + (int)(((c + 1) * 8 - ox) * sx + 0.5f));
                    float y0 = (float)(rect->y0 + (int)((r * 8 - oy) * sy + 0.5f)), y1 = (float)(rect->y0 + (int)(((r + 1) * 8 - oy) * sy + 0.5f));
                    const float xs[6] = { x0, x1, x1, x0, x1, x0 }, ys[6] = { y0, y0, y1, y0, y1, y1 };
                    float ua = u0, ub = u0 + 8, va = v0, vb = v0 + 8;
                    if (flip & 1) { ua = u0 + 8; ub = u0; }                               /* NBG0 name bits 30 / 31: horizontal / vertical flip */
                    if (flip & 2) { va = v0 + 8; vb = v0; }
                    const float us[6] = { ua, ub, ub, ua, ub, ua }, vs[6] = { va, va, vb, va, vb, vb };
                    for (int j = 0; j < 6; j++) { q[k].u = us[j]; q[k].v = vs[j]; q[k].col = 0xFFFFFFFFu; q[k].x = xs[j]; q[k].y = ys[j]; q[k].z = 0; k++; }
                }
            if (pass == 1) {
                sceGuClutLoad(256 / 8, h->clut + 256 * pal);
                sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, k, 0, q);
            }
        }
    }
}

/* the GE state of the text layers: no GE state that scene_draw relies on is switched off here: PPSSPP applies the clip-plane state of the end of the frame to the scene's last draws */
static void layer_state(const RenderRect *rect)
{
    sceGuOffset(2048 - 240, 2048 - 136);
    sceGuScissor(rect->x0, rect->y0, rect->x0 + rect->w, rect->y0 + rect->h);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuAlphaFunc(GU_GREATER, 0, 0xff);
    sceGuShadeModel(GU_FLAT);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuClutMode(GU_PSM_5551, 0, 0xff, 0);
    sceGuTexMode(GU_PSM_T8, 0, 0, 0);
}

void hud_draw(const Hud *h, const unsigned short *nt2, const unsigned short *nt3a, const unsigned short *nt3b, const int scr[4], const RenderRect *rect)
{
    if (!h->atlas) return;
    layer_state(rect);
    sceGuTexImage(0, 256, 256, 256, h->atlas);
    if (nt3a) { Layer1 l = { nt3a, nt3b, 4 }; draw_layer(h, cell1, &l, scr[2], scr[3], rect); }       /* PNCN3 supplementary character number 4: cells at 0x20000 .. */
    if (nt2) { Layer1 l = { nt2, nt2, 3 }; draw_layer(h, cell1, &l, scr[0], scr[1], rect); }          /* PNCN2: 3 -> 0x18000 .. */
}

static unsigned char end_buf[256 * 512] __attribute__((aligned(64)));

int hud_end_load(HudEnd *e, const char *path)
{
    memset(e, 0, sizeof *e);
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return -1;
    unsigned char hdr[12];
    if (sceIoRead(fd, hdr, 12) != 12 || memcmp(hdr, "HUDE", 4) != 0) { sceIoClose(fd); return -3; }
    memset(end_buf, 0, sizeof end_buf);
    int ok = sceIoRead(fd, end_buf, 256 * 384) == 256 * 384;
    sceIoClose(fd);
    if (!ok) return -3;
    e->atlas = end_buf;
    sceKernelDcacheWritebackAll();
    return 0;
}

void hud_end_free(HudEnd *e)
{
    memset(e, 0, sizeof *e);
}

void hud_draw_end(const Hud *h, const HudEnd *e, const unsigned *cells, int scx, int scy, const RenderRect *rect)
{
    if (!h->atlas || !e->atlas) return;
    layer_state(rect);
    sceGuTexImage(0, 256, 512, 256, e->atlas);
    draw_layer(h, cell0, cells, scx, scy, rect);
}
