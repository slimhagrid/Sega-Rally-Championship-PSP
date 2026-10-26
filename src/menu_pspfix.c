/* M8: the PSP's own button names in the menu pictures.
 *
 * The Saturn menus have texts about the Saturn pad drawn into their pictures ("CONFIRM WITH C BUTTON", "press A or C button", "Press C button for Replay").  The picture data
 * stays as the game made it (the oracle tests compare it); here, on the PSP only, after a picture was loaded into VDP2 VRAM the letter / words are redrawn: the confirm button is
 * X on the PSP (C on the Saturn pad, see menu_host.c).
 *
 * A fix names one or more boxes of cells of a scroll layer (the address of the pattern name of the top-left cell, the size in cells).  The box is only touched when its bitmap
 * hashes to the value of the original picture (so nothing else is ever changed); the new letter is drawn with the colour indexes of the picture's own font (outline / shade
 * ramp, measured from the original glyphs by tools/pspfix_*.py) and the box is written back into the character patterns it uses. */
#include <stdio.h>
#include <string.h>
#include "sat.h"
#include <stdlib.h>
#include "menu_pspfix.h"
#include "menu_padcfg.h"
#include "satmenu.h"

#define MAXW 320
#define MAXH 80

typedef struct { u32 addr; u32 hash; } PBox;
typedef struct {
    const char *name;
    int layer;                  /* NBG0..3 */
    int w, h;                   /* cells */
    int nbox;
    PBox box[3];
    void (*fn)(u8 *bm, int W, int H);
} PFix;

/* ---------------------------------------------------------------------------------------------------------------------------------- drawing */
static void clear_rect(u8 *bm, int W, int x, int y, int w, int h)
{
    int i, j;
    for (j = 0; j < h; j++) for (i = 0; i < w; i++) bm[(y + j) * W + x + i] = 0;
}

/* an X in the outlined style of the picture's font: (x, y, w, h) is the whole glyph including the one dot outline.  The strokes are `thick` dots wide; ramp[0] is the outline,
 * ramp[d] the colour of the dots d steps inside the stroke (the last entry for everything deeper). */
static void draw_x(u8 *bm, int W, int x, int y, int w, int h, int thick, const u8 *ramp, int nramp, int shadow)
{
    enum { MW = 20, MH = 16 };
    u8 m[MH][MW], d[MH][MW];
    int iw = w - 2, ih = h - 2, r, c, k;
    memset(m, 0, sizeof(m));
    memset(d, 0, sizeof(d));
    for (r = 0; r < (ih + 1) / 2; r++) {                /* the two strokes, inside a 1 dot border of the mask array; the lower half mirrors the upper one */
        int c1 = ih > 1 ? (r * (iw - thick) * 2 + (ih - 1)) / (2 * (ih - 1)) : 0;
        int c2 = iw - thick - c1;
        for (k = 0; k < thick; k++) { m[r + 1][c1 + k + 1] = 1; m[r + 1][c2 + k + 1] = 1; m[ih - r][c1 + k + 1] = 1; m[ih - r][c2 + k + 1] = 1; }
    }
    for (k = 1; k <= nramp; k++) {                      /* distance inside the stroke */
        for (r = 1; r <= ih; r++) for (c = 1; c <= iw; c++) {
            if (!m[r][c] || d[r][c]) continue;
            if (k == 1) { if (!m[r - 1][c] || !m[r + 1][c] || !m[r][c - 1] || !m[r][c + 1]) d[r][c] = 1; }
            else if ((d[r - 1][c] && d[r - 1][c] < k) || (d[r + 1][c] && d[r + 1][c] < k) || (d[r][c - 1] && d[r][c - 1] < k) || (d[r][c + 1] && d[r][c + 1] < k)) d[r][c] = (u8)k;
        }
    }
    for (r = 0; r < ih + 2; r++) for (c = 0; c < iw + 2; c++) {
        int px = x + c, py = y + r, near = 0, a, b;
        u8 v = 0;
        if (m[r][c]) {
            int dd = d[r][c] ? d[r][c] : nramp;
            v = ramp[dd < nramp ? dd : nramp - 1];
        } else {
            for (a = -1; a <= 1; a++) for (b = -1; b <= 1; b++) {
                int rr = r + a, cc = c + b;
                if (rr >= 0 && rr < ih + 2 && cc >= 0 && cc < iw + 2 && m[rr][cc]) near = 1;
            }
            if (near) v = (shadow && r > 0 && m[r - 1][c]) ? (u8)shadow : ramp[0];      /* the darker underside of the font's letters */
        }
        if (v) bm[py * W + px] = v;
    }
}

/* ---------------------------------------------------------------------------------------------------------------------------------- the fixes */

/* SELECT GAME / CAR SELECT / COURSE SELECT, banner on NBG3 (SCRL_SLT.DAT): "...CONFIRM WITH C BUTTON": the C of the second line (x 184..197, y 14..25 of the 320 x 32 box) */
static void fix_banner(u8 *bm, int W, int H)
{
    static const u8 ramp[3] = { 28, 25, 22 };
    (void)H;
    clear_rect(bm, W, 184, 13, 14, 14);
    draw_x(bm, W, 184, 14, 14, 12, 4, ramp, 3, 29);
}

/* options page, SOUND and KEY CONFIG rows (OPTION.DAT, NBG0): "press A or C button" -> "press X button" (box of 16 x 2 cells, 128 x 16 dots) */
static void fix_press_button(u8 *bm, int W, int H)
{
    static const u8 ramp[3] = { 1, 18, 18 };
    u8 keep[16][40];
    int x, y;
    (void)H;
    for (y = 0; y < 16; y++) for (x = 0; x < 38; x++) keep[y][x] = bm[y * W + 80 + x];      /* "button" (x 80..117) */
    clear_rect(bm, W, 40, 0, 88, 16);                   /* "A or C button" */
    draw_x(bm, W, 40, 3, 9, 10, 2, ramp, 3, 0);
    for (y = 0; y < 16; y++) for (x = 0; x < 38; x++) bm[y * W + 52 + x] = keep[y][x];
}

/* records, the best run's box (RECORD.DAT, NBG0): "Press C button for Replay": the C (x 30..38, y 2..11) */
static void fix_replay_c(u8 *bm, int W, int H)
{
    static const u8 ramp[3] = { 127, 50, 56 };
    (void)H;
    clear_rect(bm, W, 29, 1, 10, 12);
    draw_x(bm, W, 30, 2, 9, 10, 3, ramp, 3, 0);
}

