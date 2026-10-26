/* Sega Rally PSP - the HUD name-table generators, see hudgen.h.  Each routine below is one of the routines `game_display` (0x060123F8) calls for the game types 0 and 2; the addresses are
 * the console's VDP2 VRAM addresses of the original code, the cell values are name table words (bits 14 - 12 palette, 9 - 0 character number; a 256-colour 8 x 8 cell takes two character numbers). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hudgen.h"
#include "fx.h"

enum { O_MODE = 0x08, O_SPEED = 0x94, O_DIAL = 0xB0, O_GEAR = 0xB6, O_HEAD = 0xC5, O_LAPNEXT = 0xC8, O_MSG = 0xF2, O_CTRL = 0xF5, O_R = 0x500 };

static void put(HudGen *h, unsigned a, int v)
{
    unsigned i = (a - HUD_NT_BASE) >> 1;
    if (i < HUD_NT_WORDS) h->w[i] = (unsigned short)v;
}

static const unsigned char DIG[16] = { 0x00, 0x0C, 0x18, 0x24, 0x30, 0x3C, 0x48, 0x54, 0x60, 0x6C, 0x00, 0x09, 0xC5, 0x64, 0xD1, 0x2B };      /* 0x06011F90: character offset of the big digit n (12 = 6 cells) */

static int bcdw(const Phys *p, int n) { return n < 0 ? 0 : phys_r16s(p, 0x060A1000u + 2u * (unsigned)n); }                                 /* 0x060A1000: binary -> BCD (a negative index reads the zeros in front of the table) */

/* 0x06012166 / 06011F76: a big digit, 2 x 3 cells at three consecutive rows (a3, a5, a7), characters v .. v + 10 */
static void draw6(HudGen *h, unsigned a3, unsigned a5, unsigned a7, int v)
{
    put(h, a3, v);      put(h, a3 + 2, v + 2);
    put(h, a5, v + 4);  put(h, a5 + 2, v + 6);
    put(h, a7, v + 8);  put(h, a7 + 2, v + 10);
}

static void clear6(HudGen *h, unsigned a3, unsigned a5, unsigned a7)
{
    put(h, a3, 0); put(h, a3 + 2, 0); put(h, a5, 0); put(h, a5 + 2, 0); put(h, a7, 0); put(h, a7 + 2, 0);
}

/* 0x0601224A: the low nibble of `bcd` as a small digit (1 x 2 cells: the cell at a9 + 2 and under it at a10 + 2); 0x0601223C: both nibbles (cells a9, a9 + 2) */
static void small1(HudGen *h, unsigned a9, unsigned a10, int bcd, int base)
{
    int v = base + ((bcd & 15) << 2);
    put(h, a9 + 2, v);
    put(h, a10 + 2, v + 2);
}

static void small2(HudGen *h, unsigned a9, unsigned a10, int bcd, int base)
{
    int v = base + ((bcd & 0xF0) >> 2);
    put(h, a9, v);
    put(h, a10, v + 2);
    small1(h, a9, a10, bcd, base);
}

/* 0x06011ECC: the wrong-way message blinks (NBG3 y = 0 shows it, 0x100 hides it) */
static void wrong_way(HudGen *h, Drive *d, unsigned frame)
{
    const unsigned char *c = d->car;
    if (g8(c, O_HEAD) == 0) {
        if ((frame & 0x1F) != 31) return;
        if (h->scroll[3] != 0) { SET_MSG(d, 30); h->scroll[3] = 0; return; }
    }
    h->scroll[3] = 0x100;
}

/* 0x06011EF8: the tachometer dial, a 3 x 3 block of cells chosen by the engine speed index (+0xB0) */
static void dial(HudGen *h, const Drive *d)
{
    int v = 0x30D8 + 18 * g16(d->car, O_DIAL);
    for (int row = 0; row < 3; row++) {
        unsigned a = 0x25E3AC10u + 0x80u * (unsigned)row;
        for (int k = 0; k < 3; k++) { put(h, a + 2u * (unsigned)k, v); v += 2; }
    }
}

/* 0x06011F2C: the gear */
static void gear(HudGen *h, const Drive *d)
{
    unsigned a3 = 0x25E3A144u, a5 = 0x25E3A1C4u, a7 = 0x25E3A244u;
    int w = bcdw(d->phys, (signed char)g8(d->car, O_GEAR)), base = 0x2002, off;
    if (w >= 16) {
        draw6(h, a3, a5, a7, 12 + base);                                                  /* a leading "1" */
        off = DIG[w & 15];
    } else {
        clear6(h, a3, a5, a7);
        if (w == 1) { base = 0x20CC; off = 0; } else off = DIG[w & 15];
    }
    draw6(h, a3 + 4, a5 + 4, a7 + 4, off + base);
}

/* 0x06012084: the speed, three small digits: [0x0604000D] bit 0 (set by the boot code in the USA release) selects mph (x 67) instead of km/h (x 108) */
static void speed(HudGen *h, const Drive *d)
{
    unsigned a9 = 0x25E3AC98u, a10 = 0x25E3AD18u;
    int base = 0x2090;
    int w = bcdw(d->phys, HM(g32(d->car, O_SPEED), 0x00430000));
    if (w >= 0x100) {
        int v = (((w >> 8) << 2) & 60) + base;
        put(h, a9, v); put(h, a10, v + 2);
    } else {
        put(h, a9, 0); put(h, a10, 0);
        if (w < 0x10) { put(h, a9 + 2, 0); put(h, a10 + 2, 0); goto units; }
    }
    {
        int v = ((w >> 2) & 60) + base;
        put(h, a9 + 2, v); put(h, a10 + 2, v + 2);
    }
units:
    {
        int v = ((w << 2) & 60) + base;
        put(h, a9 + 4, v); put(h, a10 + 4, v + 2);
    }
}

/* 0x060120F0: the remaining time, two big digits (the units digit turns red for the last 5 seconds) */
static void remaining(HudGen *h, const Drive *d)
{
    const unsigned char *c = d->car;
    unsigned a3 = 0x25E3A128u, a5 = 0x25E3A1A8u, a7 = 0x25E3A228u;
    int t = g32(c, O_R + 0x34);
    int secs = (int)((u32)t >> 8);
    if (secs > 99) secs = 99;
    int w = bcdw(d->phys, secs), base = 0x21C2;
    if (w < 16) {
        clear6(h, a3, a5, a7);
        if (!(w > 5)) base = 0x223A;                                                      /* (the original also queues a beep each second here) */
    } else {
        draw6(h, a3, a5, a7, DIG[(w >> 4) & 15] + base);
    }
    draw6(h, a3 + 4, a5 + 4, a7 + 4, DIG[w & 15] + base);
}

