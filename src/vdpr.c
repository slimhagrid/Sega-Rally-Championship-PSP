/* M8: software VDP2 + VDP1 on the memory model.  VDP2: NBG0-3 (tiles or bitmaps, 16 / 256 / 2048 / 32K colours, 1- or 2-word pattern names, 8x8 or 16x16 characters, planes of 1x1 .. 2x2 pages,
 * scroll with integer + fraction, zoom, line scroll), RBG0 (rotation parameter table A, per-line start + per-dot increment), back screen, colour offset, priorities with the
 * hardware tie order sprite > RBG0 > NBG0 > NBG1 > NBG2 > NBG3.  Windows 0 / 1 (rectangles, inside / outside, OR / AND) of the scroll screens are applied while the picture is composed; not done: line windows, the sprite window, colour calculation / shadows, rotation parameter B / coefficient tables, 640 / 704 dot modes. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "vdpr.h"

#define V2R(o) ((u32)(sat->v2reg[(o)] << 8 | sat->v2reg[(o) + 1]))      /* a VDP2 register (the same as R16(0x25F80000 + o), without the memory map's detour) */

/* where the time of a picture goes: a host that sets vdpr_clock (any monotonic counter) gets the sum per part in vdpr_t (the PSP host logs it to data/menu_perf.txt, tools/test_menu.c -DPROF prints it) */
unsigned long long (*vdpr_clock)(void);
unsigned long long vdpr_t[VDPR_NPARTS];
#define PT_BEGIN() unsigned long long _pt = vdpr_clock ? vdpr_clock() : 0
#define PT_END(i) do { if (vdpr_clock) { unsigned long long _pn = vdpr_clock(); vdpr_t[i] += _pn - _pt; _pt = _pn; } } while (0)
#ifdef PROF
unsigned vdpr_prof_why[10], vdpr_prof_reg[V2REG_SIZE];  /* tools/menu_prof.sh PROF_WHY: what kept a picture from being reused */
#endif

/* ---------------------------------------------------------------------------------------------------------------------------------- colour helpers */
static u32 abgr(u32 r, u32 g, u32 b) { return 0xFF000000u | (b << 16) | (g << 8) | r; }

static u32 rgb555(u32 v)
{
    u32 r = v & 31, g = (v >> 5) & 31, b = (v >> 10) & 31;
    return abgr((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2));
}

static u32 cramtab[2048];
static int cram_mask;

static u32 cram_color_slow(u32 idx);
static u32 cram_color(u32 idx) { return cramtab[idx & (u32)cram_mask]; }

static u32 cram_gen;                                    /* counts the rebuilds of cramtab */

static void build_cramtab(void)
{
    u32 mode = (V2R(0x0E) >> 12) & 3, i;
    cram_gen++;
    cram_mask = mode == 1 ? 0x7FF : 0x3FF;
    for (i = 0; i <= (u32)cram_mask; i++) cramtab[i] = cram_color_slow(i);
}

static u32 cram_color_slow(u32 idx)
{
    u32 mode = (V2R(0x0E) >> 12) & 3;
    if (mode == 2) {                                    /* RGB888: 4 bytes per entry */
        const u8 *p = sat->cram + ((idx & 0x3FF) << 2);
        return abgr(p[3], p[2], p[1]);
    }
    {
        u32 i = idx & (mode == 0 ? 0x3FF : 0x7FF);
        return rgb555((u32)(sat->cram[2 * i] << 8) | sat->cram[2 * i + 1]);
    }
}

static int sx(u32 v, int bits) { v &= (1u << bits) - 1; return (v & (1u << (bits - 1))) ? (int)v - (int)(1u << bits) : (int)v; }

/* ---------------------------------------------------------------------------------------------------------------------------------- layer description */
typedef struct {
    int on, prio;
    int bitmap, bmw, bmh;
    u32 bmaddr;
    int chsz;                       /* 1 = 8x8, 2 = 16x16 */
    int colors;                     /* 16, 256, 2048, 32768 */
    int onew, cnsm;
    u32 sup;                        /* supplement bits of the pattern name control register */
    int pw, ph;                     /* pages per plane (1 or 2) */
    int ncols;                      /* planes per row: 2 for the NBGs, 4 for RBG0 */
    u32 plane[16];                  /* start address of every plane (bytes) */
    int tpon;                       /* dot 0 is opaque */
    u32 palbank;                    /* colour RAM offset (in 256-entry units) */
    u32 bmpal;
    int coff_en, coff_sel;
    /* scroll */
    int scx, scy, zx, zy;           /* integer position + fraction in 1/256, zoom 1.0 = 256 (NBG0/1 only) */
    int lsx, lsy, lzm, vcs, lsh;    /* NBG0 / NBG1 (SCRCTL): line scroll X / Y and line zoom in the line table, vertical cell scroll, interval shift (1, 2, 4, 8 lines) */
    int ls;                         /* a line scroll table is in use: lnx / lny / lnz hold its entry of every line (scroll in 1/256 dots, zoom 1.0 = 256) */
    int lnx[VDPR_MAXH], lny[VDPR_MAXH], lnz[VDPR_MAXH];
    int plsh, plhsh, nclsh, cellsh, ncellsh, namesh;   /* log2 of: plane width / height in dots, planes per row, character size in dots, characters per page row, name size in bytes */
    u32 areaw, areah, planew, planeh;
} Layer;

static Layer L[5];                  /* 0..3 NBG, 4 RBG0 */
int vdpr_unsupported;               /* development: bit 0 vertical cell scroll, 1 line scroll Y, 2 line zoom were switched on (not all are drawn exactly) */

static void setup_layers(void)
{
    u32 bgon = V2R(0x20), chctla = V2R(0x28), chctlb = V2R(0x2A), plsz = V2R(0x3A), mpofn = V2R(0x3C), mpofr = V2R(0x3E);
    u32 prin_a = V2R(0xF8), prin_b = V2R(0xFA), prin_r = V2R(0xFC), craof = V2R(0xE4), craofb = V2R(0xE6);
    u32 clofen = V2R(0x110), clofsl = V2R(0x112);
    int n, k;
    memset(L, 0, sizeof(L));
    for (n = 0; n < 5; n++) {
        Layer *l = &L[n];
        u32 ctl, pnc;
        l->on = (bgon >> n) & 1;
        l->tpon = (bgon >> (8 + n)) & 1;
        l->coff_en = (clofen >> n) & 1;
        l->coff_sel = (clofsl >> n) & 1;
        if (n == 0) { l->prio = prin_a & 7; ctl = chctla; }
        else if (n == 1) { l->prio = (prin_a >> 8) & 7; ctl = chctla >> 8; }
        else if (n == 2) { l->prio = prin_b & 7; ctl = chctlb; }
        else if (n == 3) { l->prio = (prin_b >> 8) & 7; ctl = chctlb >> 4; }
        else { l->prio = prin_r & 7; ctl = chctlb >> 8; }
        if (n < 2) {
            int cn = (ctl >> 4) & 7;
            l->chsz = (ctl & 1) ? 2 : 1;
            l->bitmap = (ctl >> 1) & 1;
            l->colors = cn == 0 ? 16 : cn == 1 ? 256 : cn == 2 ? 2048 : cn == 3 ? 32768 : (1 << 24);
            if (l->bitmap) {
                static const int bw[4] = { 512, 512, 1024, 1024 }, bh[4] = { 256, 512, 256, 512 };
                l->bmw = bw[(ctl >> 2) & 3]; l->bmh = bh[(ctl >> 2) & 3];
                l->bmaddr = ((mpofn >> (4 * n)) & 7) * 0x20000u;
                l->bmpal = V2R(0x2C) >> (8 * n);
            }
        } else if (n < 4) {
            l->chsz = (ctl & 1) ? 2 : 1;
            l->colors = (ctl & 2) ? 256 : 16;
        } else {
            int cn = (ctl >> 4) & 7;
            l->chsz = (ctl & 1) ? 2 : 1;
            l->bitmap = (ctl >> 1) & 1;
            l->colors = cn == 0 ? 16 : cn == 1 ? 256 : cn == 2 ? 2048 : cn == 3 ? 32768 : (1 << 24);
            if (l->bitmap) {
                l->bmw = 512; l->bmh = (ctl & 4) ? 512 : 256;
                l->bmaddr = (mpofr & 7) * 0x20000u;
                l->bmpal = V2R(0x2E);
            }
        }
        if (n < 4) pnc = V2R(0x30 + 2 * n); else pnc = V2R(0x38);
        l->onew = (pnc >> 15) & 1;
        l->cnsm = (pnc >> 14) & 1;
        l->sup = pnc & 0x3FF;
        {
            u32 sz = n < 4 ? (plsz >> (2 * n)) & 3 : (plsz >> 8) & 3;
            l->pw = (sz == 0) ? 1 : 2;
            l->ph = (sz == 3) ? 2 : 1;
            if (sz == 1) { l->pw = 2; l->ph = 1; }
        }
        l->ncols = n < 4 ? 2 : 4;
        l->plsh = 9 + (l->pw == 2); l->plhsh = 9 + (l->ph == 2); l->nclsh = n < 4 ? 1 : 2;
        l->cellsh = 2 + l->chsz; l->ncellsh = 7 - l->chsz; l->namesh = l->onew ? 1 : 2;
        l->planew = 1u << l->plsh; l->planeh = 1u << l->plhsh; l->areaw = l->planew << l->nclsh; l->areah = l->planeh << l->nclsh;
        if (n < 4) {
            u32 off = (mpofn >> (4 * n)) & 7;
            for (k = 0; k < 4; k++) {
                u32 w = V2R(0x40 + 4 * n + 2 * (k >> 1));
                u32 m = ((k & 1) ? (w >> 8) : w) & 0x3F;
                l->plane[k] = (m | (off << 6)) * 0x2000u * (l->onew ? 1 : 2) * l->pw * l->ph;
            }
        } else {
            u32 off = mpofr & 7;
            for (k = 0; k < 16; k++) {
                u32 w = V2R(0x50 + 2 * (k >> 1));
                u32 m = ((k & 1) ? (w >> 8) : w) & 0x3F;
                l->plane[k] = (m | (off << 6)) * 0x2000u * (l->onew ? 1 : 2) * l->pw * l->ph;
            }
        }
        l->palbank = n < 4 ? (craof >> (4 * n)) & 7 : craofb & 7;
        /* scroll */
        if (n == 0) { l->scx = ((V2R(0x70) & 0x7FF) << 8) | ((V2R(0x72) >> 8) & 0xFF); l->scy = ((V2R(0x74) & 0x7FF) << 8) | ((V2R(0x76) >> 8) & 0xFF); }
        else if (n == 1) { l->scx = ((V2R(0x80) & 0x7FF) << 8) | ((V2R(0x82) >> 8) & 0xFF); l->scy = ((V2R(0x84) & 0x7FF) << 8) | ((V2R(0x86) >> 8) & 0xFF); }
        else if (n == 2) { l->scx = (V2R(0x90) & 0x7FF) << 8; l->scy = (V2R(0x92) & 0x7FF) << 8; }
        else if (n == 3) { l->scx = (V2R(0x94) & 0x7FF) << 8; l->scy = (V2R(0x96) & 0x7FF) << 8; }
        l->zx = l->zy = 256;
        if (n < 2) {
            u32 zmx = ((V2R(0x78 + 16 * (u32)n) & 7) << 8) | ((V2R(0x7A + 16 * (u32)n) >> 8) & 0xFF);       /* ZMXIN0 / DN0 = 0x78 / 0x7A, ZMXIN1 / DN1 = 0x88 / 0x8A (the Y ones 0x7C / 0x7E and 0x8C / 0x8E) */
            u32 zmy = ((V2R(0x7C + 16 * (u32)n) & 7) << 8) | ((V2R(0x7E + 16 * (u32)n) >> 8) & 0xFF);
            if (zmx) l->zx = (int)zmx;
            if (zmy) l->zy = (int)zmy;
        }
        if (n < 2) {                                    /* the line scroll table: per interval of lines [scroll X][scroll Y][zoom X], 4 bytes each, those that are enabled */
            u32 sc = V2R(0x9A) >> (8 * n);
            l->vcs = (int)(sc & 1); l->lsx = (int)((sc >> 1) & 1); l->lsy = (int)((sc >> 2) & 1); l->lzm = (int)((sc >> 3) & 1); l->lsh = (int)((sc >> 4) & 3);
            l->ls = l->on && (l->lsx | l->lsy | l->lzm);
            if (l->on && l->vcs) vdpr_unsupported |= 1;
            if (l->ls) {
                u32 tab = ((((V2R(0xA0 + 4 * (u32)n) & 7) << 16) | V2R(0xA2 + 4 * (u32)n)) * 2) & 0x7FFFF;
                u32 esz = 4u * (u32)(l->lsx + l->lsy + l->lzm);
                int y;
                if (l->lsy) vdpr_unsupported |= 2;
                if (l->lzm) vdpr_unsupported |= 4;
                for (y = 0; y < VDPR_MAXH; y++) {
                    u32 a = tab + (u32)(y >> l->lsh) * esz, v;
                    int k = 0;
                    if (l->lsx) { v = R32(0x25E00000u + ((a + 4u * (u32)k++) & 0x7FFFF)); l->lnx[y] = (int)((v >> 8) & 0x7FFFF); }
                    if (l->lsy) { v = R32(0x25E00000u + ((a + 4u * (u32)k++) & 0x7FFFF)); l->lny[y] = (int)((v >> 8) & 0x7FFFF); }
                    if (l->lzm) { v = R32(0x25E00000u + ((a + 4u * (u32)k++) & 0x7FFFF)); l->lnz[y] = (int)((v >> 8) & 0x7FF); }
                }
            }
        }
    }
}