/* car set-up, the save row (CAR_SET0.DAT, NBG1, 15 x 2 cells, 120 x 16 dots): "Press C button to SAVE": the C (x 33..42, y 2..12) */
static void fix_save_c(u8 *bm, int W, int H)
{
    static const u8 ramp[3] = { 1, 57, 16 };
    (void)H;
    clear_rect(bm, W, 33, 2, 10, 11);
    draw_x(bm, W, 34, 3, 8, 10, 2, ramp, 3, 0);
}

static u8 *psp_pad;                                     /* data/menu/psp_pad.bin: the PSP picture of the KEY CONFIG page (128 x 80 RGB, 255 0 255 = transparent), see kc_update() */

int pspfix_load(const char *dir)
{
    char path[300];
    FILE *f;
    u8 hdr[8];
    snprintf(path, sizeof path, "%s/menu/psp_pad.bin", dir);
    f = fopen(path, "rb");
    if (!f) return 0;
    if (fread(hdr, 1, 8, f) != 8 || memcmp(hdr, "PAD1", 4) != 0 || hdr[4] != 128 || hdr[6] != 80) { fclose(f); return 0; }
    free(psp_pad);
    psp_pad = (u8 *)malloc(128 * 80 * 3);
    if (!psp_pad || fread(psp_pad, 1, 128 * 80 * 3, f) != 128 * 80 * 3) { free(psp_pad); psp_pad = NULL; fclose(f); return 0; }
    fclose(f);
    return 1;
}

/* the pictures that name Saturn buttons, redrawn with the PSP's: the box, its checksum, the drawing function */
static const PFix fixes[] = {
    { "banner", 3, 40, 4, 1, { { 0x25E3CC04u, 0xA85D1437u } }, fix_banner },
    { "press A or C button", 0, 16, 2, 2, { { 0x25E31434u, 0x3DF4E385u }, { 0x25E31734u, 0x3DF4E385u } }, fix_press_button },
    { "Press C button for Replay", 0, 16, 2, 1, { { 0x25E33364u, 0xADEDF95Au } }, fix_replay_c },
    { "Press C button to SAVE", 1, 15, 2, 1, { { 0x25E385DCu, 0x5F38D26Cu } }, fix_save_c },
};
#define NFIX (int)(sizeof(fixes) / sizeof(fixes[0]))

/* ---------------------------------------------------------------------------------------------------------------------------------- engine */
static u32 fnv(const u8 *p, int n)
{
    u32 h = 0x811C9DC5u;
    while (n-- > 0) h = (h ^ *p++) * 0x01000193u;
    return h;
}

/* the character pattern address of the cell at the pattern name address `a` of layer n (256 colour, 8 x 8 characters); the pattern names' size and the character number rules
 * come from the pattern name control register of the layer (the same decoding as vdpr.c) */
static u32 cell_pattern(int n, u32 a)
{
    u32 pnc = R16(0x25F80030u + 2u * (u32)n), ch;
    if (pnc & 0x8000) {
        u32 pn = (u32)(sat->v2ram[(a - 0x25E00000u) & 0x7FFFF] << 8) | sat->v2ram[(a - 0x25E00000u + 1) & 0x7FFFF];
        if (pnc & 0x4000) ch = (pn & 0xFFF) | ((pnc & 0x1C) << 10);
        else ch = (pn & 0x3FF) | ((pnc & 0x1F) << 10);
    } else {
        u32 o = (a - 0x25E00000u) & 0x7FFFF;
        ch = (((u32)sat->v2ram[o + 2] << 8) | sat->v2ram[o + 3]) & 0x7FFF;
    }
    return ch * 0x20u;
}

/* the palette (first colour RAM entry, 256 colour characters) of the cell at the pattern name address `a`: 2-word names only */
static u32 cell_pal(int n, u32 a)
{
    u32 o = (a - 0x25E00000u) & 0x7FFFF;
    if (!(R16(0x25F80030u + 2u * (u32)n) & 0x8000)) return (((u32)sat->v2ram[o + 1] >> 4) & 7u) * 256u;
    return 0;
}

static int cell_size(int n) { return (R16(0x25F80030u + 2u * (u32)n) & 0x8000) ? 2 : 4; }

static int read_box(const PFix *f, const PBox *b, u8 *bm, u32 *pat)
{
    int r, c, y, W = f->w * 8, ns = cell_size(f->layer), stride = (f->layer < 4 && ns == 2) ? 128 : 256;
    for (r = 0; r < f->h; r++) for (c = 0; c < f->w; c++) {
        u32 p = cell_pattern(f->layer, b->addr + (u32)(r * stride + c * ns));
        pat[r * f->w + c] = p;
        if (p + 64 > 0x80000u) return 0;
        for (y = 0; y < 8; y++) memcpy(&bm[(r * 8 + y) * W + c * 8], sat->v2ram + p + y * 8, 8);
    }
    return 1;
}

static u32 plane_base(int n, int k);

/* the character number bits of the pattern name at `a` (VRAM address) of layer n, and a way to set them: the same decoding as cell_pattern() */
static u32 pn_off(u32 a) { return (a - 0x25E00000u) & 0x7FFFF; }

static void set_pn_char(int n, u32 a, u32 ch)
{
    u32 pnc = R16(0x25F80030u + 2u * (u32)n), o = pn_off(a);
    if (pnc & 0x8000) {
        u32 pn = (u32)(sat->v2ram[o] << 8) | sat->v2ram[o + 1], mask = (pnc & 0x4000) ? 0xFFFu : 0x3FFu;
        pn = (pn & ~mask) | (ch & mask);
        sat->v2ram[o] = (u8)(pn >> 8); sat->v2ram[o + 1] = (u8)pn;
    } else {
        sat->v2ram[o + 2] = (u8)(ch >> 8); sat->v2ram[o + 3] = (u8)ch;
    }
}

/* the tile windows: a layer reaches only part of VRAM with its pattern names (the supplement bits of the pattern name control register fix the upper bits of the character number) */
static void char_window(int n, u32 *lo, u32 *hi)
{
    u32 pnc = R16(0x25F80030u + 2u * (u32)n);
    if (pnc & 0x8000) {
        if (pnc & 0x4000) { *lo = (pnc & 0x1C) << 10; *hi = *lo + 0x1000; }
        else { *lo = (pnc & 0x1F) << 10; *hi = *lo + 0x400; }
    } else { *lo = 0; *hi = 0x4000; }
    if (*hi > 0x4000) *hi = 0x4000;
}

static u32 cells_of_plane(int n)
{
    u32 plsz = R16(0x25F8003Au), sz = (plsz >> (2 * n)) & 3, pw = sz == 0 ? 1 : 2, ph = sz == 3 ? 2 : 1;
    return 64u * 64u * pw * ph;
}