/* a time m:ss.hh as small digits starting at cell a9 (+ the row below at a10): mode 0 = 0x0601220A (one minute digit, the cell before it is left alone), 1 = 0x06012204 (the same, that cell cleared),
 * 2 = 0x06012224 (two minute digits) */
static void time_row(HudGen *h, unsigned a9, unsigned a10, const unsigned char *t, int mode)
{
    const int base = 0x2090;
    if (mode == 2) small2(h, a9, a10, t[1], base);
    else {
        if (mode == 1) { put(h, a9, 0); put(h, a10, 0); }
        small1(h, a9, a10, t[1], base);
    }
    small2(h, a9 + 6, a10 + 6, t[2], base);
    small2(h, a9 + 12, a10 + 12, t[3], base);
}

/* 0x060121BC: this lap's time in its row, and the total time (on the last stage of the arcade game: a blinking message instead) */
static void lap_times(HudGen *h, const Drive *d, int lap, unsigned frame)
{
    const unsigned char *c = d->car;
    const unsigned char *slot = c + O_R + 0x10 + 4 * lap;
    unsigned r9 = (unsigned)phys_r32(d->phys, 0x00298570u + 8u * (unsigned)lap), r10 = (unsigned)phys_r32(d->phys, 0x00298570u + 8u * (unsigned)lap + 4u);
    time_row(h, r9, r10, slot, 0);
    if (d->cfg.game_type == 0 && d->cfg.course == 3) {                                    /* 0x06012258 */
        for (int row = 0; row < 2; row++)
            for (int k = 0; k < 11; k++) {
                unsigned a = 0x25E3A104u + 0x80u * (unsigned)row + 2u * (unsigned)k;
                put(h, a, (frame & 0x10) ? 0x333C + 2 * (11 * row + k) : 0);
            }
        return;
    }
    const unsigned char *tot = c + O_R + 12;
    time_row(h, 0x25E3A106u, 0x25E3A186u, tot, tot[1] >= 16 ? 2 : 1);
}

/* 0x0601308C: the co-driver message (car +0xF2 = message number, +0xF3 = frames left) as a 4 x 4 block of cells */
static void navigator(HudGen *h, Drive *d)
{
    unsigned char *c = d->car;
    unsigned dst = 0x25E3D2A8u;
    if (d->cfg.mirror) return;
    int word = g16u(c, O_MSG);
    if (word == 0) return;
    int clear = 0;
    if (g8(c, O_HEAD) == 0) { p8(c, O_MSG + 1, 0); clear = 1; }
    else {
        int t = g8(c, O_MSG + 1);
        if (t == 0) {
            int msg = word >> 8;
            SET_MSG(d, msg);
            p16(c, O_MSG, 45);
            for (int row = 0; row < 4; row++)
                for (int k = 0; k < 4; k++)
                    put(h, dst + 0x80u * (unsigned)row + 2u * (unsigned)k, 0x407A + 2 * phys_r8(d->phys, 0x00298DF8u + 16u * (unsigned)(msg - 1) + 4u * (unsigned)row + (unsigned)k));
            return;
        }
        t -= 1;
        p8(c, O_MSG + 1, t);
        if (t != 0) return;
        clear = 1;
    }
    if (clear)
        for (int row = 0; row < 4; row++)
            for (int k = 0; k < 4; k++) put(h, dst + 0x80u * (unsigned)row + 2u * (unsigned)k, 0);
}

/* ---- the end of the race: game_display's branch for the control byte >= 8 (0x06012D18), a state machine on [0x060AA178] (Drive.ret_state, timer [0x060AA17C] = Drive.cd_timer) that draws the
 * result messages on NBG0.  Cells there are two-word names (palette 0x10 << 16 | character number; a 256-colour 8 x 8 cell takes two character numbers); the block copiers of the original
 * (0x060056B4 ..) write a rectangle of them with a row pitch of 0x100 bytes.  The sound calls (0x06016F10, 0x0600F756, 0x06016FA4, 0x06017548) go to sndev.c's sink; the fade (0x0600585C) is not ported. */
static void put0(HudGen *h, unsigned a, unsigned v)
{
    unsigned i = (a - HUDE_BASE) >> 2;
    if (i < HUDE_CELLS) h->nt0[i] = v;
}

/* table reads that treat an address outside the converted tables like the oracle's memory does: zeros (the original reads the console's open bus there; only reached with odd placings) */
static int mapped(const Phys *p, unsigned a, unsigned n)
{
    for (int i = 0; i < p->nregions; i++)
        if (a >= p->reg[i].addr && a + n <= p->reg[i].addr + p->reg[i].size) return 1;
    return 0;
}
static unsigned tbl32(const Phys *p, unsigned a) { return mapped(p, a, 4) ? (unsigned)phys_r32(p, a) : 0; }
static int tbl16s(const Phys *p, unsigned a) { return mapped(p, a, 2) ? phys_r16s(p, a) : 0; }
static int tbl8(const Phys *p, unsigned a) { return mapped(p, a, 1) ? phys_r8(p, a) : 0; }

/* 0x060056B4: a rectangle of cells from a table of words (sign-extended), + add */
static void cp_words(HudGen *h, const Phys *p, unsigned src, unsigned dst, int cols, int rows, int add)
{
    for (int r = 0; r < rows; r++)
        for (int k = 0; k < cols; k++, src += 2) put0(h, dst + 0x100u * (unsigned)r + 4u * (unsigned)k, (unsigned)(tbl16s(p, src) + add));
}

/* 0x060056BC: the same from a table of bytes (unsigned) */
static void cp_bytes(HudGen *h, const Phys *p, unsigned src, unsigned dst, int cols, int rows, int add)
{
    for (int r = 0; r < rows; r++)
        for (int k = 0; k < cols; k++, src++) put0(h, dst + 0x100u * (unsigned)r + 4u * (unsigned)k, (unsigned)(tbl8(p, src) + add));
}

