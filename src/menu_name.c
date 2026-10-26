/* M8: what follows a race (arcade / practice): the name entry (a wheel of 3D letters, 0x060300C0 .. 0x06030C02 and its actors), ported from the original.  The 3D parts (the letters of the
 * wheel, the car) go through the matrix requests of satmenu.h (m3 / menu3d_car) for the host.  The dispatcher, the ranking screen and the game over screen are in menu_post.c.
 *
 * Name entry: the dispatcher runs name_entry() inline (it loops until the screen is over).  [0x060A8C60] = the screen state (0 fade in, 1 letter wheel, 2 wheel returns to the detent,
 * 3 save, 4 / 5 fade out, 6 time attack variant), 0x060E4400 + 0x80 * n (n = 0 .. 28) the letter actors of the wheel (+70 letter number, +68 state, +72 next / +76 previous letter of the
 * ring), 0x060E5280 the timer actor, 0x060E5300 + 0x80 * n (n = 0 .. 3) the chosen-letter actors (+70 = the letter chosen, +68 state).  [0x060A8C68] = the letter in front (actor
 * address), [0x060A8C64] = the wheel angle, [0x060A8C30] = the wheel's remaining rotation, [0x060A8C6C] = number of letters chosen, [0x060A8C6D] = phase of the choosing animation
 * (0 idle, 1 a letter flies, 2.., 8 / 9 time out, 11 end).  [0x060A8C62] = the rank the time reached (-1: none), [0x060A8C63] = 1 when no name entry is needed.
 *
 * The original's register r13 is state here: the chosen-letter actor reads whatever the screen's state code left in it (g_r13). */
#include "satlib.h"
#include "menu_lib.h"
#include "satmenu.h"
#include "menu_records.h"
#include "menu_post.h"

#define SND(id) snd_call(0x06016FBC, (u32)(id), 0, 0)

static s32 g_r13;                                       /* the original's r13 as the state code of the name entry leaves it */

/* ---- the lap list of the best run goes into the stored replay record (0x06030F50 / 0x06030F68 / 0x06030FA0) ---- */
static void lap_store(u32 r4, u32 r5, u32 r6, s32 r1)  /* 0x06030FA0: r4 = the lap times (BCD, cumulative), r5 = the record (total + one halfword per lap), r6 = laps, r1 > 0: store r1 itself as total */
{
    u32 r7 = r4, r8 = r6, r2 = 0;
    s32 r3;
    for (;;) {
        r3 = RS32(r7);
        r7 += 4;
        if (r3 <= 0) break;
        r2 = (u32)(-(s32)(r2 - (u32)r3)) >> 16;         /* sub r3,r2; neg r2,r2; shlr16 r2: the high word of this lap time */
        if (r2 == 6) r2 -= 6;
        if ((s32)r2 > 0) return;
        r2 = (u32)r3;
        if (--r8 == 0) break;
    }
    if (r1 <= 0) {
        s32 r0 = RS32(r5);
        r1 = RS32(0x060B050C);
        if (r0 > 0 && r1 >= r0) return;
    }
    W32(r5, (u32)r1);
    r5 += 4;
    r2 = 0;
    do {
        r1 = RS32(r4);
        r4 += 4;
        r2 = (u32)(-(s32)(r2 - (u32)r1));
        W16(r5, r2);
        r5 += 2;
        r2 = (u32)r1;
    } while (--r6);
}

static void lap_store_50(u32 r4, u32 r5)                /* 0x06030F50 */
{
    s32 type = RS16(0x060AA860);
    u32 r6;
    if (type == 0) r6 = R8(0x060AA80C) == 3 ? 48 : 16;
    else if (type == 2) r6 = RS16(0x060AA87E) == 0 ? 15 : 12;
    else r6 = 18;
    lap_store(r4, r5, r6, 0);
}

static void lap_store_68(u32 r4, u32 r5)                /* 0x06030F68 */
{
    lap_store(r4, r5, 6, 1);
}

/* ---- 0x06030CB8: a name word found in the table 0x06030CE0 is replaced by the word behind it ---- */
static u32 name_lookup(u32 r2)
{
    u32 r4 = r2 & 0x00FFFFFF, t = 0x06030CE0;
    for (;;) {
        s32 r1 = RS32(t);
        u32 r3;
        t += 4;
        if (r1 < 0) break;
        r3 = R32(t);
        t += 4;
        if (r4 != (u32)r1) continue;
        return (r2 & 0xFF000000u) + r3;
    }
    return r2;
}