/* how many pattern names of layer n point to the character pattern `pat` (distinct planes only) */
static int tile_refs(int n, u32 pat)
{
    int k, j, cnt = 0, ns = cell_size(n);
    u32 base[4], cells = cells_of_plane(n), i;
    for (k = 0; k < 4; k++) {
        base[k] = plane_base(n, k) + 0x25E00000u;
        for (j = 0; j < k; j++) if (base[j] == base[k]) break;
        if (j < k) continue;
        for (i = 0; i < cells; i++) if (cell_pattern(n, base[k] + i * (u32)ns) == pat) cnt++;
    }
    return cnt;
}

/* a character pattern that is all zero and that no layer's pattern name uses, in the window of layer n: for a cell that has to change while others keep the old pattern */
static int tile_free_scan_done;
static u8 tile_used[0x2000];
static void mark_used_tiles(void)
{
    int n, k, j;
    memset(tile_used, 0, sizeof tile_used);
    for (n = 0; n < 4; n++) {
        u32 base[4], cells = cells_of_plane(n), i;
        int ns = cell_size(n);
        for (k = 0; k < 4; k++) {
            base[k] = plane_base(n, k) + 0x25E00000u;
            for (j = 0; j < k; j++) if (base[j] == base[k]) break;
            if (j < k) continue;
            for (i = 0; i < cells; i++) { u32 p = cell_pattern(n, base[k] + i * (u32)ns) >> 6; if (p < 0x2000) tile_used[p] = 1; }
        }
    }
    tile_free_scan_done = 1;
}

static u32 alloc_tile(int n)
{
    u32 lo, hi, ch, i;
    if (!tile_free_scan_done) mark_used_tiles();
    char_window(n, &lo, &hi);
    for (ch = hi; ch-- > lo; ) {
        u32 pat = ch * 0x20u;
        if (ch & 1) continue;                                   /* 256 colour patterns are 0x40 bytes: even character numbers */
        if (pat + 64 > 0x80000u || tile_used[pat >> 6]) continue;
        for (i = 0; i < 64 && !sat->v2ram[pat + i]; i++) { }
        if (i < 64) continue;
        tile_used[pat >> 6] = 1;
        return ch;
    }
    return 0xFFFFFFFFu;
}

typedef struct { u32 pn_addr, pat; const u8 *src; int stride; int done; } CellW;     /* a cell to write: the new 8 x 8 dots start at src (rows `stride` apart) */

static int same_dots(const CellW *a, const CellW *b)
{
    int y;
    for (y = 0; y < 8; y++) if (memcmp(a->src + y * a->stride, b->src + y * b->stride, 8) != 0) return 0;
    return 1;
}

static void write_cells(const PFix *f, CellW *cw, int ncw)
{
    int i, j, y;
    for (i = 0; i < ncw; i++) {
        int total, inside = 0, same = 1;
        if (cw[i].done) continue;
        for (j = 0; j < ncw; j++) {
            if (cw[j].pat != cw[i].pat) continue;
            inside++;
            if (!same_dots(&cw[i], &cw[j])) same = 0;
        }
        total = tile_refs(f->layer, cw[i].pat);
        if (total == inside && same) {                          /* every user of the character pattern is ours and gets the same dots: write in place */
            for (y = 0; y < 8; y++) memcpy(sat->v2ram + cw[i].pat + y * 8, cw[i].src + y * cw[i].stride, 8);
            for (j = 0; j < ncw; j++) if (cw[j].pat == cw[i].pat) cw[j].done = 1;
        } else {                                                /* shared with cells that keep the old dots (or our own cells differ): new character patterns */
            for (j = 0; j < ncw; j++) {
                u32 ch;
                int q, changed = 0;
                if (cw[j].pat != cw[i].pat || cw[j].done) continue;
                for (y = 0; y < 8; y++) if (memcmp(cw[j].src + y * cw[j].stride, sat->v2ram + cw[j].pat + y * 8, 8) != 0) changed = 1;
                cw[j].done = 1;
                if (!changed) continue;
                ch = alloc_tile(f->layer);
                if (ch == 0xFFFFFFFFu) continue;                /* no room: the old picture stays */
                for (y = 0; y < 8; y++) memcpy(sat->v2ram + ch * 0x20u + y * 8, cw[j].src + y * cw[j].stride, 8);
                set_pn_char(f->layer, 0x25E00000u + cw[j].pn_addr, ch);
                for (q = j + 1; q < ncw; q++)                   /* cells of ours that get the very same dots share the new pattern */
                    if (cw[q].pat == cw[i].pat && !cw[q].done && same_dots(&cw[q], &cw[j])) { set_pn_char(f->layer, 0x25E00000u + cw[q].pn_addr, ch); cw[q].done = 1; }
            }
        }
    }
}

/* ---------------------------------------------------------------------------------------------------------------------------------- the KEY CONFIG page
 * The option screen's page 3 shows, for each of the two players, the Saturn pad (NBG0: 16 x 10 cells of 256 colours at 0x25E30854 / 0x25E31254, the grey scale of colour RAM palette 5) with
 * labels and leader lines (NBG1: 23 x 9 cells at 0x25E38424 / 0x25E38924, rewritten on every pass of the screen: the text of the 8 label slots depends on the pad type A..D).  Each
 * slot belongs to a Saturn button: L, D-pad up, D-pad down, X Y Z, R, C, B, A.  On the PSP the same buttons are (menu_host.c / play.c: the race pad word): L, R = L, R, D-pad = D-pad,
 * X = Triangle, Y = Select, Z = none, C = Circle, B = Cross, A = Square.  So the page is redrawn as one picture on NBG0 (23 x 10 cells per player, own character patterns in the free part
 * of the VRAM): the PSP (data/menu/psp_pad.bin, tools/conv_pspad.py) with the text and underlines of the original labels and new leader lines to the PSP's buttons.  The colours are the
 * Saturn picture's own grey scale (nearest colour), plus the yellow of the lines in one unused entry of the same palette. */