/* 0x06005704: a rectangle of consecutive characters (steps of 2) */
static void cp_seq(HudGen *h, unsigned dst, int cols, int rows, int add)
{
    int v = 0;
    for (int r = 0; r < rows; r++)
        for (int k = 0; k < cols; k++, v += 2) put0(h, dst + 0x100u * (unsigned)r + 4u * (unsigned)k, (unsigned)(v + add));
}

/* 0x06012CA4: one digit of 2 x 2 cells at (a9, a10) = the upper and the lower cell row, then moves on to the next digit */
static void digit0(HudGen *h, unsigned *a9, unsigned *a10, int v)
{
    put0(h, *a9, (unsigned)v); put0(h, *a9 + 4, (unsigned)v + 2);
    put0(h, *a10, (unsigned)v + 4); put0(h, *a10 + 4, (unsigned)v + 6);
    *a9 += 8; *a10 += 8;
}

/* 0x06012C44: a time (BCD bytes t[1..3] = minutes, seconds, hundredths) as 2 x 2 cell digits with the two marks between; the address pair (upper / lower cell row) is at `pair` in the tables, + off.
 * From 10 minutes on the first digit moves one place to the left. */
static void time0(HudGen *h, const Phys *p, const unsigned char *t, unsigned pair, int off)
{
    const int base = 0x00100002;
    unsigned a9 = tbl32(p, pair) + (unsigned)off, a10 = tbl32(p, pair + 4) + (unsigned)off;
    int m = (signed char)t[1];
    if (!(16 > m)) { a9 -= 8; a10 -= 8; digit0(h, &a9, &a10, base + ((m & 0xF0) >> 1)); }
    digit0(h, &a9, &a10, base + ((m & 15) << 3));
    digit0(h, &a9, &a10, base + 0x50);
    digit0(h, &a9, &a10, base + ((t[2] & 0xF0) >> 1));
    digit0(h, &a9, &a10, base + ((t[2] & 15) << 3));
    digit0(h, &a9, &a10, base + 0x58);
    digit0(h, &a9, &a10, base + ((t[3] & 0xF0) >> 1));
    digit0(h, &a9, &a10, base + ((t[3] & 15) << 3));
}

/* 0x06012A96: the block of the car's final placing ([GBR + 0xB6]): 10 x 5 cells from the table at 0x002987DC */
static void rank_block(HudGen *h, const Drive *d, unsigned dst)
{
    unsigned src = tbl32(d->phys, 0x002987DCu + 4u * (unsigned)(signed char)g8(d->car, 0xB6));
    cp_words(h, d->phys, src, dst, 10, 5, 0x00100062);
}

/* 0x06012AA8 (arcade game over): one earlier stage's time per pass; R+0 counts the stages drawn (-1 = all) */
static void stage_times(HudGen *h, Drive *d)
{
    unsigned char *R = d->car + O_R;
    int course = d->cfg.course, k = (signed char)R[0];
    if (k == -1) return;
    if (k == course) { R[0] = 0xFF; return; }
    R[0] = (unsigned char)(k + 1);
    time0(h, d->phys, R + 0x38 + 4 * k, tbl32(d->phys, 0x002986D4u + 4u * (unsigned)course) + 8u * (unsigned)k, 0);
}

/* 0x06012AE4 (arcade goal): the stage times of the game so far, then the total */
static void goal_times(HudGen *h, Drive *d)
{
    unsigned char *R = d->car + O_R;
    int course = d->cfg.course, k = (signed char)R[0];
    if (k == -1) return;
    if (k == course + 1) { R[0] = 0xFF; time0(h, d->phys, R + 12, 0x002986BCu + 8u * (unsigned)course, 0); return; }
    switch (course) {
    case 1:  R[0] = 2;    time0(h, d->phys, R + 0x3C, tbl32(d->phys, 0x00298688u), 0); break;
    case 3:  R[0] = 0xFF; time0(h, d->phys, R + 0x44, tbl32(d->phys, 0x00298690u), 0); break;
    default: R[0] = (unsigned char)(k + 1); time0(h, d->phys, R + 0x38 + 4 * k, tbl32(d->phys, 0x00298684u + 4u * (unsigned)course) + 8u * (unsigned)k, 0); break;
    }
}

/* 0x06012B68 (practice game over): one lap row per pass (time + label), then the "total" label */
static void lap_rows(HudGen *h, Drive *d)
{
    unsigned char *R = d->car + O_R;
    int lap = g16(d->car, O_LAPNEXT), k = (signed char)R[0];
    unsigned base = lap > 2 ? 0x00298714u + 24 : 0x00298714u;
    if (k == -1) return;
    if (k == lap) {
        R[0] = 0xFF;
        cp_seq(h, tbl32(d->phys, base + 8u * (unsigned)lap) - 0x4000u, 14, 2, 0x00100386);
    } else {
        R[0] = (unsigned char)(k + 1);
        time0(h, d->phys, R + 0x10 + 4 * k, base + 8u * (unsigned)k, -0x4000);
    }
    unsigned row = tbl32(d->phys, 0x00298598u + 4u * (unsigned)lap) + 8u * (unsigned)k;
    cp_bytes(h, d->phys, tbl32(d->phys, row), tbl32(d->phys, row + 4) - 0x4000u, 15, 2, 0x001003BE);
}

/* 0x06012BF4 (practice goal): the lap times one per pass, then the total time */
static void lap_list(HudGen *h, Drive *d)
{
    unsigned char *R = d->car + O_R;
    int n = d->total_laps, k = (signed char)R[0];
    if (k == -1) return;
    if (k == n) { R[0] = 0xFF; time0(h, d->phys, R + 12, 0x00298754u + 8u * (unsigned)(n - 1), 0); return; }
    R[0] = (unsigned char)(k + 1);
    time0(h, d->phys, R + 0x10 + 4 * k, (n > 3 ? 0x00298714u + 24 : 0x00298714u) + 8u * (unsigned)k, 0);
}

