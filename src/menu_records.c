/* M8: the record screens (rec_main 0x0602CC74 .. rec_menu2 0x0602E8AC, rec_disp 0x0602EE6C, the cursor / mark actions 0x0602F02C .. 0x0602F490, FUN_06032D84) ported from the original.
 *
 * Screen structure: record_main (0x0602CC74) is a blocking function like the mode select: it loops over the original's per-frame handler until the exit state sets [0x060AA803] bit 1.
 *   [0x060A8C10] screen state: 0 = running, 1 = fade in, 2 = fade out.
 *   0x06040040 (P, the original's r14 in the page handlers) = the page state block, one byte per menu level: +0 level 0 (4 / 5 = the ARCADE / TIME ATTACK choice, 2 = idle on a page, 10 / 11 / 12 = slide),
 *      +1 arcade (page type), +2 arcade course list, +5 arcade course ranking, +3 time attack course (arcade: the car list), +4 time attack car, +6 time attack set-up
 *      ([0x060AA811]), +7 time attack transmission ([0x060AA809]);
 *      +8 mode (4 arcade, 5 time attack), +9 arcade page type (4 course, 5 car), +10 arcade course cursor, +11 time attack course / arcade car cursor, +12 car, +13 arcade ranking,
 *      +14 set-up, +15 transmission cursor (4 = "all" = the lists are merged, 5 .. = the entry, 7 only with the unlock bits).
 *   0x060A8C14 (Q, the original's r13): +0 header redraw request, +1 page change request, +2 page change state.
 *   0x060A8C10 / 12 / 16 screen / change / request state, 0x060A8C30 the slide position of the page, 0x060A8C34 the slide position of the header, 0x060A8C38 the blink counter,
 *   0x060A8C39 the row of the player's own record (-1 = none).
 * The keys: [0x060AA994] is cleared every pass, key_repeat gives the direction bits with repeat (r12 of the original: 0x1000 up, 0x2000 down, 0x4000 left, 0x8000 right, buttons from 0x0100). */
#include "satlib.h"
#include "menu_lib.h"
#include "satmenu.h"
#include "menu_records.h"

#define P_BASE 0x06040040u
#define Q_BASE 0x060A8C14u
#define PB(i)      ((s32)RS8(P_BASE + (i)))
#define PSET(i, v) W8(P_BASE + (i), (v))
#define QB(i)      ((s32)RS8(Q_BASE + (i)))
#define QSET(i, v) W8(Q_BASE + (i), (v))

#define K_UP    0x1000
#define K_DOWN  0x2000
#define K_LEFT  0x4000
#define K_RIGHT 0x8000

static u32 keys;                                        /* r12: key_repeat result of this pass */

/* ---- small helpers ---- */
static void pad_sound(void)                             /* 0x0602D7B0: a direction key was pressed */
{
    if (keys & 0xF000) snd_call(0x06016FBC, 72, 0, 0);
}

static void tile16(u32 r4, u32 r5, u32 r6, u32 r7)      /* 0x0602EF2E: r7 rows of r6 cells of 16 bits, numbered from r4, row = 0x80 bytes */
{
    do {
        u32 r3 = r6, r1 = r5;
        do { W16(r1, r4); r4 += 2; r1 += 2; } while (--r3);
        r5 += 0x80;
    } while (--r7);
}

static void tile32(u32 r4, u32 r5, u32 r6, u32 r7)      /* 0x0602EF4A: the same with 32-bit cells (row = 0x100 bytes) */
{
    do {
        u32 r3 = r6, r1 = r5;
        do { W32(r1, r4); r4 += 2; r1 += 4; } while (--r3);
        r5 += 0x100;
    } while (--r7);
}

static u32 swap_w(u32 v) { return (v << 16) | (v >> 16); }

/* the slide effect of the page / header: 4 line-scroll pairs from 0x25E7FE34 downwards (0x0602D6FA) */
static void R_SCR_SUB(s32 r4, s32 r6, s32 r7)           /* 0x0602D6FA */
{
    u32 r5 = 0x25E7FE34, r7s = (u32)r7 << 16;
    int i;
    for (i = 0; i < 4; i++) {
        s32 r9;
        if (r6 > 0) r9 = 0;
        else { r9 = r6; if (-256 >= r6) r9 = -256; }
        r9 = (s32)(((u32)r9 << 16) + r7s);
        r5 -= 4; W32(r5, (u32)r9);
        r5 -= 4; W32(r5, (u32)r9);
        r6 += r4;
    }
}

static s32 menu_scr_common(s32 d)                       /* 0x0602D72C / 0x0602D74C: the 9 words at 0x25E7FE30 downwards move by d << 16 */
{
    u32 r3 = (u32)d << 16, r1 = 0x25E7FE30, r0, r2;
    s32 c;
    r0 = R32(r1) + r3;
    W32(r1, r0);
    for (r2 = 8; r2 != 0; r2--) { r1 -= 4; W32(r1, r0); }
    c = RS8(0x060A8C3A) + 1;
    W8(0x060A8C3A, c);
    return c;
}
static s32 rec_menu_scrR(void) { return menu_scr_common(14); }              /* 0x0602D72C */
static s32 rec_menu_scrL(void) { return menu_scr_common(-14); }             /* 0x0602D74C */
static s32 rec_menu_scrRd(void) { W16(0x060AA878, RS16(0x060AA878) + 14); return menu_scr_common(14); }     /* 0x0602D750 */
static s32 rec_menu_scrLd(void) { W16(0x060AA878, RS16(0x060AA878) - 14); return menu_scr_common(-14); }    /* 0x0602D75A */

/* ---- the action objects (0x0601BB5C / 0x0601BB02): slots of 0x80 bytes, slot+0 = the action function, run with the slot as r14 / gbr ---- */
static u32 act_slot;                                    /* r14 of the running action */

static void act_init(u32 slot, u32 fn, u32 n)           /* 0x0601BB5C: r4 = function, r7 = number of slots, r14 = first slot */
{
    u32 r3 = 0, r5 = 0, r14 = slot, r0;
    do {
        W32(r14 + 0, fn);
        W32(r14 + 4, r5);
        W16(r14 + 68, 0);
        W16(r14 + 70, r3);
        r3++;
        r5 = r14;
        r0 = r14 + 0x80;
        W32(r14 + 8, r0);
        if (--n == 0) { W32(r14 + 8, 0); break; }
        r14 = r0;
    } while (1);
}

static void coract(void);
static void markact(void);
static void cmarkact(void);

static void act_run(u32 slot, u32 n)                    /* 0x0601BB02 */
{
    u32 r14 = slot;
    do {
        u32 fn = R32(r14);
        if (fn) {
            act_slot = r14;
            switch (fn) {
            case 0x0602F02C: coract(); break;
            case 0x0602F194: markact(); break;
            case 0x0602F2B4: cmarkact(); break;
            default: menu_unported(fn);
            }
        }
        r14 += 0x80;
    } while (--n);
}