enum { KC_CX = 23, KC_CY = 10, KC_W = KC_CX * 8, KC_H = KC_CY * 8, KC_LH = 72, KC_PADX = 4, KC_COL0 = 18 };
typedef struct { int uy, ux0, ux1, tx0, tx1; } KSlot;
static const KSlot kslot[8] = {                         /* the underline of every label (row, first / last dot) and the rectangle of its text (the 9 rows above the underline; wide enough for the longest text, "CHANGE VIEW") */
    { 10,   2,  53,   0,  64 },                         /* L */
    { 26,   2,  51,   0,  56 },                         /* D-pad up */
    { 55,   2,  51,   0,  51 },                         /* D-pad down */
    { 25,  63, 120,  54, 132 },                         /* X Y Z */
    { 10, 118, 170, 112, 183 },                         /* R */
    { 44, 141, 181, 142, 183 },                         /* C */
    { 55, 126, 178, 128, 183 },                         /* B */
    { 66, 129, 175, 129, 183 },                         /* A */
};
typedef struct { int n; int x[4], y[4]; } KLine;
static const KLine klines[9] = {                        /* the leader lines, from the end of the underline to the PSP's button (label box dots; the PSP picture starts at x = 24) */
    { 2, { 54, 44 }, { 10, 15 } },                      /* L: the left shoulder */
    { 2, { 52, 44 }, { 26, 31 } },                      /* D-pad up */
    { 3, { 52, 58, 44, 0 }, { 55, 49, 43, 0 } },        /* D-pad down */
    { 2, { 120, 134 }, { 25, 30 } },                    /* X: Triangle */
    { 2, { 92, 96 }, { 26, 59 } },                      /* Y: Select */
    { 2, { 118, 131 }, { 10, 15 } },                    /* R: the right shoulder */
    { 2, { 141, 141 }, { 44, 38 } },                    /* C: Circle */
    { 2, { 126, 133 }, { 55, 45 } },                    /* B: Cross */
    { 4, { 129, 122, 122, 127 }, { 66, 60, 41, 37 } },  /* A: Square */
};

static struct {
    int on;                                             /* the composite of this player is installed */
    u32 ch[KC_CX * KC_CY];                              /* its character numbers (0 = none yet) */
    u32 hash;                                           /* of the label art it was built from */
} kc[2];
static int kc_ncand;
static u8 kc_cidx[256], kc_crgb[256][3];                /* the grey scale of the Saturn picture: palette index, colour */
static int kc_yidx, kc_palbase;                         /* the index of the yellow, the palette's first entry in colour RAM */
static u32 kc_blank1;                                   /* a blank character of NBG1 (hides the original labels) */

static void kc_rgb(u32 e, u8 *rgb)                      /* colour RAM entry e (15 bit) -> 8 bit per channel */
{
    u32 w = ((u32)sat->cram[e * 2] << 8) | sat->cram[e * 2 + 1];
    rgb[0] = (u8)((w & 31) << 3); rgb[1] = (u8)(((w >> 5) & 31) << 3); rgb[2] = (u8)(((w >> 10) & 31) << 3);
}

static u8 kc_nearest(const u8 *rgb)
{
    int i, best = 0, bd = 1 << 30;
    for (i = 0; i < kc_ncand; i++) {
        int dr = rgb[0] - kc_crgb[i][0], dg = rgb[1] - kc_crgb[i][1], db = rgb[2] - kc_crgb[i][2], d = dr * dr + dg * dg + db * db;
        if (d < bd) { bd = d; best = i; }
    }
    return kc_cidx[best];
}

static void kc_line(u8 *img, int x0, int y0, int x1, int y1)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
    for (;;) {
        int e2;
        if (x0 >= 0 && x0 < KC_W && y0 >= 0 && y0 < KC_H) img[y0 * KC_W + x0] = (u8)kc_yidx;
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

static u32 kc_alloc(void)                               /* an all zero character in the free stretch of the VRAM (0x44000 .. 0x5FFFF: nothing of the screens lives there), upwards */
{
    u32 pat, i;
    if (!tile_free_scan_done) mark_used_tiles();
    for (pat = 0x44000u; pat < 0x60000u; pat += 64) {
        if (tile_used[pat >> 6]) continue;
        for (i = 0; i < 64 && !sat->v2ram[pat + i]; i++) { }
        if (i < 64) continue;
        tile_used[pat >> 6] = 1;
        return pat / 0x20u;
    }
    return 0;
}

static void kc_pn(u32 a, u32 v)                         /* a 2 word pattern name */
{
    u32 o = (a - 0x25E00000u) & 0x7FFFF;
    sat->v2ram[o] = (u8)(v >> 24); sat->v2ram[o + 1] = (u8)(v >> 16); sat->v2ram[o + 2] = (u8)(v >> 8); sat->v2ram[o + 3] = (u8)v;
}

static void kc_off(int p)                               /* back to the other pages: the cells go, the characters are given back */
{
    int i, r0 = 8 + 10 * p;
    for (i = 0; i < KC_CX * KC_CY; i++) {
        u32 a = 0x25E30000u + (u32)((r0 + i / KC_CX) * 256 + (KC_COL0 + i % KC_CX) * 4);
        if (R32(a) == 0 || (kc[p].ch[i] && (R32(a) & 0x7FFFu) == kc[p].ch[i])) kc_pn(a, 0);        /* a cell the next page has drawn into since (its text) stays */
        if (kc[p].ch[i]) memset(sat->v2ram + kc[p].ch[i] * 0x20u, 0, 64);
        kc[p].ch[i] = 0;
    }
    kc[p].on = 0;
}

/* ---- the label texts: the six words of the original's art (the picture of type A: SHIFT DOWN, NOT USED, CHANGE VIEW, SHIFT UP, BRAKE, ACCEL), cut out once and placed where the type's
 * functions (menu_padcfg.c) say; a word that is wider than its slot's room (CHANGE VIEW in a narrow slot) goes on two lines ---- */
typedef struct { int w, h, y0; u8 px[10][80]; } KBmp;       /* w x h dots, y0 = the first row inside the 9 rows above the underline */
typedef struct { KBmp one; int two; KBmp part[2]; } KWord;
static KWord kwords[PC_NACT];
static int kwords_ok;
static const int kword_slot[PC_NACT] = { 1, 5, 6, 4, 0, 3 };    /* the slot of type A's art that holds the word: NOT USED (D-pad up), BRAKE (C), ACCEL (B), SHIFT UP (R), SHIFT DOWN (L), CHANGE VIEW (X Y Z) */

static u32 pn_pattern(int n, u32 pn)                    /* the character pattern of a one word pattern name of layer n */
{
    u32 pnc = R16(0x25F80030u + 2u * (u32)n), ch;
    if (pnc & 0x4000) ch = (pn & 0xFFF) | ((pnc & 0x1C) << 10);
    else ch = (pn & 0x3FF) | ((pnc & 0x1F) << 10);
    return ch * 0x20u;
}

static int kc_bmp_cut(KBmp *b, const u8 (*src)[KC_W], int x0, int x1, int y0, int y1)      /* the dots of src inside the rectangle, trimmed to the inked part */
{
    int x, y, minx = 1 << 20, maxx = -1, miny = 1 << 20, maxy = -1;
    for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) if (src[y][x]) { if (x < minx) minx = x; if (x > maxx) maxx = x; if (y < miny) miny = y; if (y > maxy) maxy = y; }
    if (maxx < 0) { memset(b, 0, sizeof *b); return 0; }
    b->w = maxx - minx + 1; b->h = maxy - miny + 1; b->y0 = miny - y0;
    if (b->w > 80 || b->h > 10) return 0;
    memset(b->px, 0, sizeof b->px);
    for (y = 0; y < b->h; y++) for (x = 0; x < b->w; x++) b->px[y][x] = src[miny + y][minx + x];
    return 1;
}