void hudend_pass(HudGen *h, Drive *d)
{
    unsigned char *c = d->car;
    int course = d->cfg.course;
    switch (d->ret_state) {
    case 0:
        d->g803 &= ~0x40;
        se_call(&d->se, SND_CMD, 16, 0, 0);
        if (g8(c, O_CTRL) != 10) { h->nscroll[0] = 0x400; d->ret_state = 16; d->cd_timer = 90; SET_MSG(d, 36); }
        else { d->ret_state = 2; d->cd_timer = 90; }
        h->bgon = 0x10;
        se_call(&d->se, SND_CDPLAY, g8(c, O_CTRL) != 10 ? 6 : 7, 0, 0);                  /* the goal / game over jingle (CD track 6 / 7) */
        se_call(&d->se, SND_FA4, -64, 0, 0);
        break;
    case 2:                                                                               /* game over: the placing block, then the times */
        d->ret_state = d->cfg.game_type == 0 ? 4 : 10;
        rank_block(h, d, 0x25E2A550u);
        h->bgon = 0x11;
        break;
    case 16:                                                                              /* goal */
        if (d->cfg.game_type == 0) {
            static const unsigned dst[4] = { 0x25E2E650u, 0x25E2E650u, 0x25E2E450u, 0x25E2E550u };
            d->ret_state = 18;
            rank_block(h, d, dst[course & 3]);
        } else {
            d->ret_state = 22;
            rank_block(h, d, 0x25E2E550u);
        }
        h->bgon = 0x11;
        break;
    case 4: case 10: case 18: case 22:
        if (d->ret_state == 4) stage_times(h, d);
        else if (d->ret_state == 10) lap_rows(h, d);
        else if (d->ret_state == 18) {
            if (course == 2 && g8(c, 0xB6) != 1) h->wctla = 0x82;
            goal_times(h, d);
        } else lap_list(h, d);
        if (--d->cd_timer == 0) {
            if (d->ret_state == 18 || d->ret_state == 22) sndev_eng_idle(&d->se);          /* 0x06017548: the engine idles */
            d->ret_state += 2; h->nscroll[1] = 0x100; d->cd_timer = 0x78;
        }
        break;
    case 6: case 12: case 24:
        if (--d->cd_timer == 0) d->ret_state = 26;
        break;
    case 20:                                                                              /* the blinking window (arcade goal, not on the last stage) */
        if (course != 3 && (course != 2 || g8(c, 0xB6) == 1)) h->wctla = ((d->cd_timer + 16) & 0x10) ? 0x82 : 0;
        if (--d->cd_timer == 0) d->ret_state = 26;
        break;
    case 26:
        h->done = 1;
        if (!(d->g803 & 0x20)) d->g803 |= 2;
        break;
    default: break;                                                                       /* 8, 14: nothing */
    }
}

static int rec_long(const Drive *d, unsigned a) { unsigned o = a - 0x060AA400u; return o <= sizeof d->rec - 4 ? g32(d->rec, (int)o) : 0; }       /* a long of the record buffers (0x060AA400 ..) */

/* ---- time attack: the small state machines of display2 (0x06012664 .. 0x060129C2) ---- */
/* 0x06012354: a time as 1 x 1 cell digits (the other character set, base 0x22B2): minutes (the tens digit only from 10 minutes), seconds, hundredths, each part behind its mark */
static void time1x1(HudGen *h, unsigned r5, const unsigned char *t)
{
    const int base = 0x22B2;
    int m = (signed char)t[1];
    if (m >= 16) put(h, r5, ((m >> 4) << 1) + base);
    put(h, r5 + 2, ((m & 15) << 1) + base);
    r5 += 4;
    put(h, r5, 22 + base);
    put(h, r5 + 2, ((t[2] & 0xF0) >> 3) + base);
    put(h, r5 + 4, ((t[2] & 15) << 1) + base);
    r5 += 6;
    put(h, r5, 20 + base);
    put(h, r5 + 2, ((t[3] & 0xF0) >> 3) + base);
    put(h, r5 + 4, ((t[3] & 15) << 1) + base);
}

static void box_clear8(HudGen *h, unsigned a) { for (int k = 0; k < 8; k++) put(h, a + 2u * (unsigned)k, 0); }          /* 0x060129B8: boxclr_word, 8 cells of one row */

/* 0x0601295C (the three times of the new-best flags, drawn) / 0x0601298C (cleared): the blink of the retime display */
static void retime_times(HudGen *h, Drive *d, int clear)
{
    const unsigned char *c = d->car;
    static const unsigned dst[3] = { 0x25E3A2C2u, 0x25E3A442u, 0x25E3A5C2u };
    const unsigned char *src[3] = { c + O_R + 0x30, c + O_R + 12, c + O_R + 112 };
    for (int k = 0; k < 3; k++)
        if (d->g302 & (1 << k)) { if (clear) box_clear8(h, dst[k]); else time1x1(h, dst[k], src[k]); }
}

static void record_box(HudGen *h, const Drive *d, unsigned a, int clear_first);

/* 0x0601289C (retime_etc_display): [0x060AA301] = state (re_1x1time_ft sets 2 when a lap was a new best lap), [0x060AA302] = which times (bit 0 the best lap R+0x30, bit 1 the total R+12, bit 2 R+0x70),
 * [0x060AA303] = frames left; the time blinks (10 frames drawn, 8 cleared) five times, then stays drawn */
static void retime_etc(HudGen *h, Drive *d)
{
    switch (d->g301) {
    case 0: break;
    case 2:
        d->g301 += 2; d->g303 = 1;
        if (d->g302 & 4) record_box(h, d, 0x25E2C984u, 0);                             /* bit 2: a course record: the record box (record_sub 0x060118FC, the second plane) before the time */
        retime_times(h, d, 0);
        break;
    case 4: case 8: case 12: case 16: case 20:
        if (--d->g303 == 0) { d->g301 += 2; retime_times(h, d, 0); d->g303 = 10; }
        break;
    case 6: case 10: case 14: case 18:
        if (--d->g303 == 0) { d->g301 += 2; retime_times(h, d, 1); d->g303 = 8; }
        break;
    case 22:
        if (g8(d->car, O_CTRL) == 8) {                                                 /* the goal screen: blinks on */
            d->g301 = 4; d->g303 = 1;
            if (--d->g303 == 0) { d->g301 += 2; retime_times(h, d, 0); d->g303 = 10; }
        } else { d->g301 = 0; retime_times(h, d, 0); d->g302 = 0; }
        break;
    default: break;
    }
}