/* the cursor frame sprites of the two top buttons / the page (0x0602F02C): slot+56 = position word, +60 = remembered cursor, +4 = frame counter, +68 = state */
static void coract_draw(void)                           /* 0x0602F0DC */
{
    u32 r14 = act_slot, r1 = 0x0602F140, r12, r3;
    s32 r0 = RS32(r14 + 8);
    if (r0 == 4) r1 += 36;
    else {
        r0 = RS32(r14 + 60);
        if (r0 != 5) {
            r1 += 12;
            if (r0 != 6) {
                r1 += 12;
                if (r0 != 7) {
                    r1 += 12;
                    if (R8(0x06040062) & 4) r1 += 20;
                }
            }
        }
    }
    r12 = R32(r1); r1 += 4;
    do {
        u32 r5, r2, r4;
        r3 = R32(r14 + 56);
        r5 = R32(r1); r1 += 4;
        r2 = (u32)(s32)RS16(r1) << 16; r1 += 2;
        r3 += r2;
        r2 = (u32)(s32)RS16(r1) << 16; r1 += 2;
        r3 = swap_w(r3);
        r3 += r2;
        r4 = swap_w(r3);
        draw_sprite(r4, r5, r12 << 16);
    } while (--r12);
}

static void coract(void)                                /* 0x0602F02C */
{
    u32 r14 = act_slot;
    s32 r1;
    switch (RS16(r14 + 68)) {
    case 0:
        W32(r14 + 56, 0x01080042);
        W32(r14 + 4, 12);
        W32(r14 + 60, (u32)PB(12));
        W32(r14 + 8, (u32)PB(8));
        W16(r14 + 68, 2);
        break;
    case 1:
        if (!((u32)PB(8) == R32(r14 + 8) && (u32)PB(12) == R32(r14 + 60))) {
            W32(r14 + 4, 12);
            W16(r14 + 68, 3);
        }
        break;
    case 2:
        W32(r14 + 56, R32(r14 + 56) + ((u32)-20 << 16));
        r1 = RS32(r14 + 4) - 1;
        if (r1 == 0) { W16(r14 + 68, 1); r1 = 1; }
        W32(r14 + 4, (u32)r1);
        break;
    case 3:
        r1 = RS32(r14 + 4);
        W32(r14 + 56, R32(r14 + 56) + ((u32)20 << 16));
        r1 -= 1;
        if (r1 == 0) { W16(r14 + 68, 0); }
        W32(r14 + 4, (u32)r1);
        break;
    default:
        menu_unported(0);
    }
    coract_draw();
}

static void markact_draw(void)                          /* 0x0602F280 */
{
    u32 r14 = act_slot;
    draw_sprite(R32(r14 + 56), R32(0x0602F2A4 + (((u32)RS32(r14 + 60) - 4) << 2)), 0x100);
}

static void markact(void)                               /* 0x0602F194: the mark of the highlighted course / car row */
{
    u32 r14 = act_slot;
    s32 r1;
    switch (RS16(r14 + 68)) {
    case 0:
        W32(r14 + 4, 12);
        W32(r14 + 56, 0x010C0053);
        W16(r14 + 68, 3);
        if (PB(8) == 4 && PB(9) == 4) W16(r14 + 68, 1);
        W32(r14 + 60, (u32)PB(11));
        break;
    case 1:
        if (!(PB(8) == 4 && PB(9) == 4)) {
            W32(r14 + 56, R32(r14 + 56) + 0x00F00000u);
            W32(r14 + 4, 24);
            W16(r14 + 68, 3);
        }
        break;
    case 2:
        if (!(PB(8) == 4 && PB(9) == 4)) {
            if ((u32)PB(11) == R32(r14 + 60)) break;
        }
        W32(r14 + 4, 12);
        W16(r14 + 68, 4);
        break;
    case 3:
        W32(r14 + 56, R32(r14 + 56) + ((u32)-20 << 16));
        r1 = RS32(r14 + 4) - 1;
        if (r1 == 0) { W16(r14 + 68, 2); r1 = 2; }
        W32(r14 + 4, (u32)r1);
        break;
    case 4:
        r1 = RS32(r14 + 4);
        W32(r14 + 56, R32(r14 + 56) + ((u32)20 << 16));
        r1 -= 1;
        if (r1 == 0) { W16(r14 + 68, 0); }
        W32(r14 + 4, (u32)r1);
        break;
    default:
        menu_unported(0);
    }
    markact_draw();
}

static void cmark_blink(u32 r4, u32 r5, u32 r6)         /* 0x0602F394 */
{
    if ((R8(0x060A8C38) >> 4) & 1) draw_sprite(r4, r5, r6 << 16);
}

static void cmarkact(void)                              /* 0x0602F2B4: the up / down arrows beside the lists */
{
    u32 r14 = act_slot;
    s32 r8 = RS32(r14 + 56), r9 = RS32(r14 + 60);
    u32 held;
    int st = RS16(r14 + 68);
    switch (st) {
    case 0:
        r8 = 0; r9 = 0;
        W16(r14 + 68, 1);
        /* fall through */
    case 1:
    case 4: {
        s32 r2;
        held = (u32)RS16(0x060AA990) & 0xFFFF;
        if (r8 > 0 && !(held & 0x0090)) {
            cmark_blink(0xFFACFFFE, 0x060485E8, 16);
            if (held & 0x0048) {
                r9 = 8;
                W16(r14 + 68, 3);
                draw_sprite(0xFFACFFFE, 0x060485E8, 0x00010000);
                r8 += -2;
                r9 -= 1;
                if (r9 == 0) W16(r14 + 68, 1);
                break;
            }
        }
        if (PB(8) == 5) break;
        r9 = 8;
        r2 = 48;
        if (PB(9) == 4) r2 += 128;
        if (r8 == r2) break;
        if (held & 0x0048) break;
        cmark_blink(0xFFAC005A, 0x06048620, 17);
        if (!(held & 0x0090)) break;
        W16(r14 + 68, 2);
        draw_sprite(0xFFAC005A, 0x06048620, 0x00020000);
        r8 += 2;
        r9 -= 1;
        if (r9 == 0) W16(r14 + 68, 1);
        break;
    }
    case 2:
        draw_sprite(0xFFAC005A, 0x06048620, 0x00020000);
        r8 += 2;
        r9 -= 1;
        if (r9 == 0) W16(r14 + 68, 1);
        break;
    case 3:
        draw_sprite(0xFFACFFFE, 0x060485E8, 0x00010000);
        r8 += -2;
        r9 -= 1;
        if (r9 == 0) W16(r14 + 68, 1);
        break;
    default:
        menu_unported(0);
    }
    W32(r14 + 56, (u32)r8);
    W32(r14 + 60, (u32)r9);
    W16(0x060AA870, (u32)r8);
}

/* ---- palette animation (0x0602EF68 with r14 = 0x060E4000): the highlight colours of the page ---- */
static u32 mix555(u32 a, u32 b, s32 t)                  /* 0x0601DB9A */
{
    static const u32 mask[3] = { 0x7C00, 0x03E0, 0x001F };
    u32 out = 0;
    int k;
    for (k = 0; k < 3; k++) {
        s32 x = (s32)(a & mask[k]), y = (s32)(b & mask[k]);
        s32 d = (s32)((u32)(y - x) << 16);
        s32 hi = (s32)(((s64)d * (s64)t) >> 32);
        out |= (u32)(hi + x) & mask[k];
    }
    return out;
}

