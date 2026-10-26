/* M8: the option screen: opt_main 0x06032F60 .. 0x06033C5D (option_init = the screen's main function and loop, one handler per page state), opt_disp 0x06033C60 .. 0x06034370 (page
 * drawing, string drawing), file OPTION.DAT.  The pictures are loaded by option_pict_init (0x06033970), which the mode select calls while it slides out.
 *
 * The screen is one loop: each pass runs the handler of the state byte [0x060A8CC0] (jump table 0x060330E4), then the palette cross fade (0x0602EF68 with the block 0x060E4000)
 * and intm_sync(46).  Registers the original keeps through the loop: r12 = 0x060A8CC0 (screen variables), r13 = 0x06042A38 (the option bytes of the save data), r14 = the keys of
 * the pass (key_repeat), r11 = the picture base [0x060A8CC8] (the OPTION.DAT block address 0x060E4000 turned into the table addresses 0x02840000 + x).
 *
 * Screen variables (bytes at 0x060A8CC0 unless noted):  +0 state  +1 cursor (item)  +2 pass counter  +3 target page  +4 page of the running slide  +5 slide flag
 *   +6 / +7 kind of pad 1 / pad 2 picture (0 none, 1 digital, 2 analog, -1 not connected)  +8 (long) picture base  +12 last sound number (-9 = none)
 *   0x060A8C28 (long) colour / fade counter   0x060A8C30 (long) slide counter   0x060A8C34 (long) slide position
 * States:  0 start  1 page 1 (items 0..8)  2 page 2 (the sound page, items 0..9)  3 leave (fade out, save)  4 slide a page out  5 slide a page in  6 page 3 (pads)
 *   7 / 8 slide of page 3 in / out  9 / 10 colour fade of page 3
 * Option bytes (0x06042A38 + n):  page 1 values 0..6 (n < 7), 7 = pad assignment (low nibble pad 1, high nibble pad 2);  page 2 values 8..17 (8 = stereo / mono, mirrored into the unlock byte
 *   0x06040062 bit 5 and the sound driver byte 0x25A00483 bit 7);  14 / 15 / 16 = the music / effect / test selections.
 */
#include "satlib.h"
#include "menu_lib.h"
#include "satmenu.h"
#include "sat_save.h"



#define O 0x060A8CC0u                                   /* r12 */
#define D 0x06042A38u                                   /* r13 */

static u32 keys;                                        /* r14 of the loop: the keys of this pass */

/* ---- shared helpers ---- */
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

static void pal_anim(u32 b)                             /* 0x0602EF68 (r14 = the block) */
{
    int guard = 0;
    for (;;) {
        u32 r4 = R32(b + 4), r5 = R32(b + 8);
        u32 phase = R32(b + 28), step, coef;
        if (++guard > 100) menu_unported(0x0602EF68);
        if (phase == 1) { r4 = R32(b + 8); r5 = R32(b + 24); }
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
        phase = R32(b + 28) + 1;
        if (phase == 3) phase = 0;
        W32(b + 28, phase);
    }
}

void menu_pal_anim(u32 blk) { pal_anim(blk); }

/* _bpe_expand (0x06015870): byte-pair expansion of one stream into 16-bit words at r5 (+ r6 added to every word) */
static u32 bpe_expand(u32 r4, u32 r5, u32 r6)
{
    const u32 r8 = 0x060A7E00, r9 = 0x060A7F00;
    u8 stk[512];
    u32 r5_start = r5, r14 = 0, r11, r1, r7, r13, r2, r3;
    int i;
    r11 = R32(r4);
    r4 += 4;
    r11 += 1;
    for (;;) {
        r1 = R8(r4); r4++;
        r11--;
        if (r11 == 0) break;
        for (i = 0; i < 256; i++) W8(r8 + i, i);
        r7 = 0;
        r13 = 0x100;
        for (;;) {                                      /* the pair table: runs of literal entries */
            if (r1 > 127) { r7 += r1; r7 += (u32)-127; r1 = 0; }
            if (r7 == r13) break;
            for (r2 = 0; !((s32)r2 > (s32)r1); r2++, r7++) {
                u32 r12 = R8(r4); r4++;
                r11--;
                W8(r8 + r7, r12);
                if (r12 != r7) { r12 = R8(r4); r4++; r11--; W8(r9 + r7, r12); }
            }
            if (r13 == r7) break;
            r1 = R8(r4); r4++;
            r11--;
        }
        r3 = (R8(r4) << 8) | R8(r4 + 1);
        r4 += 2;
        r11 -= 2;
        r3 &= 0xFFFF;
        r2 = 0;
        for (;;) {                                      /* the packed bytes of the block */
            if (r2 != 0) { r2--; r7 = stk[r2]; }
            else {
                if (r3 == 0) break;
                r3--;
                r7 = R8(r4); r4++;
                r11--;
            }
            r7 &= 0xFF;
            r13 = R8(r8 + r7);
            if (r13 == r7) {
                if (r5 & 1) {
                    r14 |= r13;
                    r5--;
                    r14 += r6;
                    W16(r5, r14);
                    r5 += 2;
                } else {
                    r14 = r13;
                    r5++;
                    r14 <<= 8;
                }
            } else {
                stk[r2] = R8(r9 + r7);
                stk[r2 + 1] = R8(r8 + r7);
                r2 += 2;
            }
        }
    }
    if (r5 & 1) {
        r5--;
        r14 += r6;
        W16(r5, r14);
        r5 += 2;
    }
    return r5 - r5_start;
}