/* ---------------------------------------------------------------------------------------------------------------------------------- one dot of a tile / bitmap layer: returns 0 (transparent) or an ABGR colour */
static u32 dot_color(const Layer *l, u32 dot, u32 pal)  /* 0 = transparent, idx + 1 = a colour RAM index (resolved per frame), else a direct ABGR colour */
{
    if (l->colors == 32768) return (dot & 0x8000) ? rgb555(dot) : 0;
    if (l->colors == (1 << 24)) return 0;
    if ((dot & (l->colors == 16 ? 15 : l->colors == 256 ? 255 : 2047)) == 0 && !l->tpon) return 0;
    {
        u32 idx;
        if (l->colors == 16) idx = (pal << 4) + dot;
        else if (l->colors == 256) idx = (pal << 8) + dot;
        else idx = dot;
        idx += l->palbank << 8;
        return (idx & (u32)cram_mask) + 1;
    }
}

static u32 cell_dot(u32 cellbase, int colors, int px, int py)     /* dot of an 8x8 cell */
{
    const u8 *v = sat->v2ram;
    if (colors == 16) {
        u8 b = v[(cellbase + py * 4 + (px >> 1)) & 0x7FFFF];
        return (px & 1) ? (b & 15) : (b >> 4);
    }
    if (colors == 256) return v[(cellbase + py * 8 + px) & 0x7FFFF];
    {
        u32 o = (cellbase + (py * 8 + px) * 2) & 0x7FFFF;
        return (u32)(v[o] << 8) | v[(o + 1) & 0x7FFFF];
    }
}

/* the pattern name of the character at (x, y) of the scroll area: its address, plus the position inside the character */
static u32 pn_address(const Layer *l, u32 x, u32 y, u32 *in_x, u32 *in_y)
{
    u32 pl, ix, iy, pgx, pgy, cx, cy;
    x &= l->areaw - 1; y &= l->areah - 1;
    pl = ((y >> l->plhsh) << l->nclsh) + (x >> l->plsh);
    ix = x & (l->planew - 1); iy = y & (l->planeh - 1);
    pgx = ix >> 9; pgy = iy >> 9;
    ix &= 511; iy &= 511;
    cx = ix >> l->cellsh; cy = iy >> l->cellsh;
    *in_x = ix & ((1u << l->cellsh) - 1); *in_y = iy & ((1u << l->cellsh) - 1);
    return (l->plane[pl] + ((((pgy * (u32)l->pw + pgx) << (2 * l->ncellsh)) + (cy << l->ncellsh) + cx) << l->namesh)) & 0x7FFFF;
}

static u32 tile_color(const Layer *l, u32 x, u32 y)
{
    const u8 *v = sat->v2ram;
    u32 in_x, in_y, pn_addr = pn_address(l, x, y, &in_x, &in_y), char_n, pal, hf = 0, vf = 0, pn, px, py, cellbase;
    if (l->onew) {
        pn = (u32)(v[pn_addr] << 8) | v[pn_addr + 1];
        if (l->cnsm) { char_n = (pn & 0xFFF) | ((l->sup & 0x1C) << 10); }
        else { char_n = (pn & 0x3FF) | ((l->sup & 0x1F) << 10); vf = (pn >> 11) & 1; hf = (pn >> 10) & 1; }
        pal = (l->colors == 16) ? (((pn >> 12) & 15) | (((l->sup >> 5) & 7) << 4)) : ((pn >> 12) & 7);
    } else {
        pn = ((u32)v[pn_addr] << 24) | ((u32)v[pn_addr + 1] << 16) | ((u32)v[pn_addr + 2] << 8) | v[pn_addr + 3];
        char_n = pn & 0x7FFF;
        vf = (pn >> 31) & 1; hf = (pn >> 30) & 1;
        pal = (l->colors == 16) ? ((pn >> 16) & 0x7F) : (((pn >> 16) & 0x7F) >> 4) & 7;
    }
    px = in_x; py = in_y;
    cellbase = char_n * 0x20u;
    if (hf) px = (8u * l->chsz) - 1 - px;
    if (vf) py = (8u * l->chsz) - 1 - py;
    if (l->chsz == 2) {
        u32 bytes = (l->colors == 16) ? 0x20 : (l->colors == 256) ? 0x40 : 0x80;
        cellbase += (((py >> 3) << 1) + (px >> 3)) * bytes;
        px &= 7; py &= 7;
    }
    return dot_color(l, cell_dot(cellbase, l->colors, (int)px, (int)py), pal);
}

static u32 bitmap_color(const Layer *l, u32 x, u32 y)
{
    const u8 *v = sat->v2ram;
    u32 pal = l->colors == 16 ? (l->bmpal & 7) << 4 >> 4 : (l->bmpal & 7);
    x &= (u32)l->bmw - 1; y &= (u32)l->bmh - 1;
    if (l->colors == 256) return dot_color(l, v[(l->bmaddr + y * l->bmw + x) & 0x7FFFF], pal);
    if (l->colors == 16) {
        u8 b = v[(l->bmaddr + (y * l->bmw + x) / 2) & 0x7FFFF];
        return dot_color(l, (x & 1) ? (b & 15) : (b >> 4), pal);
    }
    if (l->colors == 32768) {
        u32 o = (l->bmaddr + (y * l->bmw + x) * 2) & 0x7FFFF;
        return dot_color((l), (u32)(v[o] << 8) | v[o + 1], 0);
    }
    return 0;
}

/* ---------------------------------------------------------------------------------------------------------------------------------- RBG0 */
typedef struct { double Xst, Yst, Zst, dXst, dYst, dX, dY, A, B, C, D, E, F, Px, Py, Pz, Cx, Cy, Cz, Mx, My; } Rot;

static double rf(u32 v, u32 mask, int bits) { return sx(v & mask, bits) / 65536.0; }