static void pal_mix(u32 pa, u32 pb, u32 dst, u32 n, s32 t)   /* 0x0601DB80 */
{
    while (n--) {
        W16(dst, mix555((u32)RS16(pa), (u32)RS16(pb), t));
        pa += 2; pb += 2; dst += 2;
    }
}

static void pal_anim(void)                              /* 0x0602EF68 */
{
    const u32 b = 0x060E4000;
    int guard = 0;
    for (;;) {
        u32 r4 = R32(b + 4), r5 = R32(b + 8);
        u32 step, coef;
        if (++guard > 100) menu_unported(0x0602EF68);
        if (R32(b + 28) == 1) { r4 = R32(b + 8); r5 = R32(b + 24); }
        else if (R32(b + 28) == 2) { r4 = R32(b + 24); r5 = R32(b + 4); }
        step = R32(b + 20);
        coef = R16(0x0602EFBC + 2 * step);
        step++;
        if ((s32)coef > 0) {
            W32(b + 20, step);
            pal_mix(r4, r5, R32(b + 12), R32(b + 16), (s32)coef);
            return;
        }
        W32(b + 20, 0);
        {
            u32 phase = R32(b + 28) + 1;
            if (phase == 3) phase = 0;
            W32(b + 28, phase);
        }
    }
}

/* ---- the buttons at the top of the page (0x0602E754 record_menu_init, FUN_06032D84): groups of items from a table, each item = the cell numbers of a 12 x 2 button ---- */
static u32 menu_table(void)                             /* 0x0602E754: the button table of the current mode / page type */
{
    if (PB(8) != 4) return 0x0602E864;
    if (PB(9) != 4) return 0x0602E834;
    return 0x0602E7FC;
}

static void record_menu_init(void)                      /* 0x0602E754 */
{
    u32 r14 = menu_table(), r13, r11, r10, r9, r8, r7;
    r13 = R32(r14); r14 += 4;
    for (;;) {
        r11 = R8(r14); r10 = R8(r14 + 1); r9 = R8(r14 + 2); r8 = R8(r14 + 3);
        r14 += 4;
        r7 = r11;
        do {
            if (r9 == 1 || (r9 == 2 && r7 == 1)) {
                if (!(R8(0x06040062) & r8)) { r14 += 4; continue; }
            }
            {
                u32 r4 = (u32)(s32)RS16(r14), r2 = 0x5000, r5;
                s32 r0;
                r14 += 2;
                r0 = PB(r10 & 0xF) - 4;
                if ((s32)(r11 - r7) == r0) r2 = 0x4000;
                r5 = (u32)(s32)RS16(r14); r14 += 2;
                r4 += r2;
                tile16(r4, r5 + 0x25E3C000u, 12, 2);
            }
        } while (--r7);
        if (--r13 == 0) break;
    }
}

static void record_head_draw(void)                      /* 0x06032D84 (res_timeact module): the buttons of the page, 16-bit cells first, then 32-bit */
{
    u32 r14, r13, r12 = 1, r11, r10, r9, r8, r7;
    box_fill32(0, 0x25E32500, 0x00400008);
    if (PB(8) != 4) r14 = 0x06032F08;
    else if (PB(9) != 4) r14 = 0x06032ED8;
    else r14 = 0x06032EA0;
    r13 = R32(r14); r14 += 4;
    for (;;) {
        r11 = R8(r14); r10 = R8(r14 + 1); r9 = R8(r14 + 2); r8 = R8(r14 + 3);
        r14 += 4;
        r7 = r11;
        do {
            int skip = 0;
            if (r9 == 1) {
                if (!(R8(0x06040062) & r8)) skip = 1;
            } else {
                if (r9 == 3) {
                    if (PB(12) == 7) {
                        if (r7 == 1 || r7 == 3) { r14 += 4; continue; }
                    }
                }
                if (r9 == 2 && r7 == 1) {
                    if (!(R8(0x06040062) & r8)) skip = 1;
                }
            }
            if (skip) { r14 += 4; continue; }
            {
                u32 r4 = (u32)(s32)RS16(r14), r2 = 0x5000, r5;
                s32 r0;
                r14 += 2;
                r0 = PB(r10 & 0xF) - 4;
                if ((s32)(r11 - r7) == r0) {
                    r2 = 0x4000;
                    r0 = PB((r10 & 0xF) - 8);
                    if (!(r0 == 0 || r0 == 2 || r0 == 8 || r0 == 9)) r2 = 0x7000;
                }
                r5 = (u32)(s32)RS16(r14); r14 += 2;
                if (r12 > 0) {
                    r4 += r2;
                    tile16(r4, r5 + 0x25E3C000u, 12, 2);
                } else {
                    r2 <<= 8;
                    r4 += r2;
                    tile32(r4, r5 + 0x25E30000u, 12, 2);
                }
            }
        } while (--r7);
        r12 = 0;
        if (--r13 == 0) break;
    }
}

/* ---- page change (0x0602E49C): the page slides out, the next one is built, it slides in ---- */
static void line_scroll(s32 r4, s32 r6)                 /* 0x0602E53C: 5 line scroll pairs (page / shadow) from the slide position r6, step r4 */
{
    u32 r5 = 0x25E7FC64, r3 = 0x25E7FE64;
    s32 r1;
    for (r1 = 5; r1 != 0; r1--) {
        s32 r9 = r6;
        if (r6 <= 0) r9 = 0;
        else if (r6 >= 0x100) r9 = 0x100;
        r9 = (s32)((u32)r9 << 16);
        r5 -= 4; W32(r5, (u32)r9);
        r5 -= 4; W32(r5, (u32)r9);
        {
            s32 p0 = PB(0);
            if (!(p0 == 5 || p0 == 11 || p0 == 12 || r1 == 2)) r9 = 0;
        }
        r9 = -r9;
        r3 -= 4; W32(r3, (u32)r9);
        r3 -= 4; W32(r3, (u32)r9);
        r6 += r4;
    }
}

static void record_change(void)                         /* 0x0602E49C */
{
    s32 v;
    switch (RS8(0x060A8C12)) {
    case 0:
        W8(0x060A8C38, 0);
        v = RS32(0x060A8C30) + 40;
        W32(0x060A8C30, (u32)v);
        line_scroll(48, v);
        if (v >= 0x120) {
            W8(0x060A8C12, 1);
            W8(0x060A8C16, 2);
            W32(0x060A8C30, 0x260);
        }
        break;
    case 1:
        v = RS32(0x060A8C30) - 40;
        W32(0x060A8C30, (u32)v);
        line_scroll(-48, v);
        if (0 >= v) W8(0x060A8C12, 2);
        break;
    case 2: {
        u32 r1 = 0x25E7FC6C, r2 = 0x25E7FE6C;
        int i;
        W32(0x060A8C30, 0xFFFFFF40u);
        for (i = 0; i < 12; i++) { r1 -= 4; W32(r1, 0); r2 -= 4; W32(r2, 0); }
        W8(0x060A8C15, 0);
        W8(0x060A8C16, 0);
        W8(0x060A8C12, 0);
        break;
    }
    default:
        menu_unported(0x0602E49C);
    }
}