/* build the six words from type A's art; the colours become indexes of the Saturn pad picture's grey scale (kc_nearest) */
static void kc_words_build(void)
{
    static u8 idx[KC_LH][KC_W];
    static u8 text[KC_LH][KC_W];
    u32 src = R32(0x06033E5Cu) + R32(0x060A8CC8u);
    int w = RS16(src), h = RS16(src + 2), r, c, y, x, a;
    kwords_ok = 0;
    if (w != KC_CX || h != 9) return;
    memset(idx, 0, sizeof idx);
    memset(text, 0, sizeof text);
    for (r = 0; r < h; r++) for (c = 0; c < w; c++) {
        u32 pn = R16(src + 4 + 2 * (u32)(r * w + c)) + 0x6000u, pat = pn_pattern(1, pn), bank = ((pn >> 12) & 7u) * 256u;
        if (pat + 64 > 0x80000u) continue;
        for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) {
            u8 v = sat->v2ram[pat + (u32)(y * 8 + x)], rgb[3];
            if (!v) continue;
            kc_rgb(bank + v, rgb);
            if (rgb[0] > 180 && rgb[1] > 180 && rgb[2] < 100) continue;         /* the yellow of the underlines and leader lines */
            idx[r * 8 + y][c * 8 + x] = kc_nearest(rgb);
        }
    }
    for (y = 0; y < KC_LH; y++) for (x = 0; x < KC_W; x++) {      /* a dot without a horizontal neighbour is part of a thin diagonal line, not of a letter */
        int l = x > 0 && idx[y][x - 1], rr = x + 1 < KC_W && idx[y][x + 1];
        text[y][x] = (idx[y][x] && (l || rr)) ? idx[y][x] : 0;
    }
    memcpy(idx, text, sizeof idx);
    for (a = 0; a < PC_NACT; a++) {
        const KSlot *s = &kslot[kword_slot[a]];
        KWord *k = &kwords[a];
        int gx0 = -1, gx1 = -1, best = 2, x0, x1, ok;
        memset(k, 0, sizeof *k);
        for (y = 0; y < KC_LH; y++) for (x = 0; x < KC_W; x++) text[y][x] = (y >= s->uy - 9 && y < s->uy && x >= s->tx0 && x <= s->tx1) ? idx[y][x] : 0;
        ok = kc_bmp_cut(&k->one, (const u8 (*)[KC_W])text, s->tx0, s->tx1, s->uy - 9, s->uy - 1);
        if (!ok) continue;
        {                                               /* the space between two words: the italic letters leave no empty column, but the lower rows are empty for a few columns (at most 3 dots of ink) */
            int run0 = -1, ink[80];
            gx0 = -1; best = 1;
            for (x = 0; x < k->one.w; x++) { int yy; ink[x] = 0; for (yy = 0; yy < k->one.h; yy++) if (k->one.px[yy][x]) ink[x]++; }
            for (x = 0; x <= k->one.w; x++) {
                int low = x < k->one.w && ink[x] <= 3;
                if (low && run0 < 0) run0 = x;
                if (!low && run0 >= 0) {
                    if (run0 * 4 > k->one.w && x * 4 < k->one.w * 3 && x - run0 > best) { best = x - run0; gx0 = (run0 + x) / 2; gx1 = gx0; }
                    run0 = -1;
                }
            }
        }
        if (gx0 > 0) {                                  /* two lines: the part before and the part after the space */
            static u8 tmp[KC_LH][KC_W];
            memset(tmp, 0, sizeof tmp);
            for (y = 0; y < k->one.h; y++) for (x = 0; x < gx0; x++) tmp[y][x] = k->one.px[y][x];
            x0 = 0; x1 = gx0 - 1;
            kc_bmp_cut(&k->part[0], (const u8 (*)[KC_W])tmp, x0, x1, 0, k->one.h - 1);
            memset(tmp, 0, sizeof tmp);
            for (y = 0; y < k->one.h; y++) for (x = gx1; x < k->one.w; x++) tmp[y][x - gx1] = k->one.px[y][x];
            kc_bmp_cut(&k->part[1], (const u8 (*)[KC_W])tmp, 0, k->one.w - gx1 - 1, 0, k->one.h - 1);
            k->part[0].y0 += k->one.y0; k->part[1].y0 += k->one.y0;
            k->two = k->part[0].w > 0 && k->part[1].w > 0;
        }
    }
    kwords_ok = 1;
}

static void kc_blit(u8 *img, const KBmp *b, int x, int ybase)      /* ybase = the first of the 9 text rows */
{
    int xx, yy;
    for (yy = 0; yy < b->h; yy++) for (xx = 0; xx < b->w; xx++) {
        int px = x + xx, py = ybase + b->y0 + yy;
        if (b->px[yy][xx] && px >= 0 && px < KC_W && py >= 0 && py < KC_H) img[py * KC_W + px] = b->px[yy][xx];
    }
}

/* ---- the labels and their leader lines ----
 * The PSP picture is at x = KC_PADX.  The labels of the two shoulder buttons sit above the picture (L at the left, R over the screen's top), the labels of the four face buttons in a column to its
 * right, stacked from the top; every leader is one vertical line from the label's underline to its button (the underline runs to the button's column).  The D-pad's up / down have no function. */
typedef struct { int x, y; } KPt;
static const KPt kbtn[6] = { { KC_PADX + 22, 17 }, { KC_PADX + 112, 17 }, { KC_PADX + 111, 30 }, { KC_PADX + 118, 37 }, { KC_PADX + 111, 44 }, { KC_PADX + 104, 37 } };      /* L, R, Triangle, Circle, Cross, Square (the dots on the PSP picture) */

static void kc_hline(u8 *img, int x0, int x1, int y)
{
    int x;
    if (y < 0 || y >= KC_H) return;
    for (x = x0; x <= x1; x++) if (x >= 0 && x < KC_W) img[y * KC_W + x] = (u8)kc_yidx;
}