/* 0x0602D2B8: a table of BPE streams: u32 count, then (u32 source (+ base), u16 destination (<< 4) in VDP2 VRAM, u16 add) */
static u32 bpe_table(u32 t, u32 base)
{
    u32 n = R32(t);
    t += 4;
    do {
        bpe_expand(R32(t) + base, ((u32)(s32)RS16(t + 4) << 4) + 0x25E00000u, (u32)(s32)RS16(t + 6));
        t += 8;
    } while (--n);
    return t;
}

u32 rec_cg_set(u32 t, u32 base) { return bpe_table(t, base); }       /* 0x0602D2B8 (menu_post.h) */

/* 0x0602D330: palettes: u32 count, then (u32 source (+ base), u16 destination (+ 0x060B8000), u16 long words) */
static u32 cram_table(u32 t, u32 base)
{
    u32 n = R32(t);
    t += 4;
    do {
        copy_l(R32(t) + base, (u32)(s32)RS16(t + 4) + 0x060B8000u, (u32)(s32)RS16(t + 6));
        t += 8;
    } while (--n);
    return t;
}

/* 0x0602D370: VDP2 register words: u32 count, then (u16 offset, u16 value) */
static void reg_table(u32 t)
{
    u32 n = R32(t);
    t += 4;
    do {
        W16(0x25F80000u + (u32)(s32)RS16(t), R16(t + 2));
        t += 4;
    } while (--n);
}

/* ---- sound and CD interface ---- */
static void snd(u32 fn, u32 a, u32 b, u32 c) { snd_call(fn, a, b, c); }

static void pcm_reload(void)                            /* 0x06004C90: the PCM data of the sound driver is loaded again (to LWRAM 0x002DF000; the driver copies it to the sound RAM) */
{
    if (R8(0x060AA82C)) {
        sat_load_name_at(0x06004CB0);                   /* PCMDATA.DAT */
        W8(0x060AA82C, 0);
    }
}

static void cd_play(s32 track, s32 times)               /* 0x0600F756: CD audio play (CD block commands; hardware) */
{
    snd_call(0x0600F756, (u32)track, (u32)times, 0);
}

/* ---- picture helpers of the pages ---- */
static void string_disp(u32 r4, u32 r5, u32 r6)         /* 0x0603436C: a string as 2 x 1 cell glyphs (two 32-bit cells per glyph, the lower half 74 cells further) at r4 */
{
    for (;;) {
        s32 c = RS8(r5);
        r5++;
        if (c == 0) return;
        if (c == 32) {
            W32(r4, 0);
            W32(r4 + 0x100, 0);
        } else {
            u32 w;
            if (c >= 65) c -= 7;
            c -= 48;
            w = ((u32)c << 1) + r6 + 0x0700;
            W32(r4, w);
            W32(r4 + 0x100, w + 74);
        }
        r4 += 4;
    }
}

static void draw_cursor1(u32 r11)                       /* 0x06033E7C: the cursor frames of page 1 */
{
    u32 r9 = 0x06033D04, r10 = 6;
    do {
        u32 r4 = R32(r9), r5 = R32(r9 + 4), r6 = 0x2000;
        r9 += 8;
        if (RS8(0x06033EC0 + (u32)RS8(O + 1)) == (s32)r10) {
            r6 = 0x4000;
            if (!((s32)r10 > 1)) r6 = 0x7000;
        }
        box_copy16(r4 + r11, r5, r6);
        r10--;
    } while (r10 != 0);
}