/* ---- page requests (0x0602E1F8): the page type and list for the cursor positions, drawn while the page is off screen ---- */
static void record_request(void)                        /* 0x0602E1F8 */
{
    u32 r4, r5;
    switch (RS8(0x060A8C16)) {
    case 0:
        return;
    case 1:
        record_change();
        return;
    case 2:
        break;
    default:
        menu_unported(0x0602E1F8);
    }
    /* 0x0602E22C */
    if (PB(8) == 4) {
        if (PB(9) == 4) {
            if (PB(13) == 4) {
                r4 = ((u32)(PB(10) - 4) << 3) + 0x06041000;
                r5 = record_merge16(r4);
                r4 = 3;
            } else {
                r4 = 0;
                r5 = (u32)(PB(10) - 4) << 3;
                r5 += (u32)(PB(13) - 5) << 2;
            }
        } else {
            r4 = 1;
            r5 = (u32)(PB(11) - 4) << 2;
        }
    } else if (PB(12) == 4) {
        if (PB(14) == 4) {
            if (PB(15) == 4) {
                r4 = ((u32)(PB(11) - 4) << 6) + 0x06041020;
                r5 = record_sort_c(r4);
            } else {
                r4 = ((u32)((PB(11) - 4) << 4) + (u32)((PB(15) - 5))) << 2;
                r4 += 0x06041020;
                r5 = record_sort_b(r4, 8);
            }
        } else if (PB(15) == 4) {
            r4 = ((u32)((PB(11) - 4) << 4) + (u32)((PB(14) - 5) << 1)) << 2;
            r4 += 0x06041020;
            r5 = record_sort_b(r4, 4);
        } else {
            r4 = (u32)(((PB(11) - 4) << 4) + ((PB(14) - 5) << 1) + (PB(15) - 5)) << 2;
            r4 += 0x06041020;
            r5 = record_merge3(r4, 0x060A8CF0);
        }
        r4 = 5;
    } else if (PB(14) == 4) {
        if (PB(15) == 4) {
            r4 = ((u32)(((PB(11) - 4) << 4) + ((PB(12) - 5) << 2))) << 2;
            r4 += 0x06041020;
            r5 = record_sort_a(r4);
        } else {
            r4 = (u32)(((PB(11) - 4) << 4) + ((PB(12) - 5) << 2) + (PB(15) - 5)) << 2;
            r4 += 0x06041020;
            r5 = record_merge2(r4, r4 + 8, 0x060A8CF0);
        }
        r4 = 5;
    } else if (PB(15) == 4) {
        r4 = (u32)(((PB(11) - 4) << 4) + ((PB(12) - 5) << 2) + ((PB(14) - 5) << 1)) << 2;
        r4 += 0x06041020;
        r5 = record_merge2(r4, r4 + 4, 0x060A8CF0);
        r4 = 5;
    } else {
        r4 = 2;
        r5 = (u32)(((PB(11) - 4) << 4) + ((PB(12) - 5) << 2) + ((PB(14) - 5) << 1) + (PB(15) - 5)) << 2;
    }
    W32(0x060A8C1C, r4);
    W32(0x060A8C20, r5);
    box_fill16(0, 0x25E38782, 0x00170020);
    box_fill32(0, 0x25E32F60, 0x00120006);
    W16(0x060E4244, 0);
    record_page_list(R32(0x060A8C1C), R32(0x060A8C20));
    record_change();
}

/* ---- the page state machines: levels 0x0602D5A0 (record_R) down to 0x0602E11C (record_RTxxx).  Each level's states: 0 / 1 / 2 / 3 / 10 = enter (set the page, cursor of the level above), 4 .. 7 = the cursor positions,
 * 8 / 9 = slide out to the left / right, 11 / 12 = slide of the whole page.  The `else` chains keep the original's order of tests. ---- */
static void refresh_head(void)                          /* 0x06032D84 called with r11..r14 saved (the original pushes and pops them around the call) */
{
    record_head_draw();
}

static void rec_slide_start(void)                       /* 0x0602D69C: the header buttons slide away */
{
    W32(0x060A8C34, 0x90);
    QSET(1, 1);
    PSET(0, 11);
}

static void record_RTxxx(void)                          /* 0x0602E11C: time attack, transmission level (P+7) */
{
    s32 r0 = -1;
    switch (PB(7)) {
    case 0: PSET(7, 2); return;
    case 1: PSET(7, PB(15)); return;
    case 2: case 3: return;
    case 10: PSET(7, 2); return;
    case 4:
        pad_sound();
        if (keys & K_DOWN) r0 = 5; else if (keys & K_UP) r0 = 6;
        break;
    case 5:
        pad_sound();
        if (keys & K_DOWN) r0 = 6; else if (keys & K_UP) r0 = 4;
        break;
    case 6:
        pad_sound();
        if (keys & K_DOWN) r0 = 4; else if (keys & K_UP) r0 = 5;
        break;
    default:
        menu_unported(0x0602E11C);
    }
    if (r0 >= 0) {
        PSET(7, r0); PSET(15, r0);
        QSET(0, 1); QSET(1, 1);
        return;
    }
    if (keys & K_LEFT) {
        QSET(0, 1);
        PSET(6, 1);
        PSET(7, 2);
    }
}

static void record_RTxx(void)                           /* 0x0602DFF0: time attack, car level (P+6) */
{
    s32 r0 = -1;
    switch (PB(6)) {
    case 0: PSET(6, 2); record_RTxxx(); return;
    case 1: PSET(6, PB(14)); record_RTxxx(); return;
    case 2: case 3: record_RTxxx(); return;
    case 10: PSET(6, 2); record_RTxxx(); return;
    case 4:
        pad_sound();
        if (keys & K_DOWN) r0 = 5; else if (keys & K_UP) r0 = 6;
        break;
    case 5:
        pad_sound();
        if ((keys & K_DOWN) && PB(12) != 7) r0 = 6;
        else if ((keys & K_UP) && PB(12) != 7) r0 = 4;
        break;
    case 6:
        pad_sound();
        if (keys & K_DOWN) r0 = 4; else if (keys & K_UP) r0 = 5;
        break;
    case 9:
        if (rec_menu_scrL() == 8) { PSET(4, 1); PSET(6, 2); }
        record_RTxxx();
        return;
    default:
        menu_unported(0x0602DFF0);
    }
    if (r0 >= 0) {
        PSET(6, r0); PSET(14, r0);
        QSET(0, 1); QSET(1, 1);
        record_RTxxx();
        return;
    }
    if (keys & K_RIGHT) {
        QSET(0, 1);
        PSET(7, 1);
        PSET(6, 2);
    }
    if (!(keys & K_RIGHT) && (keys & K_LEFT)) {
        QSET(0, 1);
        PSET(4, 3);
        PSET(6, 9);
        W8(0x060A8C3A, 0);
    }
    record_RTxxx();
}