static void rot_params(Rot *p)
{
    u32 rpta = (((V2R(0xBC) & 7) << 16) | V2R(0xBE)) * 2;
    u32 t[24];
    int i;
    for (i = 0; i < 24; i++) t[i] = R32(0x25E00000u + ((rpta + 4 * i) & 0x7FFFF));
    p->Xst = rf(t[0], 0x1FFFFFC0, 29); p->Yst = rf(t[1], 0x1FFFFFC0, 29); p->Zst = rf(t[2], 0x1FFFFFC0, 29);
    p->dXst = rf(t[3], 0x0007FFC0, 19); p->dYst = rf(t[4], 0x0007FFC0, 19); p->dX = rf(t[5], 0x0007FFC0, 19); p->dY = rf(t[6], 0x0007FFC0, 19);
    p->A = rf(t[7], 0x000FFFC0, 20); p->B = rf(t[8], 0x000FFFC0, 20); p->C = rf(t[9], 0x000FFFC0, 20);
    p->D = rf(t[10], 0x000FFFC0, 20); p->E = rf(t[11], 0x000FFFC0, 20); p->F = rf(t[12], 0x000FFFC0, 20);
    {
        u32 w[8];
        for (i = 0; i < 8; i++) w[i] = R16(0x25E00000u + ((rpta + 0x34 + 2 * i) & 0x7FFFF));
        p->Px = sx(w[0] & 0x3FFF, 14); p->Py = sx(w[1] & 0x3FFF, 14); p->Pz = sx(w[2] & 0x3FFF, 14);
        p->Cx = sx(w[4] & 0x3FFF, 14); p->Cy = sx(w[5] & 0x3FFF, 14); p->Cz = sx(w[6] & 0x3FFF, 14);
    }
    p->Mx = rf(t[17], 0x3FFFFFC0, 30); p->My = rf(t[18], 0x3FFFFFC0, 30);
}

/* ---------------------------------------------------------------------------------------------------------------------------------- colour offset / compose */
static int coff[2][3];                                  /* colour offsets A / B (R, G, B), read once per frame */

static void apply_offset(u32 *c, int sel)
{
    int r, g, b;
    const int *o = coff[sel];
    r = (int)(*c & 255) + o[0]; g = (int)((*c >> 8) & 255) + o[1]; b = (int)((*c >> 16) & 255) + o[2];
    r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
    *c = abgr((u32)r, (u32)g, (u32)b);
}

/* a run of dots of one tile layer line: ax, ay = position in the scroll area, n dots (never crossing a character); fills out[0..n) */
static void tile_run(const Layer *l, u32 ax, u32 ay, int n, u32 *out)
{
    const u8 *v = sat->v2ram;
    u32 in_x, in_y, pn_addr = pn_address(l, ax, ay, &in_x, &in_y), char_n, pal, hf = 0, vf = 0, px, py, cellbase, pn;
    int k;
    if (l->onew) {
        pn = (u32)(v[pn_addr] << 8) | v[pn_addr + 1];
        if (l->cnsm) { char_n = (pn & 0xFFF) | ((l->sup & 0x1C) << 10); }
        else { char_n = (pn & 0x3FF) | ((l->sup & 0x1F) << 10); vf = (pn >> 11) & 1; hf = (pn >> 10) & 1; }
        pal = (l->colors == 16) ? (((pn >> 12) & 15) | (((l->sup >> 5) & 7) << 4)) : ((pn >> 12) & 7);
    } else {
        pn = ((u32)v[pn_addr] << 24) | ((u32)v[pn_addr + 1] << 16) | ((u32)v[pn_addr + 2] << 8) | v[pn_addr + 3];
        char_n = pn & 0x7FFF;
        vf = (pn >> 31) & 1; hf = (pn >> 30) & 1;
        pal = (l->colors == 16) ? ((pn >> 16) & 0x7F) : (((pn >> 16) & 0x7F) >> 4) & 7;
    }
    px = in_x; py = in_y;
    cellbase = char_n * 0x20u;
    if (vf) py = (8u * l->chsz) - 1 - py;
    if (l->chsz == 2) {
        u32 bytes = (l->colors == 16) ? 0x20 : (l->colors == 256) ? 0x40 : 0x80;
        u32 subx = hf ? (1 - (px >> 3)) : (px >> 3);        /* the run lies inside one 8-dot half; with a horizontal flip the halves swap */
        cellbase += ((py >> 3) * 2 + subx) * bytes;
        py &= 7;
        px &= 7;
    }
    {                                                   /* the common cells (16 / 256 colours, inside the VRAM): the cell's row is looked up once and every dot is a few operations */
        const u32 bank = l->palbank << 8, mask = (u32)cram_mask, tp = (u32)l->tpon;
        if (l->colors == 16 && cellbase + (py << 2) + 4 <= 0x80000u) {
            const u8 *row = v + cellbase + (py << 2);
            u32 base = (pal << 4) + bank;
            for (k = 0; k < n; k++) {
                u32 dx = px + (u32)k, b, dot;
                if (hf) dx = 7 - dx;
                b = row[dx >> 1];
                dot = (dx & 1) ? (b & 15) : (b >> 4);
                out[k] = (dot | tp) ? ((base + dot) & mask) + 1 : 0;
            }
            return;
        }
        if (l->colors == 256 && cellbase + (py << 3) + 8 <= 0x80000u) {
            const u8 *row = v + cellbase + (py << 3);
            u32 base = (pal << 8) + bank;
            for (k = 0; k < n; k++) {
                u32 dx = px + (u32)k, dot;
                if (hf) dx = 7 - dx;
                dot = row[dx];
                out[k] = (dot | tp) ? ((base + dot) & mask) + 1 : 0;
            }
            return;
        }
    }
    for (k = 0; k < n; k++) {
        u32 dx = px + (u32)k;
        if (hf) dx = 7 - dx;
        out[k] = dot_color(l, cell_dot(cellbase, l->colors, (int)dx, (int)py), pal);
    }
}

static void tile_line(const Layer *l, int y, int w, u32 *out)
{
    int x = 0, scx = l->scx, zx = l->zx;
    long scy = l->scy;
    u32 ay;
    if (l->ls && y < VDPR_MAXH) {
        if (l->lsx) scx += l->lnx[y];
        if (l->lsy) scy += l->lny[y];
        if (l->lzm && l->lnz[y]) zx = l->lnz[y];
    }
    ay = (u32)((scy + (long)y * l->zy) >> 8);
    if (zx == 256) {
        u32 ax0 = (u32)(scx >> 8);
        while (x < w) {
            u32 ax = ax0 + (u32)x;
            int cellsz = 8 * l->chsz, sub = (int)(ax % 8u), run = 8 - sub;
            (void)cellsz;
            if (run > w - x) run = w - x;
            tile_run(l, ax, ay, run, out + x);
            x += run;
        }
    } else {
        for (x = 0; x < w; x++) out[x] = tile_color(l, (u32)(((long)scx + (long)x * zx) >> 8), ay);
    }
}

static void bitmap_line(const Layer *l, int y, int w, u32 *out)
{
    int x, scx = l->scx, zx = l->zx;
    long scy = l->scy;
    u32 ay;
    if (l->ls && y < VDPR_MAXH) {
        if (l->lsx) scx += l->lnx[y];
        if (l->lsy) scy += l->lny[y];
        if (l->lzm && l->lnz[y]) zx = l->lnz[y];
    }
    ay = (u32)((scy + (long)y * l->zy) >> 8);
    for (x = 0; x < w; x++) out[x] = bitmap_color(l, (u32)(((long)scx + (long)x * zx) >> 8), ay);
}

int vdpr_rbg_half;                                      /* the host: RBG0 (the sky of the 3D scenes, rebuilt every picture while the camera turns) in 2 x 2 dot blocks: a quarter of the work */

static void rbg_line(const Layer *l, int y, int w, const Rot *r, u32 *out)
{
    float ysf = (float)y;
    float xs0 = (float)(r->Xst + r->dXst * ysf), ys0 = (float)(r->Yst + r->dYst * ysf);
    float dx = (float)r->dX, dy = (float)r->dY;
    float A = (float)r->A, B = (float)r->B, D = (float)r->D, E = (float)r->E;
    float Px = (float)r->Px, Py = (float)r->Py, Cx = (float)r->Cx, Cy = (float)r->Cy;
    float X0 = A * (xs0 - Px) + B * (ys0 - Py) + A * (Px - Cx) + B * (Py - Cy) + Cx + (float)r->Mx;
    float Y0 = D * (xs0 - Px) + E * (ys0 - Py) + D * (Px - Cx) + E * (Py - Cy) + Cy + (float)r->My;
    float dX = A * dx + B * dy, dY = D * dx + E * dy;
    int x;
    if (vdpr_rbg_half) {
        for (x = 0; x < w; x += 2) {
            long ix = (long)(X0 + dX * (float)x + 65536.0f) - 65536, iy = (long)(Y0 + dY * (float)x + 65536.0f) - 65536;
            out[x] = l->bitmap ? bitmap_color(l, (u32)(ix & 0xFFFFF), (u32)(iy & 0xFFFFF)) : tile_color(l, (u32)(ix & 0xFFFFF), (u32)(iy & 0xFFFFF));
            if (x + 1 < w) out[x + 1] = out[x];
        }
        return;
    }
    for (x = 0; x < w; x++) {
        long ix = (long)(X0 + dX * (float)x + 65536.0f) - 65536, iy = (long)(Y0 + dY * (float)x + 65536.0f) - 65536;     /* floor (the bias keeps the argument positive) */
        out[x] = l->bitmap ? bitmap_color(l, (u32)(ix & 0xFFFFF), (u32)(iy & 0xFFFFF)) : tile_color(l, (u32)(ix & 0xFFFFF), (u32)(iy & 0xFFFFF));
    }
}