/* 0x06011D4C (laps_number_disp_f): the empty time rows of the laps after the first ("0'00"00" marks), 7 x 2 cells each (the byte table at 0x06011D70) */
static void laps_number(HudGen *h, const Drive *d)
{
    static const unsigned char tbl[14] = { 0x30, 0x2C, 0x30, 0x30, 0x28, 0x30, 0x30, 0x32, 0x2E, 0x32, 0x32, 0x2A, 0x32, 0x32 };
    for (int b = 0; b < d->total_laps - 1; b++)
        for (int row = 0; row < 2; row++)
            for (int col = 0; col < 7; col++) put(h, 0x25E3A388u + 0x100u * (unsigned)b + 0x80u * (unsigned)row + 2u * (unsigned)col, 0x2090 + tbl[row * 7 + col]);
}

/* 0x060127DC: the window blink counter: R+3 counts down, at 0 the window control register is set to r5, R+2 goes on one state and the counter restarts with r6 */
static void blink(HudGen *h, Drive *d, int r5, int r6)
{
    unsigned char *c = d->car;
    int n = (signed char)g8(c, O_R + 3) - 1;
    if (n != 0) { p8(c, O_R + 3, n); return; }
    h->wctlb = (unsigned short)r5;
    p8(c, O_R + 2, g8(c, O_R + 2) + 2);
    p8(c, O_R + 3, r6);
}

/* 0x06012718 (state 2 of the compare displays): the difference of the last section / lap time to the best run's, signed, in the box at 0x25E3A4A0: R+100 / R+104 hold it (rec_tail, drive.c): byte 0 = the
 * sign (0x80 = slower), the rest minutes, seconds, hundredths.  The mark in front: 13 (slower, palette 1) / 12 (faster) / 14 (equal, palette 2) */
static void cmp_show(HudGen *h, Drive *d)
{
    unsigned char *c = d->car;
    p8(c, O_R + 2, 4); p8(c, O_R + 3, 1);
    const unsigned char *r2 = c + O_R + (g8(c, O_R + 1) == 1 ? 100 : 104);
    int v = g32(r2, 0), total = g32(c, O_R + 12), flag = (signed char)g8(c, O_R + 126), mark, base;
    unsigned a9 = 0x25E3A4A0u, a10 = 0x25E3A520u;
    if (v > 0) { mark = 13; base = 0x1090; }
    else if (v != 0) { mark = 12; base = 144; }
    else { mark = 14; base = 0x2090; }
    unsigned mag = (unsigned)v & 0x7FFFFFFFu;
    if (d->cfg.game_type != 0 && (total >= 0x00095999 || flag != 0 || mag == 0x00095999u)) {        /* no valid comparison: the box is emptied */
        for (int k = 0; k < 10; k++) { put(h, a9 + 2u * (unsigned)k, 0); put(h, a10 + 2u * (unsigned)k, 0); }
        return;
    }
    small1(h, a9, a10, mark, base);
    a9 += 4; a10 += 4;
    small1(h, a9, a10, r2[1], base);
    a9 += 2; a10 += 2;
    small1(h, a9, a10, 11, base);
    a9 += 4; a10 += 4;
    small2(h, a9, a10, r2[2], base);
    a9 += 2; a10 += 2;
    small1(h, a9, a10, 10, base);
    a9 += 4; a10 += 4;
    small2(h, a9, a10, r2[3], base);
}

/* 0x06012664 (timecmp_display, game types 0, 2 and 4) with R+1 != 0: [R+2] = state: 2 draws the compare, then the box blinks (the window register: 10 passes off, 8 on) and is gone after state 24 */
static void timecmp(HudGen *h, Drive *d)
{
    unsigned char *c = d->car;
    if (!g8(c, O_R + 1)) return;
    int st = (signed char)g8(c, O_R + 2);
    switch (st) {
    case 2: cmp_show(h, d); break;
    case 4: case 8: case 12: case 16: case 20: blink(h, d, 0, 10); break;
    case 6: case 10: case 14: case 18: case 22: blink(h, d, 0x88, 8); break;
    case 24: p8(c, O_R + 2, 0); break;
    default: break;
    }
}


/* 0x0601269C (timecmp_display_f, game type 6): R+1 = 0: the lap start flash of the free run (R+2 = state); with R+1 != 0 the compare of the best run (not ported yet: states by 0x060126C0) */
static void timecmp_f(HudGen *h, Drive *d)
{
    unsigned char *c = d->car;
    int st = (signed char)g8(c, O_R + 2);
    if (g8(c, O_R + 1)) {                                                              /* the compare with the best run (table 0x060126C0) */
        switch (st) {
        case 2: cmp_show(h, d); break;
        case 4: case 8: case 12: case 16: case 20: blink(h, d, 0, 10); break;
        case 6: case 10: case 14: case 18: case 22: blink(h, d, g8(c, O_R + 125) ? 10 : 0x88, 8); break;
        case 24: case 26:
            if (st == 24 && g8(c, O_R + 125)) { p8(c, O_R + 125, 0); p8(c, O_R + 2, g8(c, O_R + 2) + 2); laps_number(h, d); }
            else { p8(c, O_R + 3, 1); blink(h, d, 0x88, 1); p8(c, O_R + 2, 0); }
            break;
        default: break;
        }
        return;
    }
    switch (st) {
    case 0:                                                                            /* 0x060126FC: the compare option asks for the best run and there is one */
        if (d->cfg.compare == 2 && rec_long(d, (unsigned)g32(c, O_R + 88)) != 0) p8(c, O_R + 1, 2);
        break;
    case 2: p8(c, O_R + 2, 4); p8(c, O_R + 3, 1); break;
    case 4: case 8: case 12: case 16: case 20: blink(h, d, 0x88, 10); break;
    case 6: case 10: case 14: case 18: case 22: blink(h, d, 2, 8); break;
    case 24:
    case 26:
        if (st == 24 && g8(c, O_R + 125)) {                                            /* 0x0601282C: the flash is over, the lap rows are cleared */
            p8(c, O_R + 125, 0);
            p8(c, O_R + 2, g8(c, O_R + 2) + 2);
            laps_number(h, d);
        } else {                                                                       /* 0x06012840 */
            p8(c, O_R + 3, 1);
            blink(h, d, 0x88, 1);
            p8(c, O_R + 2, 0);
        }
        break;
    default: break;
    }
}