static void record_RTx(void)                            /* 0x0602DE6C: time attack, course level (P+4) */
{
    s32 r0 = -1;
    switch (PB(4)) {
    case 0: PSET(4, 2); record_RTxx(); return;
    case 1: PSET(4, PB(12)); record_RTxx(); return;
    case 2: case 3: record_RTxx(); return;
    case 10: PSET(4, 2); record_RTxx(); return;
    case 4:
        pad_sound();
        if (keys & K_DOWN) r0 = 5;
        else if (keys & K_UP) {
            if (R8(0x06040062) & 4) { PSET(14, 5); r0 = 7; } else r0 = 6;
        }
        break;
    case 5:
        pad_sound();
        if (keys & K_DOWN) r0 = 6; else if (keys & K_UP) r0 = 4;
        break;
    case 6:
        pad_sound();
        if (keys & K_DOWN) {
            if (R8(0x06040062) & 4) { PSET(14, 5); r0 = 7; } else r0 = 4;
        } else if (keys & K_UP) r0 = 5;
        break;
    case 7:
        pad_sound();
        if (keys & K_DOWN) r0 = 4; else if (keys & K_UP) r0 = 6;
        break;
    case 8:
        if (rec_menu_scrR() == 8) { PSET(6, 1); PSET(4, 2); }
        record_RTxx();
        return;
    case 9:
        if (rec_menu_scrLd() == 8) { PSET(3, 1); PSET(4, 2); }
        record_RTxx();
        return;
    default:
        menu_unported(0x0602DE6C);
    }
    if (r0 >= 0) {
        PSET(4, r0); PSET(12, r0);
        QSET(0, 1); QSET(1, 1);
        record_RTxx();
        return;
    }
    if (keys & K_RIGHT) {
        QSET(0, 1);
        PSET(6, 3);
        PSET(4, 8);
        W8(0x060A8C3A, 0);
    }
    if (keys & K_LEFT) {
        QSET(0, 1);
        PSET(3, 3);
        PSET(4, 9);
        W8(0x060A8C3A, 0);
    }
    record_RTxx();
}

static void record_RT(void)                             /* 0x0602DD10: time attack, level 1 (P+3) */
{
    s32 r0 = -1;
    switch (PB(3)) {
    case 0: PSET(3, 2); record_RTx(); return;
    case 1: PSET(3, PB(11)); record_RTx(); return;
    case 2: case 3: record_RTx(); return;
    case 10: PSET(3, 2); record_RTx(); return;
    case 4:
        pad_sound();
        if (keys & K_DOWN) r0 = 5;
        else if (keys & K_UP) r0 = (R8(0x06040062) & 2) ? 7 : 6;
        break;
    case 5:
        pad_sound();
        if (keys & K_DOWN) r0 = 6; else if (keys & K_UP) r0 = 4;
        break;
    case 6:
        pad_sound();
        if (keys & K_DOWN) r0 = (R8(0x06040062) & 2) ? 7 : 4;
        else if (keys & K_UP) r0 = 5;
        break;
    case 7:
        pad_sound();
        if (keys & K_DOWN) r0 = 4; else if (keys & K_UP) r0 = 6;
        break;
    case 8:
        if (rec_menu_scrRd() == 8) { PSET(4, 1); PSET(3, 2); }
        record_RTx();
        return;
    default:
        menu_unported(0x0602DD10);
    }
    if (r0 >= 0) {
        PSET(3, r0); PSET(11, r0);
        QSET(0, 1); QSET(1, 1);
        record_RTx();
        return;
    }
    if (keys & K_RIGHT) {
        QSET(0, 1);
        PSET(4, 3);
        PSET(3, 8);
        W8(0x060A8C3A, 0);
    }
    if (keys & K_LEFT) {
        QSET(0, 1);
        PSET(0, 1);
        PSET(3, 2);
    }
    record_RTx();
}

static void record_RAP(void)                            /* 0x0602DBF8: arcade, car list (P+3) */
{
    s32 r0 = -1;
    switch (PB(3)) {
    case 0: PSET(3, 2); return;
    case 1: PSET(3, PB(11)); return;
    case 2: case 3: return;
    case 10: PSET(3, 2); return;
    case 4:
        pad_sound();
        if (keys & K_DOWN) r0 = 5;
        else if (keys & K_UP) r0 = (R8(0x06040062) & 2) ? 7 : 6;
        break;
    case 5:
        pad_sound();
        if (keys & K_DOWN) r0 = 6; else if (keys & K_UP) r0 = 4;
        break;
    case 6:
        pad_sound();
        if (keys & K_DOWN) r0 = (R8(0x06040062) & 2) ? 7 : 4;
        else if (keys & K_UP) r0 = 5;
        break;
    case 7:
        pad_sound();
        if (keys & K_DOWN) r0 = 4; else if (keys & K_UP) r0 = 6;
        break;
    default:
        menu_unported(0x0602DBF8);
    }
    if (r0 >= 0) {
        PSET(3, r0); PSET(11, r0);
        QSET(0, 1); QSET(1, 1);
        return;
    }
    if (keys & K_LEFT) {
        QSET(0, 1);
        PSET(1, 1);
        PSET(3, 2);
    }
}

static void record_RACx(void)                           /* 0x0602DB1C: arcade, course ranking (P+5) */
{
    s32 r0 = -1;
    switch (PB(5)) {
    case 0: PSET(5, 2); return;
    case 1: PSET(5, PB(13)); return;
    case 2: case 3: return;
    case 10: PSET(5, 2); return;
    case 4:
        pad_sound();
        if (keys & K_DOWN) r0 = 5; else if (keys & K_UP) r0 = 6;
        break;
    case 5:
        pad_sound();
        if (keys & K_DOWN) r0 = 6; else if (keys & K_UP) r0 = 4;
        break;
    case 6:
        pad_sound();
        if (keys & K_DOWN) r0 = 4; else if (keys & K_UP) r0 = 5;
        break;
    default:
        menu_unported(0x0602DB1C);
    }
    if (r0 >= 0) {
        PSET(5, r0); PSET(13, r0);
        QSET(0, 1); QSET(1, 1);
        return;
    }
    if (keys & K_LEFT) {
        QSET(0, 1);
        PSET(2, 1);
        PSET(5, 2);
    }
}

static void record_RAC(void)                            /* 0x0602DA0C: arcade, course level (P+2) */
{
    switch (PB(2)) {
    case 0: PSET(2, 2); break;
    case 1: PSET(2, PB(10)); break;
    case 2: case 3: break;
    case 10: PSET(2, 2); break;
    case 4:
    case 5: {
        s32 r0 = -1;
        pad_sound();
        if (keys & (K_UP | K_DOWN)) r0 = (PB(2) == 4) ? 5 : 4;
        if (r0 >= 0) {
            PSET(2, r0); PSET(10, r0);
            QSET(0, 1); QSET(1, 1);
            break;
        }
        if ((keys & K_RIGHT) && (R8(0x06040062) & 0x10)) {
            QSET(0, 1);
            PSET(5, 1);
            PSET(2, 2);
            break;
        }
        if (keys & K_LEFT) {
            QSET(0, 1);
            PSET(1, 3);
            PSET(2, 9);
            W8(0x060A8C3A, 0);
        }
        break;
    }
    case 9:
        if (R8(0x06040062) & 0x10) {
            if (rec_menu_scrLd() != 8) break;
        }
        PSET(1, 1);
        PSET(2, 2);
        break;
    default:
        menu_unported(0x0602DA0C);
    }
    record_RACx();
}