static void kc_vline(u8 *img, int x, int y0, int y1)
{
    int y;
    if (x < 0 || x >= KC_W) return;
    if (y0 > y1) { y = y0; y0 = y1; y1 = y; }
    for (y = y0; y <= y1; y++) if (y >= 0 && y < KC_H) img[y * KC_W + x] = (u8)kc_yidx;
}

static int kc_lines_needed(int act, int avail)          /* 2 when the word does not fit on one line but can be split at its space */
{
    const KWord *k = &kwords[act];
    return (k->one.w > avail && k->two) ? 2 : 1;
}

static int kc_text_width(int act, int n)
{
    const KWord *k = &kwords[act];
    if (n == 1) return k->one.w;
    return k->part[0].w > k->part[1].w ? k->part[0].w : k->part[1].w;
}

static void kc_text_draw(u8 *img, int act, int n, int x, int right, int yu)      /* the text above the underline row yu; x = its left edge, or its right edge when `right` */
{
    const KWord *k = &kwords[act];
    int i;
    if (n == 1) { kc_blit(img, &k->one, right ? x - k->one.w + 1 : x, yu - 9); return; }
    for (i = 0; i < 2; i++) kc_blit(img, &k->part[i], right ? x - k->part[i].w + 1 : x, yu - 9 - (i == 0 ? 10 : 0));
}

static void kc_layout(u8 *img, int type)
{
    const u8 *a = padcfg_act[type];
    int w, n, yu, prev;
    /* L: left aligned at the top left, the leader drops from the underline to the shoulder */
    w = kc_text_width(a[PCB_L], 1);
    kc_text_draw(img, a[PCB_L], 1, 2, 0, 12);
    kc_hline(img, 1, (2 + w > kbtn[0].x ? 2 + w : kbtn[0].x) + 1, 12);
    kc_vline(img, kbtn[0].x, 12, kbtn[0].y - 3);
    /* R: its text ends just right of the shoulder, over the top of the picture */
    w = kc_text_width(a[PCB_R], 1);
    kc_text_draw(img, a[PCB_R], 1, kbtn[1].x + 8, 1, 12);
    kc_hline(img, (kbtn[1].x + 8 - w < kbtn[1].x ? kbtn[1].x + 8 - w : kbtn[1].x) - 1, kbtn[1].x + 9, 12);
    kc_vline(img, kbtn[1].x, 12, kbtn[1].y - 3);
    /* Triangle, Circle, Cross, Square: the column on the right, from the top; a label is never lower than the bottom of the picture's canvas */
    {
        static const int order[4] = { PCB_TRIANGLE, PCB_CIRCLE, PCB_CROSS, PCB_SQUARE };
        static const int bi[4] = { 2, 3, 4, 5 };            /* kbtn index */
        static const int minyu[4] = { 12, 43, 50, 50 };     /* the underline is below the button for Circle, Cross, Square */
        int k;
        prev = 23;
        for (k = 0; k < 4; k++) {
            int act = a[order[k]], bx = kbtn[bi[k]].x, by = kbtn[bi[k]].y;
            n = kc_lines_needed(act, 56);
            yu = prev + 2 + 9 * n;
            if (k == 0) yu = by;                            /* Triangle: the underline is the leader, straight across from the button (the label sits above it) */
            if (yu < minyu[k]) yu = minyu[k];
            if (yu > KC_H - 1) yu = KC_H - 1;
            kc_text_draw(img, act, n, KC_W - 1, 1, yu);
            if (k == 0) { kc_hline(img, bx + 4, KC_W - 1, yu); prev = yu; continue; }
            kc_hline(img, bx, KC_W - 1, yu);
            kc_vline(img, bx, yu, by + 4);                  /* the others: the label is below the button */
            prev = yu;
        }
    }
}

/* ---- the type names: "TYPE C" becomes "DIY", "TYPE D" is gone (the PSP has three types); the cells are NBG3's, redrawn by the screen every pass ---- */
static u32 kc_diy_ch[10], kc_none_ch;
static const u32 kc_type_c[2] = { 0x25E3CF14u, 0x25E3D414u }, kc_type_d[2] = { 0x25E3D014u, 0x25E3D514u };

static void kc_types_off(void)
{
    int i;
    for (i = 0; i < 10; i++) if (kc_diy_ch[i]) { memset(sat->v2ram + kc_diy_ch[i] * 0x20u, 0, 64); kc_diy_ch[i] = 0; }
    if (kc_none_ch) { memset(sat->v2ram + kc_none_ch * 0x20u, 0, 64); kc_none_ch = 0; }
}

static void kc_types(void)
{
    int p, i, x, y;
    if (!kc_none_ch) {
        u8 box[16][40];
        u32 pat[10];
        int pr, pc, ok = 1;
        tile_free_scan_done = 0;
        kc_none_ch = alloc_tile(3);
        if (kc_none_ch == 0xFFFFFFFFu) { kc_none_ch = 0; return; }
        for (i = 0; i < 10; i++) { kc_diy_ch[i] = alloc_tile(3); if (kc_diy_ch[i] == 0xFFFFFFFFu) { kc_diy_ch[i] = 0; ok = 0; } }
        if (!ok) { kc_types_off(); return; }
        for (pr = 0; pr < 2; pr++) for (pc = 0; pc < 5; pc++) pat[pr * 5 + pc] = cell_pattern(3, kc_type_d[0] + (u32)(pr * 128 + pc * 2));
        memset(box, 0, sizeof box);
        {                                               /* the dots of "TYPE D" (40 x 16): D = columns 31 .. 39, Y = 8 .. 14, the stem of the T = 2 .. 5 */
            static u8 d[16][40];
            for (pr = 0; pr < 2; pr++) for (pc = 0; pc < 5; pc++) for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) d[pr * 8 + y][pc * 8 + x] = sat->v2ram[pat[pr * 5 + pc] + (u32)(y * 8 + x)];
            for (y = 3; y <= 12; y++) for (x = 0; x < 9; x++) box[y][x] = d[y][31 + x];                    /* D */
            for (y = 3; y <= 12; y++) for (x = 0; x < 7; x++) box[y][15 + x] = d[y][8 + x];                /* Y (the I between: columns 10 .. 13) */
            {
                u8 fl = d[7][3], fr = d[7][4];                  /* the I: the stem of the T with caps */
                for (y = 4; y <= 11; y++) { box[y][10] = d[6][2]; box[y][11] = y >= 6 ? d[y][3] : fl; box[y][12] = y >= 6 ? d[y][4] : fr; box[y][13] = d[6][5]; }
                for (x = 10; x <= 13; x++) { box[3][x] = d[3][0]; box[12][x] = d[12][2]; }
            }
        }
        {
            int inked = 0;
            for (y = 0; y < 16; y++) for (x = 0; x < 40; x++) if (box[y][x]) inked++;
            if (inked < 80) { kc_types_off(); return; }       /* the cells are not drawn yet: try again next pass */
        }
        for (pr = 0; pr < 2; pr++) for (pc = 0; pc < 5; pc++) for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) sat->v2ram[kc_diy_ch[pr * 5 + pc] * 0x20u + (u32)(y * 8 + x)] = box[pr * 8 + y][pc * 8 + x];
    }
    for (p = 0; p < 2; p++) {                           /* only the cells the screen has drawn: when the page slides out the original clears its labels, and they must stay clear */
        for (i = 0; i < 10; i++) { u32 a = kc_type_c[p] + (u32)((i / 5) * 128 + (i % 5) * 2); if (R16(a)) set_pn_char(3, a, kc_diy_ch[i]); }
        for (i = 0; i < 10; i++) { u32 a = kc_type_d[p] + (u32)((i / 5) * 128 + (i % 5) * 2); if (R16(a)) set_pn_char(3, a, kc_none_ch); }
    }
}

