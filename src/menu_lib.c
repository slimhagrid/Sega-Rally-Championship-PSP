/* M8: shared helpers of the menu screens, ported from the original (box copies 0x0601D6xx..D9xx, clroffset = colour offset / fades, variety = picture table loaders,
 * key_repeat).  Arguments the original passes in registers are explicit parameters; the original address is in the comment of every function. */
#include "satlib.h"
#include "menu_lib.h"
#include "satmenu.h"
#include <stdlib.h>
#include <stdio.h>

/* ---- block fills / copies (0x0601D674 ..) ---- */
void copy_b(u32 src, u32 dst, u32 n)                    /* 0x0601D674 */
{
    while (n--) { W8(dst, R8(src)); src++; dst++; }
}

void copy_l(u32 src, u32 dst, u32 n)                    /* 0x0601D694 */
{
    while (n--) { W32(dst, R32(src)); src += 4; dst += 4; }
}

void fill16(u32 dst, u32 val, u32 n)                    /* 0x0601D6B0 */
{
    while (n--) { W16(dst, val); dst += 2; }
}

void fill32(u32 dst, u32 val, u32 n)                    /* 0x0601D6BC */
{
    while (n--) { W32(dst, val); dst += 4; }
}

/* a w x h rectangle of 16-bit cells from a list (header: u16 w, u16 h) to a 64-cell wide plane (row = 0x80 bytes); `add` is added to every cell */
void box_copy16(u32 src, u32 dst, u32 add)              /* 0x0601D938 */
{
    s32 w = RS16(src), h = RS16(src + 2);
    src += 4;
    do {
        s32 c = w;
        u32 p = dst;
        do { W16(p, R16(src) + add); src += 2; p += 2; } while (--c);
        dst += 0x80;
    } while (--h);
}

void box_copy32(u32 src, u32 dst, u32 add)              /* 0x0601D964: 32-bit cells, row = 0x100 bytes */
{
    s32 w = RS16(src), h = RS16(src + 2);
    src += 4;
    do {
        s32 c = w;
        u32 p = dst;
        do { W32(p, R32(src) + add); src += 4; p += 4; } while (--c);
        dst += 0x100;
    } while (--h);
}

void box_fill16(u32 val, u32 dst, u32 wh)               /* 0x0601D986: wh = (w << 16) | h */
{
    s32 w = (s16)(wh >> 16), h = (s16)wh;
    do {
        s32 c = w;
        u32 p = dst;
        do { W16(p, val); p += 2; } while (--c);
        dst += 0x80;
    } while (--h);
}

void box_fill32(u32 val, u32 dst, u32 wh)               /* 0x0601D9A6 */
{
    s32 w = (s16)(wh >> 16), h = (s16)wh;
    do {
        s32 c = w;
        u32 p = dst;
        do { W32(p, val); p += 4; } while (--c);
        dst += 0x100;
    } while (--h);
}

/* ---- clroffset: the VDP2 colour offset registers and the fades ---- */
void setcoloffset(s32 en, s32 sel, s32 a, s32 b, s32 c) /* 0x060223D0 */
{
    W16(0x25F80110, en);
    W16(0x25F80112, sel);
    if (sel > 0) {
        W16(0x25F8011A, a);
        W16(0x25F8011E, b);
        W16(0x25F8011C, c);
    } else {
        W16(0x25F80114, a);
        W16(0x25F80116, b);
        W16(0x25F80118, c);
    }
    W16(0x060A887C, a);
    W16(0x060A887E, b);
    W16(0x060A8880, c);
}

void inccoloffset(s32 sel, s32 a, s32 b, s32 c)        /* 0x06022434 */
{
    a += RS16(0x060A887C); W16(0x060A887C, a);
    b += RS16(0x060A887E); W16(0x060A887E, b);
    c += RS16(0x060A8880); W16(0x060A8880, c);
    if (sel > 0) {
        W16(0x25F8011A, a);
        W16(0x25F8011C, b);
        W16(0x25F8011E, c);
    } else {
        W16(0x25F80114, a);
        W16(0x25F80116, b);
        W16(0x25F80118, c);
    }
}