/* ---- layer caches: the pixel codes of a layer are rebuilt only when the VDP2 VRAM or the registers that shape the layer changed; the palette (colour RAM) is applied every frame ---- */
/* A layer's pixel codes live in a torus of TW x TH dots: the dot (X, Y) of the layer's plane is at row Y & (TH - 1), column X & (TW - 1).  A layer that only moves (an unzoomed scroll screen, RBG0 with
 * the identity matrix) shows the window of w x h dots at (ox, oy) of its plane; when the scroll position changes only the dots that came into the window are built (the menus scroll every picture
 * while a screen slides in).  Other layers (zoomed, rotated) are built whole at (0, 0). */
#define TW 512
#define TH 256
typedef struct { u32 *code; u32 key, psig, gen, lg, rgen; int valid, tr, ox, oy, w, h; int rox[VDPR_MAXH]; } LCache;      /* rox: the origin of every screen line (a line scroll shifts the lines) */
static LCache lc[5];
static u32 vram_gen;
extern int sat_v2_touched;
static u8 *vram_shadow;
static u8 cram_shadow[0x1000] __attribute__((aligned(16)));
static int cram_valid;
int vdpr_nocache;
/* the host's 3D split (menu_host.c, the photo scene: the course is drawn between the sky and the text): when vdpr_hi is set, the dots whose winning layer has a priority above
 * vdpr_hi_prio go to *vdpr_hi (alpha 0 where there is none) and the picture gets the back screen there; the colour offset (the fades) is not applied when vdpr_no_offset is set */
VdpFrame *vdpr_hi;
int vdpr_hi_prio, vdpr_no_offset;

static u32 rot_gen;                                     /* changes of the rotation parameter tables only: they are read by RBG0 alone (the menus rewrite them every picture while the logo turns) */
static u32 lgen[5];                                     /* changes of a layer's own pattern name tables / bitmap (a blinking row of text rewrites its name table every few pictures) */

/* a 4 KB page of the VDP2 VRAM that lies inside the pattern name tables (or the bitmap) of an enabled layer: that layer owns it (the page holds names, not character data); -1 = anything else */
static int page_owner_mask(u32 pg)
{
    int li, k, mask = 0;
    for (li = 0; li < 5; li++) {
        const Layer *l = &L[li];
        u32 len;
        if (!l->on) continue;
        if (l->bitmap) {
            len = (u32)l->bmw * (u32)l->bmh * (l->colors == 16 ? 1u : l->colors == 256 ? 2u : 4u) / 2u;
            if (pg + 0x1000 > l->bmaddr && pg < l->bmaddr + len) mask |= 1 << li;
            continue;
        }
        len = ((l->planew >> l->cellsh) * (l->planeh >> l->cellsh)) << l->namesh;
        for (k = 0; k < (li < 4 ? 4 : 16); k++) if (pg + 0x1000 > l->plane[k] && pg < l->plane[k] + len) mask |= 1 << li;
    }
    return mask;
}

/* tables of the VDP2 VRAM that no cached layer reads: the line scroll tables (the slide of the options pages rewrites one every picture; the renderer has no line scroll), the line colour and the
 * back screen colour tables (the back colour is read from its address every frame) */
static int inert_addr(u32 a, const u32 *lo)
{
    int k;
    for (k = 0; k < 4; k++) if (a >= lo[k] && a < lo[k] + (k < 2 ? 0xC00u : 0x200u)) return 1;
    return 0;
}

static int page_differs(const u8 *a, const u8 *b)       /* one 4 KB page, both 4-byte aligned: the PSP's memcmp is a byte loop (the whole VRAM was 4 million instructions per picture) */
{
    const u32 *x = (const u32 *)a, *y = (const u32 *)b;
    int i;
    for (i = 0; i < 1024; i += 8)
        if ((x[i] ^ y[i]) | (x[i + 1] ^ y[i + 1]) | (x[i + 2] ^ y[i + 2]) | (x[i + 3] ^ y[i + 3]) | (x[i + 4] ^ y[i + 4]) | (x[i + 5] ^ y[i + 5]) | (x[i + 6] ^ y[i + 6]) | (x[i + 7] ^ y[i + 7])) return 1;
    return 0;
}

static void vram_diff(void)
{
    u32 pg, rlo = ((((V2R(0xBC) & 7) << 16) | V2R(0xBE)) * 2) & 0x7FFFF, rhi = rlo + 0x100;     /* both tables (A, B) of the rotation parameter area */
    u32 inert[4] = { ((((V2R(0xA0) & 7) << 16) | V2R(0xA2)) * 2) & 0x7FFFF, ((((V2R(0xA4) & 7) << 16) | V2R(0xA6)) * 2) & 0x7FFFF,
                     ((((V2R(0xA8) & 7) << 16) | V2R(0xAA)) * 2) & 0x7FFFF, ((((V2R(0xAC) & 7) << 16) | V2R(0xAE)) * 2) & 0x7FFFF };
    int full = sat_v2_touched || vdpr_nocache;          /* a host that wrote the VRAM itself said so: compare everything; otherwise only the pages the memory model saw written */
    if (!vram_shadow) { vram_shadow = (u8 *)malloc(0x80000); memset(vram_shadow, 0xA5, 0x80000); full = 1; }
    for (pg = 0; pg < 0x80000; pg += 0x1000) {
        if (!full && !((sat_v2_dirty[pg >> 17] >> ((pg >> 12) & 31)) & 1)) continue;
        if (page_differs(vram_shadow + pg, sat->v2ram + pg)) {
            u32 q;
            int outside = 0, rot = 0;
            if (!((rlo < pg + 0x1000 && rhi > pg) || (inert[0] < pg + 0x1000 && inert[0] + 0xC00 > pg) || (inert[1] < pg + 0x1000 && inert[1] + 0xC00 > pg) ||
                  (inert[2] < pg + 0x1000 && inert[2] + 0x200 > pg) || (inert[3] < pg + 0x1000 && inert[3] + 0x200 > pg))) {
                outside = 1;                            /* no special table in this page: whatever changed in it counts */
            } else {
                for (q = pg; q < pg + 0x1000; q++) {
                    if (vram_shadow[q] == sat->v2ram[q]) continue;
                    if (q >= rlo && q < rhi) rot = 1;
                    else if (!inert_addr(q, inert)) outside = 1;
                }
            }
            memcpy(vram_shadow + pg, sat->v2ram + pg, 0x1000);
            if (outside) {
                int own = page_owner_mask(pg), li;
                if (!own) vram_gen++;
                else for (li = 0; li < 5; li++) if (own & (1 << li)) lgen[li]++;
            }
            if (rot) rot_gen++;
        }
    }
}

static u32 hash_regs(u32 lo, u32 hi, u32 h)
{
    u32 o;
    for (o = lo; o < hi; o += 2) h = (h ^ V2R(o)) * 16777619u;
    return h;
}

static u32 layer_key(int n)
{
    u32 h = hash_regs(0x0E, 0x10, 2166136261u);         /* colour RAM mode */
    h = hash_regs(0x28, 0x70, h);                       /* character control, pattern names, plane sizes, map addresses, rotation maps */
    h = hash_regs(0xE4, 0xE8, h);                       /* colour RAM address offsets */
    if (n == 0) h = hash_regs(0x78, 0x80, h);           /* zoom (the scroll position is layer_psig) */
    else if (n == 1) h = hash_regs(0x88, 0x90, h);
    h = hash_regs(0x98, 0xAC, h);                       /* zoom control, scroll control, line scroll tables */
    if (n == 4) h = hash_regs(0xB0, 0xC0, h);
    h = hash_regs(0x20, 0x22, h) ^ (u32)n;
    if (n == 4) h ^= (u32)vdpr_rbg_half * 0x9E3779B1u;
    return h;
}

static u32 layer_psig(int n)                            /* the scroll position registers of a scroll screen */
{
    static const u32 lo[4] = { 0x70, 0x80, 0x90, 0x94 }, hi[4] = { 0x78, 0x88, 0x94, 0x98 };
    u32 h;
    if (n >= 4) return 0;
    h = hash_regs(lo[n], hi[n], 2166136261u);
    if (n < 2 && L[n].ls) {                             /* the line table (unzoomed scroll screens compare their lines' origins instead) */
        int y;
        for (y = 0; y < VDPR_MAXH; y++) h = (((h ^ (u32)L[n].lnx[y]) * 16777619u ^ (u32)L[n].lny[y]) * 16777619u ^ (u32)L[n].lnz[y]) * 16777619u;
    }
    return h;
}

/* does the layer only move?  Then its dot (x, y) of the picture is the dot (*ox + x, *oy + y) of its plane */
static int layer_origin(int li, const Rot *r, int *ox, int *oy)
{
    const Layer *l = &L[li];
    if (li < 4) {
        if (l->zx != 256 || l->zy != 256 || l->lsy || l->lzm || l->vcs) return 0;
        *ox = l->scx >> 8; *oy = l->scy >> 8;
        return 1;
    }
    if (r->A != 1.0 || r->E != 1.0 || r->B != 0.0 || r->D != 0.0 || r->dX != 1.0 || r->dY != 0.0 || r->dXst != 0.0 || r->dYst != 1.0) return 0;
    {                                                   /* rbg_line's start of the line 0 */
        float xs0 = (float)r->Xst, ys0 = (float)r->Yst, A = 1.0f, E = 1.0f;
        float Px = (float)r->Px, Py = (float)r->Py, Cx = (float)r->Cx, Cy = (float)r->Cy;
        float X0 = A * (xs0 - Px) + 0.0f * (ys0 - Py) + A * (Px - Cx) + 0.0f * (Py - Cy) + Cx + (float)r->Mx;
        float Y0 = 0.0f * (xs0 - Px) + E * (ys0 - Py) + 0.0f * (Px - Cx) + E * (Py - Cy) + Cy + (float)r->My;
        *ox = (int)((long)(X0 + 65536.0f) - 65536);
        *oy = (int)((long)(Y0 + 65536.0f) - 65536);
    }
    return 1;
}

