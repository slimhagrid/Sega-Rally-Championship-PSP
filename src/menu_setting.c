/* M8: the car set-up screen ("SETTING"): cs_setting 0x06026894 .. 0x06027700 (setting_main 0x06026A30 and its state handlers), cs_setting_pset 0x060276E0 .. 0x06028600 (its
 * picture setters, file CAR_SET0.DAT), line_scrl 0x060267A8 (the line-scroll table helpers) and the small shared helpers of the closure (pad_check, check_cursor_posi,
 * check_tune_flg, the colour flash state 0x0601FEA4; the save-data write 0x0602FC40 is bup_main_save, menu_bup.c).
 *
 * Screen vector words: [0x060A001C] = main vector (setting_main), [0x060A8800] = the screen's own state handler, [0x060A8808] = the second handler (the line-scroll animation)
 * which runs every pass while [0x060A88E4] != 0.
 * Variables (halfwords unless noted):
 *   0x060A88A4 cursor (row 0 = car, 1..5 = steering / tyres / suspension ..., 6 = the tuning row, 7 = "back")   0x060A88A6 previous cursor   0x060A88AC value column of the row   0x060A88B0 previous value
 *   0x060A88B2 animation step count   0x060A88CC animation speed   0x060A88D8 screen mode (0 = car row, 1 = car choice, 2 = rows)   0x060A88DE confirm (0 none, 1 confirm, -1 back)
 *   0x060A88C0 / 0x060A88C4 vertical / horizontal direction of the pad   0x060A88E4 second handler active   0x060A88EA exit flag   0x060A8902 (byte) picture fade off
 *   0x060A88F1 (byte) car slot   0x060A88F2 (byte) chosen car   0x060A88F3 .. 0x060A88F9 (bytes) the set-up values being edited   0x060A882C / 0x060A8830 the 3D car position
 *   0x06040064 + 8 * car: the saved set-up of a car (+1 .. +5 values, +6 car model, +7 ), 0x06040063 tune bits, 0x0604002A tune count, 0x0604002B flag.
 * The original uses r11 as a register that survives between functions (the "slide finished" flag of the animation handlers); it is modelled by r11reg.
 */
#include "satlib.h"
#include "menu_lib.h"
#include "satmenu.h"
#include "sat_save.h"

static u32 r11reg;                                      /* the original's r11 (see above) */

/* ---- shared helpers (the originals live in other modules; these are the copies this screen needs) ---- */
static u32 pic_base(void) { return (R8(0x060AA803) & 0x10) ? 0xFDA2A000u : 0; }     /* r13 of the picture setters: the table addresses of the CAR_SET0.DAT blocks -> LWRAM */

static u32 mix555(u32 a, u32 b, s32 t)                  /* 0x0601DB9A: per channel a + (b - a) * t / 65536 */
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