/* 0x06012278 (free run): the running lap in the row of lap R+124 - 1 (always lap slot 0), and the total; nothing at all while the flag R+125 is 1 */
static void free_run_times(HudGen *h, const Drive *d)
{
    const unsigned char *c = d->car;
    if (g8(c, O_R + 125) == 1) return;
    int n = (signed char)g8(c, O_R + 124) - 1;
    if (d->total_laps != n) {
        unsigned r9 = (unsigned)phys_r32(d->phys, 0x00298570u + 8u * (unsigned)n), r10 = (unsigned)phys_r32(d->phys, 0x00298570u + 8u * (unsigned)n + 4u);
        time_row(h, r9, r10, c + O_R + 0x10, 0);
    }
    const unsigned char *tot = c + O_R + 12;
    time_row(h, 0x25E3A106u, 0x25E3A186u, tot, tot[1] >= 16 ? 2 : 1);
}

/* ---- the pause menu (display3 0x06013BA4 .. 0x06013C28): a box of 8 x 8 cells at 0x25E3A5A4 with three items; [0x060AA821 ..] = item, the item drawn last, blink counter, flags (Drive.pause) ---- */
static void box_bytes(HudGen *h, const Drive *d, unsigned table, unsigned dst, int cols, int rows, int base)       /* 0x060056E4: boxwrt_c2, a byte table + base, rows 0x80 apart */
{
    for (int r = 0; r < rows; r++)
        for (int k = 0; k < cols; k++) put(h, dst + 0x80u * (unsigned)r + 2u * (unsigned)k, base + phys_r8(d->phys, table + (unsigned)(r * cols + k)));
}

static void pause_item(HudGen *h, const Drive *d, int item, int base)
{
    static const unsigned dst[3] = { 0x25E3A6A4u, 0x25E3A7A4u, 0x25E3A8A4u };
    static const unsigned src[3] = { 0x002987B0u, 0x002987C0u, 0x002987D0u };
    if (item < 0 || item > 2) return;
    box_bytes(h, d, src[item], dst[item], 8, 2, base);
}

void hudgen_pause_open(HudGen *h, Drive *d)                                              /* pause_display 0x06013BA4 */
{
    memset(d->pause, 0, sizeof d->pause);
    box_bytes(h, d, 0x002987A0u, 0x25E3A5A4u, 8, 8, 0x368);
}

void hudgen_pause_close(HudGen *h)                                                       /* pause_delete 0x06013BC0 */
{
    for (int r = 0; r < 8; r++) for (int k = 0; k < 8; k++) put(h, 0x25E3A5A4u + 0x80u * (unsigned)r + 2u * (unsigned)k, 0);
}

void hudgen_pause_cursor(HudGen *h, Drive *d)                                            /* pause_cursor 0x06013BCC: the item row blinks (8 passes in the highlight palette, 8 in the normal one) */
{
    int item = (signed char)d->pause[0], prev = (signed char)d->pause[1];
    if (prev != item) {
        pause_item(h, d, prev, 0x368);
        d->pause[1] = (unsigned char)item;
        d->pause[2] = 0;
    }
    int on = !(d->pause[2] & 8);
    d->pause[2]++;
    pause_item(h, d, item, on ? 0x1368 : 0x368);
}

/* ---- the time attack's display set-up with the race's values (init_game_disp 0x06011660 / 0x06011694; the static part is hud_init.dat): the right-hand panel's times, the boxes of the car and of
 * the transmission, the record box ---- */
static void cells32(HudGen *h, unsigned a, int cols, int rows, unsigned start, unsigned base)                    /* boxwrt_a_nmap 0x06005704: 32-bit cells, a running number (step 2) + base */
{
    for (int r = 0; r < rows; r++) for (int k = 0; k < cols; k++) put0(h, a + 0x100u * (unsigned)r + 4u * (unsigned)k, base + start + 2u * (unsigned)(r * cols + k));
}

static void cells32_table(HudGen *h, const Drive *d, unsigned table, unsigned a, int cols, int rows, unsigned base)    /* boxwrt_a4 0x060056C4: a byte table (x 2) + base */
{
    for (int r = 0; r < rows; r++) for (int k = 0; k < cols; k++) put0(h, a + 0x100u * (unsigned)r + 4u * (unsigned)k, base + 2u * (unsigned)phys_r8(d->phys, table + (unsigned)(r * cols + k)));
}

static void car_boxes(HudGen *h, const Drive *d)                                         /* FUN_0601192C: the transmission box (twice: the two planes) and the car box */
{
    const unsigned char *c = d->car;
    int ct = c[0x5C], setup = c[0x5F], mt = c[0xF4];
    unsigned tr = setup ? (mt ? 36u : 24u) : (mt ? 12u : 0u);
    cells32(h, 0x25E28198u, 3, 2, tr, 0x002004FEu);
    cells32(h, 0x25E2C198u, 3, 2, tr, 0x002004FEu);
    unsigned cr = ct == 0 ? 0u : ct == 1 ? 12u : 24u;
    cells32(h, 0x25E28188u, 3, 2, cr, 0x0070052Eu);
    cells32(h, 0x25E2C188u, 3, 2, cr, 0x0070052Eu);
}

/* record_sub 0x060118FC (r5 = the box's address, one plane): the box of the course record by the flag byte of R+0x70: 0 the empty box, 64 the long text, else the short one */
static void record_box(HudGen *h, const Drive *d, unsigned a, int clear_first)
{
    int flag = d->car[O_R + 0x70];
    if (clear_first)
        for (int r = 0; r < 2; r++) for (int k = 0; k < 10; k++) put0(h, a + 0x100u * (unsigned)r + 4u * (unsigned)k, 0);       /* (the static screen holds the empty record box: the shorter "your record" box leaves the rest empty) */
    if (flag == 0) cells32(h, a, 10, 2, 0, 0x00300478u);
    else if (flag == 64) cells32_table(h, d, 0x0029877Cu, a, 10, 2, 0x0030046Au);
    else cells32_table(h, d, 0x00298790u, a, 8, 2, 0x0030046Au);
}