/* ---- 0x06030D84: does the time reach the table?  The rank goes to [0x060A8C62] (-1: not), the list is merged at once (0x060A8CF0 ..: the shown rows) ---- */
void name_prepare(void)
{
    s32 type, r7 = 1, r9 = 0;
    u32 r5, r6, r10, r11, r12, r3;
    W32(0x060A8C70, 0); W32(0x060A8C74, 0); W32(0x060A8C78, 0); W32(0x060A8C7C, 0);
    r6 = 16; r11 = 32;
    type = RS16(0x060AA860);
    if (type == 0) {
        r5 = (u32)((RS8(0x060AA80C) - 1) + (s32)((u32)RS8(0x060AA811) >> 1)) << 2;
        r3 = 0x06041000;
    } else if (type == 2) {
        r6 = 8; r11 = 16;
        r5 = (u32)RS16(0x060AA87E) << 2;
        r3 = 0x06041010;
    } else {
        r6 = 5; r11 = 10;
        r5 = ((((u32)RS16(0x060AA87E) << 4) + ((u32)RS8(0x060AA80A) << 2) + ((u32)RS8(0x060AA811) << 1) + (u32)RS8(0x060AA809)) << 2);
        r3 = 0x06041020;
    }
    r5 += r3;
    r12 = r5;
    r5 = R32(r5);
    r10 = r5;
    type = RS16(0x060AA860);
    if (type == 6 || (type == 4 && RS8(0x060B00F5) >= 10)) {
        r9 = -1;
    } else {
        u32 r1, r3p;
        r9 = (s32)record_insert(0x060B058C, r5, r6, 0);
        r7 = R32(0x060A8C70) != 0 ? 0 : 1;              /* the original's r7 = 0 once the new time was inserted */
        r1 = R32(0x060A8C70) - 0x060A8CF0 + r10;
        W32(0x060A8C74, r1);
        r1 = r10; r3p = 0x060A8CF0;
        do { W32(r1, R32(r3p)); r3p += 4; r1 += 4; } while (--r11);
        lap_store_50(0x060AA400, R32(r12 + 0x110));
        bup_replay_save();
        if (r7 > 0) r9 = -1;
    }
    W8(0x060A8C62, (u32)r9);
    if ((RS16(0x060AA860) >> 2) > 0) {
        u32 r2;
        record_insert(0x060B0530, r10 + 40, 1, 8);
        r7 = R32(0x060A8C78) != 0 ? 0 : 1;
        W8(0x060A8C63, (u32)r7);
        r2 = R32(0x060A8C78) - 0x060A8CF0 + r10;
        W32(0x060A8C7C, r2);
        r10 += 40;
        W32(r10, R32(0x060A8D18));
        r10 += 4;
        W32(r10, R32(0x060A8D1C));
        if (r7 == 0) lap_store_68(0x060AA658, R32(r12 + 0x110) + 40);
    } else {
        W8(0x060A8C63, 1);
    }
    bup_main_save();
}

/* a w x h block of 16-bit cells (no header) from the cell list r4 to a 64 cell wide plane at r5, `add` added to every cell (the entry 0x0601D93C of the box copy) */
static void box16_nh(u32 r4, u32 r5, u32 r6, s32 w, s32 h)
{
    do {
        s32 c = w;
        u32 p = r5;
        do { W16(p, R16(r4) + r6); r4 += 2; p += 2; } while (--c);
        r5 += 0x80;
    } while (--h);
}

/* ---- the count-down of the name entry: two digits of 3 x 3 cells (0x0603106C, r11 = the BCD time) ---- */
static void name_timer_draw(u32 r11)
{
    u32 r13 = R32(0x060A8CC8), r1;
    r1 = (0xFFFFFFF0u & r11) >> 4;
    box16_nh(0x0283E970 + r1 * 18 + r13, 0x25E390CA, 0x1000, 3, 3);
    r1 = 15 & r11;
    box16_nh(0x0283E970 + r1 * 18 + r13, 0x25E390D0, 0x1000, 3, 3);
}

/* the timer actor (0x06030FF4, slot 0x060E5280): +8 divider, +4 reload, +56 the BCD time left */
static void act_timer(void)
{
    u32 r14 = act_cur;
    s32 r10 = RS32(r14 + 8) - 1, r11;
    if (r10 == 0) {
        r11 = RS32(r14 + 56) - 1;
        if (r11 < 0) {                                  /* 0x06031024: time up, the next letter is chosen by the machine */
            u32 r2, r7;
            W32(r14 + 56, (u32)r11);
            r2 = R8(0x060A8C6C);
            r7 = r2;
            W8(0x060A8C6C, r2 + 1);
            g_r13 = (s32)(0x060E5300 + (r7 << 7));
            W32((u32)g_r13 + 0, 0x0603177C);
            W16((u32)g_r13 + 70, 0xFFFF);
            W8(0x060A8C6D, 1);
            SND(6);
            return;
        }
        if ((r11 & 12) == 12) r11 -= 6;
        W32(r14 + 56, (u32)r11);
        name_timer_draw((u32)r11);
        r10 = RS32(r14 + 4);
    }
    W32(r14 + 8, (u32)r10);
}