static void draw_page1(u32 r11)                         /* 0x06033ECC: all items of page 1 with their values */
{
    u32 r9 = 0x06033D34, r10 = 8;
    do {
        u32 r4 = R32(r9), r5 = R32(r9 + 4), r6 = 0x200000, r8, r14;
        s32 r1 = 8 - (s32)r10;
        r9 += 8;
        if (RS8(O + 1) == r1) {
            r6 = 0x400000;
            if (!((s32)r10 > 2)) r6 = 0x700000;
        }
        box_copy32(r4 + r11, r5, r6);
        r8 = R32(r9);
        r9 += 4;
        r14 = r8;
        if ((s32)r8 > 0) {
            do {
                s32 v;
                r4 = R32(r9); r5 = R32(r9 + 4);
                r9 += 8;
                r6 = 0x300000;
                v = RS8(D + (8 - r10));
                if ((s32)(r14 - r8) == v) {
                    r6 = 0x400000;
                    if (RS8(O + 1) == 8 - (s32)r10) r6 = 0x700000;
                }
                box_copy32(r4 + r11, r5, r6);
                r8--;
            } while (r8 != 0);
        }
        r10--;
    } while (r10 != 0);
}

static void draw_page2(u32 r11)                         /* 0x06033F4C: page 2 (the sound page) */
{
    u32 r9 = 0x06034094, r10 = 2, r8, r14;
    do {
        u32 r4 = R32(r9), r5 = R32(r9 + 4), r6 = 0x200000;
        r9 += 8;
        if (RS8(O + 1) == 2 - (s32)r10) r6 = 0x400000;
        box_copy32(r4 + r11, r5, r6);
        r8 = R32(r9);
        r9 += 4;
        r14 = r8;
        if ((s32)r8 > 0) {
            do {
                s32 v;
                r4 = R32(r9); r5 = R32(r9 + 4);
                r9 += 8;
                r6 = 0x300000;
                v = RS8(D + 8 + (2 - r10));
                if ((s32)(r14 - r8) == v) {
                    r6 = 0x400000;
                    if (RS8(O + 1) == 2 - (s32)r10) r6 = 0x700000;
                }
                box_copy32(r4 + r11, r5, r6);
                r8--;
            } while (r8 != 0);
        }
        r10--;
    } while (r10 != 0);
    r10 = 9;
    do {
        u32 r4 = R32(r9), r5 = R32(r9 + 4), r6 = 0x200000;
        int draw = 1;
        r9 += 8;
        if (r10 == 1 && (u32)3 >= (u32)(RS8(O + 1) - 2)) {
            r6 = 0x400000;
        } else if (RS8(O + 1) == 11 - (s32)r10) {
            r6 = 0x400000;
            if (!((s32)r10 > 2)) r6 = 0x700000;
        }
        if ((R8(0x06040062) & 2) == 0) {
            if (r10 == 6) draw = 0;
            else if (!((s32)r10 > 5) && r10 != 1) r5 += (u32)(s32)(s16)0xFE00;
        }
        if (draw) box_copy32(r4 + r11, r5, r6);
        r8 = R32(r9);
        r9 += 4;
        r14 = r8;
        if ((s32)r8 > 0) {
            r4 = R32(r9);                               /* one destination, then one string pointer per value */
            r9 += 4;
            do {
                s32 v, r2;
                r5 = R32(r9);
                r9 += 4;
                r2 = 11 - (s32)r10;
                v = RS8(D + 8 + r2);
                if ((s32)(r14 - r8) == v) {
                    r6 = 0x200000;
                    r2 -= 2;
                    if (r2 == v && !(5 >= (s32)r10)) r6 = 0x400000;
                    if (RS8(O + 1) == 11 - (s32)r10) r6 = 0x700000;
                    {
                        u32 r4s = r4;
                        int drawit = 1;
                        if ((R8(0x06040062) & 2) == 0) {
                            if (r10 == 6) drawit = 0;
                            else if (!((s32)r10 > 5) && r10 != 1) r4 += (u32)(s32)(s16)0xFE00;
                        }
                        if (drawit) string_disp(r4, r5, r6);
                        r4 = r4s;
                    }
                }
                r8--;
            } while (r8 != 0);
        }
        r10--;
    } while (r10 != 0);
}