static void record_RA(void)                             /* 0x0602D7D0: arcade (P+1) */
{
    switch (PB(1)) {
    case 0: PSET(1, 2); break;
    case 1: PSET(1, PB(9)); break;
    case 2: case 3: break;
    case 10: PSET(1, 2); break;
    case 4:
        pad_sound();
        if (keys & 0x3000) {
            tile16(0x5062, 0x25E3C29C, 12, 2);
            tile16(0x7092, 0x25E3C49C, 12, 2);
            box_fill32(0, 0x25E32500, 0x000C0008);
            PSET(9, 5);
            W32(0x060A8C34, 0x90);
            QSET(1, 1);
            PSET(1, 11);
            break;
        }
        if (keys & K_RIGHT) {
            QSET(0, 1);
            PSET(2, 3);
            PSET(1, 8);
            W8(0x060A8C3A, 0);
        }
        if (keys & K_LEFT) {
            QSET(0, 1);
            PSET(0, 1);
            PSET(1, 2);
        }
        break;
    case 5:
        pad_sound();
        if (keys & 0x3000) {
            tile16(0x7062, 0x25E3C29C, 12, 2);
            tile16(0x5092, 0x25E3C49C, 12, 2);
            box_fill32(0, 0x25E32500, 0x000C0008);
            PSET(9, 4);
            W32(0x060A8C34, 0x90);
            QSET(1, 1);
            PSET(1, 11);
            break;
        }
        if (keys & K_RIGHT) {
            QSET(0, 1);
            PSET(3, 1);
            PSET(1, 2);
            break;
        }
        if (keys & K_LEFT) {
            QSET(0, 1);
            PSET(0, 1);
            PSET(1, 2);
        }
        break;
    case 8:
        if (R8(0x06040062) & 0x10) {
            if (rec_menu_scrRd() != 8) break;
        }
        PSET(2, 1);
        PSET(1, 2);
        break;
    case 11: {
        s32 v = RS32(0x060A8C34) - 32;
        W32(0x060A8C34, (u32)v);
        R_SCR_SUB(-48, v, -124);
        if (-288 >= v) {
            W32(0x060A8C34, 0xFFFFFE40u);
            QSET(0, 1);
            PSET(1, 12);
        }
        break;
    }
    case 12: {
        s32 v;
        box_fill32(0, 0x25E32500, 0x000C0008);
        v = RS32(0x060A8C34) + 32;
        W32(0x060A8C34, (u32)v);
        R_SCR_SUB(48, v, -124);
        if (v >= 0) {
            refresh_head();
            box_fill16(0, 0x25E3C29C, 0x000C0008);
            PSET(1, PB(9));
        }
        break;
    }
    default:
        menu_unported(0x0602D7D0);
    }
    if (PB(9) == 4) record_RAC(); else record_RAP();
}

static void record_R(void)                              /* 0x0602D5A0: level 0 */
{
    s32 st = PB(0);
    switch (st) {
    case 0:
    case 1:
        PSET(0, PB(8));
        break;
    case 2:
    case 3:
        break;
    case 4:
        pad_sound();
        if (keys & 0x3000) {
            tile16(0x5002, 0x25E3C280, 12, 2);
            tile16(0x7032, 0x25E3C480, 12, 2);
            PSET(8, 5);
            rec_slide_start();
            break;
        }
        if (keys & K_RIGHT) {
            QSET(0, 1);
            PSET(1, 1);
            PSET(0, 2);
        }
        break;
    case 5:
        pad_sound();
        if (keys & 0x3000) {
            tile16(0x7002, 0x25E3C280, 12, 2);
            tile16(0x5032, 0x25E3C480, 12, 2);
            PSET(8, 4);
            rec_slide_start();
            break;
        }
        if (keys & K_RIGHT) {
            QSET(0, 1);
            PSET(3, 1);
            PSET(0, 2);
        }
        break;
    case 10:
        PSET(0, 2);
        break;
    case 11: {
        s32 v = RS32(0x060A8C34) - 32;
        W32(0x060A8C34, (u32)v);
        R_SCR_SUB(-48, v, -124);
        if (-288 >= v) {
            W32(0x060A8C34, 0xFFFFFE10u);
            QSET(0, 1);
            PSET(0, 12);
        }
        break;
    }
    case 12: {
        s32 v = RS32(0x060A8C34) + 32;
        W32(0x060A8C34, (u32)v);
        R_SCR_SUB(48, v, -124);
        if (v >= 0) PSET(0, PB(8));
        break;
    }
    default:
        menu_unported(0x0602D5A0);
    }
    if (PB(8) == 4) record_RA(); else record_RT();
}

/* ---- pictures ---- */
static void vdp2_set_w(u32 tbl)                         /* 0x0602D370: n (offset, value) halfword pairs into the VDP2 registers */
{
    u32 n = R32(tbl);
    tbl += 4;
    do {
        W16(0x25F80000u + (u32)(s32)RS16(tbl), R16(tbl + 2));
        tbl += 4;
    } while (--n);
}

static void record_pict_load(u32 req)                   /* 0x0602D392: the picture file of the request block req (the screen's pictures come from the disc) -> VDP2 VRAM / work RAM, then the copies and the page's own tables */
{
    int i;
    if (!(R8(0x060AA803) & 0x10)) menu_unported(0x0602D392);   /* the BPE-from-memory path is not used when the disc is the source */
    sat_cd_request(req);
    copy_l(0x25E08000, 0x25E20000, 0x1E10);
    copy_l(0x25E04000, 0x25E10000, 0x0A80);
    copy_l(0x060B8860, 0x060B8E00, 0x18);
    box_fill32(0, 0x25E32500, 0x002C0008);
    box_fill32(0, 0x25E32F60, 0x00120006);
    record_menu_init();
    W32(0x25F800A0, 0x12F3FF00);
    W32(0x25F800A4, 0x12F3FE00);
    {
        u32 r1 = 0x25E7FE44, r2 = 0x25E7FC64;
        for (i = 0; i < 12; i++) {
            r1 -= 4; W32(r1, 0xFF840000u);
            r2 -= 4; W32(r2, 0x02600000u);
        }
    }
    W16(0x060AA878, 0x1F4);
}

static void record_pict_init(void)                      /* 0x0602D38E: RECORD.DAT (called by the mode select's picture step) */
{
    record_pict_load(0x0602D3B4);
}

static void record_pict_init2(void)                     /* 0x0602D388: RECORD2.DAT (the picture set of the return from a replay) */
{
    record_pict_load(0x0602D3D0);
}

static void set_pict_record(void)                       /* 0x06023A08: the page pictures of RECORD.DAT / RECORD2.DAT from the loaded data */
{
    u32 t;
    W16(0x060B9048, 224);
    if (!(R8(0x060AA803) & 0x10)) menu_unported(0x06023A08);
    t = cd_map_set2(0x06023AB8, 0xFDA44000u);
    t = cd_map_set2(t, 0xFDA44000u);
    t = cd_color_set(t, 0xFDA44000u);
    t = cd_map_set(t, 0xFDA23200u);
    cd_color_set(t, 0xFDA23200u);
}

static void set_select_model(void)                      /* 0x0601ADD8: the menu model pack (SELECT.MAD) */
{
    if (R32(0x060D0090) == 0x53454C45) return;
    W32(0x060D0090, 0x53454C45);
    if (R8(0x060AA803) & 0x10) sat_cd_request(0x0601AE30);
    else menu_unported(0x0601ADD8);
    W32(0x060D0048, 0x76002A00);
    setup_texture(0x0601B00C, 0xFD8B0000u);
}