/* ---- the letters of the wheel (0x06031438, slots 0x060E4400 + 0x80 * n, n = the letter) ---- */
static void act_moji(void)
{
    u32 r14 = act_cur;
    s32 r0 = RS8(0x060A8C6D), r1, r2, st;
    int draw = 1;
    if (r0 == 8) W32(r14 + 60, 40);
    if (r0 == 9) W16(r14 + 68, 3);
    r1 = (s32)(((R32(0x060A8C68) - 0x060E4400u) << 2) >> 8);
    r2 = RS16(r14 + 70) << 1;
    r1 = r1 - r2 - 2;
    if (r1 < 0) r1 += 58;
    W32(r14 + 4, (u32)(s32)RS16(0x060316D8 + (u32)r1));
    m3(M3_INIT, 0, 0, 0);
    m3_translate_v(0x060316CC);
    m3(M3_ROT_X, 0x280, 0, 0);
    m3(M3_ROT_Z, -0xC00, 0, 0);
    m3(M3_ROT_Y, RS32(r14 + 28) + RS32(0x060A8C64), 0, 0);
    m3_translate_v(r14 + 12);
    m3(M3_ROT_Y, -(RS32(r14 + 28) + RS32(0x060A8C64)), 0, 0);
    m3(M3_ROT_Z, 0xC00, 0, 0);
    m3(M3_ROT_X, RS32(r14 + 24) - 0x280, 0, 0);
    st = RS16(r14 + 68);
    switch (st) {
    case 0: {                                           /* 0x06031588: set up the letter */
        u32 n, p;
        r1 = RS16(r14 + 70) << 2;
        W32(r14 + 8, (u32)r1);
        W32(r14 + 36, R32(0x06048074 + (u32)r1));
        W32(r14 + 28, (u32)((s32)(s16)0x235 * (s32)(s16)r1));
        n = r14 + 128;
        if (n == 0x060E5280) n = 0x060E4400;
        W32(r14 + 72, n);
        p = r14 - 128;
        if (p == 0x060E4380) p = 0x060E5200;
        W32(r14 + 76, p);
        W32(r14 + 56, 0);
        W32(r14 + 12, 0);
        W32(r14 + 16, 0);
        W32(r14 + 24, 0);
        W32(r14 + 32, 0);
        W32(r14 + 20, 0xFFFCA000);
        W32(r14 + 60, 1);
        W16(r14 + 68, 1);
        break;
    }
    case 1:
        break;
    case 2: {                                           /* 0x060315E6: the colour parameter of the letters sweeps while the wheel is at rest */
        s32 r3 = RS8(0x060A8C6D), v;
        if (r3 > 0) { draw = 0; break; }
        v = RS32(0x060A8C34);
        if (r3 >= 0 || v > 0) v += 1;
        if (v == 56) v = 0;
        W32(0x060A8C34, (u32)v);
        v = (s32)(((u32)v >> 1) << 1);
        W32(r14 + 4, (u32)(s32)RS16(0x060316D8 + (u32)v));
        break;
    }
    case 3: {                                           /* 0x0603161A */
        s32 t = RS32(r14 + 60);
        if (t <= 0) {                                   /* 0x06031680 */
            s32 c;
            W32(r14 + 20, R32(r14 + 20) + 0xFFFF9800u);
            W32(r14 + 24, R32(r14 + 24) + 0xFFFFFC80u);
            c = RS32(r14 + 56) + 1;
            W32(r14 + 56, (u32)c);
            if (c < 0xA0) break;
            W16(r14 + 68, 4);
            goto case4;
        }
        t--;
        if (t == 0 && RS16(r14 + 70) == 0) SND(4);
        W32(r14 + 60, (u32)t);
        break;
    }
    case 4:
    case4:
        W32(r14 + 0, 0);                                /* 0x060316AA: this letter is done */
        W8(0x060A8C6D, 11);
        W16(r14 + 68, 0);
        break;
    default:
        menu_unported(0x06031550);
    }
    if (draw) {                                         /* 0x060314DC */
        m3(M3_SCALE, 0x9E00, 0x9E00, 0x9E00);
        m3(M3_DRAW, (s32)R32(r14 + 36), (s32)R32(r14 + 4), 0);
    }
}

/* ---- the letters that fly to the name (0x0603177C, slots 0x060E5300 + 0x80 * n): +70 the letter (-1: not chosen yet), +36 its model ---- */
static void pick_draw(u32 r14)                          /* 0x0603178E */
{
    m3(M3_INIT, 0, 0, 0);
    m3_translate_v(r14 + 12);
    m3_translate_v(0x06031B7C);
    m3(M3_ROT_X, 0x280, 0, 0);
    m3_translate_v(0x06031B88);
    m3_translate_v(0x06031BA0);
    m3(M3_ROT_X, RS32(r14 + 28), 0, 0);
    m3_translate_v(0x06031B94);
    m3(M3_ROT_X, RS32(r14 + 24) - 0x280, 0, 0);
    m3(M3_SCALE, 0x9E00, 0x9E00, 0x9E00);
    m3(M3_DRAW, (s32)R32(r14 + 36), 0x14DF, 0);
}