static void pal_anim(void)                              /* 0x0602EF68 with r14 = 0x060E5980: the palette cross fade block */
{
    const u32 b = 0x060E5980;
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

/* 0x06024A60 pad_check (the callers pass a mode in r2: 0 vertical only, 1 horizontal only, 2 both).  The mode never takes effect: key_repeat (called first) leaves its own pointer
 * 0x060A8C00 in r2, so the tests of r2 against 0 / 1 / 2 all fail.  Result: [0x060A88C0] = vertical direction (UP -1, DOWN +1), [0x060A88C4] = horizontal direction (LEFT -1, RIGHT +1;
 * not read when DOWN is pressed; r4 carries the vertical result: UP + LEFT gives -2), [A8810] = the keys, [A88DE] = -1 (B) / 1 (A, C, START) */
static void pad_check(void)
{
    u32 keys = key_repeat(0);
    s32 r4 = 0;
    W16(0x060A88C0, 0);
    W16(0x060A88C4, 0);
    W32(0x060A8810, keys);
    if (keys & 0x1000) { r4 += -1; W16(0x060A88C0, r4); }
    if (keys & 0x2000) {
        r4 += 1; W16(0x060A88C0, r4);
    } else {
        if (keys & 0x4000) { r4 += -1; W16(0x060A88C4, r4); }
        if (keys & 0x8000) { r4 += 1; W16(0x060A88C4, r4); }
    }
    if (R32(0x060A8810) & 0x100) W16(0x060A88DE, 0xFFFF);
    if (R32(0x060A8810) & 0xE00) W16(0x060A88DE, 1);
}

/* 0x06022E7A: moves the cursor variable (sel 0: [A88A4], else [A88AC]) by [A88C0] inside lo..hi with wrap; returns the new value */
static s32 check_cursor_posi(s32 hi, s32 lo, int sel)
{
    u32 var = sel ? 0x060A88AC : 0x060A88A4;
    s32 v = RS16(var) + RS16(0x060A88C0);
    if (lo - 1 >= v) v = hi;
    else if (v >= hi + 1) v = lo;
    W16(var, v);
    return v;
}

/* 0x06024BA0: the number of significant bits of the tune byte [0x06040063] -> [0x0604002A] (and returned) */
static s32 check_tune_flg(void)
{
    s32 r0 = RS8(0x06040063);
    s32 cnt = 0;
    if (r0 != 0) {
        do { r0 = (s32)((u32)r0 >> 1); cnt++; } while (r0 != 0);
    }
    W8(0x0604002A, cnt);
    return cnt;
}

/* ---- line_scrl (0x060267A8 ..): the line scroll tables in VDP2 VRAM (0x25E7FE00 = table A, 0x25E7FC00 = table B); entry = base + 4 * index, halfword ---- */
static void lscrl_init(int idx, int cnt, s32 val)       /* 0x060267A8 */
{
    u32 p = 0x25E7FE00u + ((u32)idx << 2);
    do { W16(p, val); p -= 4; } while (--cnt);
}

static int lscrl_plus(int idx, int cnt, s32 lim, s32 d)   /* 0x060267C6: add d to cnt entries ending at idx, clip at lim from above; returns 1 when the last (lowest) entry has reached lim */
{
    u32 p = 0x25E7FE00u + ((u32)idx << 2);
    do {
        s32 v = RS16(p) + d;
        if (v >= lim) v = lim;
        W16(p, v);
        p -= 4;
    } while (--cnt);
    p += 4;
    return RS16(p) == lim;
}

static int lscrl_minus(int idx, int cnt, s32 lim, s32 d)  /* 0x06026808: clip at lim from below */
{
    u32 p = 0x25E7FE00u + ((u32)idx << 2);
    do {
        s32 v = RS16(p) + d;
        if (lim >= v) v = lim;
        W16(p, v);
        p -= 4;
    } while (--cnt);
    p += 4;
    return RS16(p) == lim;
}

static int spr_plus(u32 var, s32 d, s32 lim)            /* 0x06026848: the packed (x << 16 | y) position; x += d, clipped at lim; returns 1 at the limit */
{
    u32 v = R32(var) + ((u32)d << 16);
    s32 hi = (s16)(v >> 16), lo = (s16)v;
    int r = 0;
    if (hi >= lim) { v = (u32)(lo + (s32)((u32)lim << 16)); r = 1; }
    W32(var, v);
    return r;
}

static int spr_minus(u32 var, s32 d, s32 lim)           /* 0x0602686E */
{
    u32 v = R32(var) + ((u32)d << 16);
    s32 hi = (s16)(v >> 16), lo = (s16)v;
    int r = 0;
    if (lim >= hi) { v = (u32)(lo + (s32)((u32)lim << 16)); r = 1; }
    W32(var, v);
    return r;
}

/* ---- 3D calls of the screen (the car / object are drawn by the host from the variables; only the stores that reach compared memory are kept) ---- */
static void display_car_tm(u32 pos)                     /* 0x060266FC with r14 = 0x060E5180, r12 = 128, r13 = pos: the car turntable (set_poly_offset, get_matrixbuf, dispSelectCar2) */
{
    W32(0x060E5190, R32(0x060E5190) + 128);
    menu3d_car(0x060E5180, R32(pos), 1);
}

static void disp_obj_cs(void)                           /* 0x06023C14: the object of the screen (0x06023C84: set_poly_offset, matrix set-up; draw_offset_polygon) */
{
    W32(0x060E511C, R32(0x060E511C) + 128);
    menu3d_model(R32(0x06023C50 + 4 * (u32)(u16)RS16(0x060A88A4)), R32(0x060A882C));    /* draw_offset_polygon(r5 = the object of the row) */
}

static void cs_set_crs(void)                            /* 0x06023B48: the camera / object block at 0x060E5100 */
{
    W32(0x060E5124, 0x06069D74);
    W32(0x060E510C, 0x00028000);
    W32(0x060E5110, 0x00030000);
    W32(0x060E5114, 0x00610000);
    W32(0x060E5118, 0xC00);
    W32(0x060E511C, 0);
    W32(0x060E5120, 0);
}

static void put_modeltype(void) { W8(0x060E5180 + 92, R8(0x060AA80A)); }       /* 0x0602672C with r14 = 0x060E5180 */
static void put_modeltype2(void) { W8(0x060E5180 + 92, R8(0x060AA813)); }      /* 0x06026734 */

/* ---- cs_setting_pset: pictures (file CAR_SET0.DAT) ---- */
static void cs_color_chg_init(void)                     /* 0x0602806C: the palette cross fade block 0x060E5980 */
{
    W32(0x060E5980, 0);
    W32(0x060E5984, 0x060B8400);
    W32(0x060E5988, 0x002BF000);
    W32(0x060E598C, 0x060B8E00);
    W32(0x060E5990, 0x100);
    W32(0x060E5994, 0);
    W32(0x060E5998, 0x002BF200);
    W32(0x060E599C, 0);
}

static void cs_color_chg(void)                          /* 0x0602808E: every pass */
{
    s32 mode;
    u32 r4, r5, step, coef;
    if (R8(0x060A8902) == 1) return;
    mode = RS16(0x060A88D8);
    if (mode == 0) { pal_anim(); return; }
    if (2 > mode) { r4 = 0x060B8400; r5 = 0x060B8600; }
    else { r4 = 0x060B8800; r5 = 0x002BF400; }
    r11reg = 0x060A88BC;
    step = (u32)RS16(0x060A88BC);
    coef = R16(0x0602F000 + 2 * step);
    if (!(coef > 0)) step = 0; else step++;
    W16(0x060A88BC, step);
    pal_mix(r4, r5, 0x060B8E00, 0x100, (s32)coef);
}

static void delete_cslay0(void)                         /* 0x060278EC: clears the tune boxes the car has not unlocked yet */
{
    s32 cnt = check_tune_flg();
    s32 n = 4 - cnt;
    u32 r8 = 0x06027940, base = pic_base();
    if (n == 0) return;
    n *= 2;
    do {
        box_copy16(R32(r8) + base, R32(r8 + 4), 0x4000);
        r8 += 8;
    } while (--n);
}

void set_pict_setting(void)                             /* 0x060276E0: called by the mode select while it slides out */
{
    u32 base = 0, t;
    if (R8(0x060AA803) & 0x10) {
        sat_load_name_at(0x060278DC);                   /* CAR_SET0.DAT */
        base = 0xFDA2A000u;
    } else {
        menu_unported(0x060276E0);                      /* the BPE-from-memory path is not used when the disc is the source */
    }
    W32(0x060A8870, base);
    t = cd_map_set(0x060277AC, base);
    cd_color_set(t, base);
    t = cd_map_set(0x060278B0, base);
    cd_color_set(t, base);
    delete_cslay0();
}

static void cs_chg_color_bg(void)                       /* 0x06027980 */
{
    u32 base = pic_base();
    cd_map_set(0x060279CC, base);
    cd_color_set(RS16(0x060A88DE) == 1 ? 0x060279DC : 0x06027A10, base);
}

static void cs_map_set_lay0(void)                       /* 0x06027A44 */
{
    box_fill16(0, 0x25E38400, 0x003F0014);
    lscrl_init(10, 3, 0x00A0);
    lscrl_init(15, 5, (s16)0xFF4C);
    lscrl_init(29, 12, 0x0090);
    box_fill16(0, 0x25E3C408, 0x00260012);
    cd_map_set(0x06027AC8, pic_base());
}

static void cs_map_chg(void)                            /* 0x06027B68 */
{
    u32 dst;
    if (RS16(0x060A88DE) == 1) {
        dst = 0x25E38408;
    } else {
        lscrl_init(25, 18, 0);
        box_fill16(0, 0x25E38000, 0x00400012);
        dst = 0x25E3C408;
    }
    box_fill16(0, dst, 0x00280012);
    lscrl_init(25, 18, 0x00B4);
    cd_map_set(0x06027BF8, pic_base());
}

static void cs_plane_chg(void)                          /* 0x06027C74 */
{
    cd_map_set(0x06027CB8, pic_base());
    box_fill16(0, 0x25E38400, 0x00160012);
}

static void cs_tune_pict(void)                          /* 0x06027F78: the boxes of the tune stages the car has */
{
    s32 n = 3 - check_tune_flg();
    u32 r8 = 0x06027FC8, base = pic_base();
    if (!(n > 0)) return;
    do {
        box_copy16(R32(r8) + base, R32(r8 + 4), 0x3262);        /* r4 = next word + r13, r5 = the one after */
        r8 += 8;
    } while (--n);
}

static void set_button_pict(void)                       /* 0x06027D34 */
{
    s32 row;
    u32 base;
    box_fill16(0, 0x25E383D0, 0x00180009);
    base = pic_base();
    row = RS16(0x060A88A4);
    if (row == 7) return;
    cd_map_set(R32(0x06027D94 + 4 * row), base);
    if (RS16(0x060A88A4) == 6) cs_tune_pict();
}

static void cs_pict_bright(void)                        /* 0x060280E6: the boxes of the highlighted row and of the one left */
{
    u32 base = pic_base();
    u32 mode4 = (u32)RS16(0x060A88D8) << 2;
    u32 r11 = R32(0x06028194 + mode4), r12 = R32(0x060281A0 + mode4);
    u32 r10 = R32(0x060281AC + mode4);
    s32 r8 = RS16(r10 + ((u32)RS16(0x060A88A4) << 1));
    s32 r9 = RS16(r10 + ((u32)RS16(0x060A88A6) << 1));
    r12 += (u32)r9;
    cd_map_set(r12, base);
    r11 += (u32)r8;
    cd_map_set(r11, base);
    r11reg = r11;
}

static void cs_pict_stop(void)                          /* 0x06028460 */
{
    cd_map_set(0x0602848C, pic_base());
}

static void cs_pict_dark(void)                          /* 0x060284A8 */
{
    u32 base = pic_base();
    u32 t = cd_map_set(0x060283A8, base);
    cd_map_set(t, base);
}

static void cs_pict_bright_cslay3(void)                 /* 0x060284CA */
{
    u32 base = pic_base();
    u32 mode4;
    u32 r11, r12;
    if (RS16(0x060A88A4) == 7) return;
    mode4 = (u32)RS16(0x060A88A4) << 2;
    r11 = R32(0x06028550 + mode4);
    r12 = R32(0x0602856C + mode4);
    r11 += (u32)RS16(0x06028544 + ((u32)RS16(0x060A88AC) << 1));
    r12 += (u32)RS16(0x06028544 + ((u32)RS16(0x060A88B0) << 1));
    cd_map_set(r12, base);
    cd_map_set(r11, base);
    r11reg = r11;
}

/* ---- the row values being edited: [0x060A88F3 ..] <-> [0x060A88AC] ---- */
static void get_row_value(void)                         /* 0x06027554: the value of the cursor row -> [A88AC] */
{
    W16(0x060A88AC, RS8(R32(0x06027594 + 4 * RS16(0x060A88A4))));
}

static void put_row_value(void)                         /* 0x06027574 */
{
    W8(R32(0x06027594 + 4 * RS16(0x060A88A4)), R16(0x060A88AC));
}

static void cs_set_carset_default(void)                 /* 0x06027484: the factory set-up */
{
    s32 r0;
    W8(0x060A88F3, 0);
    W8(0x060A88F4, 1);
    W8(0x060A88F5, 2);
    W8(0x060A88F6, 2);
    W8(0x060A88F7, 2);
    W8(0x060A88F8, 2);
    check_tune_flg();
    r0 = RS8(0x0604002A);
    if (r0 == 4) r0 = 3;
    W8(0x060A88F9, r0);
}

static void cs_set_carset_saved(void)                   /* 0x060274E8: the saved set-up of car [A88F2] */
{
    u32 p = 0x06040064 + (((u32)(R8(0x060A88F2) - 1)) << 3);
    W8(0x060A88F3, R8(p + 5));
    W8(0x060A88F4, R8(p + 1));
    W8(0x060A88F5, R8(p + 2));
    W8(0x060A88F6, R8(p + 3));
    W8(0x060A88F7, R8(p + 4));
    W8(0x060A88F8, R8(p + 7));
    W8(0x060AA80A, R8(p + 6));
    W8(0x060A88F9, R8(0x060A88F2) - 1);
}

static void cs_set_carset(void)                         /* 0x06027432 */
{
    if (RS8(0x060A88F2) == 0) cs_set_carset_default();
    else cs_set_carset_saved();
}

static void cs_store_carset(void)                       /* 0x060275B0: the edited set-up -> the save data of car [A88AC]; a new tune stage when the last car slot was edited */
{
    u32 p = 0x06040064 + (((u32)RS16(0x060A88AC)) << 3);
    s32 cnt, r3;
    W8(p + 5, R8(0x060A88F3));
    W8(p + 1, R8(0x060A88F4));
    W8(p + 2, R8(0x060A88F5));
    W8(p + 3, R8(0x060A88F6));
    W8(p + 4, R8(0x060A88F7));
    W8(p + 7, R8(0x060A88F8));
    W8(p + 6, R8(0x060AA80A));
    cnt = check_tune_flg();
    if (RS16(0x060A88AC) == cnt) {
        W8(0x0604002B, 1);
        r3 = RS8(0x0604002A) + 1;
        W8(0x0604002A, r3);
        W8(0x06040063, R8(0x06027654 + r3));
    }
    bup_main_save();                                        /* 0x0602FC40 */
}

static void cs_set_model2(void)                         /* 0x0602765C: the model of the car in slot [A88AC] */
{
    u32 p = 0x06040064 + ((u32)RS16(0x060A88AC) << 3);
    W8(0x060AA813, R8(p + 6));
    put_modeltype2();
}

/* ---- the colour flash state (0x0601FEA4 / 0x0601FF0C, module ac_g_select): copies two palettes alternately into [A8860] for [A88BC] steps, then continues at [A8874] ---- */
static void flash_state(void)                           /* 0x0601FEA4 */
{
    if (R8(0x060A8902) != 1) {
        box_fill16(0, 0x25E3A0CA, 0x00030003);
        box_fill16(0, 0x25E3A0D0, 0x00030003);
    }
    W16(0x060A88B8, 2);
    W16(0x060A88C0, 1);
    W32(0x060A8800, 0x0601FF0C);
}

static void flash_step(void)                            /* 0x0601FF0C */
{
    u32 r5 = R32(0x060A8860), r6 = R32(0x060A8864), r7 = R32(0x060A8868), r8 = R32(0x060A886C), r13 = R32(0x060A8870);
    s32 r1 = RS16(0x060A88B8) - 1;
    s32 idx;
    W16(0x060A88B8, r1);
    if (r1 != 0) return;
    W16(0x060A88B8, 2);
    idx = RS16(0x060A88C0);
    if (RS16(0x060A8878) == 2) idx += 2;
    switch (idx) {
    case 0: copy_l(r7 + r13, r5, r6); W16(0x060A88C0, 1); break;
    case 1: copy_l(r8 + r13, r5, r6); W16(0x060A88C0, 0); break;
    case 2: box_copy16(r7 + r13, r5, r6); W16(0x060A88C0, 1); break;
    default: box_fill16(0, r5, r8); W16(0x060A88C0, 0); break;
    }
    {
        s32 c = RS16(0x060A88BC) - 1;
        W16(0x060A88BC, c);
        if (c == 0) W32(0x060A8800, R32(0x060A8874));
    }
}

/* ---- the screen's states ---- */
static void st_init(void)                               /* 0x06026894 (_module_cs_setting) */
{
    W32(0x060A001C, 0x0602425C);
    W16(0x25F800F8, 0x606);
    W16(0x25F800FA, 0x604);
    W16(0x25F800FC, 2);
    /* rotate_light(0xA000, 0, 0): the light vector of the 3D pipeline */
    cs_set_crs();
    W16(0x060B9048, 224);
    W16(0x25F8009A, 0x3200);
    W32(0x25F800A4, 0x12F3FF00);
    lscrl_init(25, 18, 0);
    W16(0x060AA8B0, 0);
    W16(0x060AA8B2, 0);
    W16(0x060AA8B4, 0);
    W16(0x060AA8B6, 0);
    W16(0x060A88A4, 0);
    W16(0x060A88AC, 0);
    W16(0x060A88A6, 0);
    W16(0x060A88CC, 0xFFFA);
    W16(0x060A88CE, 1);
    W16(0x060A88B6, 14);
    W16(0x060A88C0, 0);
    W16(0x060A88DE, 0);
    W16(0x060A88D8, 0);
    W16(0x060A88EA, 0);
    W8(0x060A88F1, 0);
    W8(0x060A88F2, 0);
    W8(0x0604002B, 0);
    W8(0x060AA80A, 0);
    W16(0x060A88E4, 0);
    W16(0x060A88AA, 0);
    W32(0x060A8830, R32(0x060A882C));
    W32(0x060A8850, 0x00460020);
    key_repeat_init();
    cs_color_chg_init();
    cs_pict_bright();
    W32(0x060A8800, 0x06026B54);
}

static void st_car_row(void)                            /* 0x06026B54: the cursor is on the car row (screen mode 0) */
{
    W16(0x060A88DE, 0);
    pad_check();
    if (RS16(0x060A88C0) != 0) {
        s32 r4;
        W16(0x060A88A6, R16(0x060A88A4));
        r4 = check_cursor_posi(check_tune_flg(), 0, 0);
        W8(0x060A88F2, r4);
        if (r4 != 0) W8(0x060AA80A, R8(0x0604006A + (((u32)(r4 - 1)) << 3)));
        else W8(0x060AA80A, R8(0x060A88F1));
        put_modeltype();
        cs_pict_bright();
        snd_call(0x06016FBC, 72, 0, 0);
    }
    switch (RS16(0x060A88DE)) {
    case 0:
        return;
    case 1:
        if (RS16(0x060A88A4) == 0) {
            s32 r0;
            W16(0x060A88BC, 0);
            W16(0x060A88D8, 1);
            W16(0x060A88A6, 0);
            r0 = RS8(0x060A88F1);
            if (r0 > 1) r0 = 1;
            W16(0x060A88A4, r0);
            cs_pict_stop();
            snd_call(0x06016FBC, 73, 0, 0);
            W32(0x060A8800, 0x06026C8A);
        } else {
            snd_call(0x06016FBC, 73, 0, 0);
            W32(0x060A8800, 0x06026F8E);
        }
        break;
    default:
        snd_call(0x06016FBC, 77, 0, 0);
        W32(0x060A8800, 0x06026F74);
        break;
    }
}

static void st_c8a(void)                                /* 0x06026C8A */
{
    cs_pict_bright();
    W32(0x060A8800, 0x06026C9E);
}

static void st_car_choice(void)                         /* 0x06026C9E: choose the car (screen mode 1) */
{
    W16(0x060A88DE, 0);
    pad_check();
    if (RS16(0x060A88C4) != 0) {
        s32 r4;
        W16(0x060A88C0, RS16(0x060A88C4));
        W16(0x060A88A6, R16(0x060A88A4));
        r4 = check_cursor_posi(1, 0, 0);
        W8(0x060AA80A, r4);
        W8(0x060A88F1, r4);
        put_modeltype();
        cs_pict_bright();
        snd_call(0x06016FBC, 72, 0, 0);
    }
    switch (RS16(0x060A88DE)) {
    case 0:
        return;
    case 1:
        snd_call(0x06016FBC, 73, 0, 0);
        W32(0x060A8800, 0x06026F8E);
        break;
    default:
        W16(0x060A88D8, 0);
        W16(0x060A88A4, 0);
        cs_pict_dark();
        cs_pict_bright();
        snd_call(0x06016FBC, 77, 0, 0);
        W32(0x060A8800, 0x06026B54);
        break;
    }
}

static void st_backto_flash(void)                       /* 0x06026EE8: starts the colour flash that ends in st_back_row (0x06026F22) */
{
    W8(0x060A8902, 1);
    W16(0x060A88BC, 20);
    W32(0x060A8860, 0x060B8E00);
    W32(0x060A8864, 128);
    W32(0x060A8868, 0x0281B668);
    W32(0x060A886C, 0x0281C268);
    W32(0x060A8874, 0x06026F22);
    W16(0x060A8878, 0);
    W32(0x060A8800, 0x0601FEA4);
}

static void st_back_row(void)                           /* 0x06026F22 */
{
    W16(0x060A88A4, 7);
    W16(0x060A88A6, 6);
    cs_pict_bright();
    W32(0x060A8808, 0x06026FD2);
    W16(0x060A88E4, 1);
    W8(0x060A8902, 0);
    W32(0x060A8800, 0x06026D6E);
}

static void st_rows(void)                               /* 0x06026D6E: the rows of the set-up (screen mode 2) */
{
    W16(0x060A88DE, 0);
    pad_check();
    if (RS16(0x060A88C0) != 0) {                        /* up / down: the next row */
        W16(0x060A88A6, R16(0x060A88A4));
        check_cursor_posi(7, 0, 0);
        cs_pict_bright();
        snd_call(0x06016FBC, 72, 0, 0);
        W32(0x060A8808, 0x06026FD2);
        W16(0x060A88E4, 1);
        return;
    }
    if (RS16(0x060A88E4) != 0) return;
    if (RS16(0x060A88C4) != 0) {                        /* left / right: the value of the row */
        s32 row = RS16(0x060A88A4), r6;
        int go = 1;
        W16(0x060A88C0, RS16(0x060A88C4));
        W16(0x060A88B0, R16(0x060A88AC));
        if (6 > row) {
            r6 = RS16(0x06026EDC + 2 * row);
        } else if (row == 6) {
            r6 = RS8(0x0604002A);
            if (r6 == 4) r6 -= 1;
        } else {
            go = 0;
            r6 = 0;
        }
        if (go) {
            check_cursor_posi(r6, 0, 1);
            put_row_value();
            cs_pict_bright_cslay3();
            snd_call(0x06016FBC, 72, 0, 0);
        }
    }
    switch (RS16(0x060A88DE)) {
    case 0:
        return;
    case 1:
        if (RS16(0x060A88A4) == 6) {
            cs_store_carset();
            st_backto_flash();
            snd_call(0x06016FBC, 73, 0, 0);
        } else if (RS16(0x060A88A4) == 7) {
            snd_call(0x06016FBC, 78, 0, 0);
            W32(0x060A8800, 0x06026F54);
        }
        break;
    default:
        W16(0x060A88B2, 1);
        W16(0x060A88CC, 0xFFE7);
        snd_call(0x06016FBC, 77, 0, 0);
        W32(0x060A8800, 0x060271CE);
        break;
    }
}

static void st_save_exit(void)                          /* 0x06026F54: leave with saving */
{
    bup_main_save();                                        /* 0x0602FC40 */
    pre_fade_inout(0, 255, 17);
    W32(0x060A8800, 0x0602767E);
}

static void st_exit(void)                               /* 0x06026F74: leave without saving */
{
    pre_fade_inout(0, 255, 17);
    W32(0x060A8800, 0x0602767E);
}

static void st_f8e(void)                                /* 0x06026F8E */
{
    W16(0x060A88B2, 1);
    W16(0x060A88CC, 25);
    W32(0x060A8800, 0x06026FF2);
}

static void st_fa4(void)                                /* 0x06026FA4 */
{
    W16(0x060A88E4, 0);
    cs_map_chg();
    W32(0x060A882C, R32(0x060A8830));
    W16(0x060A88B2, 1);
    W16(0x060A88CC, 25);
    W32(0x060A8800, 0x060272E4);
}

static void sub_fd2(void)                               /* 0x06026FD2 (second handler) */
{
    get_row_value();
    W16(0x060A88B2, 1);
    W16(0x060A88CC, 0xFFDD);
    W32(0x060A8808, 0x060271CE);
}

static void st_ff2(void)                                /* 0x06026FF2: the rows slide in (car row -> rows) */
{
    s32 r7 = RS16(0x060A88B2), r4 = RS16(0x060A88CC), r1, r0;
    lscrl_plus(10, 3, 0x00A0, r4);
    r4 = -r4;
    lscrl_minus(15, 5, (s16)0xFF4C, r4);
    spr_plus(0x060A882C, RS16(0x060A88CC), 0xFA);
    r4 = RS16(0x060A88CC);
    r1 = 17;
    do {
        r0 = lscrl_plus(r1, 2, 0x00A0, r4);
        if (r0 == 1) r11reg = 1;
        r7--; r1 += 2; r4 += 2;
    } while (r7 != 0);
    if (r0 == 1) {
        W32(0x060A882C, R32(0x060A882C) - 6);
        cs_chg_color_bg();
        cs_map_chg();
        W32(0x060A8800, 0x0602714C);
        W16(0x060A88D8, 2);
        W16(0x060A88A4, 0);
        W16(0x060A88CC, -(RS16(0x060A88CC) - 1));
        W16(0x060A88B2, 9);
        return;
    }
    r0 = RS16(0x060A88B2) + 1;
    if (r0 >= 7) {
        if (r11reg != 1) W16(0x060A88CC, RS16(0x060A88CC) - 2);
        r0 = 5;
    }
    W16(0x060A88B2, r0);
}

static void st_14c(void)                                /* 0x0602714C */
{
    s32 r7 = RS16(0x060A88B2), r4, r1, r0;
    spr_minus(0x060A882C, RS16(0x060A88CC) * 2, 64);
    r4 = RS16(0x060A88CC);
    r1 = 9;
    do {
        r0 = lscrl_minus(r1, 2, 0, r4);
        if (r0 == 1) r11reg = 1;
        r7--; r1 += 2; r4 += -1;
    } while (r7 != 0);
    if (r0 == 1) {
        cs_plane_chg();
        cs_pict_bright();
        cs_set_carset();
        W32(0x060A8800, 0x06026D6E);
        W16(0x060A88E4, RS16(0x060A88E4) + 1);
        W32(0x060A8808, 0x06026FD2);
    }
    r7 = RS16(0x060A88B2) + 1;
    if (r7 >= 9) r7 = 9;
    if (r11reg != 1) W16(0x060A88CC, RS16(0x060A88CC) - 1);
    W16(0x060A88B2, r7);
}

static void sub_1ce(void)                               /* 0x060271CE (second handler): the row sliding back */
{
    s32 r4 = RS16(0x060A88CC), r0;
    r0 = lscrl_minus(13, 6, 0, r4);
    if (r0 == 1) {
        if (RS16(0x060A88DE) == -1) {
            W32(0x060A8800, 0x06026FA4);
            return;
        }
        /* cmp/gt r2,r5 compares r5 (= 13, the index left by lscrl_minus) with 5: always true, the clear of [A88E4] is never reached */
        W16(0x060A88B2, 1);
        W16(0x060A88CC, 25);
        W32(0x060A8808, 0x06027270);
    }
}

static void sub_270(void)                               /* 0x06027270 (second handler) */
{
    s32 r4, r0;
    set_button_pict();
    get_row_value();
    W16(0x060A88B0, 0);
    cs_pict_bright_cslay3();
    r4 = RS16(0x060A88CC);
    r0 = lscrl_plus(13, 6, 0x00B8, r4);
    if (r0 == 1) {
        W32(0x060A8808, 0x06026FD2);
        W16(0x060A88E4, RS16(0x060A88E4) - 1);
    }
}

static void st_2e4(void)                                /* 0x060272E4: back to the car row (slide in) */
{
    s32 r7 = RS16(0x060A88B2), r4 = RS16(0x060A88CC), r1 = 9, r0;
    do {
        r0 = lscrl_plus(r1, 2, 0x00A0, r4);
        if (r0 == 1) r11reg = 1;
        r7--; r1 += 2; r4 += 2;
    } while (r7 != 0);
    if (r0 == 1) {
        put_modeltype();
        cs_chg_color_bg();
        cs_map_set_lay0();
        delete_cslay0();
        W16(0x060A88D8, 0);
        W16(0x060A88A6, 0);
        W16(0x060A88A4, RS8(0x060A88F2));
        W32(0x060A8800, 0x060273B0);
        W16(0x060A88CC, -(RS16(0x060A88CC) - 1));
        W16(0x060A88B2, 5);
        return;
    }
    r0 = RS16(0x060A88B2) + 1;
    if (r0 >= 9) {
        if (r11reg != 1) W16(0x060A88CC, RS16(0x060A88CC) - 2);
        r0 = 1;
    }
    W16(0x060A88B2, r0);
}

static void st_3b0(void)                                /* 0x060273B0 */
{
    s32 r7 = RS16(0x060A88B2), r4 = RS16(0x060A88CC), r1, r0;
    lscrl_minus(10, 3, 0, r4);
    r4 = -r4;
    lscrl_plus(15, 5, 0, r4);
    spr_minus(0x060A882C, RS16(0x060A88CC), 70);
    r4 = RS16(0x060A88CC);
    r1 = 17;
    do {
        r0 = lscrl_minus(r1, 2, 0, r4);
        if (r0 == 1) r11reg = 1;
        r7--; r1 += 2; r4 += 2;
    } while (r7 != 0);
    if (r0 == 1) {
        cs_pict_bright();
        W32(0x060A8800, 0x06026B54);
    }
    r7 = RS16(0x060A88B2) + 1;
    if (r7 >= 6) r7 = 6;
    if (r11reg != 1) W16(0x060A88CC, RS16(0x060A88CC) - 1);
    W16(0x060A88B2, r7);
}

static void st_white_out(void)                          /* 0x0602767E */
{
    fade_inout_work();
    if (!(R8(0x060AA803) & 0x20)) W16(0x060A88EA, 1);
}

static void st_white_out2(void)                         /* 0x0602769C: the same wait with done flag 2 (the race / the time attack's exit) */
{
    fade_inout_work();
    if (!(R8(0x060AA803) & 0x20)) W16(0x060A88EA, 2);
}

/* ---- main ---- */
void setting_main(void)                                 /* 0x06026A30 */
{
    W32(0x060A8800, 0x06026894);
    do {
        m_mkpoly_frame_init();
        menu_call(R32(0x060A8800));
        cs_color_chg();
        if (RS16(0x060A88E4) != 0) menu_call(R32(0x060A8808));
        snd_call(0x06017580, 0, 0, 0);
        if (RS16(0x060A88EA) != 0) W8(0x060AA803, R8(0x060AA803) | 2);
        W16(0x060AA994, 0);
        if (RS16(0x060A88D8) < 2) {
            display_car_tm(0x060A882C);
        } else if (RS16(0x060A88A4) == 6) {
            if (RS8(0x0604002A) != RS16(0x060A88AC)) {
                cs_set_model2();
                display_car_tm(0x060A8830);
            }
        } else {
            disp_obj_cs();
        }
        /* game_zsort: the polygon sort (3D pipeline) */
        r11reg = 0;                                     /* the 3D calls leave their own junk in r11 (never 1: checked against the oracle) */
        intm_sync(46);
    } while (!(R8(0x060AA803) & 2));
    W8(0x060AA803, R8(0x060AA803) & 0xFD);
    W32(0x060A001C, 0x06022894);
}

void menu_setting_register(void)
{
    menu_reg(0x06026A30, setting_main);
    menu_reg(0x06026894, st_init);
    menu_reg(0x06026B54, st_car_row);
    menu_reg(0x06026C8A, st_c8a);
    menu_reg(0x06026C9E, st_car_choice);
    menu_reg(0x06026D6E, st_rows);
    menu_reg(0x06026F22, st_back_row);
    menu_reg(0x06026F54, st_save_exit);
    menu_reg(0x06026F74, st_exit);
    menu_reg(0x06026F8E, st_f8e);
    menu_reg(0x06026FA4, st_fa4);
    menu_reg(0x06026FD2, sub_fd2);
    menu_reg(0x06026FF2, st_ff2);
    menu_reg(0x0602714C, st_14c);
    menu_reg(0x060271CE, sub_1ce);
    menu_reg(0x06027270, sub_270);
    menu_reg(0x060272E4, st_2e4);
    menu_reg(0x060273B0, st_3b0);
    menu_reg(0x0602767E, st_white_out);
    menu_reg(0x0602769C, st_white_out2);
    menu_reg(0x060276E0, set_pict_setting);
    menu_reg(0x0601FEA4, flash_state);
    menu_reg(0x0601FF0C, flash_step);
}