/* ---- the return from a replay (record_main with [0x060A8C17] = 1): the pictures and the video set-up are built again ---- */
static void record_return_init(void)                    /* 0x0602CE30 */
{
    int i;
    W8(0x060AA80D, 1);
    snd_call(0x060173AC, 3, 71, 0);
    set_select_model();
    init_gspr2();
    init_oscroll();
    W16(0x25F80038, 0);
    for (i = 0; i < 8; i++) W16(0x25F80050 + 2 * i, R16(0x0602CEC8 + 2 * i));
    record_pict_init2();
    set_pict_record();
    W16(0x25F80110, 127);
    W16(0x25F80112, 0);
    W32(0x060A8C28, 255);
    W16(0x25F80114, 255);
    W16(0x25F80116, 255);
    W16(0x25F80118, 255);
    W16(0x25F800F8, 0x406);
    W16(0x25F800FA, 0x604);
    W16(0x25F800FC, 2);
    sync_vout();
    record_head_draw();
    tv_on();
    W8(0x060A8C17, 0);
    W8(0x060A8C10, 1);
}

/* the start of a replay of a record (a stored run in the record of the chosen course, 0x0602D066): the settings of the run are handed to the race and the race is started */
static int record_replay_start(void)                    /* 0x0602D066: returns 1 when the race starts */
{
    u32 r10;
    RecPair rc;
    snd_call(0x06016FBC, 78, 0, 0);
    if (RS8(0x06040048) != 5) return 0;
    r10 = 0x002C0000u + (u32)(u16)(RS8(0x0604004B) - 4) * 0x3D00u;
    if (R32(r10) == 0) return 0;
    W32(0x060AAA5C, r10 + 104);
    W16(0x060AA87E, (u32)(RS8(0x0604004B) - 4));
    W8(0x060AA80A, R8(r10 + 20));
    W8(0x060AA811, R8(r10 + 21));
    W8(0x060AA809, R8(r10 + 22));
    W16(0x060AA860, 4);
    W8(0x060AA817, R8(r10 + 24));
    W8(0x060AA818, R8(r10 + 25));
    W8(0x060AA819, R8(r10 + 26));
    W8(0x060AA81A, R8(r10 + 27));
    W8(0x060AA81B, R8(r10 + 28));
    W32(0x060B058C, R32(r10 + 4));
    W32(0x060B0590, R32(r10 + 8));
    W32(0x060B0594, R32(r10 + 12));
    W32(0x060B0598, R32(r10 + 16));
    rc = record_connect(0);
    W32(0x060B0524, R32(rc.r0 + 0));
    W32(0x060B0528, R32(rc.r0 + 8));
    W32(0x060B052C, R32(rc.r0 + 16));
    W32(0x060B0530, R32(rc.r0 + 40));
    rc = record_connect(1);
    W32(0x060B0570, R32(rc.r0 + 0));
    W8(0x060AA80E, 0);
    W8(0x060AA810, 2);
    W8(0x060AA815, 0);
    W8(0x060A8C17, 1);
    W32(0x060A001C, 0x06015408);
    W8(0x060A8C10, 2);
    return 1;
}

/* ---- the screen ---- */
void record_main(void)                                  /* 0x0602CC74 */
{
    int i;
    W32(0x060A001C, 0x06022894);
    pad_kind_init();
    mkpoly_init();
    W32(0x060A8C10, 0);
    W32(0x060E4004, 0x060B8860);
    W32(0x060E4008, 0x060B88C0);
    W32(0x060E400C, 0x060B8E00);
    W32(0x060E4010, 128);
    W32(0x060E4014, 0);
    W32(0x060E4018, 0x060B8A60);
    W32(0x060E401C, 0);
    if (RS8(0x060A8C17) == 1) record_return_init();
    box_fill16(0, 0x25E38000, 0x002C001C);
    vdp2_set_w(0x0602D28C);
    W16(0x060AA8A0, 0);
    W16(0x25F800C0, 0);
    W16(0x060AA8A4, 0x180);
    W16(0x25F800C4, 0x180);
    W16(0x060AA8A2, 120);
    W16(0x25F800C2, 120);
    W16(0x060AA8A6, 199);
    W16(0x25F800C6, 199);
    W16(0x060AA8B0, 0x300);
    W16(0x25F800D0, 0x300);
    W16(0x060AA8B2, 0);
    W16(0x25F800D2, 0);
    W16(0x060AA868, 0x300);
    W32(0x060A8C30, 0xFFFFFF40u);
    act_init(0x060E4100, 0x0602F02C, 1);
    act_init(0x060E4180, 0x0602F194, 1);
    act_init(0x060E4200, 0x0602F2B4, 1);
    W32(0x06040040, 0);
    W32(0x06040044, 0);
    W32(0x060A8C2C, 0);
    W8(0x060A8C14, 1);
    W8(0x060A8C16, 2);
    W8(0x060A8C12, 1);
    W32(0x060A8C30, 0x220);
    sync_vout();
    record_head_draw();
    box_fill16(0, 0x25E3C29C, 0x002A0008);
    W8(0x060A8C38, 0);
    W16(0x060AA994, 0);
    key_repeat_init();
    sync_vout();
    (void)i;
    for (;;) {
        u32 r4;
        s32 r0;
        keys = key_repeat(0);
        W16(0x060AA994, 0);
        m_mkpoly_frame_init();
        switch (RS8(0x060A8C10)) {
        case 0:
            record_R();
            if (QB(0) > 0) {
                QSET(0, 0);
                refresh_head();
            }
            if (!(QB(2) > 0) && QB(1) > 0) QSET(2, QB(1));
            if (QB(2) <= 0 && RS8(0x060A8C39) >= 0 && (keys & 0x600) != 0) {
                if (record_replay_start()) break;
            }
            if (keys & 0x100) {
                snd_call(0x06016FBC, 77, 0, 0);
                W8(0x060A8C10, 2);
            }
            act_run(0x060E4100, 1);
            act_run(0x060E4180, 1);
            act_run(0x060E4200, 1);
            record_request();
            r4 = (u32)RS8(0x060A8C38) + 1;
            r0 = (s32)(r4 >> 5);
            {
                u32 r3 = 0xFEE00000u;
                if (r0 != 0) {
                    if (r0 != 1) r4 = 0;
                    if (RS8(0x060A8C39) >= 0) r3 = 0;
                }
                W32(0x25E7FE50, r3);
                W32(0x25E7FE4C, r3);
            }
            W8(0x060A8C38, r4);
            break;
        case 1: {
            s32 v = RS32(0x060A8C28) - 10;
            if (v < 0) { W8(0x060A8C10, 0); v = 0; }
            W16(0x25F80114, (u32)v);
            W16(0x25F80116, (u32)v);
            W16(0x25F80118, (u32)v);
            W32(0x060A8C28, (u32)v);
            break;
        }
        case 2: {
            s32 v;
            act_run(0x060E4100, 1);
            act_run(0x060E4180, 1);
            act_run(0x060E4200, 1);
            v = 0;
            if (!(keys & 0xE00)) {
                v = RS32(0x060A8C28) + 20;
                if (v < 255) goto fade_out_step;
            }
            W8(0x060AA803, R8(0x060AA803) | 2);
            tv_off();
            W32(0x060AA8A0, 0);
            W32(0x060AA8A4, 0);
            W32(0x060AA8B0, 0);
            v = 0;
        fade_out_step:
            W16(0x25F80114, (u32)v);
            W16(0x25F80116, (u32)v);
            W16(0x25F80118, (u32)v);
            W32(0x060A8C28, (u32)v);
            break;
        }
        default:
            menu_unported(0x0602CC74);
        }
        pal_anim();
        intm_sync(46);
        if (R8(0x060AA803) & 2) break;
    }
    W8(0x060AA803, R8(0x060AA803) & 0xFD);
}