static u32 name_forbidden(void)                         /* 0x06031B18: 1 when the letters chosen so far spell a word of the table 0x06031B68 */
{
    s32 n = RS8(0x060A8C6C);
    u32 r2 = 0, r1 = 0x060E5346, t = 0x06031B68;
    if (n != 1) {
        r2 += (u32)(RS16(r1) + 1);
        if (n != 2) {
            r2 <<= 8; r1 += 0x80;
            r2 += (u32)(RS16(r1) + 1);
            if (n != 3) {
                r2 <<= 8; r1 += 0x80;
                r2 += (u32)(RS16(r1) + 1);
            }
        }
    }
    for (;;) {
        s32 w = RS32(t);
        t += 4;
        if (w < 0) return 0;
        if ((u32)w == r2) return 1;
    }
}

static void pick_done(u32 r14)                          /* 0x06031AB6 */
{
    W16(r14 + 68, 0);
    W32(r14 + 0, 0);
}

static void act_pick(void)
{
    u32 r14 = act_cur;
    s32 st = RS16(r14 + 68);
    s32 n = RS8(0x060A8C6C);
    switch (st) {
    case 0: {                                           /* 0x06031844 */
        s32 r1 = RS16(r14 + 70), r0;
        if (r1 < 0) {
            r0 = 28;
        } else {
            u32 r13 = R32(0x060A8C68), r2;
            g_r13 = (s32)r13;
            r2 = R32(r13 + 8);
            W32(r14 + 36, R32(0x06048000 + r2));
            r1 = RS16(r13 + 70);
            W16(r14 + 70, (u32)r1);
            r0 = r1;
            if (r0 == 27) {
                if (n == 1) {
                    W8(0x060A8C6C, (u32)(n - 1));
                    W8(0x060A8C6D, 0);
                } else {
                    u32 pv = r14 - 128;
                    g_r13 = (s32)pv;
                    W32(pv + 56, 0);
                    W32(pv + 60, 0x1E00);
                    W16(pv + 68, 4);
                    W8(0x060A8C6C, (u32)(n - 2));
                    W8(0x060A8C6D, 0xFF);
                    SND(3);
                }
                pick_done(r14);
                return;
            }
        }
        W32(r14 + 24, 0); W32(r14 + 28, 0); W32(r14 + 32, 0);
        W32(r14 + 12, 0); W32(r14 + 16, 0); W32(r14 + 20, 0);
        if (r0 == 28) {                                 /* the end of the name (or the time ran out) */
            s32 c;
            g_r13 = 0x060E5280;
            W32(0x060E5280, 0);
            c = RS32(0x060E5280 + 56);
            W8(0x060A8C6D, c < 0 ? 8 : 9);
            if (r14 == 0x060E5300) {                    /* the first slot: three placeholder periods are set up */
                u32 r13 = r14;
                s32 r7 = -1, k;
                for (k = 0; k < 3; k++) {
                    W32(r13, 0x0603177C);
                    W16(r13 + 70, 26);
                    W32(r13 + 36, 0x0604C660);          /* alpha_period_poly */
                    W32(r13 + 12, (u32)((s32)(s16)192 * (s32)(s16)r7) << 8);
                    r7++;
                    W32(r13 + 16, 0x4800);
                    W32(r13 + 20, 0x00038000);
                    W32(r13 + 24, 0xFFFF8000);
                    W32(r13 + 28, 0xFFFF8000);
                    W16(r13 + 68, 1);
                    r13 += 128;
                }
                g_r13 = (s32)r13;
                pick_draw(r14);
                return;
            }
            W16(r14 + 70, 0xFFFF);
            if (r14 == 0x060E5380) W16(r14 + 198, 0xFFFF);
            pick_done(r14);
            return;
        }
        W32(r14 + 56, 0);
        SND(5);
        W16(r14 + 68, 2);
        pick_draw(r14);
        return;
    }
    case 1:                                             /* 0x06031980: the name is checked against the forbidden words when the last letter is in front */
        if (RS8(0x060A8C6D) == 1 && g_r13 > 0 && R32(0x060A8C68) == 0x060E5200 && name_forbidden() > 0) {
            u32 r4;
            W16(r14 + 68, 4);
            W32(r14 + 56, 0);
            W32(r14 + 60, 0x1E00);
            r4 = 0x060E5300 + (u32)((n - 2) << 7);
            if (r14 == r4) {
                s32 r3;
                r4 += 128;
                W32(r4, 0);
                r3 = RS8(0x060A8C6C);
                W8(0x060A8C6C, 0);
                W8(0x060A8C6D, (u32)(-(r3 - 1)));
                SND(3);
            }
        }
        pick_draw(r14);
        return;
    case 2: {                                           /* 0x060319DE: the letter flies to its place */
        s32 c;
        W32(r14 + 12, R32(r14 + 12) + (u32)((s32)(s16)(n - 2) * (s32)(s16)0x500));
        W32(r14 + 24, R32(r14 + 24) + 0xFFFFFF00u);
        W32(r14 + 28, R32(r14 + 28) + 0xFFFFFC00u);
        W32(r14 + 16, R32(r14 + 16) + 0xFFFFFDC0u);
        c = RS32(r14 + 56) + 1;
        W32(r14 + 56, (u32)c);
        if (c == 32) {
            W16(r14 + 68, 3);
            W8(0x060A8C6D, 2);
        }
        pick_draw(r14);
        return;
    }
    case 3: {                                           /* 0x06031A22 */
        s32 c;
        W32(r14 + 12, R32(r14 + 12) + (u32)((s32)(s16)(n - 2) * (s32)(s16)0x240));
        W32(r14 + 24, R32(r14 + 24) + 0xFFFFFA00u);
        W32(r14 + 16, R32(r14 + 16) + 0xA00u);
        W32(r14 + 20, R32(r14 + 20) + 0x3600u);
        c = RS32(r14 + 56) + 1;
        W32(r14 + 56, (u32)c);
        if (c == 48) {
            W16(r14 + 68, 1);
            W8(0x060A8C6D, 0);
        }
        pick_draw(r14);
        return;
    }
    case 4: {                                           /* 0x06031A66: a letter that is taken back flies away */
        s32 r2, c;
        W32(r14 + 12, R32(r14 + 12) + (u32)((s32)(s16)(n - 1) * (s32)(s16)0x400));
        W32(r14 + 24, R32(r14 + 24) + 0xFFFFFC00u);
        r2 = RS32(r14 + 60) + (s32)0xFFFFFEC0;
        W32(r14 + 16, R32(r14 + 16) + (u32)r2);
        W32(r14 + 60, (u32)r2);
        W32(r14 + 20, R32(r14 + 20) + 0x9000u);
        c = RS32(r14 + 56) + 1;
        W32(r14 + 56, (u32)c);
        if (c == 84) {
            W8(0x060A8C6D, R8(0x060A8C6D) + 1);
            W8(0x060A8C6E, 0);
            pick_done(r14);
            return;
        }
        pick_draw(r14);
        return;
    }
    case 5:
        pick_done(r14);
        return;
    default:
        menu_unported(0x06031BAC);
    }
}

