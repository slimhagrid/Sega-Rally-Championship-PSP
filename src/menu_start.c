/* M8: the stage start screen (start module 0x06015408): shown between the course select and the race: the stage's name picture fades in, the race loads, the picture fades out and the
 * main vector becomes the race's own start-up (game_init 0x06010F0C: the race, ported in play.c, takes over there).
 *
 * Vectors: [0x060A001C] main (start_main 0x06015510 loops until [0x060AA803] bit 1), [0x060AA178] the screen's own state handler (0x0601554C wait for the fade-in, 0x06015594 start the
 * fade-out, 0x060155A4 wait for it). */
#include "satlib.h"
#include "menu_lib.h"
#include "satmenu.h"

/* extract::_bpe_expand (0x06015870): byte pair expansion of a stream (u32 length, then blocks: pair table, packed size, packed bytes) into 16-bit words, `add` added to every word.
 * The pair tables are left at 0x060A7E00 (left symbols, reset to the identity at every block) / 0x060A7F00 (right symbols, only the entries of real pairs are written).  Returns the
 * number of bytes written. */
static u32 bpe_expand(u32 src, u32 dst, u32 add)        /* 0x06015870 */
{
    const u32 left = 0x060A7E00, right = 0x060A7F00;
    u32 r4 = src, r5 = dst, r14 = 0;
    s32 r11 = (s32)R32(r4) + 1;
    r4 += 4;
    for (;;) {
        u32 r1, r7, r3;
        u8 stk[256];
        int r2, i;
        r1 = R8(r4++);
        r11--;
        if (r11 == 0) break;
        for (i = 255; i >= 0; i--) W8(left + (u32)i, (u32)i);
        r7 = 0;
        for (;;) {                                      /* the pair table: runs of entries, a run header > 127 skips symbols */
            u32 k;
            if (r1 > 127) { r7 += r1 - 127; r1 = 0; }
            if (r7 == 0x100) break;
            for (k = 0; k <= r1; k++) {
                u32 sym = R8(r4++);
                r11--;
                W8(left + r7, sym);
                if (sym != r7) { W8(right + r7, R8(r4++)); r11--; }
                r7++;
            }
            if (r7 == 0x100) break;
            r1 = R8(r4++);
            r11--;
        }
        r3 = ((u32)R8(r4) << 8) | R8(r4 + 1);
        r4 += 2;
        r11 -= 2;
        r2 = 0;
        for (;;) {                                      /* the packed bytes: a symbol that is not its own left expands to left, right */
            u32 sym, l;
            if (r2 != 0) { r2--; sym = stk[r2]; }
            else {
                if (r3 == 0) break;
                r3--;
                sym = R8(r4++);
                r11--;
            }
            l = R8(left + sym);
            if (l == sym) {
                if (r5 & 1) { r5--; W16(r5, (r14 | l) + add); r5 += 2; }
                else { r14 = l << 8; r5++; }
            } else {
                stk[r2] = (u8)R8(right + sym);
                stk[r2 + 1] = (u8)l;
                r2 += 2;
            }
        }
    }
    if (r5 & 1) { r5--; W16(r5, r14 + add); r5 += 2; }
    return r5 - dst;
}

static u32 cd_cg_set(u32 tbl, u32 base)                 /* 0x060227C8: cell graphics = BPE streams (source + base, destination, add) */
{
    u32 n = R32(tbl);
    tbl += 4;
    do {
        bpe_expand(R32(tbl) + base, R32(tbl + 4), R32(tbl + 8));
        tbl += 12;
    } while (--n);
    return tbl;
}

/* ---- the screen ---- */
static void start_wait_fadein(void);
static void start_fadeout(void);
static void start_wait_fadeout(void);

void start_init(void)                                   /* 0x06015408 */
{
    u32 c, base, base2;
    tv_off();
    if (R8(0x060AA825) & 1) {                           /* no start screen: straight to the race */
        W32(0x060A001C, 0x06010F0C);
        menu_call(0x06010F0C);
        return;
    }
    init_gspr();
    W8(0x060AA806, R8(0x060AA806) & 0xFE);
    W32(0x060A001C, 0x06015510);
    if (R8(0x060AA803) & 0x10) {
        c = (u32)RS16(0x060AA87E) << 2;
        sat_cd_request(R32(0x060156A0 + c));            /* SCRL_DST / FST / MST / LST.DAT -> 0x00244000 */
        base = R32(0x06015690 + c);
        base2 = 0xFDE13F68u;
    } else {
        base = 0;
        base2 = 0;
        menu_unported(0x06015408);
    }
    {
        u32 t = R32(0x06015680 + ((u32)RS16(0x060AA87E) << 2));
        t = cd_cg_set(t, base);
        t = cd_map_set2(t, base);
        cd_color_set(t, base);
    }
    {
        u32 t = 0x06015770;
        t = cd_cg_set(t, base2);
        t = cd_map_set(t, base2);
        cd_color_set(t, base2);
    }
    W32(0x25FE0020, 0x060B8000);
    W32(0x25FE0024, 0x25F00000);
    W32(0x25FE0028, 0);
    W32(0x25FE002C, 0x101);
    W32(0x25FE0034, 7);
    W32(0x25FE0030, 0x100);
    W32(0x25FE0030, 0x101);
    pre_fade_inout(0x2FF, 0x1FF, -54);
    W32(0x060AA178, 0x0601554C);
    W8(0x060A0001, 2);
    W8(0x060A0013, 2);
    W16(0x25F80020, 3);
    W16(0x060AA8E6, 3);
    tv_on();
}

void start_main(void)                                   /* 0x06015510 */
{
    do {
        W16(0x060AA800, 0x2804);
        menu_call(R32(0x060AA178));
        snd_call(0x06017580, 0, 0, 0);
        fade_inout_work();
        intm_sync(4);
    } while (!(R8(0x060AA803) & 2));
    W8(0x060AA803, R8(0x060AA803) & 0xFD);
}

static void start_wait_fadein(void)                     /* 0x0601554C */
{
    if (R8(0x060AA803) & 0x20) return;
    W32(0x060AA178, 0x06015594);
    W32(0x060A001C, 0x060154EC);
    W8(0x060AA803, R8(0x060AA803) | 2);
    if (RS16(0x060AA860) == 0 && R8(0x060AA810) == 2 && RS16(0x060AA87E) != 0) return;
    snd_call(0x06016F3C, 0, 0, 96);
}

static void start_fadeout(void)                         /* 0x06015594 */
{
    W32(0x060AA178, 0x060155A4);
    pre_fade_inout(0x1FF, 0x2FF, 8);
}

static void start_wait_fadeout(void)                    /* 0x060155A4 */
{
    if (R8(0x060AA803) & 0x20) return;
    W8(0x060AA803, R8(0x060AA803) | 2);
    W32(0x060A001C, 0x06010F0C);
}

static void start_second(void)                          /* 0x060154EC: back into start_main for the fade-out */
{
    W8(0x060AA806, R8(0x060AA806) & 0xFE);
    W8(0x060A0001, 2);
    W8(0x060A0013, 2);
    W32(0x060A001C, 0x06015510);
    /* tail call FUN_060041F0 (0x060041F0): the race's data set-up, replaced by the race port (play.c) */
    menu_unported(0x060041F0);
}

void menu_start_register(void)
{
    menu_reg(0x06015408, start_init);
    menu_reg(0x06015510, start_main);
    menu_reg(0x0601554C, start_wait_fadein);
    menu_reg(0x06015594, start_fadeout);
    menu_reg(0x060155A4, start_wait_fadeout);
    menu_reg(0x060154EC, start_second);
}