void hudgen_ta_init(HudGen *h, const Drive *d)
{
    const unsigned char *c = d->car;
    if (d->cfg.game_type != 4 && d->cfg.game_type != 6) return;
    time1x1(h, 0x25E3A2C2u, c + O_R + 0x30);                                                      /* FUN_06011CBC / 0x0601169E: the best lap */
    if (d->cfg.game_type == 4) {
        time1x1(h, 0x25E3A442u, c + O_R + 0x24);                                                  /* the best time */
        time1x1(h, 0x25E3A5C2u, c + O_R + 0x70);                                                  /* the course record */
    }
    car_boxes(h, d);
    if (d->cfg.game_type == 4) {
        record_box(h, d, 0x25E28984u, 1);
        record_box(h, d, 0x25E2C984u, 1);
    }
}

/* 0x06012F84 (result_display, game type 4): the control byte >= 8 (goal): the jingle, a new best total time lights up the retime display, 210 passes later the next screen */
static void ta_end(HudGen *h, Drive *d)
{
    unsigned char *c = d->car;
    switch (d->ret_state) {
    case 0:                                                                            /* 0x06012F8C */
        d->g803 &= ~0x40;
        se_call(&d->se, SND_CMD, 16, 0, 0);
        h->nscroll[0] = 0x400; d->ret_state = 2; d->cd_timer = 210; SET_MSG(d, 36);
        h->bgon = 0x15;
        se_call(&d->se, SND_CDPLAY, 6, 0, 0);
        se_call(&d->se, SND_FA4, -64, 0, 0);
        break;
    case 2: {                                                                          /* 0x06012FCC */
        int total = g32(c, O_R + 12), best = g32(c, O_R + 36), flag = 0, rec = 1;
        d->ret_state += 2; d->cd_timer--;
        if (total >= best || d->cfg.mirror) break;                                     /* not a new best (R+0x24 = the stored one) */
        d->g301 = 2; d->g302 |= 2;
        if (total < d->cfg.rec184) flag = 0;
        else if (total < d->cfg.rec18c) flag = -128;
        else if (total < d->cfg.rec188) flag = 64;
        else rec = 0;                                                                  /* not a course record: the total only */
        if (rec) {
            p32(c, O_R + 112, total | (int)((unsigned)flag << 24));
            d->g302 |= 4;
        }
        retime_etc(h, d);                                                              /* [V: the oracle's pass of the new best runs the blink machine twice: state 2 (draw) and state 4 (draw, the 10 pass timer) */
        break;
    }
    case 4:                                                                            /* 0x06013034 */
        if (--d->cd_timer == 0) d->ret_state += 2;                                      /* (fade_color_init(2): the fade is not ported) */
        break;
    case 6:                                                                            /* 0x06012F58 */
        h->done = 1;
        if (!(d->g803 & 0x20)) d->g803 |= 2;
        break;
    default: break;
    }
}

/* ---- time attack (game types 4 and 6): game_display's branches 0x06012472 / 0x060124D2 (no gear, no time limit) ---- */
static void timeattack_pass(HudGen *h, Drive *d, unsigned frame)
{
    const unsigned char *c = d->car;
    if (d->hud_lapnum) {                                                                  /* lap_rewrite 0x06012304 (called by the free run's new-lap code): the lap just finished in the total's place */
        time_row(h, 0x25E3A106u, 0x25E3A186u, d->hud_laptime, d->hud_laptime[1] >= 16 ? 2 : 1);
        d->hud_lapnum = 0;
    }
    if (d->hud_area) {                                                                    /* area_rewrite 0x06012334: a section time in a lap row */
        unsigned r9 = (unsigned)phys_r32(d->phys, 0x00298570u + 8u * (unsigned)d->hud_arearow), r10 = (unsigned)phys_r32(d->phys, 0x00298570u + 8u * (unsigned)d->hud_arearow + 4u);
        time_row(h, r9, r10, d->hud_areatime, 0);
        d->hud_area = 0;
    }
    wrong_way(h, d, frame);
    dial(h, d);
    speed(h, d);
    if (d->cfg.game_type == 4) lap_times(h, d, g16(c, O_LAPNEXT), frame);
    else free_run_times(h, d);
    navigator(h, d);
    if (d->cfg.game_type == 6) timecmp_f(h, d);
    else timecmp(h, d);
    retime_etc(h, d);
}

/* ---- a replay (play mode 2): display3::replay_display 0x06013A84 (the blinking REPLAY sign, the window of the lap row), resreplay_display 0x06014138 (the end), the display set-up's times ---- */
static void cells16(HudGen *h, unsigned a, int cols, int rows, unsigned base)          /* boxwrt_d_nmap 0x0600571C: 16-bit cells, a running number (step 2) + base, rows 0x80 apart */
{
    for (int r = 0; r < rows; r++) for (int k = 0; k < cols; k++) put(h, a + 0x80u * (unsigned)r + 2u * (unsigned)k, (int)(base + 2u * (unsigned)(r * cols + k)));
}

static void replay_display(HudGen *h, const Drive *d, unsigned frame)
{
    unsigned tbl = 0x06013B74u + 8u * (unsigned)g16(d->car, O_LAPNEXT);                    /* window 1 (registers 0x25F800C8 ..): one row of the table per lap */
    for (int k = 0; k < 4; k++) h->win[k] = (unsigned short)phys_r16s(d->phys, tbl + 2u * (unsigned)k);
    int ctrl = g8(d->car, O_CTRL), chase = d->rp_chase;
    if (frame & 8) {                                                                       /* the sign is off: its box cleared */
        h->wctlb = ctrl == 8 ? 0 : chase ? 0x88 : 0x0A;
        for (int r = 0; r < 3; r++) for (int k = 0; k < 12; k++) put(h, 0x25E3CC3Cu + 0x80u * (unsigned)r + 2u * (unsigned)k, 0);
    } else {
        h->wctlb = (chase || ctrl == 8) ? 0 : 0x82;
        cells16(h, 0x25E3CC3Cu, 12, 3, 0x41B4u);
    }
}

