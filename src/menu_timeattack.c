/* M8: the TIME ATTACK screens (cs_tm_attck 0x06024068 .. 0x06025478, cs_tm_att_pset 0x0602552C .. 0x06026162, the car / sprite calls of cs_poly_set / cs_car_set, line_scrl 0x060267A8 ..).
 *
 * Entered from the mode select (cursor 1 -> main vector 0x0602425C; the mode select has already called set_pict_tm_att 0x0602552C while it slid out).
 * Screen vector words: [0x060A001C] = main vector (tm_attck_main), [0x060A8800] = the screen's own handler (a state machine), [0x060A8808] = a second handler that
 * runs every pass while [0x060A88E4] != 0 (the sprite slide-in / slide-out of the record box).
 * Variables (halfwords unless noted):
 *   0x060A88D6 page: 0 car, 1 tuned car (saved car list), 2 transmission, 3 course, 4 lap / mode      0x060A88A4 cursor of the page   0x060A88A6 previous cursor
 *   0x060A88AC transmission cursor (0 auto, 1 manual)    0x060A88C0 vertical key direction (-1 / 0 / 1)    0x060A88C4 horizontal key direction
 *   0x060A88DE answer: 0 nothing, 1 confirm (START / A / C), -1 cancel (B)    0x060A88EA done flag (1 = back to the mode select, 2 = start the race)
 *   0x060A88B2 slide step (1..4)   0x060A88CC slide speed   0x060A88CE lines per step   0x060A88E4 sprite slide counter
 *   save data: 0x0604001A saved-car mode (1 = the tuned car list is used)  0x0604001C car cursor  0x0604001E tuned car index  0x06040020 / 22 / 24 transmission of the car / of the first / of the other cars
 *   0x06040026 course  0x06040028 lap mode   0x0604002A number of tuned cars (bit length of the tune byte 0x06040063)   0x06040062 unlock bits (2 = lake, 4 = stratos)   0x0604006A + 8 * n tuned car records
 *   0x060A88FB tune flag, 0x060A88FC stratos flag, 0x060A88FD lake flag, 0x060A88FE last car cursor, 0x060A8901 last course cursor (calc_padmax0 / 1 / 3)
 * Game result (read by the race start): 0x060AA80A car, 0x060AA809 transmission, 0x060AA811 tuned car flag, 0x060AA87E course, 0x060AA860 game type (4 time attack, 6 with lap mode), 0x060AA82E mirror.
 */
#include "satlib.h"
#include "menu_lib.h"
#include "satmenu.h"
#include <stdio.h>

/* r11 of the original is a free register of the screen's code: the slide states set it to 1 when a scroll line reached its target and never clear it, and tm_pict_bright /
 * pict_bright_tmlay2 / pre_fade_inout leave other values in it (a table pointer, a table entry, the fade step).  The slide states test it for == 1, so it is tracked here. */
static u32 r11;

#define DISC_BASE 0xFDA35000u       /* table addresses of TM_ATT0.DAT (0x02800000 ..) -> the data block loaded at 0x00235000 */

/* ---- palette cross fade of the car picture (0x0602EF68 with r14 = 0x060E5980, FUN_0601DB80 / FUN_0601DB9A) ---- */
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