static void draw_page3a(u32 r11)                        /* 0x06034280 */
{
    u32 r9 = 0x06033E04, r10 = 2, r8, r14;
    do {
        u32 r4 = R32(r9), r5 = R32(r9 + 4), r6 = 0x2000;
        r9 += 8;
        box_copy16(r4 + r11, r5, r6);
        r8 = R32(r9);
        r9 += 4;
        r14 = r8;
        if ((s32)r8 > 0) {
            do {
                s32 r0;
                r4 = R32(r9); r5 = R32(r9 + 4);
                r9 += 8;
                r6 = 0x3000;
                r0 = RS8(D + 7);
                if (r10 == 1) r0 = (s32)((u32)r0 >> 2 >> 2);
                r0 &= 0xF;
                if ((s32)(r14 - r8) == r0) {
                    r6 = 0x4000;
                    if (RS8(O + (8 - r10)) != 0) r6 = 0x7000;
                    else r6 = 0x3000;
                }
                box_copy16(r4 + r11, r5, r6);
                r8--;
            } while (r8 != 0);
        }
        r10--;
    } while (r10 != 0);
}

static void draw_page3b(u32 r11)                        /* 0x060342F4: the two pad pictures */
{
    if (RS8(O + 6) != 0) {
        u32 r1 = (u32)(RS8(D + 7) & 0xF) << 2, r2 = 0x06033E5C, r5 = 0x25E38424;
        if (RS8(O + 6) == 2) { r2 = 0x06033E6C; r5 = 0x25E38520; }
        box_copy16(R32(r2 + r1) + r11, r5, 0x6000);
    }
    if (RS8(O + 7) != 0) {
        u32 r1 = (u32)(RS8(D + 7) & 0xF0) >> 2, r2 = 0x06033E5C, r5 = 0x25E38924;
        if (RS8(O + 7) == 2) { r2 = 0x06033E6C; r5 = 0x25E38A20; }
        box_copy16(R32(r2 + r1) + r11, r5, 0x6000);
    }
}

static void blip(void)                                  /* 0x060332CE: the cursor sound when a direction key is down */
{
    if (keys & 0xF000) snd(0x06016FBC, 72, 0, 0);
}

static void scroll_a(s32 r6, s32 r4)                    /* 0x06033614: the line scroll values of the page slide (VDP2 line scroll table at 0x25E7FE64) */
{
    u32 r5 = 0x25E7FE64;
    s32 r1;
    if (RS8(O + 4) == 1) r5 += 8;
    r1 = 12 - RS8(O + 4);
    do {
        s32 r9 = r6, r0;
        if (r6 > 0) r9 = 0;
        else {
            r0 = (s16)0xFEE8;
            if (r0 >= r6) r9 = r0;
        }
        if (RS8(O + 4) == 1) r9 += 32;
        r5 -= 4;
        W32(r5, (u32)r9 << 16);
        {
            u32 r3 = RS8(O + 4) == 0 ? 0 : 12;
            if (R8(0x060336CC - 1 + r3 + (u32)r1) == 1) { r5 -= 4; W32(r5, (u32)r9 << 16); }
        }
        r6 += r4;
        r1--;
    } while (r1 != 0);
}

static void scroll_b(s32 r2)                            /* 0x0603382A */
{
    u32 r5 = 0x25E7FE70;
    int r1 = 21;
    do { r5 -= 4; W32(r5, (u32)r2 << 16); r1--; } while (r1 != 0);
}

/* ---- the sound tests (module opt_main end): the music / effect / test sound selections; everything but the stores goes to the sound driver (snd_call) ---- */
static void snd_test_music(void)                        /* 0x06033A60 */
{
    s32 r0, r2;
    snd(0x06017080, 0, 0, 0);
    snd(0x06016F10, 1, 0, 0);
    snd(0x060175A4, 1, 0, 0);
    W8(0x060AAA24, 0);
    W8(0x060AAA14, 0);
    r0 = RS8(D + 14) << 1;
    r2 = RS8(0x06033B38 + (u32)r0);
    r0 += 1;
    if (!(r2 > 0)) {
        cd_play(RS8(0x06033B38 + (u32)r0), 15);
        snd(0x06016FA4, (u32)-64, 0, 0);
        return;
    }
    r2 -= 1;
    W8(O + 12, r2);
    snd(0x06017080, 0, 0, 0);
    W8(0x060AA80D, 1);
    snd(0x060173AC, (u32)r2, 1, 0);
    W8(0x060AA80D, 1);
    W16(0x060AAA24, 0);
    snd(0x060175A4, 1, R32(0x06033B38 + (u32)r0), 0);
}