static int kc_update(void)
{
    int state = R8(0x060A8CC0), page3 = state >= 6 && state <= 10, p, n = 0;
    static u8 bm[128 * 80], img[KC_W * KC_H];
    if (!psp_pad) return 0;
    if (!page3) { if (kc_none_ch) kc_types_off(); kwords_ok = 0; }
    for (p = 0; p < 2; p++) {
        u32 pad = 0x25E30854u + (u32)p * 0xA00u, lab = 0x25E38424u + (u32)p * 0x500u, hash = 0x811C9DC5u, pn0;
        int r0 = 8 + 10 * p, r, c, i, x, y, orig = 0, type = (R8(0x06042A3F) >> (4 * p)) & 15;      /* the cursors of the page: the options save area (option_init: D + 7) */
        if (!page3) { if (kc[p].on) kc_off(p); continue; }
        if (state == 8 && kc[p].on) {                   /* the page slides out: our picture goes when it has left the screen (NBG0's scroll position) */
            int sxs = (int)(R16(0x25F80070u) & 0x1FF), c0 = KC_COL0 * 8, c1 = c0 + KC_CX * 8, vis = 0;
            if (c0 < sxs + 352 && c1 > sxs) vis = 1;
            if (sxs + 352 > 512 && c0 < sxs + 352 - 512) vis = 1;
            if (!vis) { kc_off(p); continue; }
        }
        if (type >= PC_TYPES) type = 0;
        {                                               /* is the Saturn pad (still) there? */
            PFix f = { "pad", 0, 16, 10, 1, { { 0, 0 } }, NULL };
            u32 pat[160];
            f.box[0].addr = pad;
            if (read_box(&f, &f.box[0], bm, pat) && fnv(bm, 128 * 80) == 0x192A8472u) orig = 1;
        }
        if (orig) {                                     /* the screen has just drawn the picture (it does so every pass): the grey scale is read from it */
            kc_palbase = (int)cell_pal(0, pad);
            if (!kc_ncand || !kc_yidx) {
                u8 seen[256];
                memset(seen, 0, sizeof seen);
                kc_ncand = 0;
                for (i = 0; i < 128 * 80; i++) if (bm[i] && !seen[bm[i]]) { seen[bm[i]] = 1; kc_cidx[kc_ncand] = bm[i]; kc_rgb((u32)kc_palbase + bm[i], kc_crgb[kc_ncand]); kc_ncand++; }
                for (kc_yidx = 255; kc_yidx > 0 && seen[kc_yidx]; kc_yidx--) { }
            }
        } else if (!kc[p].on) continue;
        if (!kc_ncand || !kc_yidx) continue;
        if (!kwords_ok) kc_words_build();
        if (!kwords_ok) continue;
        tile_free_scan_done = 0;
        for (i = 0; i < PC_NBTN; i++) hash = (hash ^ padcfg_act[type][i]) * 0x01000193u;
        hash = (hash ^ (u32)type) * 0x01000193u;
        if (!kc_blank1) { kc_blank1 = alloc_tile(1); if (kc_blank1 == 0xFFFFFFFFu) kc_blank1 = 0; }
        if (kc_blank1) for (r = 0; r < 9; r++) for (c = 0; c < KC_CX; c++) set_pn_char(1, lab + (u32)(r * 128 + c * 2), kc_blank1);       /* the original labels go */
        {                                               /* the palette bits of the Saturn pad's cells */
            u32 o = (pad - 0x25E00000u) & 0x7FFFF;
            pn0 = ((u32)sat->v2ram[o + 1] << 16);
            if (!orig) pn0 = ((u32)kc_palbase / 256u) << 20;
        }
        if (kc[p].on && hash == kc[p].hash) {           /* the composite of this type is in the VRAM: only the cells the screen redrew are ours again */
            if (orig) for (i = 0; i < KC_CX * KC_CY; i++) kc_pn(0x25E30000u + (u32)((r0 + i / KC_CX) * 256 + (KC_COL0 + i % KC_CX) * 4), kc[p].ch[i] ? ((pn0 & 0x007F0000u) | kc[p].ch[i]) : 0);
            n++;
            continue;
        }
        /* the picture: PSP, label text, underlines, leader lines */
        memset(img, 0, sizeof img);
        for (y = 0; y < 80; y++) for (x = 0; x < 128; x++) {
            const u8 *q = psp_pad + 3 * (y * 128 + x);
            if (q[0] == 255 && q[1] == 0 && q[2] == 255) continue;
            img[y * KC_W + KC_PADX + x] = kc_nearest(q);
        }
        {                                               /* the yellow of the original's lines goes into the unused entry (colour RAM and its shadow, which the fades copy back) */
            u8 yl[3] = { 248, 248, 0 };
            u32 w, e;
            w = (u32)(yl[0] >> 3) | (u32)(yl[1] >> 3) << 5 | (u32)(yl[2] >> 3) << 10;
            e = (u32)kc_palbase + (u32)kc_yidx;
            sat->cram[e * 2] = (u8)(w >> 8); sat->cram[e * 2 + 1] = (u8)w;
            sat->hram[0xB8000 + e * 2] = (u8)(w >> 8); sat->hram[0xB8000 + e * 2 + 1] = (u8)w;
        }
        kc_layout(img, type);
        for (i = 0; i < KC_CX * KC_CY; i++) {
            u32 a = 0x25E30000u + (u32)((r0 + i / KC_CX) * 256 + (KC_COL0 + i % KC_CX) * 4);
            int cx = i % KC_CX, cy = i / KC_CX, any = 0;
            u8 t[64];
            for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) { u8 v = img[(cy * 8 + y) * KC_W + cx * 8 + x]; t[y * 8 + x] = v; if (v) any = 1; }
            if (!any && !kc[p].ch[i]) { kc_pn(a, 0); continue; }
            if (!kc[p].ch[i]) kc[p].ch[i] = kc_alloc();
            if (!kc[p].ch[i]) continue;
            memcpy(sat->v2ram + kc[p].ch[i] * 0x20u, t, 64);
            kc_pn(a, (pn0 & 0x007F0000u) | kc[p].ch[i]);
        }
        kc[p].on = 1;
        kc[p].hash = hash;
        n++;
    }
    if (page3) kc_types();
    return n;
}