/* the dots ax0 .. ax1 - 1 of the row ay of a moving layer's plane into the torus */
static void fill_span(int li, int ax0, int ax1, int ay)
{
    const Layer *l = &L[li];
    u32 *row = lc[li].code + (ay & (TH - 1)) * TW;
    int ax = ax0;
    while (ax < ax1) {
        int slot = ax & (TW - 1), n = ax1 - ax, k;
        if (TW - slot < n) n = TW - slot;
        if (l->bitmap) {
            for (k = 0; k < n; k++) row[slot + k] = bitmap_color(l, (u32)(ax + k), (u32)ay);
        } else {
            for (k = 0; k < n; ) {                      /* tile_run never crosses a character (8 dots) */
                int run = 8 - ((ax + k) & 7);
                if (run > n - k) run = n - k;
                tile_run(l, (u32)(ax + k), (u32)ay, run, row + slot + k);
                k += run;
            }
        }
        ax += n;
    }
}

static int update_layer(int li, int w, int h, const Rot *rot, u32 key, u32 psig)
{
    LCache *c = &lc[li];
    const Layer *l = &L[li];
    int ox = 0, oy = 0, tr = layer_origin(li, rot, &ox, &oy), y, same, nrox[VDPR_MAXH];
    if (!c->code) c->code = (u32 *)malloc(sizeof(u32) * TW * TH);
    for (y = 0; y < VDPR_MAXH; y++) nrox[y] = (tr && li < 4 && l->lsx) ? (l->scx + l->lnx[y]) >> 8 : ox;
    same = !vdpr_nocache && c->valid && c->key == key && c->gen == vram_gen && c->lg == lgen[li] && c->w == w && c->h == h && c->tr == tr;
    if (same && !tr && c->psig == psig && c->rgen == rot_gen) return 0;         /* nothing changed */
    if (same && tr) {
        int any = oy != c->oy;
        for (y = 0; y < h && !any; y++) if (nrox[y] != c->rox[y]) any = 1;
        if (!any) return 0;
        for (y = 0; y < h; y++) {
            int ay = oy + y, yo = ay - c->oy, in_old = yo >= 0 && yo < h, ox_n = nrox[y], ox_o = in_old ? c->rox[yo] : 0, dx = ox_n - ox_o;
            if (!in_old || dx >= w || -dx >= w) fill_span(li, ox_n, ox_n + w, ay);
            else if (dx > 0) fill_span(li, ox_o + w, ox_n + w, ay);
            else if (dx < 0) fill_span(li, ox_n, ox_o, ay);
        }
    } else if (tr) {
        for (y = 0; y < h; y++) fill_span(li, nrox[y], nrox[y] + w, oy + y);
    } else {
        for (y = 0; y < h; y++) {
            u32 *out = c->code + y * TW;
            if (li == 4 && vdpr_rbg_half && (y & 1)) memcpy(out, out - TW, sizeof(u32) * (size_t)w);
            else if (li == 4) rbg_line(l, y, w, rot, out);
            else if (l->bitmap) bitmap_line(l, y, w, out);
            else tile_line(l, y, w, out);
        }
    }
    c->valid = 1; c->key = key; c->psig = psig; c->gen = vram_gen; c->lg = lgen[li]; c->rgen = rot_gen;
    c->tr = tr; c->ox = tr ? ox : 0; c->oy = tr ? oy : 0; c->w = w; c->h = h;
    for (y = 0; y < VDPR_MAXH; y++) c->rox[y] = tr ? nrox[y] : 0;
    return 1;
}

/* the composed picture: for every dot the winning layer's pixel code with its priority and colour offset mode; rebuilt when a layer or the priority / enable / offset registers changed.
 * pixel word: bit 31 = direct colour (bits 23..0 = BGR), else 12-bit colour RAM index + 1; bits 30..28 priority, bits 27..26 colour offset mode (0 none, 1 A, 2 B); 0 = back screen */
static u32 *comp;
static u32 comp_key, comp_gen_sum;
static int comp_valid, comp_w, comp_h;

static u32 composed_key(int nact, const int *act)
{
    u32 h = hash_regs(0xC0, 0xE0, hash_regs(0xE0, 0x100, 2166136261u));
    int n;
    h = (h ^ V2R(0x20)) * 16777619u;
    h = (h ^ V2R(0x110)) * 16777619u;
    h = (h ^ V2R(0x112)) * 16777619u;
    for (n = 0; n < nact; n++) h = (h ^ (u32)act[n]) * 16777619u;
    return h;
}

int vdpr_noskip;                                        /* development: always compose the picture (the reference of the unchanged-picture check) */
static int vdpr_forgot;
void vdpr_forget(void) { vdpr_forgot = 1; }             /* the host drew into the picture itself: the next one is composed again */
static u32 lut0[4096], lutA[4096], lutB[4096];          /* the colour of a palette word of the composed picture (its low 12 bits), without / with colour offset A / B */
static u32 lut_cram_gen = ~0u, lut_back, lut_sig;
static int lut_mask = -1;
static short v1_x0[VDPR_MAXH], v1_x1[VDPR_MAXH];        /* the span of every picture line that the sprite layer may have drawn in (vdp1_draw) */
#ifdef PROF
extern int vdpr_ref;
#endif
static int skip_ok, v1_on;                           /* set by vdp_render_frame: the sprite layer's state is tracked (v1_gen counts the pictures it drew), so an unchanged picture may be skipped */
static u32 v1_gen;