/* ---- the car behind the wheel: the slave job 0x06031714 ---- */
static void name_car_job(void)
{
    u32 b = 0x060E4000;
    s32 r0 = RS8(0x060AA80A);
    if (r0 == 2) r0 = 10;
    W8(b + 92, (u32)r0);
    W8(b + 94, 0);
    W32(0x060E4018, 0x1800);
    W32(0x060E401C, 0xFFFF0000);
    W32(0x060E4020, 0x00054000);
    W32(0x060E400C, 0);
    W32(0x060E4014, 0);
    W32(0x060E4010, R32(0x060A8C64) + 0x7F80);
    W32(b + 900, 1);                                    /* dispSelectCar(r4 = 0x060D1000, r5 = 1): r5 is kept in the block (no shadow) */
    menu3d_car(b, 0, 0);
}

/* ---- the name entry itself: the inline part of the dispatcher (0x060300C0 .. 0x060304A2) ---- */
static void name_pict_init(void)                        /* the pictures and the actors (0x060300F0 .. 0x06030348) */
{
    u32 r11, r1;
    if (R8(0x060AA803) & 0x10) {
        sat_cd_request(0x06030184);                     /* NAME.DAT */
        r11 = 0x038A7D00;
    } else {
        menu_unported(0x0602D2B8);                      /* _cg_set from the cartridge */
        r11 = 0;
    }
    rec_pal_set(0x06030D28, r11);
    rec_vdp2_set_w(0x06030D6C);
    W8(0x060AA80D, 1);
    snd_call(0x060173AC, 1, 0, 0);
    W8(0x060AA80D, 1);
    W16(0x060AAA24, 0);
    snd_call(0x0600F756, 8, 15, 0);                     /* the CD track of the name entry */
    W32(0x060A8CC8, r11);
    W8(0x060AA825, 0);
    W16(0x25F80110, 127);
    W32(0x060A8C28, 255);
    W16(0x25F80114, 255);
    W16(0x25F80116, 255);
    W16(0x25F80118, 255);
    copy_l(0x25E18040, 0x25E20040, 0x16C0);
    box_fill16(0, 0x25E28000, 0x002C001C);
    box_copy16(0x0283EA3C + r11, 0x25E40000, 0x7000);
    rec_tile16(74, 0x25E3A114, 25, 4);
    box_copy16(0x0283E65C + r11, 0x25E3830C, 0);
    r1 = 0x0283E6DC + r11;
    if (RS16(0x060AA860) == 2) r1 += (u32)((s32)(s16)132 * (s32)(s16)(RS16(0x060AA87E) + 1));
    box_copy16(r1, 0x25E3809C, 0);
    box_copy16(0x0283E634 + r11, 0x25E38C9A, 0x3000);
    box_copy16(0x0283EA24 + r11, 0x25E38CAC, 0x3000);
    box_copy16(0x0283E620 + r11, 0x25E38CB6, 0x3000);
    W16(0x060AA8B0, 0);
    W16(0x060AA8B2, 0);
    W16(0x060AA8B4, 0);
    W16(0x060AA86C, 0xFFFE);
    W16(0x060AA870, 250);
    for (r1 = 0x060E5500; r1 > 0x060E5300; ) { r1 -= 4; W32(r1, 0); }
    actor_init(0x060E4400, 0x06031438, 29);
    W32(0x060E5300, 0);
    W32(0x060E5380, 0);
    W32(0x060E5400, 0);
    W32(0x060E5480, 0);
    actor_init(0x060E5280, 0x06030FF4, 1);
    W32(0x060E52B8, 80);
    W32(0x060E5284, 104);
    W32(0x060E5288, 104);
    name_timer_draw(80);
    W32(0x060A8C68, 0x060E4400);
    actor_run(0x060E4400, 29);
    W16(0x060E4444, 2);
    W32(0x060A8C64, 0);
    W32(0x060A8C30, 0);
    W8(0x060A8C6C, 0);
    W8(0x060A8C6D, 0);
    W8(0x060A8C6E, 0);
    W16(0x25F800F8, 0x0406);
    W16(0x25F800FA, 0x0404);
    W16(0x25F800FC, 2);
    W32(0x060A8C34, 0xFFFFFFFF);
    tv_on();
    W8(0x060A8C60, 0);
    W16(0x060AA994, 0);
}