int pspfix_apply(void)
{
    int i, n = 0;
    for (i = 0; i < NFIX; i++) {
        const PFix *f = &fixes[i];
        static u8 bm[3][MAXW * MAXH];
        static u32 pat[3][MAXW / 8 * MAXH / 8];
        static CellW cw[3 * MAXW / 8 * MAXH / 8];
        int k, r, c, ncw = 0, W = f->w * 8, H = f->h * 8, ok = 1, ns = cell_size(f->layer), stride = (f->layer < 4 && ns == 2) ? 128 : 256;
        for (k = 0; k < f->nbox && ok; k++) {
            if (!read_box(f, &f->box[k], bm[k], pat[k])) { ok = 0; break; }
            if (fnv(bm[k], W * H) != f->box[k].hash) ok = 0;
        }
        if (!ok) continue;
        if (getenv("PSPFIX_TRACE")) fprintf(stderr, "pspfix: applying %s\n", f->name);
        tile_free_scan_done = 0;
        for (k = 0; k < f->nbox; k++) {
            f->fn(bm[k], W, H);
            for (r = 0; r < f->h; r++) for (c = 0; c < f->w; c++) {
                CellW *w = &cw[ncw++];
                w->pn_addr = f->box[k].addr + (u32)(r * stride + c * ns) - 0x25E00000u;
                w->pat = pat[k][r * f->w + c];
                w->src = &bm[k][r * 8 * W + c * 8];
                w->stride = W;
                w->done = 0;
            }
        }
        write_cells(f, cw, ncw);
        n++;
    }
    n += kc_update();
    return n;
}

/* development: the hash of every fix box in the current memory (to fill the table), and the positions in the layer's planes where a box of the same bitmap is found */
void pspfix_learn(void)
{
    int i, k;
    for (i = 0; i < NFIX; i++) {
        const PFix *f = &fixes[i];
        static u8 bm[MAXW * MAXH];
        static u32 pat[MAXW / 8 * MAXH / 8];
        for (k = 0; k < f->nbox; k++)
            if (read_box(f, &f->box[k], bm, pat)) printf("pspfix %-26s box %d at %08x: hash %08x%s\n", f->name, k, (unsigned)f->box[k].addr, (unsigned)fnv(bm, f->w * f->h * 64), fnv(bm, f->w * f->h * 64) == f->box[k].hash ? " (matches)" : "");
    }
}

/* ---------------------------------------------------------------------------------------------------------------------------------- development: where else does a picture appear?
 * pspfix_scan_tick() is called for every few ticks of an oracle trace (tools/render_mtr.c -fixscan): when box 0 of a fix is in its table place the bitmap is kept as the template,
 * and in every tick the pattern name planes of the layer are searched for the template at other places; the new places are printed (to be added to the table). */
static u8 tpl[8][MAXW * MAXH];
static int have_tpl[8];
static u32 found[8][64];
static int nfound[8];

static u32 plane_base(int n, int k)
{
    u32 pnc = R16(0x25F80030u + 2u * (u32)n), plsz = R16(0x25F8003Au), mpofn = R16(0x25F8003Cu);
    u32 sz = (plsz >> (2 * n)) & 3, pw = sz == 0 ? 1 : 2, ph = sz == 3 ? 2 : 1, off = (mpofn >> (4 * n)) & 7;
    u32 w = R16(0x25F80040u + 4u * (u32)n + 2u * (u32)(k >> 1)), m = ((k & 1) ? (w >> 8) : w) & 0x3F;
    if (sz == 1) { pw = 2; ph = 1; }
    return (m | (off << 6)) * 0x2000u * ((pnc & 0x8000) ? 1 : 2) * pw * ph;
}

void pspfix_scan_tick(unsigned tick)
{
    int i, k;
    for (i = 0; i < NFIX; i++) {
        const PFix *f = &fixes[i];
        static u8 bm[MAXW * MAXH], cand[MAXW * MAXH];
        static u32 pat[MAXW / 8 * MAXH / 8];
        int W = f->w * 8, H = f->h * 8, ns = cell_size(f->layer), stride = ns == 2 ? 128 : 256;
        if (!have_tpl[i] && read_box(f, &f->box[0], bm, pat) && fnv(bm, W * H) == f->box[0].hash) { memcpy(tpl[i], bm, (size_t)(W * H)); have_tpl[i] = 1; printf("pspfix scan: template of %s from tick %u\n", f->name, tick); }
        if (!have_tpl[i]) continue;
        for (k = 0; k < 4; k++) {
            u32 pb = plane_base(f->layer, k) + 0x25E00000u;
            int r, c;
            for (r = 0; r + f->h <= 64; r++) for (c = 0; c + f->w <= 64; c++) {
                PBox b;
                int j, same = 1, y;
                u32 a = pb + (u32)(r * stride + c * ns);
                b.addr = a; b.hash = 0;
                if (!read_box(f, &b, cand, pat)) continue;
                for (y = 0; y < H && same; y++) for (j = 0; j < W; j++) if (cand[y * W + j] != tpl[i][y * W + j]) { same = 0; break; }
                if (same) {
                    int q, known = 0;
                    for (q = 0; q < f->nbox; q++) if (f->box[q].addr == a) known = 1;
                    for (q = 0; q < nfound[i]; q++) if (found[i][q] == a) known = 1;
                    if (!known && nfound[i] < 64) { found[i][nfound[i]++] = a; printf("pspfix scan: %-26s tick %u: also at %08x (layer %d, plane %d)\n", f->name, tick, (unsigned)a, f->layer, k); }
                }
            }
        }
    }
}