void vdp2_render(VdpFrame *f, const u16 *fb)
{
    u32 tvmd = V2R(0x00), bk;
    int w = ((tvmd & 7) == 1) ? 352 : 320, h = (tvmd & 0x30) == 0x10 ? 240 : 224;
    int y, x, n, p, nact = 0;
    Rot rot;
    u32 back, clofen = V2R(0x110), clofsl = V2R(0x112), prisa = V2R(0xF0) & 7;
    int order[5] = { 3, 2, 1, 0, 4 };                   /* ascending importance at equal priority */
    int act[5];                                         /* the enabled layers, most important first */
    int actprio[5];
    int any_off = 0, rebuilt = 0, comp_changed = 0, cram_changed = 0, any_direct = 0;
    PT_BEGIN();
    f->w = w; f->h = h;
    if (vdpr_hi) { vdpr_hi->w = w; vdpr_hi->h = h; }
    setup_layers();
    if (sat_v2_touched || sat_v2_dirty_any || vdpr_nocache) {
        vram_diff();
        sat_v2_touched = 0; sat_v2_dirty_any = 0;
        memset(sat_v2_dirty, 0, sizeof(u32) * (V2RAM_SIZE / 4096 / 32));
    }
    PT_END(0);
    {
        u32 mode = (V2R(0x0E) >> 12) & 3;
        int mask = mode == 1 ? 0x7FF : 0x3FF;
        if (mask != cram_mask) cram_valid = 0;
        cram_mask = mask;
    }
    if (!cram_valid || page_differs(cram_shadow, sat->cram)) {
        memcpy(cram_shadow, sat->cram, 0x1000);
        cram_valid = 1;
        build_cramtab();
        cram_changed = 1;
    }
    PT_END(1);
    for (n = 0; n < 2; n++) { u32 base = n ? 0x11A : 0x114; coff[n][0] = sx(V2R(base), 9); coff[n][1] = sx(V2R(base + 2), 9); coff[n][2] = sx(V2R(base + 4), 9); if (coff[n][0] | coff[n][1] | coff[n][2]) any_off = 1; }
    if (vdpr_no_offset) any_off = 0;
    bk = (((V2R(0xAC) & 7) << 16) | V2R(0xAE)) * 2;
    back = rgb555((u32)(sat->v2ram[bk & 0x7FFFF] << 8) | sat->v2ram[(bk + 1) & 0x7FFFF]);
    if (any_off && (clofen & 0x20)) apply_offset(&back, (clofsl >> 5) & 1);
    rot_params(&rot);
    for (p = 7; p >= 1; p--)
        for (n = 4; n >= 0; n--) {
            int li = order[n];
#ifdef PROF
            if (getenv("VDPR_ONLY") && !((atoi(getenv("VDPR_ONLY")) >> li) & 1)) continue;      /* development: only the layers of this mask (bit 0 NBG0 .. 3 NBG3, 4 RBG0) */
#endif
            if (L[li].on && L[li].prio == p) { actprio[nact] = p; act[nact++] = li; }
        }
    for (n = 0; n < nact; n++) {
        int li = act[n];
        if (update_layer(li, w, h, &rot, layer_key(li), layer_psig(li))) rebuilt = 1;
    }
    PT_END(2);
    {
        u32 ck = composed_key(nact, act) ^ (u32)(w * 7 + h);
        if (vdpr_nocache || rebuilt || !comp_valid || ck != comp_key || comp_w != w || comp_h != h) {
            if (!comp) comp = (u32 *)malloc(sizeof(u32) * VDPR_MAXW * VDPR_MAXH);
            u32 pbits[5], wbits[5];
            int wrect[2][4], anywin = 0;
            for (n = 0; n < nact; n++) {
                int li = act[n];
                u32 om = (L[li].coff_en && (clofen >> li & 1)) ? (u32)(L[li].coff_sel + 1) : 0;
                pbits[n] = ((u32)actprio[n] << 28) | (om << 26);
                wbits[n] = (li == 4 ? V2R(0xD4) : li >= 2 ? V2R(0xD2) : V2R(0xD0)) >> ((li & 1) && li < 4 ? 8 : 0) & 0xFF;     /* the window control byte of the layer (WCTLA .. C) */
#ifdef PROF
                if (getenv("VDPR_NOWIN")) wbits[n] = 0;                                                    /* development: the pictures without windows (which scenarios do they change?) */
#endif
                if (!(wbits[n] & 0x0A)) wbits[n] = 0;                                                      /* no window 0 / 1 enabled: the whole picture */
                else anywin = 1;
            }
            for (n = 0; n < 2; n++) {                   /* the rectangles of the windows 0 / 1 (x in half dots in the normal modes) */
                wrect[n][0] = (int)(V2R(0xC0 + 8 * (u32)n) & 0x3FF) >> 1; wrect[n][1] = (int)(V2R(0xC4 + 8 * (u32)n) & 0x3FF) >> 1;
                wrect[n][2] = (int)(V2R(0xC2 + 8 * (u32)n) & 0x3FF);      wrect[n][3] = (int)(V2R(0xC6 + 8 * (u32)n) & 0x3FF);
            }
            for (y = 0; y < h; y++) {                   /* the layers from the least important to the most important, each over the dots before it */
                u32 *o = comp + y * VDPR_MAXW;
                memset(o, 0, sizeof(u32) * (size_t)w);
                for (n = nact - 1; n >= 0; n--) {
                    const LCache *c = &lc[act[n]];
                    const u32 *src = c->code + ((c->oy + y) & (TH - 1)) * TW;
                    u32 pb = pbits[n];
                    int x0 = 0, sx0 = c->rox[y] & (TW - 1);
                    if (anywin && wbits[n]) {           /* a layer with windows: the dots where the windows (rectangles, combined with OR / AND) let it show */
                        u32 wb = wbits[n];
                        int pal = L[act[n]].colors != 32768;
                        for (x = 0; x < w; x++) {
                            int ok, v0 = 1, v1 = 1, e0 = (wb & 2) != 0, e1 = (wb & 8) != 0;
                            u32 v;
                            if (e0) { int in = y >= wrect[0][2] && y <= wrect[0][3] && x >= wrect[0][0] && x <= wrect[0][1]; v0 = (wb & 1) ? in : !in; }
                            if (e1) { int in = y >= wrect[1][2] && y <= wrect[1][3] && x >= wrect[1][0] && x <= wrect[1][1]; v1 = (wb & 4) ? in : !in; }
                            ok = (e0 && e1) ? ((wb & 0x80) ? (v0 && v1) : (v0 || v1)) : (e0 ? v0 : v1);
                            if (!ok) continue;
                            v = src[(sx0 + x) & (TW - 1)];
                            if (!v) continue;
                            o[x] = pal ? (v | pb) : v < 0x10000u ? (v & 0xFFF) | pb : 0x80000000u | pb | (v & 0xFFFFFF);
                        }
                        continue;
                    }
                    while (x0 < w) {                    /* the part of the row up to the wrap of the torus */
                        int cnt = TW - sx0 < w - x0 ? TW - sx0 : w - x0, k;
                        const u32 *s = src + sx0;
                        u32 *d = o + x0;
                        if (L[act[n]].colors != 32768) {        /* palette dots: a colour RAM index + 1 (at most 0x800) */
                            for (k = 0; k < cnt; k++) {
                                u32 v = s[k], old = d[k];
                                d[k] = v ? (v | pb) : old;
                            }
                        } else {
                            for (k = 0; k < cnt; k++) {
                                u32 v = s[k];
                                if (v) d[k] = v < 0x10000u ? (v & 0xFFF) | pb : 0x80000000u | pb | (v & 0xFFFFFF);
                            }
                        }
                        x0 += cnt; sx0 = 0;
                    }
                }
            }
            comp_valid = 1; comp_key = ck; comp_w = w; comp_h = h;
            comp_changed = 1;
        }
    }
    PT_END(3);
    {                                                   /* nothing that shapes the picture changed (an idle screen: only the host's 3D parts move): the picture of the last call is still there */
        static u8 reg_prev[V2REG_SIZE] __attribute__((aligned(16)));
        static u32 prev_back, prev_gen;
        static const VdpFrame *prev_f, *prev_hi;
        static int prev_ok, prev_v1on, prev_prio, prev_nooff, prev_w, prev_h;
        int regs_same = 1, same, k;
        if (vdpr_forgot) { prev_ok = 0; vdpr_forgot = 0; }
        for (k = 0; k < V2REG_SIZE / 4; k++) if (((const u32 *)reg_prev)[k] != ((const u32 *)sat->v2reg)[k]) { regs_same = 0; break; }
        same = skip_ok && prev_ok && prev_f == f && prev_hi == vdpr_hi && !comp_changed && !cram_changed && back == prev_back && v1_gen == prev_gen && v1_on == prev_v1on &&
               vdpr_hi_prio == prev_prio && vdpr_no_offset == prev_nooff && w == prev_w && h == prev_h && regs_same;
#ifdef PROF
        if (!same) {                                    /* the first reason that kept the picture from being reused */
            int why = !skip_ok ? 0 : !prev_ok ? 1 : (prev_f != f || prev_hi != vdpr_hi) ? 2 : comp_changed ? 3 : cram_changed ? 4 : back != prev_back ? 5 : v1_gen != prev_gen ? 6 : v1_on != prev_v1on ? 7 : !regs_same ? 8 : 9;
            vdpr_prof_why[why]++;
            if (why == 8) for (k = 0; k < V2REG_SIZE; k++) if (reg_prev[k] != sat->v2reg[k]) vdpr_prof_reg[k]++;
        }
#endif
        f->same = same;
        if (vdpr_hi) vdpr_hi->same = same;
        if (!regs_same) memcpy(reg_prev, sat->v2reg, V2REG_SIZE);
        prev_back = back; prev_gen = v1_gen; prev_f = f; prev_hi = vdpr_hi; prev_ok = skip_ok; prev_v1on = v1_on; prev_prio = vdpr_hi_prio; prev_nooff = vdpr_no_offset; prev_w = w; prev_h = h;
        if (same) { PT_END(4); return; }
    }
    for (n = 0; n < nact; n++) if (L[act[n]].colors == 32768) any_direct = 1;
    {                                                   /* the colour of a palette word: one table per picture (rebuilt when colour RAM, the back colour or the colour offsets changed) */
        u32 sig = (u32)any_off * 7u, i, m = (u32)cram_mask;
        int om_used = 0;
        if (any_off) {
            for (n = 0; n < nact; n++) if (L[act[n]].coff_en && (clofen >> act[n] & 1)) om_used |= 1 << L[act[n]].coff_sel;
            sig = (sig ^ (u32)om_used) * 16777619u;
            for (n = 0; n < 6; n++) sig = (sig ^ (u32)coff[n / 3][n % 3]) * 16777619u;
        }
        if (lut_cram_gen != cram_gen || lut_back != back || lut_sig != sig || lut_mask != cram_mask) {
            lut0[0] = back;
            for (i = 1; i < 4096; i++) lut0[i] = cramtab[(i - 1) & m];
            if (om_used & 1) for (i = 1; i < 4096; i++) { lutA[i] = lut0[i]; apply_offset(&lutA[i], 0); }
            if (om_used & 2) for (i = 1; i < 4096; i++) { lutB[i] = lut0[i]; apply_offset(&lutB[i], 1); }
            lut_cram_gen = cram_gen; lut_back = back; lut_sig = sig; lut_mask = cram_mask;
        }
    }
    for (y = 0; y < h; y++) {
        const u32 *o = comp + y * VDPR_MAXW;
        u32 *out = &f->pix[y * VDPR_MAXW];
        const u16 *sp = fb ? fb + y * w : NULL;
#ifdef PROF
        if (vdpr_ref) goto slow_row;
#endif
        if (vdpr_hi && !any_direct) {                   /* the host's 3D split with palette layers only: a table lookup per dot, the dots above the sprite priority go to the second picture */
            u32 *hout = &vdpr_hi->pix[y * VDPR_MAXW];
            const u32 *const lt[4] = { lut0, any_off ? lutA : lut0, any_off ? lutB : lut0, lut0 };      /* (the offset tables are only built for a fade) */
            const int hip = vdpr_hi_prio;
            if (!any_off) {
                for (x = 0; x < w; x++) {
                    u32 word = o[x], col = lut0[word & 0xFFF];
                    if ((int)((word >> 28) & 7) > hip) { out[x] = back; hout[x] = col; }
                    else { out[x] = col; hout[x] = 0; }
                }
            } else {
                for (x = 0; x < w; x++) {
                    u32 word = o[x], col = lt[(word >> 26) & 3][word & 0xFFF];
                    int hi = (int)((word >> 28) & 7) > hip;
                    out[x] = hi ? back : col;
                    hout[x] = hi ? col : 0;
                }
            }
            if (sp) {                                   /* the sprite layer where its priority wins (such a dot is below the split: hout stays clear) */
                int xa = skip_ok ? v1_x0[y] : 0, xb = skip_ok ? v1_x1[y] : w - 1;
                if (xa < 0) xa = 0;
                if (xb > w - 1) xb = w - 1;
                for (x = xa; x <= xb; x++) {
                    u32 s = sp[x];
                    if ((s & 0x8000) && (int)prisa >= (int)((o[x] >> 28) & 7)) {
                        u32 col = rgb555(s);
                        if (any_off && (clofen & 0x40)) apply_offset(&col, (clofsl >> 6) & 1);
                        out[x] = col;
                    }
                }
            }
            continue;
        }
#ifdef PROF
    slow_row:
#endif
        if (vdpr_hi) {                                  /* the host's 3D split: the dots of the layers above vdpr_hi_prio go to the second picture */
            u32 *hout = &vdpr_hi->pix[y * VDPR_MAXW];
            for (x = 0; x < w; x++) {
                u32 word = o[x], col;
                int prio = (int)((word >> 28) & 7);
                if (!word) {
                    col = back;                         /* the back screen: a sprite over it still shows */
                } else {
                    col = (word & 0x80000000u) ? 0xFF000000u | (word & 0xFFFFFF) : cramtab[((word & 0xFFF) - 1) & (u32)cram_mask];
                    if (any_off && (word & (3u << 26))) apply_offset(&col, (int)((word >> 26) & 3) - 1);
                    if (prio > vdpr_hi_prio) { out[x] = back; hout[x] = col; continue; }
                }
                hout[x] = 0;
                if (sp && (sp[x] & 0x8000) && (int)prisa >= prio) {
                    col = rgb555(sp[x]);
                    if (any_off && (clofen & 0x40)) apply_offset(&col, (clofsl >> 6) & 1);
                }
                out[x] = col;
            }
            continue;
        }
#ifdef PROF
        if (vdpr_ref) {                                 /* the straightforward per-dot composition: the reference of the table driven loops below (tools/menu_skipcheck.sh, VDPR_REF) */
            for (x = 0; x < w; x++) {
                u32 word = o[x], col;
                int prio = (int)((word >> 28) & 7);
                if (!word) col = back;
                else {
                    if (word & 0x80000000u) col = 0xFF000000u | (word & 0xFFFFFF);
                    else col = cramtab[((word & 0xFFF) - 1) & (u32)cram_mask];
                    if (any_off && (word & (3u << 26))) apply_offset(&col, (int)((word >> 26) & 3) - 1);
                }
                if (sp && (sp[x] & 0x8000) && (int)prisa >= prio) {
                    col = rgb555(sp[x]);
                    if (any_off && (clofen & 0x40)) apply_offset(&col, (clofsl >> 6) & 1);
                }
                out[x] = col;
            }
            continue;
        }
#endif
        if (!any_direct) {                              /* palette layers only: every dot is one table lookup (a fade: the table of the dot's colour offset mode, bits 27..26 of the word: 0 none, 1 A, 2 B) */
            if (!any_off) {
                for (x = 0; x < w; x += 4) {
                    u32 a = o[x], b = o[x + 1], c = o[x + 2], d = o[x + 3];
                    out[x] = lut0[a & 0xFFF]; out[x + 1] = lut0[b & 0xFFF]; out[x + 2] = lut0[c & 0xFFF]; out[x + 3] = lut0[d & 0xFFF];
                }
            } else {
                const u32 *const lt[4] = { lut0, lutA, lutB, lut0 };
                for (x = 0; x < w; x += 4) {
                    u32 a = o[x], b = o[x + 1], c = o[x + 2], d = o[x + 3];
                    out[x] = lt[(a >> 26) & 3][a & 0xFFF]; out[x + 1] = lt[(b >> 26) & 3][b & 0xFFF]; out[x + 2] = lt[(c >> 26) & 3][c & 0xFFF]; out[x + 3] = lt[(d >> 26) & 3][d & 0xFFF];
                }
            }
        } else if (!any_off) {                          /* the dots of the layers: a palette word's low 12 bits index lut0 (entry 0 = the back screen), a direct colour word carries its BGR */
            for (x = 0; x < w; x++) {
                u32 word = o[x];
                out[x] = (word & 0x80000000u) ? 0xFF000000u | (word & 0xFFFFFF) : lut0[word & 0xFFF];
            }
        } else {                                        /* a fade: the layers' colour offset (the word's bits 27..26: 0 none, 1 A, 2 B) */
            for (x = 0; x < w; x++) {
                u32 word = o[x], om = (word >> 26) & 3, col;
                if (word & 0x80000000u) {
                    col = 0xFF000000u | (word & 0xFFFFFF);
                    if (om) apply_offset(&col, (int)om - 1);
                } else {
                    col = om == 0 ? lut0[word & 0xFFF] : om == 1 ? lutA[word & 0xFFF] : lutB[word & 0xFFF];
                }
                out[x] = col;
            }
        }
        if (sp) {                                       /* the sprite layer over them where its priority wins; only the span of the row that the sprites touched is looked at */
            int xa = skip_ok ? v1_x0[y] : 0, xb = skip_ok ? v1_x1[y] : w - 1;
            if (xa < 0) xa = 0;
            if (xb > w - 1) xb = w - 1;
            for (x = xa; x <= xb; x++) {
                u32 s = sp[x];
                if ((s & 0x8000) && (int)prisa >= (int)((o[x] >> 28) & 7)) {
                    u32 col = rgb555(s);
                    if (any_off && (clofen & 0x40)) apply_offset(&col, (clofsl >> 6) & 1);
                    out[x] = col;
                }
            }
        }
    }
    PT_END(4);
}