static void snd_test_effect(void)                       /* 0x06033B52 */
{
    s32 r0, r4;
    snd(0x06017080, 0, 0, 0);
    snd(0x06016F10, 1, 0, 0);
    r0 = RS8(D + 15) << 1;
    r4 = RS8(0x06033C34 + (u32)r0);
    if (RS8(O + 12) != r4) {
        W8(O + 12, r4);
        if (r4 >= 0) {
            W8(0x060AA80D, 1);
            snd(0x060173AC, (u32)r4, 0, 0);
            W8(0x060AA80D, 1);
            W16(0x060AAA24, 0);
            snd(0x060175A4, 1, 0, 0);
        } else {
            W16(0x060AA87E, (u32)(r4 + 2));
            W8(0x060AA80A, 0);
            W8(0x060AA811, 0);
            W8(0x060AA810, 0);
            snd(0x06017418, 0, 0, 0);
        }
    }
    r0 += 1;
    snd(0x06016FBC, (u32)RS8(0x06033C34 + (u32)r0), 0, 0);
}

static void snd_test_voice(void)                        /* 0x06033C5E */
{
    snd(0x06016F10, 1, 0, 0);
    snd(0x06017080, 0, 0, 0);
    if (RS8(O + 12) != -9) {
        W8(0x060AA80D, 1);
        snd(0x060173AC, 1, 0, 0);
        W8(0x060AA80D, 1);
        W16(0x060AAA24, 0);
    }
    W8(O + 12, (u32)-9);
    snd(0x06016F10, 1, 0, 0);
    W8(0x060AAA45, R8(0x06033CF8 + R8(D + 16)));
    sync_vout();                                        /* 0x060051FC */
}

/* ---- option_pict_init (0x06033970): called by the mode select while it slides out ---- */
void option_pict_init(void)                              /* 0x06033970: called by the mode select while it slides out */
{
    u32 base = 0, t;
    mkpoly_init();
    if (R8(0x060AA803) & 0x10) {
        sat_cd_request(0x060339A4);                     /* OPTION.DAT */
        base = 0x038A3800u;
    }
    t = bpe_table(0x06033908, base);
    cram_table(t, base);
    W32(0x060A8CC8, base);
    copy_l(0x02845230u + base, 0x060EE800, 0x18);
    W32(0x25F800A0, 0x12F3FF00);
    W32(0x25F800A4, 0x12F3FF00);
    box_fill32(0, 0x25E30500, 0x002C0017);
    box_fill32(0, 0x25E30480, 0x000C0001);
    W8(O + 1, 0);
    draw_cursor1(base);
}

/* ---- the states ---- */
static void st0(u32 r11)                                /* 0x06033110 */
{
    W32(0x060A8C34, (u32)(s32)(s16)0xFD80);
    W8(O + 3, 0);
    W8(O + 5, 1);
    draw_page1(r11);
    W8(O + 0, 5);
}

static void leave_page(void)                            /* 0x06033240: B / START / the last item: leave the screen */
{
    snd(0x06016FBC, 77, 0, 0);
    W16(0x25F800EC, 0);
    W32(0x060A8C28, 0);
    W8(O + 0, 3);
}

static void value_edit(u32 addr, u32 range_tbl, u32 idx, s32 r4)   /* the LEFT / RIGHT edit of a value: wraps inside 0 .. range - 1 */
{
    s32 r2 = RS8(range_tbl + idx), r1 = RS8(addr);
    if (r2 > 0) {
        r1 += r4;
        if (r1 >= 0) { if (r1 == r2) r1 = 0; }
        else r1 = r2 - 1;
    }
    W8(addr, r1);
}

static void st1(u32 r11)                                /* 0x0603312C: page 1 */
{
    s32 r4 = 0, idx;
    blip();
    if (keys & 0xFFFF8000u) r4 = 1;
    if (keys & 0x4000) r4 = -1;
    idx = RS8(O + 1);
    value_edit(D + (u32)idx, 0x060332C4, (u32)idx, r4);
    idx = RS8(O + 1);
    if (keys & 0x1000) { idx -= 1; if (idx == -1) idx = 8; }
    if (keys & 0x2000) { idx += 1; if (idx == 9) idx = 0; }
    W8(O + 1, idx);
    draw_cursor1(r11);
    if (keys & 0x0600) {
        s32 item = RS8(O + 1);
        if (item == 6) {
            snd(0x06016FBC, 73, 0, 0);
            box_fill16(0, 0x25E3C284, 0x000A000C);
            box_fill16(0, 0x25E3CB84, 0x000A0005);
            W32(0x060A8C34, 0x160);
            W8(O + 3, 1);
            W8(O + 2, 1);
            pcm_reload();
            snd(0x06016F3C, 0, 0, 32);
            W8(O + 12, (u32)-10);
            W8(O + 0, 4);
        } else if (item == 7) {
            snd(0x06016FBC, 73, 0, 0);
            pad_kind_init();
            box_fill16(0, 0x25E3C284, 0x000A0011);
            box_fill16(0, 0x25E3CC84, 0x00090003);
            W32(0x060A8C34, 0x160);
            W8(O + 3, 2);
            W8(O + 2, 2);
            W8(O + 0, 4);
        } else if (item == 8) {
            leave_page();
            goto draw;
        }
    }
    if (keys & 0x0900) leave_page();
draw:
    draw_page1(r11);
    W8(O + 2, R8(O + 2) + 1);
}