static void snd_step(void)                              /* the wheel clicks over to the next letter: the sound of the even letters (state 2) */
{
    SND(2);
}

static void set_alphabet_model(void)                    /* 0x0601AD84: the polygon models of the letters (ALPHA.MDL -> 0x06048000) */
{
    if (R32(0x060D0090) == 0x4E414D45) return;
    W32(0x060D0090, 0x4E414D45);
    if (R8(0x060AA803) & 0x10) sat_cd_request(0x0601ADBC);
    else menu_unported(0x0601D644);
}

void name_entry(void)
{
    W8(0x060A8C6F, 0);
    init_gspr2();
    init_oscroll();
    mkpoly_init();
    s_mkpoly_init();
    set_alphabet_model();
    name_pict_init();
    for (;;) {
        s32 state;
        snd_call(0x06017580, 0, 0, 0);
        m_mkpoly_frame_init();
        s_mkpoly_frame_init();
        state = R8(0x060A8C60);
        switch (state) {
        case 0: {                                       /* 0x060304F0: fade in */
            s32 r0;
            name_car_job();
            actor_run(0x060E4400, 29);
            r0 = RS32(0x060A8C28) - 10;
            if (r0 < 0) { W8(0x060A8C60, 1); r0 = 0; }
            W16(0x25F80114, (u32)r0);
            W16(0x25F80116, (u32)r0);
            W16(0x25F80118, (u32)r0);
            W32(0x060A8C28, (u32)r0);
            g_r13 = 0x25F80110;
            break;
        }
        case 1: {                                       /* 0x06030522: the letter wheel */
            s32 r5, r6, r8;
            u32 r14, r13, r2 = 0x060A8C30, r7 = 0x060A8C68, r3;
            name_car_job();
            actor_run(0x060E4400, 29);
            if (R8(0x060A8C6D) == 8) W8(0x060A8C6D, 9);
            if (R8(0x060AA803) & 4) {                   /* the analog pad / wheel */
                r5 = (s32)R8(0x060AA99C) - 127;
                r5 = (r5 + (r5 << 2)) >> 1;
                r8 = 64;
                if (r5 > 0) { if (r8 > r5) r5 = 0; }
                else if (r5 > -r8) r5 = 0;
            } else {
                r8 = 96;
                r5 = 0;
                if (RS16(0x060AA990) & 0x8000) r5 = -224;
                if (RS16(0x060AA990) & 0x4000) r5 = 224;
            }
            r6 = RS32(r2);
            if (RS8(0x060A8C6D) > 0) goto L80e;
            if (RS8(0x060A8C6C) != 3 || RS8(0x060A8C6E) != 0) goto L604;
            W8(0x060A8C60, R8(0x060A8C60) + 1);
            goto L80e;
        L604:
            if (r5 != 0 || r6 == 0) goto L6f8;
            if (r6 > 0) {                               /* a residual rotation to the detent: forwards */
                r5 = r8;
                r6 += r5;
                W32(r2, (u32)r6);
                if (r6 >= 0x8D4) {
                    r14 = R32(r7);
                    W16(r14 + 68, 1);
                    r13 = R32(r14 + 76);
                    g_r13 = (s32)r13;
                    W16(r13 + 68, 2);
                    W32(r7, r13);
                    snd_step();
                    r6 = 0;
                }
            } else {                                    /* backwards */
                r8 = -r8;
                r5 = r8;
                r6 += r5;
                W32(r2, (u32)r6);
                if (r6 <= -0x8D4) {
                    r14 = R32(r7);
                    W16(r14 + 68, 1);
                    r13 = R32(r14 + 72);
                    g_r13 = (s32)r13;
                    W16(r13 + 68, 2);
                    W32(r7, r13);
                    snd_step();
                    r6 = 0;
                }
            }
            W32(r2, (u32)r6);
            goto L7d6;
        L6f8:
            r14 = R32(r7);
            if (r5 > 0) {                               /* 0x06030770 */
                r6 += r5;
                W32(r2, (u32)r6);
                if (r6 > 0x8D4) {
                    r6 += (s32)0xFFFFF72C;
                    W32(r2, (u32)r6);
                    W16(r14 + 68, 1);
                    r13 = R32(r14 + 76);
                    g_r13 = (s32)r13;
                    W16(r13 + 68, 2);
                    snd_step();
                    W32(r7, r13);
                }
            } else {
                r6 += r5;
                W32(r2, (u32)r6);
                if (r6 < (s32)0xFFFFF72C) {
                    r6 += 0x8D4;
                    W32(r2, (u32)r6);
                    W16(r14 + 68, 1);
                    r13 = R32(r14 + 72);
                    g_r13 = (s32)r13;
                    W16(r13 + 68, 2);
                    W32(r7, r13);
                    snd_step();
                }
            }
        L7d6:
            r3 = R32(0x060A8C64) + (u32)r5;
            W32(0x060A8C64, r3);
            if (RS8(0x060A8C6C) == 3) {
                if ((s32)0xFFFF08D0 > (s32)r3) { r6 = 0; W32(r2, 0); }
                else if ((s32)r3 > (s32)0xFFFF11A4) { r6 = 0; W32(r2, 0); }
            }
            if (r6 != 0) goto L8c8;
            r14 = R32(r7);
            W32(0x060A8C64, (u32)(-RS32(r14 + 28)));
        L80e: {
                u32 r6a = 0x060A8C6C;
                s32 cnt = RS8(r6a);
                r14 = 0x060E5300 + ((u32)cnt << 7);
                r3 = (u32)RS16(0x060AA994);
                if (RS8(0x060A8C6D) == 0 && (r3 & 0xE00)) {
                    int ok = 1;
                    if (cnt == 3) {
                        u32 cur = R32(r7);
                        if (cur != 0x060E5180 && cur != 0x060E5200) ok = 0;
                    }
                    if (ok) {                           /* confirm: the letter in front flies to the name */
                        W32(r14, 0x0603177C);
                        W8(r6a, (u32)(cnt + 1));
                        W8(0x060A8C6D, 1);
                        goto L8c8;
                    }
                }
                if (RS8(0x060A8C6D) > 0) goto L8c8;
                if (!(r3 & 0x100)) goto L8c8;
                if (cnt == 0) goto L8c8;
                r14 -= 128;                             /* cancel: the last letter is taken back */
                W32(r14 + 56, 0);
                W32(r14 + 60, 0x1E00);
                W16(r14 + 68, 4);
                W8(r6a, (u32)(cnt - 1));
                W8(0x060A8C6D, (u32)(RS8(0x060A8C6D) - 1));
                SND(3);
            }
        L8c8:
            actor_run(0x060E5280, 1);
            g_r13 = RS32(0x060E52B8);
            actor_run(0x060E5300, 4);
            if (RS8(0x060A8C6D) == 11) {
                W32(0x060A8C30, 0);
                W8(0x060A8C60, 3);
            }
            break;
        }
        case 2: {                                       /* 0x06030AEA: the wheel runs on to the detent */
            u32 r14, r13;
            s32 r1;
            m_mkpoly_frame_init();
            s_mkpoly_frame_init();
            name_car_job();
            actor_run(0x060E4400, 29);
            r1 = RS32(0x060A8C64);
            if (r1 >= (s32)0xFFFF8468) {
                r1 += 0x8D4;
                r14 = R32(0x060A8C68);
                W16(r14 + 68, 1);
                r13 = R32(r14 + 76);
                g_r13 = (s32)r13;
                W16(r13 + 68, 2);
                W32(0x060A8C68, r13);
                W32(0x060A8C64, (u32)r1);
                if (!((r13 << 1) >> 8 & 1)) SND(2);
                if (r1 > 0x8CC) W8(0x060A8C60, 1);
            } else {
                r1 -= 0x8D4;
                r14 = R32(0x060A8C68);
                W16(r14 + 68, 1);
                r13 = R32(r14 + 72);
                g_r13 = (s32)r13;
                W16(r13 + 68, 2);
                W32(0x060A8C68, r13);
                W32(0x060A8C64, (u32)r1);
                if (!((r13 << 1) >> 8 & 1)) SND(2);
                if ((s32)0xFFFF11A4 > r1) W8(0x060A8C60, 1);
            }
            actor_run(0x060E5300, 4);
            W8(0x060A8C6E, 1);
            break;
        }
        case 3: {                                       /* 0x06030940: the name is complete: save it */
            s32 c;
            if (RS16(0x060AA860) & 4) {
                actor_run(0x060E5300, 4);
                name_car_job();
            }
            c = RS32(0x060A8C30) + 1;
            W32(0x060A8C30, (u32)c);
            if (c == 18) {
                u32 r2, r4, r5;
                r4 = (u32)RS8(0x060AA811) << 2;
                r2 = (u32)RS8(0x060AA809);
                r2 += ((u32)RS8(0x060AA80A) + r4) << 4;
                r2 <<= 8;
                r4 = (u32)(RS16(0x060E5346) + 1); r2 += r4; r2 <<= 8;
                r4 = (u32)(RS16(0x060E53C6) + 1); r2 += r4; r2 <<= 8;
                r4 = (u32)(RS16(0x060E5446) + 1); r2 += r4;
                r2 = name_lookup(r2);
                r5 = R32(0x060A8C70);
                if ((s32)r5 > 0) { W32(r5, r2); r5 = R32(0x060A8C74); W32(r5, r2); }
                r5 = R32(0x060A8C78);
                if ((s32)r5 > 0) { W32(r5, r2); r5 = R32(0x060A8C7C); W32(r5, r2); }
                bup_main_save();
                W8(0x060A8C60, 4);
            }
            break;
        }
        case 4: {                                       /* 0x06030A08 */
            if ((RS16(0x060AA860) >> 2) == 1) {
                W8(0x060A8C60, 6);
                W32(0x060A8C28, 0);
                actor_run(0x060E5300, 4);
                name_car_job();
            } else {
                s32 r0 = RS32(0x060A8C30) + 1, r2;
                W32(0x060A8C30, (u32)r0);
                r2 = ((r0 - 18) << 2) - 2;
                W16(0x25F8010A, (u32)r2);
                W16(0x25F80108, (u32)(r2 << 8));
                if (r0 == 26) {
                    W16(0x25F80108, 31);
                    W16(0x25F8010A, 31);
                    W16(0x25F80020, 17);
                    W16(0x060AA8E6, 17);
                    box_fill16(0, 0x25E38046, 0x00080006);
                    box_fill16(0, 0x25E3A114, 0x00190004);
                    W16(0x060AA86C, 0xFFFE);
                    W16(0x25F80080, 0xFFFE);
                    W16(0x060AA870, 0xFFFA);
                    W16(0x25F80084, 0xFFFA);
                    W16(0x060AA876, 0xFF70);
                    W16(0x25F80090, 0xFF70);
                    W16(0x060AA87A, 0xFF70);
                    W16(0x25F80094, 0xFF70);
                    sync_vout();
                    W8(0x060A8C60, 5);
                }
            }
            break;
        }
        case 5: {                                       /* 0x06030AB0: fade out */
            s32 r0;
            W16(0x25F80020, 31);
            W16(0x060AA8E6, 31);
            r0 = RS32(0x060A8C30) + 1;
            W32(0x060A8C30, (u32)r0);
            W16(0x25F80108, (u32)(((34 - r0) << 2) - 2) << 8);
            if (r0 == 34) {
                W16(0x25F80108, 0);
                W16(0x25F800EC, 0);
                W8(0x060AA803, R8(0x060AA803) | 2);
            }
            break;
        }
        case 6: {                                       /* 0x06030B9C: the time attack variant: wait for a key, fade out */
            s32 r0;
            actor_run(0x060E5300, 4);
            name_car_job();
            r0 = 0;
            if ((RS16(0x060AA994) & 0xE00) == 0) {
                r0 = RS32(0x060A8C28) + 20;
            }
            if ((RS16(0x060AA994) & 0xE00) != 0 || r0 >= 255) {
                tv_off();
                W32(0x060A001C, 0x06032514);
                if (RS16(0x060AA860) == 6) W32(0x060A001C, 0x060262A4);
                if (RS8(0x060B00F5) == 12) W32(0x060A001C, 0x06015408);
                W8(0x060AA803, R8(0x060AA803) | 2);
                r0 = 0;
            }
            W16(0x25F80114, (u32)r0);
            W16(0x25F80116, (u32)r0);
            W16(0x25F80118, (u32)r0);
            W32(0x060A8C28, (u32)r0);
            g_r13 = 0x25F80110;
            break;
        }
        default:
            menu_unported(0x060304D4);
        }
        W16(0x060AA994, 0);
        intm_sync(46);
        if (R8(0x060AA803) & 2) break;
    }
    W8(0x060AA803, R8(0x060AA803) & 0xFD);
}

void menu_name_register(void)
{
    menu_reg(0x06031438, act_moji);
    menu_reg(0x06030FF4, act_timer);
    menu_reg(0x0603177C, act_pick);
}