/* ---------------------------------------------------------------------------------------------------------------------------------- VDP1 */
static int clip_x0, clip_y0, clip_x1, clip_y1, loc_x, loc_y;

static void put_px(u16 *fb, int w, int h, int x, int y, u32 col)
{
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    if (x < clip_x0 || x > clip_x1 || y < clip_y0 || y > clip_y1) return;
    fb[y * w + x] = (u16)(col | 0x8000);
    if (x < v1_x0[y]) v1_x0[y] = (short)x;
    if (x > v1_x1[y]) v1_x1[y] = (short)x;
}

static u32 v1_texel(u32 srca, u32 colr, u32 mode, int tw, int u, int v, int *transparent)
{
    const u8 *vr = sat->v1ram;
    u32 idx;
    *transparent = 0;
    if (mode == 0 || mode == 1) {                        /* 16 colours: bank or LUT */
        u8 b = vr[(srca + v * (tw >> 1) + (u >> 1)) & 0x7FFFF];
        idx = (u & 1) ? (b & 15) : (b >> 4);
        if (idx == 0) { *transparent = 1; return 0; }
        if (mode == 1) {
            u32 a = (colr * 8) & 0x7FFFF;
            return (u32)(vr[(a + 2 * idx) & 0x7FFFF] << 8) | vr[(a + 2 * idx + 1) & 0x7FFFF];
        }
        return (colr & 0xFFF0) | idx;
    }
    if (mode == 5) {                                     /* RGB */
        u32 o = (srca + 2 * (v * tw + u)) & 0x7FFFF;
        u32 c = (u32)(vr[o] << 8) | vr[(o + 1) & 0x7FFFF];
        if (c == 0) *transparent = 1;
        return c;
    }
    idx = vr[(srca + v * tw + u) & 0x7FFFF];
    if (idx == 0) { *transparent = 1; return 0; }
    return (colr & (mode == 2 ? 0xFFC0 : mode == 3 ? 0xFF80 : 0xFF00)) | idx;
}

/* a textured quad A B C D: along the edges A-D and B-C one line of texels per step (oversampled so no holes appear) */
static void v1_quad(u16 *fb, int w, int h, int xa, int ya, int xb, int yb, int xc, int yc, int xd, int yd, u32 cmd, u32 pmod, u32 colr, u32 srca, u32 size, int textured, int hflip, int vflip)
{
    int tw = (int)(((size >> 8) & 0x3F) * 8), th = (int)(size & 0xFF);
    int n, m, i, j;
    int lenl = abs(xd - xa) > abs(yd - ya) ? abs(xd - xa) : abs(yd - ya);
    int lenr = abs(xc - xb) > abs(yc - yb) ? abs(xc - xb) : abs(yc - yb);
    n = lenl > lenr ? lenl : lenr;
    (void)cmd;
    if (textured && (tw == 0 || th == 0)) return;
    if (n > 600) n = 600;
    n += 1;
    for (i = 0; i <= n * 2; i++) {
        double t = (double)i / (double)(n * 2);
        double lx = xa + (xd - xa) * t, ly = ya + (yd - ya) * t, rx = xb + (xc - xb) * t, ry = yb + (yc - yb) * t;
        int len = (int)(fabs(rx - lx) > fabs(ry - ly) ? fabs(rx - lx) : fabs(ry - ly)) + 1;
        if (len > 600) len = 600;
        m = len * 2;
        for (j = 0; j <= m; j++) {
            double s = m ? (double)j / (double)m : 0;
            int px = (int)floor(lx + (rx - lx) * s + 0.5), py = (int)floor(ly + (ry - ly) * s + 0.5);
            if (textured) {
                int u = (int)(s * tw), v = (int)(t * th), tr;
                u32 col;
                if (u >= tw) u = tw - 1;
                if (v >= th) v = th - 1;
                if (hflip) u = tw - 1 - u;
                if (vflip) v = th - 1 - v;
                col = v1_texel(srca, colr, (pmod >> 3) & 7, tw, u, v, &tr);
                if (tr && !(pmod & 0x40)) continue;
                if (tr) col = 0;
                put_px(fb, w, h, px, py, col);
            } else {
                if (!(pmod & 0x40) && (colr & 0x7FFF) == 0 && !(colr & 0x8000)) continue;
                put_px(fb, w, h, px, py, colr);
            }
        }
    }
}