static void st2(u32 r11)                                /* 0x060332EC: page 2 (the sound page) */
{
    s32 r4 = 0, idx;
    if (keys & 0xFFFF8000u) r4 = 1;
    if (keys & 0x4000) r4 = -1;
    idx = RS8(O + 1);
    value_edit(D + 8 + (u32)idx, 0x060334A0, (u32)idx, r4);
    idx = RS8(O + 1);
    if (idx == 0) {
        W8(0x06040062, R8(0x06040062) & 0xDF);
        W8(0x25A00483, R8(0x25A00483) & 0x7F);          /* the sound driver's byte: bit 7 = stereo */
        if (RS8(D + 8) != 0) {
            W8(0x06040062, R8(0x06040062) | 0x20);
            W8(0x25A00483, R8(0x25A00483) | 0x80);
        }
    }
    idx = RS8(O + 1);
    if (keys & 0x1000) {
        idx -= 1;
        if ((R8(0x06040062) & 2) == 0 && idx == 5) idx = 4;
        if (idx == -1) idx = 9;
    }
    if (keys & 0x2000) {
        idx += 1;
        if ((R8(0x06040062) & 2) == 0 && idx == 5) idx = 6;
        if (idx == 10) idx = 0;
    }
    W8(O + 1, idx);
    if (!(keys & 0x0900)) {
        if (keys & 0x0600) {
            s32 item = RS8(O + 1);
            if (item == 6) { snd_test_music(); goto draw; }
            if (item == 7) { snd_test_effect(); goto draw; }
            if (item == 8) { snd_test_voice(); goto draw; }
            if (item != 9) goto draw;
        } else {
            goto draw;
        }
    }
    /* back to page 1 (B / START / the last item) */
    W32(0x060A8C34, 0x160);
    W32(0x060A8C30, 13);
    W8(O + 3, 0);
    W8(O + 2, 17);
    W8(O + 1, 9);
    W8(O + 0, 4);
    snd(0x06017080, 0, 0, 0);
    snd(0x06016F10, 1, 0, 0);
    snd(0x060175A4, 1, 0, 0);
    W8(0x060AAA24, 0);
    W8(0x060AAA14, 0);
    W8(0x060AA80D, 1);
    snd(0x060173AC, 3, 71, 0);
    snd(0x06016FBC, 77, 0, 0);
draw:
    draw_page2(r11);
    W8(O + 2, R8(O + 2) + 1);
}

static void st3(u32 r11)                                /* 0x060334AA: leave (white fade out, then save) */
{
    s32 r0;
    (void)r11;
    if (keys & 0x0E00) goto leave;
    r0 = (s32)R32(0x060A8C28) + 20;
    if (r0 >= 0xFF) goto leave;
    goto set;
leave:
    tv_off();                                           /* 0x06005560 */
    bup_main_save();                                    /* 0x0602FC40 */
    W8(0x060AA803, R8(0x060AA803) | 2);
    r0 = 0;
set:
    W16(0x25F80110 + 4, r0);
    W16(0x25F80110 + 6, r0);
    W16(0x25F80110 + 8, r0);
    W32(0x060A8C28, r0);
}