static void resreplay_display(HudGen *h, Drive *d)                                      /* 0x06014138: the end of a replay: the picture of NBG0 for 90 passes, then 120 more, then the screen is over */
{
    switch (d->ret_state) {
    case 0:                                                                            /* 0x06014150 */
        d->g803 &= ~0x40;
        h->nscroll[0] = 0x400; d->ret_state = 2; d->cd_timer = 90;
        h->bgon = 0x1D;
        break;
    case 2:                                                                            /* 0x060141B8 */
        if (--d->cd_timer != 0) break;
        d->ret_state = 4; d->cd_timer = 120;
        break;
    case 4:                                                                            /* 0x060141FC */
        if (--d->cd_timer != 0) break;
        d->ret_state = 6;
        break;
    case 6:                                                                            /* 0x06014214 */
        h->done = 1;
        if (!(d->g803 & 0x20)) d->g803 |= 2;
        break;
    default: break;
    }
}

void hudgen_replay_init(HudGen *h, const Drive *d)                                      /* record_sub 0x06011DEE in play mode 2: the total and the lap times of the replayed run (R+0x8C = the total, R+0x90 .. the laps) */
{
    const unsigned char *c = d->car;
    if (d->cfg.game_type != 4) return;
    car_boxes(h, d);
    const unsigned char *t = c + O_R + 0x8C;
    time_row(h, 0x25E3A106u, 0x25E3A186u, t, t[1] >= 16 ? 2 : 1);
    for (int k = 0; k < d->total_laps; k++)
        time_row(h, (unsigned)phys_r32(d->phys, 0x00298570u + 8u * (unsigned)k), (unsigned)phys_r32(d->phys, 0x00298570u + 8u * (unsigned)k + 4u), c + O_R + 0x90 + 4 * k, 0);
}

void hudgen_pass(HudGen *h, Drive *d, unsigned frame)
{
    const unsigned char *c = d->car;
    if (h->replay) {                                                                      /* game_display's replay branches (0x060124B0 for the time attack): the sign, then the dial and the speed; the end state machine after the goal */
        replay_display(h, d, frame);
        if (g8(c, O_CTRL) >= 8) resreplay_display(h, d);
        else { dial(h, d); speed(h, d); }
        return;
    }
    if (g16u(c, O_MODE) == 2) return;                                                     /* (the demo is not ported) */
    if (d->lap_redraw) { lap_times(h, d, d->lap_redraw - 1, frame); d->lap_redraw = 0; }       /* 0x060121BE, called by the new-lap code and by goali_plmove (time attack) */
    if (g8(c, O_CTRL) >= 8) {
        if (d->cfg.game_type >= 4) { dial(h, d); speed(h, d); ta_end(h, d); retime_etc(h, d); return; }      /* time attack: 0x060124A0 */
        hudend_pass(h, d);
        return;
    }
    if (d->cfg.game_type >= 4) { timeattack_pass(h, d, frame); return; }
    wrong_way(h, d, frame);
    dial(h, d);
    gear(h, d);
    speed(h, d);
    remaining(h, d);
    lap_times(h, d, g16(c, O_LAPNEXT), frame);
    navigator(h, d);
    timecmp(h, d);                                                                         /* 0x06012664: the compare with the best run (while R+1 is not 0) */
}

/* the NBG0 pages the display set-up leaves (static parts of the goal / game-over pictures; hude_init.dat next to the main file, tools/oracle.py hudinit): sparse, 8 set-ups like hud_init.dat */
static void load_end_init(HudGen *h, const char *path, int idx)
{
    char epath[256];
    const char *name = strstr(path, "hud_init.dat");
    if (!name || strlen(path) + 2 > sizeof epath) return;
    snprintf(epath, sizeof epath, "%.*shude_init.dat", (int)(name - path), path);
    FILE *f = fopen(epath, "rb");
    if (!f) return;
    unsigned char hdr[4], rec[8];
    int ok = fread(hdr, 1, 4, f) == 4 && memcmp(hdr, "HUE1", 4) == 0;
    for (int set = 0; ok && set <= idx; set++) {
        unsigned char nb[4];
        ok = fread(nb, 1, 4, f) == 4;
        unsigned count = ok ? (unsigned)nb[0] | (unsigned)nb[1] << 8 | (unsigned)nb[2] << 16 | (unsigned)nb[3] << 24 : 0;
        for (unsigned k = 0; ok && k < count; k++) {
            ok = fread(rec, 1, 8, f) == 8;
            if (ok && set == idx) {
                unsigned i = (unsigned)rec[0] | (unsigned)rec[1] << 8 | (unsigned)rec[2] << 16 | (unsigned)rec[3] << 24;
                if (i < HUDE_CELLS) h->nt0[i] = (unsigned)rec[4] | (unsigned)rec[5] << 8 | (unsigned)rec[6] << 16 | (unsigned)rec[7] << 24;
            }
        }
    }
    fclose(f);
}

int hudgen_load(HudGen *h, const char *path, int game_type, int course)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    unsigned char hdr[4];
    int replay = (game_type & HUD_REPLAY) != 0;
    game_type &= 0xFF;
    int idx = replay ? 16 + course : (game_type / 2) * 4 + course;                         /* the sets: game type 0, 2, 4, 6 (tools/oracle.py hudinit) x course, then the time attack replays */
    size_t n = HUD_NT_WORDS * 2 + 8;
    unsigned char *buf = (unsigned char *)malloc(n);
    int bad = !buf || fread(hdr, 1, 4, f) != 4 || memcmp(hdr, "HUD2", 4) != 0 || fseek(f, 4 + (long)(n * (size_t)idx), SEEK_SET) != 0 || fread(buf, 1, n, f) != n;
    fclose(f);
    if (!bad) {
        for (int i = 0; i < HUD_NT_WORDS; i++) h->w[i] = (unsigned short)(buf[2 * i] << 8 | buf[2 * i + 1]);
        for (int i = 0; i < 4; i++) h->scroll[i] = (unsigned short)(buf[2 * HUD_NT_WORDS + 2 * i] << 8 | buf[2 * HUD_NT_WORDS + 2 * i + 1]);
        memset(h->nt0, 0, sizeof h->nt0);
        load_end_init(h, path, idx);
        h->nscroll[0] = 0x200;                                                              /* what the screens before the race leave in [0x060AA864] (tools/hud.py setup) */
        h->nscroll[1] = 0;
        h->bgon = game_type >= 4 ? 0x1D : 0x1C;                                            /* the time attack's display set-up leaves NBG0 on as well */
        h->wctla = 0;
        h->done = 0;
        h->replay = replay;
    }
    free(buf);
    return bad;
}