void white_in(s32 sel, s32 a, s32 b, s32 c)             /* 0x06022494: only offsets that are not negative grow */
{
    s32 t;
    t = RS16(0x060A887C); if (t >= 0) { a += t; W16(0x060A887C, a); }
    t = RS16(0x060A887E); if (t >= 0) { b += t; W16(0x060A887E, b); }
    t = RS16(0x060A8880); if (t >= 0) { c += t; W16(0x060A8880, c); }
    if (sel > 0) {
        W16(0x25F8011A, a);
        W16(0x25F8011C, b);
        W16(0x25F8011E, c);
    } else {
        W16(0x25F80114, a);
        W16(0x25F80116, b);
        W16(0x25F80118, c);
    }
}

void sl_white_out(void)                                 /* 0x06022500 */
{
    white_in(0, 51, 51, 51);
    if (RS16(0x060A887C) > 255) W16(0x060A88EA, 1);
}

void fade_inout_init(s32 speed, s32 c0, s32 c1, s32 c2, s32 t0, s32 t1, s32 t2, s32 s0, s32 s1, s32 s2)   /* 0x06022564 */
{
    W16(0x25F80110, 0x7F);
    W16(0x25F80112, 0);
    W8(0x060AA803, R8(0x060AA803) | 0x20);
    W16(0x060AA88A, speed);
    W16(0x060AA88C, speed);
    W16(0x060AA88E, c0); W16(0x25F80114, c0);
    W16(0x060AA890, c1); W16(0x25F80116, c1);
    W16(0x060AA892, c2); W16(0x25F80118, c2);
    W16(0x060AA894, t0);
    W16(0x060AA896, t1);
    W16(0x060AA898, t2);
    W16(0x060AA89A, s0);
    W16(0x060AA89C, s1);
    W16(0x060AA89E, s2);
}

void pre_fade_inout(s32 from, s32 to, s32 step)         /* 0x0602253C: all three channels start at `from`, go to `to` by `step` per tick */
{
    fade_inout_init(1, from, from, from, to, to, to, step, step, step);
}

static int fade_channel(u32 cur, u32 tgt, u32 stp, u32 reg)
{
    s32 c = RS16(cur), t = RS16(tgt), s;
    if (c == t) return 0;
    s = RS16(stp);
    c += s;
    if (s > 0) { if (c >= t) c = t; }
    else { if (c <= t) c = t; }
    W16(cur, c);
    W16(reg, c);
    return 1;
}

void fade_inout_work(void)                              /* 0x060225DC: one fade step per pass while [0x060AA803] bit 5 is set */
{
    int moving;
    if (!(R8(0x060AA803) & 0x20)) return;
    W16(0x060AA88A, RS16(0x060AA88A) - 1);
    if (RS16(0x060AA88A) != 0) return;
    W16(0x060AA88A, RS16(0x060AA88C));
    moving  = fade_channel(0x060AA88E, 0x060AA894, 0x060AA89A, 0x25F80114);
    moving += fade_channel(0x060AA890, 0x060AA896, 0x060AA89C, 0x25F80116);
    moving += fade_channel(0x060AA892, 0x060AA898, 0x060AA89E, 0x25F80118);
    if (moving == 0) W8(0x060AA803, R8(0x060AA803) & 0xDF);
}

/* ---- variety: picture tables.  A table = u32 count, then count x (u32 source (+ base), u32 destination, u32 argument); the functions return the end of the table (the original's r14) ---- */
u32 cd_map_set(u32 tbl, u32 base)                       /* 0x060227E4: 16-bit cell boxes */
{
    u32 n = R32(tbl);
    tbl += 4;
    do {
        box_copy16(R32(tbl) + base, R32(tbl + 4), R32(tbl + 8));
        tbl += 12;
    } while (--n);
    return tbl;
}

void menu_dbg_cd(const char *w, u32 tbl, u32 n);
u32 cd_map_set2(u32 tbl, u32 base)                      /* 0x06022816: 32-bit cell boxes */
{
    u32 n = R32(tbl);
    menu_dbg_cd("map2", tbl, n);
    tbl += 4;
    do {
        box_copy32(R32(tbl) + base, R32(tbl + 4), R32(tbl + 8));
        tbl += 12;
    } while (--n);
    return tbl;
}