static void st4(u32 r11)                                /* 0x060334F8: a page slides out */
{
    s32 r0 = (s32)R32(0x060A8C30) - 1, r2;
    if (r0 != 0) {
        W32(0x060A8C30, r0);
        W16(0x060AA87A, RS16(0x060AA87A) - 10);
    }
    r2 = (s32)R32(0x060A8C34) - 32;
    W32(0x060A8C34, r2);
    scroll_a(r2, -32);
    if (!((s16)0xFEE0 >= r2)) return;
    W32(0x060A8C34, (u32)(s32)(s16)0xFD40);
    box_fill32(0, 0x25E30500, 0x002C0017);
    r0 = RS8(O + 3);
    if (r0 == 1) {                                      /* to page 2 */
        W8(O + 1, 0);
        draw_page2(r11);
        W32(0x060A8C30, 13);
    } else if (r0 == 2) {                               /* to page 3: the pad pictures */
        s32 r10 = 0;
        u32 r4, r5;
        W32(0x060A8C34, (u32)(s32)(s16)0xFF00);
        W32(0x060A8C30, 15);
        W8(O + 0, 7);
        r0 = RS8(0x060AA9A4);
        if (r0 != -16) {
            if (r0 == 0) { r4 = 0x028486F0; r10 = 1; }
            else { r4 = 0x0284846C; r10 = 2; }
            r5 = 0x25E30854;
            box_copy32(r4 + r11, r5, 0x00500000);
        }
        W8(O + 6, r10);
        r10 = 0;
        r0 = RS8(0x060AA9A6);
        if (r0 != -16) {
            if (r0 == 0) { r4 = 0x028486F0; r10 = 1; }
            else { r4 = 0x0284846C; r10 = 2; }
            r5 = 0x25E31254;
            box_copy32(r4 + r11, r5, 0x00500000);
        }
        W8(O + 7, r10);
        return;
    } else {                                            /* back to page 1 */
        box_fill16(0, 0x25E38420, 0x001C0014);
        draw_page1(r11);
        W8(O + 1, 6);
    }
    W8(O + 4, R8(O + 3));
    W8(O + 0, 5);
}

static void st5(u32 r11)                                /* 0x060335DA: a page slides in */
{
    s32 r0 = (s32)R32(0x060A8C30) - 1, r2;
    (void)r11;
    if (r0 != 0) {
        W32(0x060A8C30, r0);
        W16(0x060AA87A, RS16(0x060AA87A) + 10);
    }
    r2 = (s32)R32(0x060A8C34) + 32;
    W32(0x060A8C34, r2);
    scroll_a(r2, 32);
    if (r2 >= 0) {
        W8(O + 5, 0);
        W8(O + 0, R8(O + 3) + 1);
    }
}

static void st6(u32 r11)                                /* 0x060336E4: page 3 (the pads) */
{
    blip();
    draw_page3b(r11);
    if (RS8(O + 6) != -1) {
        s32 r0 = RS8(D + 7) & 0xF;
        if (keys & 0x1000) { r0 -= 1; if (r0 == -1) r0 = menu_option_ntypes - 1; }
        if (keys & 0x2000) { r0 += 1; if (r0 == menu_option_ntypes) r0 = 0; }
        W8(D + 7, (RS8(D + 7) & 0xF0) + r0);
    }
    if (RS8(O + 7) != -1) {
        u32 r2 = key_repeat(1);
        s32 r0;
        if (r2 & 0xF000) snd(0x06016FBC, 72, 0, 0);
        r0 = RS8(D + 7) & 0xF0;
        if (r2 & 0x1000) { r0 -= 16; if (r0 == -16) r0 = 16 * (menu_option_ntypes - 1); }
        if (r2 & 0x2000) { r0 += 16; if (r0 == 16 * menu_option_ntypes) r0 = 0; }
        W8(D + 7, (RS8(D + 7) & 0x0F) + r0);
    }
    draw_page3a(r11);
    if ((keys & 0x0200) && menu_option_diy_hook && RS8(O + 6) != -1 && (RS8(D + 7) & 0xF) == menu_option_ntypes - 1 && menu_option_diy_hook()) {      /* the PSP: the confirm key on DIY edits it */
        W8(O + 2, R8(O + 2) + 1);
        return;
    }
    if (keys & 0x0F00) {
        snd(0x06016FBC, 73, 0, 0);
        W32(0x060A8C30, 15);
        W8(O + 3, 0);
        W8(O + 0, 10);
    }
    W8(O + 2, R8(O + 2) + 1);
}

static void st7(u32 r11)                                /* 0x0603379A */
{
    s32 r0 = (s32)R32(0x060A8C30) - 1, r2;
    (void)r11;
    if (r0 != 0) {
        W32(0x060A8C30, r0);
        W16(0x060AA87A, RS16(0x060AA87A) + 10);
    }
    r2 = (s32)R32(0x060A8C34) + 16;
    W32(0x060A8C34, r2);
    scroll_b(r2);
    if (r2 >= 0) {
        W8(O + 0, 9);
        W8(O + 1, 7);
        W32(0x060A8C28, 31);
        W16(0x25F80108, 31);
    }
}