/* a normal sprite (command 0) is the texture copied 1:1 to (x0, y0): every destination pixel once (the oversampling v1_quad needs for the scaled shapes was 4 times the work) */
static void v1_sprite(u16 *fb, int w, int h, int x0, int y0, u32 pmod, u32 colr, u32 srca, u32 size, int hflip, int vflip)
{
    int tw = (int)(((size >> 8) & 0x3F) * 8), th = (int)(size & 0xFF);
    int xa = x0 > clip_x0 ? x0 : clip_x0, xb = x0 + tw - 1 < clip_x1 ? x0 + tw - 1 : clip_x1;
    int ya = y0 > clip_y0 ? y0 : clip_y0, yb = y0 + th - 1 < clip_y1 ? y0 + th - 1 : clip_y1;
    u32 mode = (pmod >> 3) & 7;
    const u8 *vr = sat->v1ram;
    int x, y, tpen = (pmod & 0x40) != 0;                /* tpen: a transparent dot is drawn as colour 0 */
    u32 pal[16];
    if (tw == 0 || th == 0) return;
    if (xa < 0) xa = 0;
    if (ya < 0) ya = 0;
    if (xb > w - 1) xb = w - 1;
    if (yb > h - 1) yb = h - 1;
    if (xa > xb || ya > yb) return;
    if (mode <= 1) {                                    /* 16 colours: the colour of a dot code is the same all over the sprite */
        u32 a = (colr * 8) & 0x7FFFF, i;
        for (i = 0; i < 16; i++) pal[i] = mode == 1 ? (u32)(vr[(a + 2 * i) & 0x7FFFF] << 8) | vr[(a + 2 * i + 1) & 0x7FFFF] : (colr & 0xFFF0) | i;
    }
    for (y = ya; y <= yb; y++) {
        int v = vflip ? th - 1 - (y - y0) : y - y0;
        u16 *row = fb + y * w;
        if (xa < v1_x0[y]) v1_x0[y] = (short)xa;
        if (xb > v1_x1[y]) v1_x1[y] = (short)xb;
        if (mode <= 1) {
            u32 line = srca + (u32)(v * (tw >> 1));
            for (x = xa; x <= xb; x++) {
                int u = hflip ? tw - 1 - (x - x0) : x - x0;
                u8 b = vr[(line + (u32)(u >> 1)) & 0x7FFFF];
                u32 idx = (u & 1) ? (b & 15) : (b >> 4);
                if (idx == 0) { if (tpen) row[x] = 0x8000; continue; }
                row[x] = (u16)(pal[idx] | 0x8000);
            }
        } else if (mode == 5) {                         /* RGB */
            for (x = xa; x <= xb; x++) {
                int u = hflip ? tw - 1 - (x - x0) : x - x0;
                u32 o = (srca + 2u * (u32)(v * tw + u)) & 0x7FFFF, c = (u32)(vr[o] << 8) | vr[(o + 1) & 0x7FFFF];
                if (c == 0) { if (tpen) row[x] = 0x8000; continue; }
                row[x] = (u16)(c | 0x8000);
            }
        } else {                                        /* 64 / 128 / 256 colours: a bank (the dot code is the low bits of the colour word) */
            u32 line = srca + (u32)(v * tw), hi = colr & (mode == 2 ? 0xFFC0u : mode == 3 ? 0xFF80u : 0xFF00u);
            for (x = xa; x <= xb; x++) {
                int u = hflip ? tw - 1 - (x - x0) : x - x0;
                u32 idx = vr[(line + (u32)u) & 0x7FFFF];
                if (idx == 0) { if (tpen) row[x] = 0x8000; continue; }
                row[x] = (u16)((hi | idx) | 0x8000);
            }
        }
    }
}

#ifdef PROF
int vdpr_ref;                                           /* development (tools/menu_skipcheck.sh): the straightforward sprite and composition loops */
static void v1_sprite_ref(u16 *fb, int w, int h, int x0, int y0, u32 pmod, u32 colr, u32 srca, u32 size, int hflip, int vflip)
{
    int tw = (int)(((size >> 8) & 0x3F) * 8), th = (int)(size & 0xFF);
    int xa = x0 > clip_x0 ? x0 : clip_x0, xb = x0 + tw - 1 < clip_x1 ? x0 + tw - 1 : clip_x1;
    int ya = y0 > clip_y0 ? y0 : clip_y0, yb = y0 + th - 1 < clip_y1 ? y0 + th - 1 : clip_y1;
    u32 mode = (pmod >> 3) & 7;
    int x, y;
    if (tw == 0 || th == 0) return;
    if (xa < 0) xa = 0;
    if (ya < 0) ya = 0;
    if (xb > w - 1) xb = w - 1;
    if (yb > h - 1) yb = h - 1;
    for (y = ya; y <= yb; y++) {
        int v = vflip ? th - 1 - (y - y0) : y - y0;
        u16 *row = fb + y * w;
        for (x = xa; x <= xb; x++) {
            int u = hflip ? tw - 1 - (x - x0) : x - x0, tr;
            u32 col = v1_texel(srca, colr, mode, tw, u, v, &tr);
            if (tr) {
                if (!(pmod & 0x40)) continue;
                col = 0;
            }
            row[x] = (u16)(col | 0x8000);
        }
    }
}
#endif

static void v1_command(u16 *fb, int w, int h, u32 a)           /* a = address of the command in the table (console address) */
{
    u32 ctrl = R16(a), pmod = R16(a + 4), colr = R16(a + 6), srca = (u32)R16(a + 8) * 8, size = R16(a + 10);
    int cmd = (int)(ctrl & 15), xs[4], ys[4], k;
    for (k = 0; k < 4; k++) { xs[k] = (s16)R16(a + 0xC + 4 * k); ys[k] = (s16)R16(a + 0xE + 4 * k); }
    if (cmd == 8) { clip_x0 = xs[0]; clip_y0 = ys[0]; clip_x1 = xs[2]; clip_y1 = ys[2]; return; }
    if (cmd == 9) { clip_x0 = 0; clip_y0 = 0; clip_x1 = xs[2]; clip_y1 = ys[2]; return; }
    if (cmd == 10) { loc_x = xs[0]; loc_y = ys[0]; return; }
    if (cmd == 0) {
#ifdef PROF
        if (vdpr_ref) { v1_sprite_ref(fb, w, h, xs[0] + loc_x, ys[0] + loc_y, pmod, colr, srca, size, (int)((ctrl >> 4) & 1), (int)((ctrl >> 5) & 1)); return; }
#endif
        v1_sprite(fb, w, h, xs[0] + loc_x, ys[0] + loc_y, pmod, colr, srca, size, (int)((ctrl >> 4) & 1), (int)((ctrl >> 5) & 1));
        return;
    }
    if (cmd == 1 || cmd == 2 || cmd == 4) {
        int px[4], py[4];
        if (cmd == 0) {
            int tw = (int)(((size >> 8) & 0x3F) * 8), th = (int)(size & 0xFF);
            px[0] = xs[0]; py[0] = ys[0]; px[1] = xs[0] + tw - 1; py[1] = ys[0]; px[2] = xs[0] + tw - 1; py[2] = ys[0] + th - 1; px[3] = xs[0]; py[3] = ys[0] + th - 1;
        } else if (cmd == 1) {
            px[0] = xs[0]; py[0] = ys[0]; px[1] = xs[2]; py[1] = ys[0]; px[2] = xs[2]; py[2] = ys[2]; px[3] = xs[0]; py[3] = ys[2];
        } else {
            for (k = 0; k < 4; k++) { px[k] = xs[k]; py[k] = ys[k]; }
        }
        for (k = 0; k < 4; k++) { px[k] += loc_x; py[k] += loc_y; }
        v1_quad(fb, w, h, px[0], py[0], px[1], py[1], px[2], py[2], px[3], py[3], ctrl, pmod, colr, srca, size, cmd != 4, (ctrl >> 4) & 1, (ctrl >> 5) & 1);
    }
}

void vdp1_draw(u16 *fb, int w, int h, const u16 *list, int n)
{
    int i;
    clip_x0 = 0; clip_y0 = 0; clip_x1 = w - 1; clip_y1 = h - 1; loc_x = 176; loc_y = 112;       /* game_zsort's head commands (0x0601B604, table 0x0601B364): the local coordinate origin is the middle of the 352 x 224 picture */
    memset(fb, 0, (size_t)w * (size_t)h * 2);
    for (i = 0; i < VDPR_MAXH; i++) { v1_x0[i] = 0x7FFF; v1_x1[i] = -1; }
    for (i = 0; i < n; i++) v1_command(fb, w, h, 0x060C0000u + 8u * list[i]);
}

/* the draw order of the master pipeline: the entries of the z-buckets from the farthest bucket to the nearest, in insertion order (game_zsort without the slave half) */
int vdp1_sorted_list(u16 *list, int max)
{
    u32 wk = 0x060D1400u, zbuf = R32(wk + 32), cnt = R32(wk + 36);
    int n = 0, b, k;
    if (!zbuf || !cnt) return 0;
    for (b = 0x1FFE; b >= 0; b -= 2) {                  /* b = (z >> 10) & ~1: counters at cnt + b, entries at zbuf + 16 * b */
        u32 c = R8(cnt + (u32)b);
        for (k = 0; k < (int)c && k < 16; k += 2) {
            if (n < max) list[n++] = R16(zbuf + ((u32)b << 4) + (u32)k);
        }
    }
    return n;
}

void vdp_render_frame(VdpFrame *f)
{
    static u16 fb[VDPR_MAXW * VDPR_MAXH];
    static u16 list[2048];
    static int prev_n, prev_w, prev_h, fb_ok;
    static u32 prev_hash;
    u32 tvmd = V2R(0x00), hash = 2166136261u;
    int w = ((tvmd & 7) == 1) ? 352 : 320, h = (tvmd & 0x30) == 0x10 ? 240 : 224;
    unsigned long long t0 = vdpr_clock ? vdpr_clock() : 0, t1 = t0;
    int n = vdp1_sorted_list(list, 2048), i;
    if (vdpr_clock) { t1 = vdpr_clock(); vdpr_t[6] += t1 - t0; }
    if (n || prev_n) {                                  /* nothing drawn twice in a row: no sprite layer, no clearing */
        for (i = 0; i < n; i++) {                       /* the same commands over the same textures draw the same sprites again */
            const u8 *c = sat_ptr(0x060C0000u + 8u * list[i]);
            int k;
            hash = (hash ^ list[i]) * 16777619u;
            for (k = 0; k < 32; k += 4) hash = (hash ^ ((u32)c[k] << 24 | (u32)c[k + 1] << 16 | (u32)c[k + 2] << 8 | c[k + 3])) * 16777619u;
        }
        if (!(fb_ok && !sat_v1_touched && n == prev_n && w == prev_w && h == prev_h && hash == prev_hash)) {
            vdp1_draw(fb, w, h, list, n);
            v1_gen++;
        }
        fb_ok = 1; prev_hash = hash; prev_w = w; prev_h = h;
        sat_v1_touched = 0;
    }
    prev_n = n;
    if (vdpr_clock) vdpr_t[5] += vdpr_clock() - t1;
    skip_ok = !vdpr_noskip; v1_on = n != 0;
    vdp2_render(f, n ? fb : NULL);
    skip_ok = 0;
}