static void pal_anim(void)                              /* 0x0602EF68 with r14 = 0x060E5980 */
{
    const u32 b = 0x060E5980;
    int guard = 0;
    for (;;) {
        u32 r4 = R32(b + 4), r5 = R32(b + 8);
        if (++guard > 100) menu_unported(0x0602EF68);   /* the fade table (0x0602EFBC) is missing */
        u32 phase = R32(b + 28), step, coef;
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

/* ---- unlock flags (save data 0x06040062 / 0x06040063) ---- */
static u32 check_stratos_flg(void)                      /* 0x06024B7C */
{
    return (R8(0x06040062) & 4) ? 1 : 0;
}

static u32 check_lake_flg(void)                         /* 0x06024B8E */
{
    return (R8(0x06040062) & 2) ? 1 : 0;
}

static u32 check_tune_flg(void)                         /* 0x06024BA0: also leaves the bit length of the tune byte in [0x0604002A] (and in r3) */
{
    u32 v = (u32)RS8(0x06040063), r1 = 0, r3 = 0;
    if (v != 0) {
        do { v >>= 1; r3++; } while (v != 0);
        r1 = 1;
    }
    W8(0x0604002A, r3);
    return r1;
}

static void calc_padmax0(void)                          /* 0x06029A00 (cs_battle_2p) */
{
    W8(0x060A88FB, check_tune_flg());
    W8(0x060A88FC, check_stratos_flg());
    W8(0x060A88FE, check_stratos_flg() + 1 + R8(0x060A88FB));
}

static void calc_padmax1(void)                          /* 0x06029A28 (cs_battle_2p) */
{
    W8(0x060A88FF, R8(0x0604002A) - 1);
}

static void calc_padmax3(void)                          /* 0x06029A50 (cs_battle_2p) */
{
    u32 r = check_lake_flg();
    W8(0x060A88FD, r);
    W8(0x060A8901, r + 2);
}

/* ---- cursor with wrap (0x06022E7A -> 0x06022F16): moves the cursor variable by the key direction [0x060A88C0] inside lo..hi and returns it ---- */
static s32 check_cursor_posi(u32 var, s32 hi, s32 lo)    /* 0x06022E7A */
{
    s32 v = RS16(var) + RS16(0x060A88C0);
    if (lo - 1 >= v) v = hi;
    else if (v >= hi + 1) v = lo;
    W16(var, v);
    return v;
}

/* ---- the keys of the screen (0x06024A60).  The original takes a mode in r2 (0 = up / down, 1 = left / right, 2 = both) but key_repeat leaves 0x060A8C00 in r2, so the mode is never
 * what the caller passed: the horizontal keys are read whenever DOWN is not pressed (and r4 keeps what the vertical keys left in it).
 * Sets [0x060A88C0] (up -1, down +1) and [0x060A88C4] (left / right), and the answer [0x060A88DE]: -1 for B, 1 for START / A / C ---- */
static void pad_check(void)                             /* 0x06024A60 */
{
    u32 keys = key_repeat(0);
    s32 r4 = 0;
    W16(0x060A88C0, 0);
    W16(0x060A88C4, 0);
    W32(0x060A8810, keys);
    if (keys & 0x1000) { r4 = -1; W16(0x060A88C0, r4); }
    if (keys & 0x2000) {
        r4 += 1;
        W16(0x060A88C0, r4);
    } else {
        if (keys & 0x4000) { r4 += -1; W16(0x060A88C4, r4); }
        if (keys & 0x8000) { r4 += 1; W16(0x060A88C4, r4); }
    }
    keys = R32(0x060A8810);
    if (keys & 0x100) W16(0x060A88DE, -1);
    if (keys & 0xE00) W16(0x060A88DE, 1);
}

static void check_mirror(void)                          /* 0x06024A48: Y + C held: mirror mode on */
{
    if (R16(0x060AA990) == 0x220) W8(0x060AA82E, R8(0x060AA82E) | 1);
}

/* ---- line scroll table of VDP2 (0x25E7FE00 + 4 * line): lscrl_init / lscrl_plus / lscrl_minus (line_scrl 0x060267A8 ..) ---- */
static void lscrl_init(u32 line, u32 n, u32 val)        /* 0x060267A8 */
{
    u32 p = 0x25E7FE00u + (line << 2);
    do { W16(p, val); p -= 4; } while (--n);
}

static u32 lscrl_plus(u32 line, s32 n, s32 limit, s32 delta)   /* 0x060267C6: add delta to n entries going down from `line`, clamp to limit; 1 when the last entry has reached it */
{
    u32 p = 0x25E7FE00u + (line << 2);
    do {
        s32 v = RS16(p) + delta;
        if (v >= limit) v = limit;
        W16(p, v);
        p -= 4;
    } while (--n);
    p += 4;
    return RS16(p) == limit;
}

static u32 lscrl_minus(u32 line, s32 n, s32 limit, s32 delta)  /* 0x06026808 */
{
    u32 p = 0x25E7FE00u + (line << 2);
    do {
        s32 v = RS16(p) + delta;
        if (limit >= v) v = limit;
        W16(p, v);
        p -= 4;
    } while (--n);
    p += 4;
    return RS16(p) == limit;
}

/* sprite positions: a long word (x << 16) + y (y signed, borrows from x) */
static u32 spr_plus(s32 delta, u32 var, s32 limit)      /* 0x06026848: x += delta, stopped at limit.  Returns 1 when the limit was reached */
{
    u32 v = R32(var) + ((u32)delta << 16);
    s32 hi = (s16)(v >> 16), lo = (s16)v;
    u32 r = 0;
    if (hi >= limit) { v = (u32)lo + ((u32)limit << 16); r = 1; }
    W32(var, v);
    return r;
}

static u32 spr_minus(s32 delta, u32 var, s32 limit)     /* 0x0602686E */
{
    u32 v = R32(var) + ((u32)delta << 16);
    s32 hi = (s16)(v >> 16), lo = (s16)v;
    u32 r = 0;
    if (limit >= hi) { v = (u32)lo + ((u32)limit << 16); r = 1; }
    W32(var, v);
    return r;
}

/* ---- 3D parts: the pipeline's own work areas are not compared; what is kept is the state the screen itself changes ---- */
static void md_set_crs(void)                            /* 0x06023B08: the course model of the time attack background */
{
    W32(0x060E5124, 0x06066C74);
    W32(0x060E5100 + 12, 0xFFFFF000);
    W32(0x060E5100 + 16, 0x00024000);
    W32(0x060E5100 + 20, 0x00377000);
    W32(0x060E5100 + 24, 0x1000);
    W32(0x060E5100 + 28, 0);
    W32(0x060E5100 + 32, 0);
}

static void put_modeltype(u32 blk) { W8(blk + 92, R8(0x060AA80A)); }              /* 0x0602672C: the car model of the turntable block */

static void display_car_tm(void)                        /* 0x060266FC with r12 = 128, r13 = 0x060A882C, r14 = 0x060E5180: the turntable car turns by 128 */
{
    W32(0x060E5180 + 16, R32(0x060E5180 + 16) + 128);
    menu3d_car(0x060E5180, R32(0x060A882C), 1);            /* set_poly_offset(r4 = [r13]), dispSelectCar2 */
    /* set_poly_offset(r4 = [r13]), get_matrixbuf, dispSelectCar2: pipeline.  The car drawing leaves a leftover constant in r11 (the oracle shows 0x2800 / 0x2000 / 0x1c00 for the
     * cars): never 1, which is all the slide states look at */
    r11 = 0x2000;
}

static void course_turn(void)                           /* 0x06023C84: the course model turns by 128 (set_poly_offset, initial_matrix, translate, rotate_x / y: pipeline) */
{
    W32(0x060E5100 + 28, R32(0x060E5100 + 28) + 128);
}

static void disp_course(void)                           /* 0x06023BC8: the course model of the course / lap pages */
{
    W32(0x060E5124, R32(0x06023C04 + 4 * RS16(0x060AA87E)));
    course_turn();
    menu3d_model(R32(0x060E5124), R32(0x060A882C));     /* draw_offset_polygon(r5 = [0x060E5124]) */
}

static void disp_spr_tm(void)                           /* 0x06023CC0: the car picture sprite of the first pages */
{
    s32 r8 = 0;
    u32 r4;
    if (RS16(0x060A88E8) == 1) r8 = -RS16(0x060A88CC);
    r4 = R32(0x060A8858) + (u32)r8;
    W32(0x060A8858, r4);
    draw_sprite(r4, R32(0x06023D38 + 4 * R8(0x060AA80A)), 0x00010000);
}

static void disp_spr(void)                              /* 0x06023D44: the sprites of the course / lap pages */
{
    u32 tbl;
    draw_sprite(R32(0x060A8830), R32(0x06023DF8 + 4 * R8(0x060AA80A)), 0x00010000);
    tbl = RS16(0x0604001A) == 0 ? 0x06023E14u : 0x06023E1Cu;
    draw_sprite(R32(0x060A8830) + 0xFFE00000u, R32(tbl + 4 * R8(0x060AA809)), 0x00010000);
    draw_sprite(R32(0x060A8834), R32(0x06023E04 + 4 * RS16(0x060AA87E)), 0x00010000);
    draw_sprite(R32(0x060A8838), 0x06048310, 0x00010000);
}

/* ---- picture tables (TM_ATT0.DAT, loaded at 0x00235000): u32 count + entries for cd_map_set; the single entry variant has no count ---- */
static u32 cd_map_set_s(u32 tbl, u32 base)              /* 0x06022800 */
{
    box_copy16(R32(tbl) + base, R32(tbl + 4), R32(tbl + 8));
    return tbl + 12;
}

static u32 pict_base(void)                              /* r13 of the picture routines: the data block's offset when the files come from the disc */
{
    return (R8(0x060AA803) & 0x10) ? DISC_BASE : 0;
}

static void set_pict_tm_att(void)                       /* 0x0602552C: load TM_ATT0.DAT and put the first pictures */
{
    u32 base = 0, t, r12;
    if (R8(0x060AA803) & 0x10) {
        sat_load_name_at(0x0602570C);                   /* TM_ATT0.DAT */
        base = DISC_BASE;
    } else {
        menu_unported(0x0602552C);                      /* cd_cg_set: the BPE-from-memory path is not used when the disc is the source */
        return;
    }
    t = cd_map_set(0x06025618, base);
    r12 = check_stratos_flg();
    if (r12) t = cd_map_set(0x06025690, base);
    if (check_tune_flg()) t = cd_map_set(r12 == 0 ? 0x06025670u : 0x06025680u, base);
    (void)t;
    t = cd_color_set(0x060256A0, base);
    (void)t;
    t = cd_map_set(0x060256EC, base);
    cd_color_set(t, base);
}

static void tm_color_chg(void)                          /* 0x06025718: the palette fade block of the picture */
{
    W32(0x060E5980, 0);
    W32(0x060E5984, 0x060B8400);
    W32(0x060E5988, 0x060B8800);
    W32(0x060E598C, 0x060B8E00);
    W32(0x060E5990, 0x100);
    W32(0x060E5994, 0);
    W32(0x060E5998, 0x060B8A00);
    W32(0x060E599C, 0);
}

static void tm_pict_bright(void)                        /* 0x0602573A: highlight the current entry, un-highlight the previous one */
{
    u32 base = pict_base(), sel = (u32)RS16(0x060A88D6), t11, t12;
    if (sel == 0 && check_stratos_flg() == 1) sel = (u32)RS16(0x060A88D6) + 2;
    t11 = R32(0x060257CC + (sel << 2)) + ((u32)RS16(0x060A88A4) << 2);
    t12 = R32(0x060257E0 + (sel << 2)) + ((u32)RS16(0x060A88A6) << 2);
    cd_map_set(R32(t12), base);
    cd_map_set(R32(t11), base);
    r11 = t11;
}

static void pict_bright_tmlay2(void)                    /* 0x06025AFC: transmission pictures */
{
    u32 r12;
    r11 = (u32)RS16(0x06025B5C + 2 * RS16(0x060A88AC));
    r12 = (u32)RS16(0x06025B54 + 2 * RS16(0x060A88A4)) + r11;
    cd_map_set_s(0x06025B60 + r12, pict_base());
}

static void fn_060260b4(void)                           /* 0x060260B4: clear the car name boxes that are not unlocked */
{
    if (check_tune_flg() == 1) return;
    if (check_stratos_flg() == 0) box_fill16(0, 0x25E3883E, 0x000D0003);
    else box_fill16(0, 0x25E3893E, 0x000D0003);
}

static void fn_060260f6(void)                           /* 0x060260F6: clear the tuned car boxes that do not exist */
{
    s32 n;
    u32 p = 0x06026134;
    check_tune_flg();
    n = 4 - (s32)R8(0x0604002A);
    if (n < 0) menu_unported(0x060260F6);
    while (n-- > 0) {
        box_fill16(0, R32(p), 0x000D0003);
        p += 4;
    }
}

static void fn_06026144(void)                           /* 0x06026144 */
{
    if (check_lake_flg() != 1) box_fill16(0, 0x25E3893E, 0x000D0003);
}

static void chg_c_name(void)                            /* 0x06025BA8: the car names */
{
    u32 base = pict_base(), tbl;
    box_fill16(0, 0x25E3863E, 0x000B0008);
    if (RS16(0x060A88DE) == 1) tbl = 0x06025C34;
    else tbl = check_stratos_flg() == 0 ? 0x06025C98u : 0x06025CC0u;
    cd_map_set(tbl, base);
    if (RS16(0x060A88DE) == 1) fn_060260f6();
    else fn_060260b4();
}

static void chg_crs_name(void)                          /* 0x06025CF4: the course names */
{
    u32 base = pict_base(), tbl;
    box_fill16(0, 0x25E384AC, 0x0016000C);
    if (RS16(0x060A88DE) == 1) tbl = 0x06025DB0;
    else if (RS16(0x0604001A) == 0) tbl = check_stratos_flg() == 0 ? 0x06025EA8u : 0x06025F0Cu;
    else tbl = 0x06025E08;
    cd_map_set(tbl, base);
    if (RS16(0x060A88DE) == 1) fn_06026144();
    else if (RS16(0x0604001A) == 0) fn_060260b4();
    else fn_060260f6();
}

static void chg_lap_name(void)                          /* 0x06025F7C: the lap mode names */
{
    u32 base = pict_base(), tbl;
    box_fill16(0, 0x25E384AC, 0x0016000C);
    tbl = RS16(0x060A88DE) == 1 ? 0x06025FE8u : 0x06026034u;
    cd_map_set(tbl, base);
    if (RS16(0x060A88DE) == -1) fn_06026144();
}

static void delete_lap_seat(void)                       /* 0x0602608C */
{
    box_fill16(0, 0x25E38AE8, 0x00090004);
}

/* ---- the record box of the page (best times): record_connect 0x0602C9E8, FUN_06024CE2 / 06024CAA / 06024C6C ---- */
static u32 record_connect(int r4)                       /* 0x0602C9E8: the record entry of the game type / course / car / set-up (time attack types 4 and 6 only) */
{
    u32 type = (u32)RS16(0x060AA860), r3, p, r1;
    if (type == 0 || type == 2 || r4 == 1) menu_unported(0x0602C9E8);   /* the other game types belong to the record screens */
    r3 = (u32)RS16(0x060AA87E) << 4;
    r3 += (u32)RS8(0x060AA80A) << 2;
    r3 += (u32)RS8(0x060AA811) << 1;
    r3 += (u32)RS8(0x060AA809);
    r3 <<= 2;
    p = 0x06041020 + r3;
    r1 = R32(p + 0x110);
    if (type == 6) r1 += 40;
    (void)r1;
    return R32(p);
}

static void boxwrt_b2(u32 src, u32 dst, u32 cols, u32 rows, u32 add)   /* 0x060056D4: byte cells + add -> 16-bit cells, 64-cell plane */
{
    do {
        u32 d = dst, c = cols;
        do { W16(d, (u32)R8(src) + add); src++; d += 2; } while (--c);
        dst += 0x80;
    } while (--rows);
}

static void rec_digit_tens_units(s32 b, u32 r9, u32 r10)       /* 0x06024CC6: two digits of a BCD byte (tile = 0x6002 + 4 * digit, bottom row +2) */
{
    u32 r3 = (((u32)b & 0xF0) >> 2) + 0x6002, r0;
    W16(r9, r3);
    W16(r10, r3 + 2);
    r0 = (((u32)b & 0xF) << 2) + 0x6002;
    W16(r9 + 2, r0);
    W16(r10 + 2, r0 + 2);
}

static void rec_digits(u32 rec, u32 r9, u32 r10)        /* 0x06024CAA: minutes (units), seconds and hundredths of the record at `rec` */
{
    u32 r0 = (((u32)RS8(rec + 1) & 0xF) << 2) + 0x6002;
    W16(r9 + 2, r0);
    W16(r10 + 2, r0 + 2);
    r9 += 6; r10 += 6;
    rec_digit_tens_units(RS8(rec + 2), r9, r10);
    r9 += 6; r10 += 6;
    rec_digit_tens_units(RS8(rec + 3), r9, r10);
}

static void rec_box_frame(void)                         /* 0x06024CE2 */
{
    boxwrt_b2(0x06024D0C, 0x25E38AC4, 7, 2, 0x6002);
    boxwrt_b2(0x06024D0C, 0x25E38BC4, 7, 2, 0x6002);
}

static void rec_box_draw(void)                          /* 0x06024C6C */
{
    u32 rec;
    rec_box_frame();
    rec = record_connect(0);
    if (!(RS16(0x060A88D6) == 4 && RS16(0x060A88A4) == 1)) rec_digits(rec, 0x25E38BC2, 0x25E38C42);
    rec_digits(rec + 40, 0x25E38AC2, 0x25E38B42);
}

static void spr_offsets_init(void)                      /* 0x06024BBE: window / scroll offsets of the transmission page */
{
    u32 idx = (u32)RS16(0x060A88A4) << 2, a, b;
    pict_bright_tmlay2();
    a = 0x06024C10 + idx; b = 0x06024C04 + idx;
    W16(0x060AA8A2, R16(a));     W16(0x060A8896, R16(a));
    W16(0x060AA8A6, R16(a + 2)); W16(0x060A889A, R16(a + 2));
    W16(0x060AA8A0, R16(b));     W16(0x060A8894, R16(b));
    W16(0x060AA8A4, R16(b + 2)); W16(0x060A8898, R16(b + 2));
    W16(0x060AA8B0, 0x202);
    W16(0x060AA8B2, 2);
    W16(0x060AA8B4, 0);
    W16(0x060AA8B6, 0);
}

static void spr_offsets_clear(void)                     /* 0x06024C20 */
{
    W16(0x060A8894, 0);
    W16(0x060A8896, 0);
    W16(0x060A8898, 0);
    W16(0x060A889A, 0);
    W16(0x060AA8A0, 0);
    W16(0x060AA8A2, 0);
    W16(0x060AA8A4, 0);
    W16(0x060AA8A6, 0);
    W16(0x060AA8B0, 0);
    W16(0x060AA8B2, 0);
    W16(0x060AA8B4, 0);
    W16(0x060AA8B6, 0);
}

static void set_tuned_car(void)                         /* 0x06025478: the tuned car record -> the race set-up bytes */
{
    u32 r1 = 0x06040064 + ((u32)RS16(0x0604001C) << 3);
    W8(0x060AA811, 1);
    W8(0x060AA816, 0);
    W8(0x060AA80A, R8(r1 + 6));
    W8(0x060AA809, R8(r1 + 5));
    W8(0x060AA817, R8(r1 + 1));
    W8(0x060AA818, R8(r1 + 2));
    W8(0x060AA819, R8(r1 + 3));
    W8(0x060AA81A, R8(r1 + 4));
    W8(0x060AA81B, R8(r1 + 7));
}

/* ---- the screen's own states ([0x060A8800]) ---- */
static void tm_page_car(void)                           /* 0x060243D6: page 0, the car */
{
    u32 next = 0;
    W16(0x060A88DE, 0);
    pad_check();
    if (RS16(0x060A88C0) != 0) {
        s32 hi = RS8(0x060A88FE), r4;
        W32(0x060A8808, 0x06024D1A);
        W16(0x060A88E4, RS16(0x060A88E4) + 1);
        W16(0x060A88A6, R16(0x060A88A4));
        r4 = check_cursor_posi(0x060A88A4, hi, 0);
        W16(0x0604001C, r4);
        if (hi == r4 && RS8(0x060A88FB) == 1) {
            W16(0x0604001A, 1);
            r4 = RS8(0x0604006A + ((u32)RS16(0x0604001E) << 3));
        } else {
            W16(0x0604001A, 0);
        }
        W8(0x060AA80A, r4);
        put_modeltype(0x060E5180);
        tm_pict_bright();
        snd_call(0x06016FBC, 72, 0, 0);
    }
    if (RS16(0x060A88DE) != 0) {
        if (RS16(0x060A88DE) == -1) {
            snd_call(0x06016FBC, 77, 0, 0);
            next = 0x06024994;
        } else {
            s32 hi = RS8(0x060A88FE), r4 = RS16(0x060A88A4);
            if (r4 == hi && RS8(0x060A88FB) == 1) {
                W16(0x060A88D6, 1);
                W16(0x060A88A4, 0);
                snd_call(0x06016FBC, 73, 0, 0);
                next = 0x060249AE;
            } else {
                u32 v;
                W16(0x060A88D6, 2);
                v = RS16(RS16(0x060A88A4) == 0 ? 0x06040022u : 0x06040024u);
                W16(0x060A88AC, v);
                W16(0x06040020, v);
                W8(0x060AA809, v);
                spr_offsets_init();
                snd_call(0x06016FBC, 73, 0, 0);
                next = 0x060246DE;
            }
        }
        W32(0x060A8800, next);
    }
}

static void tm_page_tuned(void)                         /* 0x06024574: page 1, the tuned (saved) cars */
{
    u32 next;
    W16(0x060A88DE, 0);
    W16(0x0604001A, 1);
    pad_check();
    if (RS16(0x060A88C0) != 0) {
        s32 r4;
        u32 idx;
        if (RS8(0x0604002A) != 1) {
            W32(0x060A8808, 0x06024D1A);
            W16(0x060A88E4, RS16(0x060A88E4) + 1);
        }
        W16(0x060A88A6, R16(0x060A88A4));
        r4 = check_cursor_posi(0x060A88A4, RS8(0x0604002A) - 1, 0);
        W16(0x0604001E, r4);
        idx = (u32)r4 << 3;
        W8(0x060AA80A, R8(0x0604006A + idx));
        W8(0x060AA809, R8(0x06040069 + idx));
        put_modeltype(0x060E5180);
        tm_pict_bright();
        snd_call(0x06016FBC, 72, 0, 0);
    }
    if (RS16(0x060A88DE) != 0) {
        if (RS16(0x060A88DE) == 1) {
            u32 cur;
            W8(0x060AA811, 1);
            W16(0x0604001A, 1);
            cur = R16(0x060A88A4);
            W16(0x0604001C, cur);
            W8(0x060AA809, R8(0x06040069 + (cur << 3)));
            cur = R16(0x06040026);
            W16(0x060A88A4, cur);
            W16(0x060AA87E, cur);
            snd_call(0x06016FBC, 73, 0, 0);
            next = 0x060249D0;
        } else {
            W16(0x060A88D6, 0);
            W8(0x060AA811, 0);
            put_modeltype(0x060E5180);
            W16(0x060A88A4, RS8(0x060A88FE));
            snd_call(0x06016FBC, 77, 0, 0);
            next = 0x060249AE;
        }
        W32(0x060A8800, next);
    }
}

static void tm_page_trans(void)                         /* 0x060246DE: page 2, the transmission */
{
    W16(0x060A88DE, 0);
    pad_check();
    if (RS16(0x060A88C0) != 0) {
        s32 r4 = check_cursor_posi(0x060A88AC, 1, 0);
        W8(0x060AA809, r4);
        W16(0x06040020, r4);
        W16(RS16(0x060A88A4) == 0 ? 0x06040022u : 0x06040024u, r4);
        pict_bright_tmlay2();
        snd_call(0x06016FBC, 72, 0, 0);
    }
    if (RS16(0x060A88DE) != 0) {
        u32 next;
        if (RS16(0x060A88DE) == 1) {
            u32 v;
            W8(0x060AA811, 0);
            v = R16(0x06040026);
            W16(0x060A88A4, v);
            W16(0x060AA87E, v);
            snd_call(0x06016FBC, 73, 0, 0);
            next = 0x060249D0;
        } else {
            W16(0x060A88D6, 0);
            snd_call(0x06016FBC, 77, 0, 0);
            next = 0x060243D6;
        }
        W32(0x060A8800, next);
        spr_offsets_clear();
    }
}

static void tm_page_course(void)                        /* 0x060247CE: page 3, the course */
{
    W16(0x060A88DE, 0);
    pad_check();
    check_mirror();
    if (RS16(0x060A88C0) != 0) {
        s32 r4;
        W32(0x060A8808, 0x0602542A);
        W16(0x060A88E4, RS16(0x060A88E4) + 1);
        W16(0x060A88A6, R16(0x060A88A4));
        r4 = check_cursor_posi(0x060A88A4, RS8(0x060A8901), 0);
        W16(0x060AA87E, r4);
        W16(0x06040026, r4);
        rec_box_draw();
        tm_pict_bright();
        snd_call(0x06016FBC, 72, 0, 0);
    }
    if (RS16(0x060A88DE) != 0) {
        u32 next;
        if (RS16(0x060A88DE) == 1) {
            W16(0x060A88D6, 4);
            W16(0x060A88A4, R16(0x06040028));
            snd_call(0x06016FBC, 73, 0, 0);
            next = 0x060249FE;
        } else {
            W8(0x060AA82E, 0);
            delete_lap_seat();
            snd_call(0x06016FBC, 77, 0, 0);
            next = 0x060249D0;
        }
        W32(0x060A8800, next);
    }
}

static void tm_page_lap(void)                           /* 0x060248C6: page 4, the lap mode */
{
    W16(0x060A88DE, 0);
    pad_check();
    if (RS16(0x060A88C0) != 0) {
        s32 r4;
        W16(0x060A88A6, R16(0x060A88A4));
        r4 = check_cursor_posi(0x060A88A4, 1, 0);
        W16(0x06040028, r4);
        W16(0x060AA860, r4 == 1 ? 6 : 4);
        rec_box_draw();
        tm_pict_bright();
        snd_call(0x06016FBC, 72, 0, 0);
    }
    if (RS16(0x060A88DE) != 0) {
        u32 next;
        if (RS16(0x060A88DE) == 1) {
            snd_call(0x06016FBC, 78, 0, 0);
            next = 0x06024A12;
        } else {
            W8(0x060AA82E, 0);
            W16(0x060A88A4, R16(0x06040026));
            snd_call(0x06016FBC, 77, 0, 0);
            next = 0x060249FE;
        }
        W32(0x060A8800, next);
    }
}

static void tm_leave(void)                              /* 0x06024994: cancel on the first page: fade out, back to the mode select */
{
    pre_fade_inout(0, 255, 17);
    r11 = 17;
    W32(0x060A8800, 0x0602767E);
}

static void tm_go_car(void)                             /* 0x060249AE: leave the car page: slide the picture out, then the transmission / tuned car page */
{
    W32(0x060A8808, 0x06024D1A);
    W16(0x060A88E4, RS16(0x060A88E4) + 1);
    W16(0x060A88B2, 1);
    W16(0x060A88CC, 0xFFE7);
    W32(0x060A8800, 0x06024E18);
}

static void tm_go_course(void)                          /* 0x060249D0: slide to the course page */
{
    if (RS16(0x060A88DE) == -1) box_fill16(0, 0x25E38AC2, 0x00090007);
    W16(0x060A88B2, 1);
    W16(0x060A88CC, 0xFFE7);
    W32(0x060A8800, 0x06024F9A);
}

static void tm_go_lap(void)                             /* 0x060249FE: slide to the lap mode page */
{
    W16(0x060A88B2, 1);
    W16(0x060A88CC, 0xFFE7);
    W32(0x060A8800, 0x06025268);
}

static void tm_go_race(void)                            /* 0x06024A12: go: fade out, the done flag becomes 2 (race start) */
{
    if (RS16(0x06040028) == 1) W16(0x060AA860, 6);
    if (RS16(0x0604001A) == 1) set_tuned_car();
    pre_fade_inout(0, 255, 17);
    r11 = 17;
    W32(0x060A8800, 0x0602769C);
}

/* the fade-out wait states 0x0602767E (done flag 1: back to the mode select) and 0x0602769C (done flag 2: the race) are the "white out" handlers of cs_setting: ported with
 * the arcade select (menu_arcade.c) and the setting screen (menu_setting.c), registered by them */

/* ---- the second handler [0x060A8808]: the car sprite slides in / out beside the page change ---- */
static void tm_spr_out(void)                            /* 0x06024D1A */
{
    if (spr_minus(-48, 0x060A8858, -350) == 1) W32(0x060A8808, 0x06024D3A);
}

static void tm_spr_in(void)                             /* 0x06024D3A */
{
    if (spr_plus(48, 0x060A8858, -128) == 1) {
        W16(0x060A88E4, RS16(0x060A88E4) - 1);
        if (RS16(0x060A88E4) != 0) W32(0x060A8808, 0x06024D3A);
    }
}

static void tm_spr_out2(void)                           /* 0x0602542A */
{
    if (spr_minus(-25, 0x060A8834, -160) == 1) {
        W32(0x060A8808, 0x0602544E);
        W32(0x060A8834, 0xFEB80049);
    }
}

static void tm_spr_in2(void)                            /* 0x0602544E */
{
    if (spr_plus(25, 0x060A8834, -128) == 1) {
        W16(0x060A88E4, RS16(0x060A88E4) - 1);
        if (RS16(0x060A88E4) != 0) W32(0x060A8808, 0x0602544E);
    }
}

/* ---- line scroll slides ---- */

/* the common step of the slide: `groups` = [0x060A88B2] steps of [0x060A88CE] calls each; line index 13 + 2 * g; returns the result of the last call */
static u32 slide_lines(int plus, u32 line, s32 delta, s32 limit, s32 ddelta)    /* the loop at 0x06024E28 / 0x06024F28 / 0x0602503A / 0x060251CE / 0x0602528A / 0x0602539C */
{
    s32 groups = RS16(0x060A88B2), cnt = RS16(0x060A88CE), k;
    u32 r0 = 0;
    do {
        k = cnt;
        do {
            r0 = plus ? lscrl_plus(line, 2, limit, delta) : lscrl_minus(line, 2, limit, delta);
            if (r0 == 1) r11 = 1;
        } while (--k);
        groups--;
        line += 2;
        delta += ddelta;
        cnt = RS16(0x060A88CE);
    } while (groups != 0);
    return r0;
}

static void slide_tail_cap(void)                        /* 0x06024EF8 (also 0x06025120, 0x06025358): the step counter grows to 4; the speed drops by one each pass once it is there and no line has arrived yet */
{
    s32 r0 = RS16(0x060A88B2) + 1;
    if (r0 >= 4) {
        if (r11 != 1) W16(0x060A88CC, RS16(0x060A88CC) - 1);
        r0 = 4;
    }
    W16(0x060A88B2, r0);
}

static void slide_tail_every(void)                      /* 0x06024F7A (also 0x06025246, 0x06025408): as above but the speed drops every pass */
{
    s32 r7 = RS16(0x060A88B2) + 1;
    if (r7 >= 4) r7 = 4;
    if (r11 != 1) W16(0x060A88CC, RS16(0x060A88CC) - 1);
    W16(0x060A88B2, r7);
}

static void tm_slide_car_out(void)                      /* 0x06024E18: car page -> the old picture scrolls away (lines go down) */
{
    s32 cc = RS16(0x060A88CC);
    u32 r0 = slide_lines(0, 13, cc, -160, 2);
    if (r0 != 1) { slide_tail_cap(); return; }
    if (RS16(0x060A88DE) == 1) {
        W16(0x060A88A4, R16(0x0604001E));
        W16(0x060A88A6, 0);
    } else {
        W16(0x060A88A6, 0);
        if (check_stratos_flg() == 1) W16(0x060A88A4, 3);
        else W16(0x060A88A4, 2);
    }
    chg_c_name();
    tm_pict_bright();
    W32(0x060A8800, 0x06024F18);
    W16(0x060A88CC, -RS16(0x060A88CC));
    W16(0x060A88B2, 1);
}

static void tm_slide_car_in(void)                       /* 0x06024F18 */
{
    s32 cc = RS16(0x060A88CC);
    u32 r0 = slide_lines(1, 13, cc, 0, -1);
    if (r0 == 1) {
        u32 next;
        if (RS16(0x060A88DE) == 1) next = 0x06024574;
        else next = 0x060243D6;
        W32(0x060A8800, next);
        W16(0x060A88DE, 0);
    }
    slide_tail_every();
}

static void tm_slide_course_out(void)                   /* 0x06024F9A */
{
    s32 cc = RS16(0x060A88CC);
    u32 line = 10, r0;
    if (RS16(0x060A88DE) <= 0) {
        spr_minus(cc, 0x060A8830, -284);
        spr_minus(RS16(0x060A88CC), 0x060A8834, -300);
        spr_plus(-RS16(0x060A88CC), 0x060A8838, 180);
    } else {
        spr_minus(cc, 0x060A8858, -350);
    }
    spr_minus(RS16(0x060A88CC), 0x060A882C, -350);
    cc = RS16(0x060A88CC);
    {
        s32 k = RS16(0x060A88CE);
        do { lscrl_minus(line, 2, -160, cc); } while (--k);
    }
    r0 = slide_lines(0, 13, cc, -160, 2);
    if (r0 != 1) { slide_tail_cap(); return; }
    W16(0x060A88D6, RS16(0x060A88DE) > 0 ? 3 : RS16(0x0604001A));
    chg_crs_name();
    if (RS16(0x060A88DE) == 1) W16(0x060A88A4, R16(0x06040026));
    else W16(0x060A88A4, R16(0x0604001C));
    W16(0x060A88A6, 0);
    tm_pict_bright();
    W32(0x060A8800, 0x0602513C);
    W16(0x060A88CC, -(RS16(0x060A88CC) - 1));
    W16(0x060A88B2, 1);
}

static void tm_slide_course_in(void)                    /* 0x0602513C */
{
    s32 cc = RS16(0x060A88CC);
    u32 r0, line = 10;
    if (RS16(0x060A88DE) > 0) {
        spr_plus(cc, 0x060A8830, -112);
        spr_plus(RS16(0x060A88CC), 0x060A8834, -128);
        spr_minus(-RS16(0x060A88CC), 0x060A8838, 0);
    } else {
        spr_plus(cc, 0x060A8858, -128);
    }
    spr_plus(RS16(0x060A88CC), 0x060A882C, -60);
    cc = RS16(0x060A88CC);
    {
        s32 k = RS16(0x060A88CE);
        do { lscrl_plus(line, 2, 0, cc); } while (--k);
    }
    r0 = slide_lines(1, 13, cc, 0, -1);
    if (r0 == 1) {
        u32 next;
        if (RS16(0x060A88DE) == 1) { rec_box_draw(); next = 0x060247CE; }
        else if (RS16(0x0604001A) == 0) next = 0x060243D6;
        else next = 0x06024574;
        W32(0x060A8800, next);
    }
    slide_tail_every();
}

static void tm_slide_lap_out(void)                      /* 0x06025268 */
{
    s32 cc = RS16(0x060A88CC);
    u32 r0, line = 10;
    {
        s32 k = RS16(0x060A88CE);
        do { lscrl_minus(line, 2, -160, cc); } while (--k);
    }
    r0 = slide_lines(0, 13, cc, -160, 2);
    if (r0 != 1) { slide_tail_cap(); return; }
    if (RS16(0x060A88DE) == 1) {
        W16(0x060A88D6, 4);
        W16(0x060A88A4, R16(0x06040028));
    } else {
        u32 v;
        W16(0x060A88D6, 3);
        v = R16(0x06040026);
        W16(0x060A88A4, v);
        W16(0x060AA87E, v);
    }
    W16(0x060A88A6, 0);
    chg_lap_name();
    tm_pict_bright();
    W32(0x060A8800, 0x0602537A);
    W16(0x060A88CC, -RS16(0x060A88CC));
    W16(0x060A88B2, 1);
}

static void tm_slide_lap_in(void)                       /* 0x0602537A */
{
    s32 cc = RS16(0x060A88CC);
    u32 r0, line = 10;
    {
        s32 k = RS16(0x060A88CE);
        do { lscrl_plus(line, 2, 0, cc); } while (--k);
    }
    r0 = slide_lines(1, 13, cc, 0, -1);
    if (r0 == 1) {
        u32 next;
        if (RS16(0x060A88DE) == 1) { rec_box_draw(); next = 0x060248C6; }
        else { rec_box_draw(); W16(0x060AA860, 4); next = 0x060247CE; }
        W32(0x060A8800, next);
        W16(0x060A88DE, 0);
    }
    slide_tail_every();
}

/* ---- init / main loop ---- */
static void tm_attck_init(void)                         /* 0x06024068 */
{
    W32(0x060A001C, 0x0602425C);
    md_set_crs();
    W16(0x25F800F0, 0x404);
    W16(0x25F800F2, 0x404);
    W16(0x25F800F4, 0x404);
    W16(0x25F800F6, 0x404);
    W16(0x25F800F8, 0x606);
    W16(0x25F800FA, 0x404);
    W16(0x25F800FC, 2);
    W16(0x060B9048, 224);
    W16(0x25F8009A, 0x3200);
    W32(0x25F800A4, 0x12F3FF00);
    lscrl_init(25, 17, 0);
    W16(0x060A88E4, 0);
    W16(0x060AA8B0, 0);
    W16(0x060AA8B2, 0);
    W16(0x060AA8B4, 0);
    W16(0x060AA8B6, 0);
    W16(0x060A88A4, 0);
    W16(0x060A88A6, 0);
    W16(0x060A88CC, 0xFFFA);
    W16(0x060A88CE, 1);
    W16(0x060A88B6, 14);
    W16(0x060A88C0, 0);
    W16(0x060A88DE, 0);
    W16(0x060A88D6, 0);
    W16(0x060A88EA, 0);
    W16(0x060A88BE, 3);
    W8(0x060AA809, 0);
    W8(0x060AA811, 0);
    W8(0x060AA816, 0);
    W8(0x060AA81B, 2);
    W8(0x060AA820, 2);
    W8(0x060AA82E, 0);
    W16(0x060AA860, 4);
    W32(0x060A8850, 0xFFC40014);
    W32(0x060A8800, 0x060243D6);
    key_repeat_init();
    calc_padmax0();
    calc_padmax1();
    calc_padmax3();
    if (RS16(0x0604001A) == 0) W16(0x060A88A4, R16(0x0604001C));
    else W16(0x060A88A4, RS8(0x060A88FE));
    tm_color_chg();
    tm_pict_bright();
    W32(0x060A8830, 0xFF130038);
    W32(0x060A8834, 0xFF600049);
    W32(0x060A8838, 0x01150038);
    W16(0x060A88BC, 0);
}

void tm_attck_main(void)                                /* 0x0602425C */
{
    tm_attck_init();
    W16(0x060AA994, 0);
    W8(0x060A0001, 2);
    W8(0x060A0013, 2);
    sync_vout();
    do {
        m_mkpoly_frame_init();
        pal_anim();
        menu_call(R32(0x060A8800));
        snd_call(0x06017580, 0, 0, 0);
        if (RS16(0x060A88E4) != 0) menu_call(R32(0x060A8808));
        if (RS16(0x060A88EA) != 0) W8(0x060AA803, R8(0x060AA803) | 2);
        W16(0x060AA994, 0);
        /* set_light_obj (0x06023C70): rotate_light(0xA000, 0, 0) -> the light vector of the pipeline */
        if (RS16(0x060A88D6) <= 2) {
            display_car_tm();
            disp_spr_tm();
        } else {
            disp_course();
            disp_spr();
        }
        /* game_zsort: pipeline */
        intm_sync(46);
    } while (!(R8(0x060AA803) & 2));
    W8(0x060AA803, R8(0x060AA803) & 0xFD);
    if (RS16(0x060A88EA) == 1) {
        W32(0x060A001C, 0x06022894);                    /* back to the mode select */
    } else {
        W8(0x060AA816, 0);
        W16(0x060A88EA, 0);
        W8(0x060AA810, 0);
        W32(0x060A001C, 0x06015408);                    /* start_init: the race (not part of this package) */
    }
}

/* ---- the entry from the result / name entry screens: tm_set_tm_att 0x060262A4 (sets the screen up from scratch, fades in with the car on the turntable, then hands over to tm_attck_main) ---- */
static void ta_set_car_texture(void)                    /* 0x0601ABA2: the car models + textures (OBJECT.MAD), as in the mode select */
{
    if (R32(0x060D008C) == 0x4F424A20) return;
    W32(0x060D008C, 0x4F424A20);
    if (R8(0x060AA803) & 0x10) sat_cd_request(0x0601ABFC);
    else menu_unported(0x0601ABA2);
    W32(0x060D0048, 0x30002100);
    setup_texture(0x0601AF8C, 0xFDEE0000u);
}

static void ta_set_select_model(void)                   /* 0x0601ADD8: the menu model pack (SELECT.MAD) */
{
    if (R32(0x060D0090) == 0x53454C45) return;
    W32(0x060D0090, 0x53454C45);
    if (R8(0x060AA803) & 0x10) sat_cd_request(0x0601AE30);
    else menu_unported(0x0601ADD8);
    W32(0x060D0048, 0x76002A00);
    setup_texture(0x0601B00C, 0xFD8B0000u);
}

static void ta_set_car(void)                            /* 0x06026510: the two turntable car blocks (0x060E5180 / 0x060E5580) */
{
    W8(0x060E5180 + 92, 0);
    W32(0x060E5180 + 24, 0);
    W32(0x060E5180 + 28, 0);
    W32(0x060E5180 + 32, 0x00078000);
    W32(0x060E5180 + 12, 0xF00);
    W32(0x060E5180 + 16, 0);
    W32(0x060E5180 + 20, 0);
    W8(0x060E5580 + 92, 0);
    W32(0x060E5580 + 24, 0);
    W32(0x060E5580 + 28, 0);
    W32(0x060E5580 + 32, 0x00078000);
    W32(0x060E5580 + 12, 0xE00);
    W32(0x060E5580 + 16, 0x8000);
    W32(0x060E5580 + 20, 0);
}

static void ta_set_pict_md_slt(void)                    /* 0x0602358C: the mode select pictures (MD_SLT0.DAT), as in the mode select */
{
    u32 t, base;
    if (R8(0x060AA803) & 0x10) {
        sat_load_name_at(0x060236D8);
        base = 0xFDA44000u;
    } else {
        menu_unported(0x0602358C);
        return;
    }
    t = cd_map_set2(0x06023600, base);
    t = cd_color_set(t, base);
    cd_map_set2(t, base);
}

void tm_set_tm_att(void)                                /* 0x060262A4 */
{
    int i;
    tv_off();
    if (RS16(0x060A88EA) == 0) {
        pad_kind_init();
        init_gspr2();
        init_oscroll();
        delete_nbg0();
        W16(0x060AA994, 0);
    }
    /* FUN_0601D6C8: an empty routine */
    ta_set_car_texture();
    ta_set_select_model();
    mkpoly_init();
    ta_set_car();
    put_modeltype(0x060E5180);
    W16(0x25F80020, 31);
    W16(0x060AA8E6, 31);
    W16(0x25F800F8, 0x406);
    W16(0x25F800FA, 0x604);
    W16(0x25F800FC, 2);
    W16(0x25F80038, 0);
    W16(0x25F800F0, 0x404);
    W16(0x25F800F2, 0x404);
    W16(0x25F800F4, 0x404);
    W16(0x25F800F6, 0x404);
    for (i = 0; i < 8; i++) W16(0x25F80050 + 2 * i, R16(0x06026500 + 2 * i));
    W16(0x060B9048, 224);
    W16(0x060AA864, 0);
    W16(0x060AA868, 0);
    W16(0x060AA870, 0);
    W16(0x060AA86C, 0);
    W16(0x060AA876, 0);
    W16(0x060AA874, 0);
    W16(0x060AA87A, 0);
    W16(0x060AA878, 0);
    snd_call(0x060173AC, 3, 71, 0);
    snd_call(0x06017580, 0, 0, 0);
    if (RS16(0x060A88EA) == 0) ta_set_pict_md_slt();
    set_pict_tm_att();
    W16(0x060A88EA, 0);
    W32(0x060A882C, 0xFFC40016);
    W32(0x060A8858, 0xFF80003C);
    pre_fade_inout(255, 0, -18);
    tv_on();
    do {
        m_mkpoly_frame_init();
        fade_inout_work();
        snd_call(0x06017580, 0, 0, 0);
        if (!(R8(0x060AA803) & 32)) W8(0x060AA803, R8(0x060AA803) | 2);
        /* rotate_light(0xA000, 0, 0): pipeline */
        display_car_tm();
        disp_spr_tm();
        /* game_zsort: pipeline */
        intm_sync(46);
    } while (!(R8(0x060AA803) & 2));
    W8(0x060AA803, R8(0x060AA803) & 0xFD);
    W16(0x060A88DE, 0);
    W32(0x060A001C, 0x0602425C);
}

void return_tm_att(void)                                /* 0x06026284 */
{
    W16(0x060A88DE, 1);
    W32(0x060A001C, 0x0602425C);
}

void menu_timeattack_register(void)
{
    menu_reg(0x0602425C, tm_attck_main);
    menu_reg(0x060243D6, tm_page_car);
    menu_reg(0x06024574, tm_page_tuned);
    menu_reg(0x060246DE, tm_page_trans);
    menu_reg(0x060247CE, tm_page_course);
    menu_reg(0x060248C6, tm_page_lap);
    menu_reg(0x06024994, tm_leave);
    menu_reg(0x060249AE, tm_go_car);
    menu_reg(0x060249D0, tm_go_course);
    menu_reg(0x060249FE, tm_go_lap);
    menu_reg(0x06024A12, tm_go_race);
    menu_reg(0x06024D1A, tm_spr_out);
    menu_reg(0x06024D3A, tm_spr_in);
    menu_reg(0x0602542A, tm_spr_out2);
    menu_reg(0x0602544E, tm_spr_in2);
    menu_reg(0x06024E18, tm_slide_car_out);
    menu_reg(0x06024F18, tm_slide_car_in);
    menu_reg(0x06024F9A, tm_slide_course_out);
    menu_reg(0x0602513C, tm_slide_course_in);
    menu_reg(0x06025268, tm_slide_lap_out);
    menu_reg(0x0602537A, tm_slide_lap_in);
    menu_reg(0x0602552C, set_pict_tm_att);
    menu_reg(0x060262A4, tm_set_tm_att);
    menu_reg(0x06026284, return_tm_att);
}