static void st8(u32 r11)                                /* 0x060337DA */
{
    s32 r0 = (s32)R32(0x060A8C30) - 1, r2;
    if (r0 != 0) {
        W32(0x060A8C30, r0);
        W16(0x060AA87A, RS16(0x060AA87A) - 10);
    }
    r2 = (s32)R32(0x060A8C34) - 16;
    W32(0x060A8C34, r2);
    scroll_b(r2);
    if ((s16)0xFF00 >= r2) {
        W32(0x060A8C34, (u32)(s32)(s16)0xFD40);
        box_fill32(0, 0x25E30600, 0x002C0017);
        draw_page1(r11);
        W8(O + 1, 7);
        W8(O + 4, R8(O + 3));
        W8(O + 0, 5);
    }
}

static void fade3_store(s32 r0)                         /* 0x060338AA */
{
    W32(0x060A8C28, r0);
    W16(0x25F80108, (u32)r0 << 8);
}

static void st9(u32 r11)                                /* 0x06033894 */
{
    s32 r0 = (s32)R32(0x060A8C28) - 4;
    draw_page3b(r11);
    if (!(r0 >= 0)) {
        W8(O + 0, 6);
        r0 = 0;
    }
    fade3_store(r0);
}

static void st10(u32 r11)                               /* 0x060338C4 */
{
    s32 r0 = (s32)R32(0x060A8C28) + 4;
    (void)r11;
    if (r0 == 32) {
        W8(O + 0, 8);
        box_fill16(0, 0x25E38420, 0x001B0014);
        box_fill16(0, 0x25E3CD04, 0x000E0012);
        r0 = 0;
    }
    fade3_store(r0);
}

/* ---- option_init (0x06032F60): the screen's main function ---- */
void option_init(void)                                  /* 0x06032F60: the screen's main function and loop */
{
    u32 r11;
    int first = 1;
    W32(0x060A001C, 0x06022894);
    pad_kind_init();
    reg_table(0x06033958);
    W32(0x060AA8A0, 0);
    W32(0x060AA8A4, 0);
    W32(0x060AA8B0, 0);
    W16(0x060AA864 + 12, 0);
    W16(0x060AA864 + 22, 0);
    W16(0x25F80110, 127);
    W16(0x25F80110 + 4, 0);
    W16(0x25F80110 + 6, 0);
    W16(0x25F80110 + 8, 0);
    W16(0x060B9044, 0x0200);
    {
        u32 r1 = 0x25E7FE6C, r2 = 0x25E7FC6C;
        int r3 = 22;
        do { r1 -= 4; r2 -= 4; W32(r1, 0xFF000000u); W32(r2, 0xFF000000u); r3--; } while (r3 != 0);
        r1 -= 4;
        W32(r1, 0);
    }
    W32(0x060E4000 + 4, 0x060EE800);
    W32(0x060E4000 + 8, 0x060B8800);
    W32(0x060E4000 + 12, 0x060B8E00);
    W32(0x060E4000 + 16, 48);
    W32(0x060E4000 + 20, 0);
    W32(0x060E4000 + 24, 0x060B8400);
    W32(0x060E4000 + 28, 0);
    W16(0x25F800F8, 0x0606);
    W16(0x25F800FA, 0x0604);
    W16(0x25F800FC, 0x0002);
    W16(0x060AA8E6, 0x001F);
    W16(O, 0);
    W32(0x060A8C30, 1);
    key_repeat_init();
    do {
        if (!first) {
            snd_call(0x06017580, 0, 0, 0);
            m_mkpoly_frame_init();
            keys = key_repeat(0);
            W16(0x060AA994, 0);
            r11 = R32(O + 8);
            switch (R8(O)) {
            case 0: st0(r11); break;
            case 1: st1(r11); break;
            case 2: st2(r11); break;
            case 3: st3(r11); break;
            case 4: st4(r11); break;
            case 5: st5(r11); break;
            case 6: st6(r11); break;
            case 7: st7(r11); break;
            case 8: st8(r11); break;
            case 9: st9(r11); break;
            case 10: st10(r11); break;
            default: menu_unported(0x060330E4); break;
            }
        }
        first = 0;
        /* game_zsort: the polygon sort (3D pipeline) */
        pal_anim(0x060E4000);
        intm_sync(46);
    } while (!(R8(0x060AA803) & 2));
    W8(0x060AA803, R8(0x060AA803) & 0xFD);
}

void menu_option_register(void)
{
    menu_reg(0x06032F60, option_init);
    menu_reg(0x06033970, option_pict_init);
}