u32 cd_color_set(u32 tbl, u32 base)                     /* 0x06022832: palettes (long words) */
{
    u32 n = R32(tbl);
    tbl += 4;
    do {
        copy_l(R32(tbl) + base, R32(tbl + 4), R32(tbl + 8));
        tbl += 12;
    } while (--n);
    return tbl;
}

/* ---- key repeat (rec_main 0x0602D504 / 0x0602D524): two repeat channels at 0x060A8C00 / 0x060A8C08: last pad, first delay, repeat interval */
void key_repeat_init(void)                              /* 0x0602D504 */
{
    W16(0x060A8C00, 0xFFFF);
    W16(0x060A8C08, 0xFFFF);
    W16(0x060A8C02, 32);
    W16(0x060A8C0A, 32);
    W16(0x060A8C04, 9);
    W16(0x060A8C0C, 9);
}

/* auto repeat of the direction keys (0x0602D524): channel 0 = player 1 (variables at 0x060A8C00), 1 = player 2 (0x060A8C08).  Returns the new presses (low 12 bits) and the held / repeating direction bits */
u32 key_repeat(s32 player)                              /* 0x0602D524 */
{
    u32 chan = 0x060A8C00;
    s32 held = RS16(0x060AA990), edge = RS16(0x060AA994);
    s32 r1, r0, cnt;
    if (player > 0) {
        held = RS16(0x060AA996);
        edge = RS16(0x060AA99A);
        chan += 8;
    }
    r1 = RS16(chan) & (s32)0xFFFFA000;
    r0 = (s32)0xFFFFA000 & held;
    if (r0 != 0 && r1 == r0) goto cont;
    r1 = RS16(chan) & 0x5000;
    r0 = 0x5000 & held;
    if (r0 != 0 && r1 == r0) goto cont;
    W16(chan, held);
    cnt = RS16(chan + 2);
    goto done;
cont:
    W16(chan, held);
    cnt = RS16(chan + 6) - 1;
    if (cnt == 0) cnt = RS16(chan + 4);
    else held = 0;
done:
    W16(chan + 6, cnt);
    return ((u32)edge & 0x0FFF) + ((u32)held & 0xFFFFF000u);        /* held is sign-extended (mov.w) and the mask 0xF000 is loaded with mov.w too: bit 15 (RIGHT) fills the upper half */
}

void delete_nbg0(void)                                  /* 0x060233EC */
{
    box_fill32(0, 0x25E28000, 0x00400040);
    box_fill32(0, 0x25E2C000, 0x00400040);
    box_fill32(0, 0x25E38000, 0x00400040);
    box_fill32(0, 0x25E3C000, 0x00400040);
}

void sat_load_name_at(u32 nameaddr)                     /* FUN_0600F354: r4 = the name of a scatter file */
{
    char name[40];
    int i = 0;
    while (R8(nameaddr) && i < 39) name[i++] = (char)R8(nameaddr++);
    name[i] = 0;
    if (sat_load_file(name)) { fprintf(stderr, "load %s failed\n", name); menu_unported(nameaddr); }
}

void menu_dbg_cd(const char *w, u32 tbl, u32 n) { if (getenv("MENU_DBG")) fprintf(stderr, "%s tbl=%08x n=%u\n", w, tbl, n); }

/* ---- the action objects: slots of 0x80 bytes, run in order; the action function address is looked up in the menu registry (menu_reg) ---- */
u32 act_cur;

void actor_init(u32 slot, u32 fn, u32 n)                /* 0x0601BB5C: r4 = function, r7 = number of slots, r14 = first slot */
{
    u32 r3 = 0, r5 = 0, r14 = slot;
    do {
        W32(r14 + 0, fn);
        W32(r14 + 4, r5);
        W16(r14 + 68, 0);
        W16(r14 + 70, r3);
        r3++;
        r5 = r14;
        W32(r14 + 8, r14 + 0x80);
        if (--n == 0) { W32(r14 + 8, 0); break; }
        r14 += 0x80;
    } while (1);
}

void actor_run(u32 slot, u32 n)                         /* 0x0601BB02 */
{
    u32 r14 = slot;
    do {
        u32 fn = R32(r14);
        if (fn) {
            act_cur = r14;
            menu_call(fn);
        }
        r14 += 0x80;
    } while (--n);
}