/* ---- the record header slide of the title's demo (adv_recordact_init / adv_recordact): state machine on [0x060A8C10], counter [0x060A8C30] ---- */
static s32 adv_scroll(s32 r3, u32 r4, u32 r1)           /* 0x0602E70C: moves the line scroll offsets (r1 pairs from r4) by 24 towards r3 << 8; returns the original's r3: 0 = reached */
{
    s32 r0 = 0;
    int first = 1;
    r3 <<= 8;
    do {
        if (first) {
            r0 = RS16(r4) + 24;
            if (!(r3 > r0)) { r0 = r3; r3 = 0; }
            first = 0;
        }
        W16(r4, (u32)r0);
        W16(r4 + 4, (u32)r0);
        r4 += 8;
        if (r1 == 10) {
            W16(r4, (u32)r0);
            W16(r4 + 4, (u32)r0);
            r4 += 12;
        }
        r0 = -r0;
    } while (--r1);
    return r3;
}

void adv_recordact_init(void)                           /* 0x0602E5F0 */
{
    u32 r1 = 0x25E7FE80;
    int i;
    W32(0x060A8C30, 0);
    W8(0x060A8C10, 0);
    W16(0x25F8009A, 50);
    W32(0x25F800A0, 0x12F3FF00);
    for (i = 0; i < 32; i++) { r1 -= 4; W32(r1, 0); }
}

void adv_recordact(void)                                /* 0x0602E638 */
{
    u32 r4 = 0x25E7FE08;
    s32 r1, r3;
    switch (RS8(0x060A8C10)) {
    case 0:
        W8(0x060A8C10, 1);
        /* fall through */
    case 1:
        r3 = adv_scroll(2, r4, 10);
        r1 = 0;
        if (!(r3 > 0)) { W8(0x060A8C10, RS8(0x060A8C10) + 1); }
        W32(0x060A8C30, (u32)r1);
        return;
    case 2:
        W8(0x060AAAC6, R8(0x060AAAC6) + 1);
        /* fall through */
    case 5:
        r1 = RS32(0x060A8C30) + 1;
        if (r1 == 120) { W8(0x060A8C10, RS8(0x060A8C10) + 1); r1 = 0; }
        W32(0x060A8C30, (u32)r1);
        return;
    case 3:
        r4 += 28;
        r3 = adv_scroll(4, r4, 8);
        r1 = r3;
        r1 = 0;
        if (!(r3 > 0)) { W8(0x060A8C10, 4); W16(0x060AA868, 0x100); }
        W32(0x060A8C30, (u32)r1);
        return;
    case 4:
        r4 += 28;
        r3 = adv_scroll(6, r4, 8);
        break;
    case 6:
        r3 = adv_scroll(4, r4, 10);
        break;
    case 7:
        W16(0x25F8009A, 0);
        return;
    default:
        menu_unported(0x0602E638);
        return;
    }
    r1 = 0;
    if (!(r3 > 0)) { W8(0x060A8C10, RS8(0x060A8C10) + 1); }
    W32(0x060A8C30, (u32)r1);
}

/* ---- calls of the other record users (name entry, results, options): the digit / tile / table writers of rec_main and the record insertion of rec_menu2 ---- */
u32 record_insert(u32 r4, u32 r5, u32 r6, s32 r8)       /* 0x0602E8EA: the 6 (r6) entries of the list r5 with the new time at r4 inserted in order -> 0x060A8CF0 + r8 * 5; the inserted entry gets the car / transmission / the name 'AAA' and its address goes to [0x060A8C70 + r8]; returns the number of entries before it */
{
    u32 r1 = 0x060A8CF0u + (u32)((s32)(s16)r8 * 5), r9 = 0, r7 = 1;
    s32 r2 = RS32(r4), r3 = RS32(r5);
    r5 += 4;
    do {
        if (r7 > 0 && r2 < r3) {
            u32 r0, car;
            W32(r1, (u32)r2);
            r1 += 4;
            car = (u32)RS8(0x060AA809);
            r2 = (s32)((u32)RS8(0x060AA811) << 2);
            r0 = (u32)RS8(0x060AA80A);
            r0 += (u32)r2;
            r0 <<= 4;
            r0 += car;
            r0 <<= 24;
            W32(r1, 0x001B1B1Bu + r0);
            W32(0x060A8C70u + (u32)r8, r1);
            r1 += 4;
            r7 = 0;
        } else {
            W32(r1, (u32)r3);
            r1 += 4;
            r3 = RS32(r5); r5 += 4;
            W32(r1, (u32)r3);
            r1 += 4;
            r3 = RS32(r5); r5 += 4;
            r9 += r7;
        }
    } while (--r6);
    return r9;
}

void rec_vdp2_set_w(u32 tbl) { vdp2_set_w(tbl); }       /* 0x0602D370 (the table pointer is the original's r8) */

void rec_pal_set(u32 tbl, u32 base)                     /* 0x0602D330: n (source + base, halfword destination offset into 0x060B8000, halfword long count) */
{
    u32 n = R32(tbl);
    tbl += 4;
    do {
        u32 src = R32(tbl) + base;
        u32 dst = 0x060B8000u + (u32)(s32)RS16(tbl + 4);
        u32 cnt = (u32)(s32)RS16(tbl + 6);
        tbl += 8;
        copy_l(src, dst, cnt);
    } while (--n);
}

void rec_color_set(u32 tbl)                             /* 0x0602D358: n (halfword offset, halfword value) words into the colour shadow at 0x060B8000 */
{
    u32 n = R32(tbl);
    tbl += 4;
    do {
        W16(0x060B8000u + (u32)(s32)RS16(tbl), R16(tbl + 2));
        tbl += 4;
    } while (--n);
}

void rec_tile16(u32 r4, u32 r5, u32 r6, u32 r7) { tile16(r4, r5, r6, r7); }       /* 0x0602EF2E */
void rec_tile32(u32 r4, u32 r5, u32 r6, u32 r7) { tile32(r4, r5, r6, r7); }       /* 0x0602EF4A */

void menu_bup_register(void);

void menu_records_register(void)
{
    menu_bup_register();
    menu_reg(0x0602CC74, record_main);
    menu_reg(0x0602D38E, record_pict_init);
    menu_reg(0x0602E5F0, adv_recordact_init);
    menu_reg(0x0602E638, adv_recordact);
    menu_reg(0x0602C6C0, record_dataset);
}
