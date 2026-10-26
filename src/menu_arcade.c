/* M8: the arcade-mode selection chain: SELECT GAME (ac_g_select), car select (ac_c_select), course select (ac_crs_select) and the modules they share
 * (timer, blinkle, scrlmove, ac_pict_set, poly_set, the rest of clroffset and variety, the white-out handler of cs_setting at 0x0602767E).
 *
 * Screen vector words: [0x060A001C] = main vector (g_select_main 0x0601FAC4 / c_select_main 0x0602015C / crs_select_main 0x06020EA4),
 * [0x060A8800] = the screen's state handler.  Every pass of a main loop sets the V-IN / V-OUT recipe word [0x060AA800] = 0x0E04, runs the state handler and calls
 * intm_sync(4); the loop ends when [0x060AA803] bit 1 is set.  Bit 1 is set on every pass in which no fade is running (bit 5 clear): the input state arms a fade
 * (pre_fade_inout) that only the last state (fade_inout_work) runs, so the screen ends when that fade is done.
 *
 * Variables (halfwords unless noted):
 *   0x060A88B6 count-down seconds   0x060A88B8 frames to the next second   0x060A88BA frames per second   0x060A88C0 digit scratch / blink phase   0x060A88BC blink counter
 *   0x060A88A4 cursor of the screen   0x060A8810 (long) key word of the pass   0x060A88EA exit code (0 = forward, 1 = back, 0xFFFF = cancel to the previous screen)
 *   0x060A8860.. blink parameters: palette destination, count, source A, source B (+0x10 base, +0x14 state after the blink)
 *   0x06040010 chosen game style (0 arcade, 1 practice)  0x06040012 chosen car row  0x06040014 / 0x06040016 car row remembered for arcade / practice
 *   0x06040018 chosen course  0x0604000D / 0x06040062 option and unlock bytes
 */
#include "satlib.h"
#include "menu_lib.h"
#include "satmenu.h"

#define FLAGS 0x060AA803u

/* the pictures of the arcade screens are in SCRL_SLT.DAT, loaded as a raw file to 0x00244000 (the table addresses 0x02400000 + x become 0x00240200 + x with this base) */
static u32 scrl_base(void)                              /* r13 of the picture routines */
{
    return (R8(FLAGS) & 0x10) ? 0xFDE40200u : 0u;
}

/* ---- _bpe_expand (0x06015870): byte-pair expansion of one block to VDP2 / CRAM, 16-bit units (+ r6) ---- */
static u32 bpe_expand(u32 r4, u32 r5, u32 r6)           /* 0x06015870 */
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

/* ---- variety: picture table loaders and frame fills (0x060226AC ..).  A table pointer runs through consecutive tables. ---- */
static u32 cd_cg_set(u32 t, u32 base)                   /* 0x060227C8: BPE blocks (source + base, destination, add) */
{
    u32 n = R32(t);
    t += 4;
    do {
        bpe_expand(R32(t) + base, R32(t + 4), R32(t + 8));
        t += 12;
    } while (--n);
    return t;
}

/* a frame (w x h cells, 32-bit cells, row = 0x100 bytes) of `val`: the first and last row whole, the other rows only their first and last cell */
static void frmdel2(u32 val, u32 dst, u32 wh)           /* 0x06022784 */
{
    s32 w = (s16)(wh >> 16), h = (s16)wh, row, c;
    for (row = h; row > 0; row--, dst += 0x100) {
        int whole = (row == h || row == 1);
        for (c = w; c > 0; c--)
            if (whole || c == w || c == 1) W32(dst + 4 * (u32)(w - c), val);
    }
}

static void frmdel(u32 val, u32 dst, u32 wh)            /* 0x06022740: the same with 16-bit cells (row = 0x80 bytes) */
{
    s32 w = (s16)(wh >> 16), h = (s16)wh, row, c;
    for (row = h; row > 0; row--, dst += 0x80) {
        int whole = (row == h || row == 1);
        for (c = w; c > 0; c--)
            if (whole || c == w || c == 1) W16(dst + 2 * (u32)(w - c), val);
    }
}

/* frmwrt2: the same frame, cells from a list (u16 w, u16 h, then w * h long cells; every cell is read, only the frame cells are stored, + add) */
static void frmwrt2(u32 src, u32 dst, u32 add)          /* 0x060226F6 */
{
    s32 w = RS16(src), h = RS16(src + 2), row, c;
    src += 4;
    for (row = h; row > 0; row--, dst += 0x100) {
        int whole = (row == h || row == 1);
        for (c = w; c > 0; c--) {
            u32 v = R32(src);
            src += 4;
            if (whole || c == w || c == 1) W32(dst + 4 * (u32)(w - c), v + add);
        }
    }
}

/* frmwrt (0x060226AC, 16-bit cells): the frame cells of the interior rows are stored with a LONG store (the original's quirk: the cell and its right neighbour) */
static void frmwrt(u32 src, u32 dst, u32 add)           /* 0x060226AC */
{
    s32 w = RS16(src), h = RS16(src + 2), row, c;
    src += 4;
    for (row = h; row > 0; row--, dst += 0x80) {
        int whole = (row == h || row == 1);
        for (c = w; c > 0; c--) {
            s32 v = RS16(src);
            src += 2;
            if (whole) W16(dst + 2 * (u32)(w - c), (u32)v + add);
            else if (c == w || c == 1) W32(dst + 2 * (u32)(w - c), (u32)v + add);
        }
    }
}

static u32 cd_frm_set(u32 t, u32 base)                  /* 0x0602284E: one 16-bit box */
{
    box_copy16(R32(t) + base, R32(t + 4), R32(t + 8));
    return t + 12;
}

static u32 cd_frm_set2(u32 t, u32 base)                 /* 0x06022864: one 32-bit frame */
{
    frmwrt2(R32(t) + base, R32(t + 4), R32(t + 8));
    return t + 12;
}

static void clr_cmdtblbuf(void)                         /* 0x060181B8: the sprite command buffer 0x060C0000 .. 0x060D0000 */
{
    rfillff(0x060D0000, 0, 0x4000);
}

/* ---- timer (0x0601F6E0): the count-down of the selection screens ---- */
static void draw_number(u32 digit, u32 dst, u32 base)   /* 0x0601F7EC: the 3 x 3 cell glyph of a digit; the cell codes follow the digit */
{
    u32 src = R32(0x0601F81C + 4 * digit) + base;
    box_copy16(src, dst, 0x1FEC + 20 * (digit + 1));
}

static void count_show(void)                            /* 0x0601F770: the two digits of the seconds */
{
    u32 base = scrl_base();
    s32 r1 = RS16(0x060A88B6), r4 = 0, r0;
    do { r1 -= 10; r4++; } while (r1 > 10);
    W16(0x060A88C0, r1);
    if (r1 == 10) r4++;
    if (0 > r1) r4--;
    draw_number((u32)r4, 0x25E3A0CA, base);
    r0 = RS16(0x060A88C0);
    if (r0 == 10) r0 = 0;
    if (0 > r0) r0 += 10;
    draw_number((u32)r0, 0x25E3A0D0, base);
}

static void time_pict_set(void)                         /* 0x060215DC: the glyph graphics of the count-down digits */
{
    u32 base = scrl_base();
    u32 t = cd_cg_set(0x06021610, base);
    cd_color_set(t, base);
}

static void init_count(s32 seconds, s32 frames)         /* 0x0601F6E0 */
{
    W16(0x060A88B6, seconds);
    W16(0x060A88B8, frames);
    W16(0x060A88BA, frames);
    time_pict_set();
    count_show();
}

static s32 count_down(void)                             /* 0x0601F714: returns -1 when the time is up (any other value: still counting; the original leaves an address in r0) */
{
    s32 f = RS16(0x060A88B8) - 1, s;
    W16(0x060A88B8, f);
    if (f != 0) return 0x060A88B8;
    W16(0x060A88B8, RS16(0x060A88BA));
    s = RS16(0x060A88B6);
    if (s == 0) return -1;
    s -= 1;
    W16(0x060A88B6, s);
    if (s == -1) { snd_call(0x06016FBC, 40, 0, 0); return -1; }
    count_show();
    return 0;
}

/* ---- blinkle (0x0601FEA4): the picks flash for 12 half-periods, then the state in [0x060A8874] runs ---- */
static void blinkle_init(void)                          /* 0x0601FEA4 */
{
    if (R8(0x060A8902) != 1) {                          /* the count-down digits are removed (not when the count-down is not shown) */
        box_fill16(0, 0x25E3A0CA, 0x00030003);
        box_fill16(0, 0x25E3A0D0, 0x00030003);
    }
    W16(0x060A88B8, 2);
    W16(0x060A88C0, 1);
    W32(0x060A8800, 0x0601FF0C);
}

static void blinkle(void)                               /* 0x0601FF0C */
{
    u32 r5 = R32(0x060A8860), r6 = R32(0x060A8864), r7 = R32(0x060A8868), r8 = R32(0x060A886C), r13 = R32(0x060A8870);
    s32 f = RS16(0x060A88B8) - 1, ph, r0;
    W16(0x060A88B8, f);
    if (f != 0) return;
    W16(0x060A88B8, 2);
    ph = RS16(0x060A88C0);
    if (RS16(0x060A8878) == 2) ph += 2;
    switch (ph) {
    case 0: copy_l(r7 + r13, r5, r6); W16(0x060A88C0, 1); break;
    case 1: copy_l(r8 + r13, r5, r6); W16(0x060A88C0, 0); break;
    case 2: box_copy16(r7 + r13, r5, r6); W16(0x060A88C0, 1); break;
    case 3: box_fill16(0, r5, r8); W16(0x060A88C0, 0); break;
    default: menu_unported(0x0601FF48);                 /* the jump table has four entries */
    }
    r0 = RS16(0x060A88BC) - 1;
    W16(0x060A88BC, r0);
    if (r0 == 0) W32(0x060A8800, R32(0x060A8874));
}

/* ---- cs_tm_attck helpers shared with the time attack screen ---- */
static void check_mirror(void)                          /* 0x06024A48: C + Y held: the mirror course bit */
{
    if (R16(0x060AA990) == 0x220) W8(0x060AA82E, R8(0x060AA82E) | 1);
}

static u32 check_stratos_flg(void) { return (R8(0x06040062) & 4) ? 1 : 0; }     /* 0x06024B7C: the Stratos is unlocked */

/* ---- white-out of the mode select's arcade entry (cs_setting 0x0602767E) ---- */
static void white_out_wait(void)                        /* 0x0602767E */
{
    fade_inout_work();
    if (!(R8(FLAGS) & 0x20)) W16(0x060A88EA, 1);
}

/* ================================================================================================
 * SELECT GAME (ac_g_select 0x0601F844 ..): arcade (left) / practice (right) with a 10 s count-down
 * ================================================================================================ */
static void set_pict_ac_g_select(void)                  /* 0x0602169C */
{
    u32 base = 0, t, r9, r10, r11, r12;
    if (R8(FLAGS) & 0x10) {
        sat_cd_request(0x06021948);                     /* SCRL_SLT.DAT -> 0x00244000 */
        base = 0xFDE40200u;
    } else {
        menu_unported(0x0602169C);
    }
    t = 0x06021794;
    t = cd_cg_set(t, base);
    t = cd_map_set(t, base);
    t = cd_color_set(t, base);
    t = cd_cg_set(t, base);
    t = cd_map_set(t, base);
    t = cd_cg_set(t, base);
    if (R8(0x0604000D) == 0) { r9 = 0; r10 = 32; } else { r9 = 32; r10 = 0; }
    if (R8(FLAGS) & 4) { r11 = 0; r12 = 16; } else { r11 = 16; r12 = 0; }
    r11 += r9;
    r12 += r10;
    t += r11;
    t = cd_map_set(t, base);
    t += r12;
    t = cd_color_set(t, base);
    t = cd_cg_set(t, base);
    if (R8(0x0604000D) == 0) { r11 = 0; r12 = 56; } else { r11 = 56; r12 = 0; }
    t += r11;
    t = cd_cg_set(t, base);
    t = cd_map_set2(t, base);
    t += r12;
    t = cd_color_set(t, base);
    if (RS16(0x06040010) != 0) t += 40;
    t = cd_frm_set2(t, base);
    cd_color_set(t, base);
}

static void g_select_arcade(void)                       /* 0x0601FD60: the cursor on ARCADE */
{
    u32 base, t;
    W16(0x060AA87E, 0);
    W16(0x060AA860, 0);
    W16(0x060A88A4, 0);
    W16(0x06040010, 0);
    base = scrl_base();
    frmdel2(0, 0x25E28658, 0x00130012);
    t = cd_frm_set2(0x0601FDA8, base);
    cd_color_set(t, base);
}

static void g_select_practice(void)                     /* 0x0601FDD0: the cursor on PRACTICE */
{
    u32 base, t;
    W16(0x060A88A4, 1);
    W16(0x060AA860, 2);
    W16(0x06040010, 1);
    base = scrl_base();
    frmdel2(0, 0x25E2860C, 0x00130012);
    t = cd_frm_set2(0x0601FE7C, base);
    cd_color_set(t, base);
}

static void g_select_confirm(void)                      /* 0x0601FC4E: start the blink of the chosen entry; afterwards the white-out (0x060225DC) runs */
{
    u32 base = scrl_base();
    if (RS16(0x06040010) == 0) {
        W32(0x060A8860, 0x060B8800);
        W32(0x060A8864, 48);
        W32(0x060A8868, 0x0240DF54);
        W32(0x060A886C, 0x0240E014);
    } else {
        W32(0x060A8860, 0x060B8A00);
        W32(0x060A8864, 45);
        W32(0x060A8868, 0x02410E38);
        W32(0x060A886C, 0x02410EF8);
    }
    W32(0x060A8870, base);
    W32(0x060A8874, 0x060225DC);
    W32(0x060A8800, 0x0601FEA4);
}

static void g_wait_fadein(void)                         /* 0x0601FB44 */
{
    fade_inout_work();
    if (!(R8(FLAGS) & 0x20)) {
        W32(0x060A8800, 0x0601FB6E);
        pre_fade_inout(0, 255, 34);
    }
}

static void g_check_pad(void)                           /* 0x0601FB6E */
{
    u32 keys;
    if (count_down() == -1) {                           /* time is up: the highlighted entry is taken */
        g_select_confirm();
        snd_call(0x06016FBC, 42, 0, 0);
        return;
    }
    if (R8(FLAGS) & 4) {                                /* analog pad / wheel: the axis byte (127 = centre) picks the entry */
        s32 d = (s32)R8(0x060AA99C) - 127;
        if (d > 30) g_select_practice();
        else if (-30 > d) g_select_arcade();
        else if (d == 0) {
            if (R8(0x060A88EE) == 0) g_select_arcade();
            else g_select_practice();
        }
        W32(0x060A8810, (u32)(s32)RS16(0x060AA994));
    } else {
        keys = (u32)(s32)RS16(0x060AA994);
        W32(0x060A8810, keys);
        if (keys & 0x8000) { g_select_practice(); return; }
        if (keys & 0x4000) { g_select_arcade(); return; }
    }
    check_mirror();
    keys = R32(0x060A8810);
    if (keys & 0x100) {                                 /* B: back to the mode select */
        W16(0x060A88EA, 0xFFFF);
        snd_call(0x06016F3C, 0, 0, 48);
        W32(0x060A8800, 0x060225DC);
        return;
    }
    if (keys & 0xE00) {                                 /* A / C / START */
        g_select_confirm();
        snd_call(0x06016FBC, 42, 0, 0);
    }
}

static void g_select_init(void)                              /* 0x0601F978 */
{
    tv_off();
    pad_kind_init();
    init_gspr();
    init_oscroll();
    snd_call(0x060173AC, 0, 40, 0);
    W32(0x060A001C, 0x0601FAC4);
    clr_cmdtblbuf();
    W16(0x25F800F8, 0x306);
    W16(0x25F800FA, 0x606);
    W16(0x25F800FC, 4);
    set_pict_ac_g_select();
    init_count(11, 60);
    W16(0x060A88BC, 12);
    W16(0x060A88A4, 0);
    W16(0x060A8810, 0);
    W16(0x060A8878, 0);
    W16(0x060A88EA, 0);
    W16(0x060AA860, 0);
    W16(0x060AA87E, 0);
    W8(0x060AA80A, 0);
    W8(0x060AA809, 0);
    W8(0x060AA81B, 2);
    W8(0x060AA820, 2);
    W8(0x060AA811, 0);
    W8(0x060AA82E, 0);
    W8(0x060A88EE, R16(0x06040010));
    pre_fade_inout(255, 0, -34);
    tv_on();
}

static void g_select_main(void)                              /* 0x0601FAC4 */
{
    W32(0x060A8800, 0x0601FB44);
    do {
        W16(0x060AA800, 0x0E04);
        menu_call(R32(0x060A8800));
        if (!(R8(FLAGS) & 0x20)) W8(FLAGS, R8(FLAGS) | 2);
        W16(0x060AA994, 0);
        intm_sync(4);
    } while (!(R8(FLAGS) & 2));
    W8(FLAGS, R8(FLAGS) & 0xFD);
    if (RS16(0x060A88EA) == -1) {                       /* cancelled: the mode select */
        W32(0x060A001C, 0x06022894);
        W8(0x060AA80D, 1);
    } else {                                            /* the game type for the race (practice = 2) and on to the car select */
        s32 r0 = RS16(0x06040010);
        if (r0 == 1) r0 = 2;
        W16(0x060AA860, r0);
        W32(0x060A001C, 0x06020010);
    }
}


/* ================================================================================================
 * car select (ac_c_select 0x06020010 ..): the cars of the 2 x 2 grid (+ the hidden Stratos) and the transmission.
 * Row [0x06040012]: -1 Stratos (needs unlock bit 4 of 0x06040062 << 1 i.e. 0x04), 0 / 1 = car 0 (MT / AT), 2 / 3 = car 1 (AT / MT), 4 = Stratos' second row.
 * [0x060AA80A] car, [0x060AA809] transmission; the four turntable cars are the state blocks 0x060E4000 / 0x060E4400 / 0x060E4800 / 0x060E4C00.
 * ================================================================================================ */
static void set_car_texture(void)                       /* 0x0601ABA2: the car models + textures (OBJECT.MAD); the texture upload (setup_texture) is the 3D pipeline's */
{
    if (R32(0x060D008C) == 0x4F424A20) return;
    W32(0x060D008C, 0x4F424A20);
    if (R8(0x060AA803) & 0x10) sat_cd_request(0x0601ABFC);
    else menu_unported(0x0601ABA2);
    W32(0x060D0048, 0x30002100);
    setup_texture(0x0601AF8C, 0xFDEE0000u);
}

static void car_block_init(u32 b, u32 model, u32 rot)   /* the common part of the four blocks of set_car_c_select */
{
    W8(b + 92, model);
    W32(b + 24, 0);
    W32(b + 28, 0xFFFC0000);
    W32(b + 32, 0x00100000);
    W32(b + 12, 0);
    W32(b + 20, 0);
    W32(b + 16, rot);
}

static void set_car_c_select(void)                      /* 0x06022198 */
{
    car_block_init(0x060E4000, RS16(0x06040012) == -1 ? 10 : 0, 0);
    car_block_init(0x060E4400, 0, 0x8000);
    car_block_init(0x060E4800, 1, 0);
    car_block_init(0x060E4C00, RS16(0x06040012) == 4 ? 10 : 1, 0x8000);
}

/* the turntable cars: the highlighted one stands still (byte +94 = 0), the others spin; +16 is the turn angle; the 3D draw (set_poly_offset, dispSelectCar) is the host's */
static void csl_disp_poly(u32 b, int sel, s32 turn, u32 offset)
{
    W8(b + 94, sel ? 0 : 1);
    W32(b + 16, R32(b + 16) + (u32)turn);
    W32(b + 900, 0);                                       /* dispSelectCar keeps its r5 (0: with the shadow) in the block */
    menu3d_car(b, offset, 0);                              /* set_poly_offset(offset), dispSelectCar */
}

static void csl_disp_poly_mc(void) { csl_disp_poly(0x060E4000, RS16(0x06040012) < 1, -182, 0xFFA1FFE7u); }    /* 0x06022250 */
static void csl_disp_poly_ac(void) { csl_disp_poly(0x060E4400, RS16(0x06040012) == 1, 182, 0xFFC9FFA1u); }    /* 0x060222B0 */
static void csl_disp_poly_ad(void) { csl_disp_poly(0x060E4800, RS16(0x06040012) == 2, -182, 0x0038FFA1u); }   /* 0x06022310 */
static void csl_disp_poly_md(void) { csl_disp_poly(0x060E4C00, RS16(0x06040012) >= 3, 182, 0x0057FFE7u); }    /* 0x06022370 */

static void set_pict_ac_c_select(void)                  /* 0x06021964 */
{
    u32 base = scrl_base(), t, r9, r10, r11, r12;
    s32 row = RS16(0x06040012);
    t = 0x06021AE0;
    t = cd_cg_set(t, base);
    t = cd_map_set(t, base);
    t = cd_color_set(t, base);
    t = cd_cg_set(t, base);
    t = cd_map_set(t, base);
    t = cd_cg_set(t, base);
    if (R8(0x0604000D) == 0) { r9 = 0; r10 = 32; } else { r9 = 32; r10 = 0; }
    if (R8(FLAGS) & 4) { r11 = 0; r12 = 16; } else { r11 = 16; r12 = 0; }
    r11 += r9;
    r12 += r10;
    t += r11;
    t = cd_map_set(t, base);
    t += r12;
    t = cd_color_set(t, base);
    t = cd_cg_set(t, base);
    t = cd_map_set2(t, base);
    t = cd_color_set(t, base);
    switch (row) {
    case -1: W8(0x060AA80A, 2); W8(0x060AA809, 0); t = cd_map_set2(t, base); break;
    case 0:  W8(0x060AA80A, 0); W8(0x060AA809, 1); t += 80; break;
    case 1:  W8(0x060AA80A, 0); W8(0x060AA809, 0); t += 0x90; break;
    case 2:  W8(0x060AA80A, 1); W8(0x060AA809, 0); t += 0xD0; break;
    case 3:  W8(0x060AA80A, 1); W8(0x060AA809, 1); t += 0x110; break;
    case 4:  W8(0x060AA80A, 2); W8(0x060AA809, 1); t += 0x110; break;
    default: menu_unported(0x060219EC);                 /* the jump table has six entries */
    }
    t = cd_frm_set2(t, base);
    cd_color_set(t, base);
}

/* the six rows: set the row, car and transmission, the model ids of the first and last turntable car, clear three of the four name frames, draw the frame of the chosen one */
static void c_set_row(s32 row, u32 car, u32 trans, u32 id0, u32 id3, u32 f0, u32 f1, u32 f2, u32 tbl)
{
    u32 base, t;
    W16(0x06040012, row);
    W8(0x060AA80A, car);
    W8(0x060AA809, trans);
    W8(0x060E4000 + 92, id0);
    W8(0x060E4C00 + 92, id3);
    frmdel2(0, f0, 0x000E000A);
    frmdel2(0, f1, 0x000E000A);
    frmdel2(0, f2, 0x000E000A);
    base = scrl_base();
    t = cd_frm_set2(tbl, base);
    cd_color_set(t, base);
}

static void c_row_m1(void) { c_set_row(-1, 2, 0, 10, 1, 0x25E28520, 0x25E28558, 0x25E28E68, 0x060207C4); }    /* 0x06020720 */
static void c_row_0(void)  { c_set_row(0, 0, 1, 0, 1, 0x25E28520, 0x25E28558, 0x25E28E68, 0x060208A8); }      /* 0x06020804 */
static void c_row_1(void)  { c_set_row(1, 0, 0, 0, 1, 0x25E28E0C, 0x25E28558, 0x25E28E68, 0x06020990); }      /* 0x060208E8 */
static void c_row_2(void)  { c_set_row(2, 1, 0, 0, 1, 0x25E28E0C, 0x25E28520, 0x25E28E68, 0x06020A78); }      /* 0x060209D0 */
static void c_row_3(void)  { c_set_row(3, 1, 1, 0, 1, 0x25E28E0C, 0x25E28520, 0x25E28558, 0x06020B60); }      /* 0x06020AB8 */
static void c_row_4(void)  { c_set_row(4, 2, 1, 0, 10, 0x25E28E0C, 0x25E28520, 0x25E28558, 0x06020C48); }     /* 0x06020BA0 */

static void c_goto_row(s32 row)                         /* the jump table 0x06020708 */
{
    switch (row) {
    case -1: c_row_m1(); break;
    case 0: c_row_0(); break;
    case 1: c_row_1(); break;
    case 2: c_row_2(); break;
    case 3: c_row_3(); break;
    case 4: c_row_4(); break;
    default: menu_unported(0x060206AA);
    }
}

static void c_move(s32 r5)                              /* 0x0602064C: one step in the row numbering (-1 / +1) */
{
    s32 r0;
    W8(0x060A88FC, check_stratos_flg());
    r0 = RS16(0x06040012);
    W16(0x060A88C0, r0);
    r5 += r0;
    if (R8(0x060A88FC) == 0) {                          /* no Stratos: rows 0 .. 3 */
        r0 = r5;
        if (r0 < 0) r0 = 0;
        if (!(3 >= r0)) r0 = 3;
    } else {                                            /* rows -1 .. 4 (-2 wraps to -1) */
        r0 = r5;
        if (r0 == -2) r0 = -1;
        if (!(4 >= r0)) r0 = 4;
    }
    W16(0x060A88FC, r0);                                /* (the original stores the new row over the flag byte) */
    c_goto_row(r0);
}

static void c_move_abs(s32 r5)                          /* 0x060206BC: analog pad: go to a row */
{
    if (check_stratos_flg() == 0) {
        if (r5 < 0) r5 = 0;
        if (!(3 >= r5)) r5 = 3;
    } else {
        if (-1 > r5) r5 = -1;
        if (!(4 >= r5)) r5 = 4;
    }
    c_goto_row(r5);
}

static void c_confirm(void)                             /* 0x0602051C: start the blink of the chosen car; afterwards the white-out runs */
{
    u32 base = scrl_base();
    switch (RS16(0x06040012)) {
    case -1: W32(0x060A8860, 0x060B8600); W32(0x060A8868, 0x02411684); W32(0x060A886C, 0x0241221C); break;
    case 0:  W32(0x060A8860, 0x060B8800); W32(0x060A8868, 0x02412A88); W32(0x060A886C, 0x024122DC); break;
    case 1:  W32(0x060A8860, 0x060B8A00); W32(0x060A8868, 0x02411684); W32(0x060A886C, 0x0241221C); break;
    case 2:  W32(0x060A8860, 0x060B8C00); W32(0x060A8868, 0x02411DB0); W32(0x060A886C, 0x0241227C); break;
    case 3:
    case 4:  W32(0x060A8860, 0x060B8E00); W32(0x060A8868, 0x024131D4); W32(0x060A886C, 0x0241233C); break;
    default: menu_unported(0x06020536);
    }
    W32(0x060A8864, 24);
    W32(0x060A8870, base);
    W32(0x060A8874, 0x060225DC);
    W32(0x060A8800, 0x0601FEA4);
}

static void c_remember_row_in(void)                     /* 0x06020CB6: the row of the chosen style (arcade / practice) */
{
    W16(0x06040012, R16(RS16(0x06040010) == 0 ? 0x06040014 : 0x06040016));
}

static void c_remember_row_out(void)                    /* 0x06020D04 */
{
    W16(RS16(0x06040010) == 0 ? 0x06040014 : 0x06040016, R16(0x06040012));
}

static void c_draw_info(void)                           /* 0x06020C88: the info picture below the cars (the Stratos has its own) */
{
    u32 base = scrl_base(), src = 0x02412904, wh = 0x00400048;
    if (RS16(0x06040012) == -1) { src = 0x02411500; wh = 0x003000A0; }
    box_copy32(src + base, 0x25E28F10, wh);
}

static void c_cheat(void)                               /* 0x06020308: C + X held in the arcade style (not on the Stratos): unlock bit 4 and set-up byte 2 */
{
    if (RS16(0x06040010) != 0) return;
    if (R8(0x060AA80A) == 2) return;
    if (R16(0x060AA990) != 0x240) return;
    W8(0x06040062, R8(0x06040062) | 0x10);
    W8(0x060AA811, 2);
}

static void c_wait_fadein(void)                         /* 0x060202C8 */
{
    fade_inout_work();
    if (!(R8(FLAGS) & 0x20)) {
        W32(0x060A8800, 0x06020336);
        pre_fade_inout(0, 255, 34);
    }
}

static void c_check_pad(void)                           /* 0x06020336 */
{
    u32 keys;
    s32 row;
    if (count_down() == -1) {
        c_confirm();
        snd_call(0x06016FBC, 42, 0, 0);
        return;
    }
    if (R8(FLAGS) & 4) {                                /* analog pad: the axis byte picks the row */
        s32 r5 = RS8(0x0602050C + (R8(0x060AA99C) >> 4)) + RS8(0x060A88EF);
        c_move_abs(r5);
        W32(0x060A8810, (u32)(s32)RS16(0x060AA994));
    } else {
        keys = (u32)(s32)RS16(0x060AA994);
        W32(0x060A8810, keys);
        if (keys & 0x8000) { c_move(1); return; }
        if (keys & 0x4000) { c_move(-1); return; }
        row = RS16(0x06040012);
        if (row == 0) {
            if (keys & 0x1000) { c_move(1); return; }
            if (keys & 0x2000) { c_move(-1); return; }
        }
        if (row == 3) {
            if (keys & 0x1000) { c_move(-1); return; }
            if (keys & 0x2000) { c_move(1); return; }
        }
        if (row == 1) {
            if (keys & 0x2000) { c_move(-1); return; }
        }
        if (row == 2) {
            if (keys & 0x2000) { c_move(1); return; }
        }
        if (row == -1) {
            if (keys & 0x1000) { c_move(1); return; }
        }
        if (row == 4) {
            if (keys & 0x1000) { c_move(-1); return; }
        }
    }
    keys = R32(0x060A8810);
    if (keys & 0x100) {                                 /* B: back to SELECT GAME */
        W16(0x060A88EA, 0);
        W32(0x060A8800, 0x060225DC);
        return;
    }
    if (keys & 0xE00) {
        c_confirm();
        snd_call(0x06016FBC, 42, 0, 0);
    }
}

static void c_select_init(void)                         /* 0x06020010 */
{
    tv_off();
    snd_call(0x060173AC, 0, 40, 0);
    pad_kind_init();
    init_gspr();
    init_oscroll();
    swclear();
    W32(0x060A001C, 0x0602015C);
    set_car_texture();
    mkpoly_init();
    clr_cmdtblbuf();
    W16(0x25F800F8, 0x306);
    W16(0x25F800FA, 0x404);
    W16(0x25F800FC, 2);
    c_remember_row_in();
    set_pict_ac_c_select();
    set_car_c_select();
    W8(0x060A88FC, check_stratos_flg());
    init_count(11, 60);
    W16(0x060A88BC, 12);
    W32(0x060A8810, 0);
    pre_fade_inout(255, 0, -34);
    W16(0x060A8878, 1);
    W16(0x060A88EA, 1);
    W16(0x060A88C0, 1);
    W8(0x060A88EF, R16(0x06040012));
    tv_on();
}

static void c_select_main(void)                         /* 0x0602015C */
{
    W32(0x060A8800, 0x060202C8);
    do {
        W16(0x060AA800, 0x0E04);
        m_mkpoly_frame_init();
        /* set_slave_exec(0x0601D5C0): the slave's cache control register, no memory effect */
        s_mkpoly_frame_init();
        menu_call(R32(0x060A8800));
        if (!(R8(FLAGS) & 0x20)) W8(FLAGS, R8(FLAGS) | 2);
        c_cheat();
        W16(0x060AA994, 0);
        csl_disp_poly_ad();                             /* the slave draws the right-hand cars, the master the others; game_zsort is not run by the oracle */
        csl_disp_poly_mc();
        csl_disp_poly_md();
        csl_disp_poly_ac();
        c_remember_row_out();
        c_draw_info();
        intm_sync(4);
    } while (!(R8(FLAGS) & 2));
    W8(FLAGS, R8(FLAGS) & 0xFD);
    if (RS16(0x060A88EA) <= 0) {                 /* B: back to SELECT GAME */
        W32(0x060A001C, 0x0601F978);
        W8(0x060AA80D, 0);
    } else if (RS16(0x06040010) != 0) {                 /* practice: the course select */
        W32(0x060A001C, 0x06020D34);
    } else {                                            /* arcade: the race (start_init 0x06015408, not part of the menus) */
        W16(0x06040018, 0);
        W16(0x060AA87E, 0);
        W8(0x060AA810, 0);
        W32(0x060A001C, 0x06015408);
    }
}

/* ================================================================================================
 * course select (ac_crs_select 0x06020D34 ..): four courses (the last, the lake, needs unlock bit 2) in a sliding strip
 * ================================================================================================ */
static s32 slide_target, slide_step;                    /* r10 / r11 of the original: they live on between the passes of the main loop */
static s32 slide_ret;                                   /* r3 of the original after the state handler: -1 when the slide is done */

static void set_pict_ac_crs_select(void)                /* 0x06021DA0 */
{
    u32 base = scrl_base(), t, r9, r10, r11, r12;
    t = 0x06021ECC;
    t = cd_cg_set(t, base);
    t = cd_map_set(t, base);
    t = cd_color_set(t, base);
    t = cd_cg_set(t, base);
    t = cd_map_set(t, base);
    t = cd_cg_set(t, base);
    if (R8(0x0604000D) == 0) { r9 = 0; r10 = 32; } else { r9 = 32; r10 = 0; }
    if (R8(FLAGS) & 4) { r11 = 0; r12 = 16; } else { r11 = 16; r12 = 0; }
    r11 += r9;
    r12 += r10;
    t += r11;
    t = cd_map_set(t, base);
    t += r12;
    t = cd_color_set(t, base);
    t = cd_cg_set(t, base);
    t = cd_map_set2(t, base);
    t = cd_cg_set(t, base);
    t = cd_color_set(t, base);
    switch (RS16(0x06040018)) {
    case 0: break;
    case 1: t += 64; break;
    case 2: t += 128; break;
    case 3: t += 192; break;
    default: menu_unported(0x06021E2C);
    }
    t = cd_frm_set(t, base);
    cd_color_set(t, base);
}

static void crs_blink(void)                             /* 0x060210B4: blink the chosen course, then the white-out */
{
    u32 base = scrl_base();
    W32(0x060A8860, R32(0x06021138 + 4 * (u32)RS16(0x06040018)));
    W32(0x060A8864, 0x3000);
    W32(0x060A8868, 0x024133C8);
    W32(0x060A886C, 0x000C0011);
    W32(0x060A8870, base);
    W32(0x060A8874, 0x060225DC);
    W32(0x060A8800, 0x0601FEA4);
}

static void crs_course(s32 c, u32 f0, u32 f1, u32 tbl)  /* 0x060211BC / 06021260 / 06021300 / 060213A0: show the course c */
{
    u32 base, t;
    W16(0x06040018, c);
    if (c == 0) frmdel(0, f0, 0x000C0011);
    else box_fill16(0, f0, 0x000C0011);
    box_fill16(0, f1, 0x000C0011);
    base = scrl_base();
    t = cd_frm_set(tbl, base);
    cd_color_set(t, base);
}

static void crs_course_0(void) { crs_course(0, 0x25E383A0, 0x25E383B8, 0x0602122C); }
static void crs_course_1(void) { crs_course(1, 0x25E38388, 0x25E383B8, 0x060212CC); }
static void crs_course_2(void) { crs_course(2, 0x25E38388, 0x25E383A0, 0x0602136C); }
static void crs_course_3(void) { crs_course(3, 0x25E38388, 0x25E383A0, 0x0602140C); }

static void crs_goto_course(s32 c)                      /* the jump table 0x060211AC */
{
    switch (c) {
    case 0: crs_course_0(); break;
    case 1: crs_course_1(); break;
    case 2: crs_course_2(); break;
    case 3: crs_course_3(); break;
    default: menu_unported(0x06021164);
    }
}

static void crs_state_slide(void)                       /* 0x06021570 */
{
    s32 r2;
    if (count_down() == -1) {                           /* time is up while sliding: jump to the end position and blink */
        W16(0x060A8880, slide_target);
        W16(0x060AA864, slide_target);
        crs_blink();
        snd_call(0x06016FBC, 42, 0, 0);
        return;
    }
    r2 = RS16(0x060A8880);
    r2 += slide_step;
    if (slide_step > 0) {
        if (r2 >= slide_target) { r2 = slide_target; slide_ret = -1; }
    } else {
        if (slide_target >= r2) { r2 = slide_target; slide_ret = -1; }
    }
    W16(0x060A8880, r2);
    W16(0x060AA864, r2);
}

static void slide_start(s32 target, s32 step)           /* L0602 14A8 / 14FC / 1554: the strip starts to slide; the state to return to is kept in [0x060A8808] */
{
    slide_target = target;
    slide_step = step;
    W32(0x060A8808, R32(0x060A8800));
    W32(0x060A8800, 0x06021570);
}

/* judgement_scrl (0x06021440): r4 = the course asked for, r5 = the step; returns the course to show.  Moving to or from the last course (3) makes the strip slide. */
static s32 judgement_scrl(s32 r4, s32 r5)               /* 0x06021440 */
{
    s32 r0;
    int t;
    if (r4 == 3) {
        t = (r5 > -1);
        r4 += r5;
        if (!t) goto L4a4;
        if (r4 > 3) r4 = 3;
        r0 = RS16(0x060A88A4);
        t = (3 > r0);
        r4 += r5;
        if (!t) goto L498;
        return r4;
    }
    r4 += r5;
    if (r4 >= 3) {
        if (R8(0x06040062) & 2) { r4 = 3; goto L498; }
        return 2;
    }
    r0 = RS16(0x060A88A4);
    if (r0 != 3) return r4;
    if (!(r0 > r4)) return r4;
    goto L4a4;
L498:
    r4 = 3;
    slide_start(0x260, 32);
    return r4;
L4a4:
    slide_start(0x200, -32);
    return r4;
}

static void crs_step(s32 r4, s32 r5, int analog)        /* 0x06021148 (keys) / 0x0602117C (analog pad) */
{
    r4 = judgement_scrl(r4, r5);
    if (!analog) { if (r4 == -1) r4 = 0; }
    else { if (!(r4 > 0)) r4 = 0; }
    W16(0x060A88A4, r4);
    crs_goto_course(r4);
}

static void crs_wait_fadein(void)                       /* 0x06020F68 */
{
    fade_inout_work();
    if (!(R8(FLAGS) & 0x20)) {
        W32(0x060A8800, 0x06020FA8);
        pre_fade_inout(0, 255, 34);
    }
}

static void crs_check_pad(void)                         /* 0x06020FA8 */
{
    u32 keys;
    if (count_down() == -1) {
        crs_blink();
        snd_call(0x06016FBC, 42, 0, 0);
        return;
    }
    if (R8(FLAGS) & 4) {
        s32 r5 = RS8(0x060210AC + (R8(0x060AA99C) >> 5));
        s32 r4 = RS8(0x060A88F0);
        crs_step(r4, r5, 1);
        W32(0x060A8810, (u32)(s32)RS16(0x060AA994));
    } else {
        keys = (u32)(s32)RS16(0x060AA994);
        W32(0x060A8810, keys);
        if (keys & 0x8000) { crs_step(RS16(0x060A88A4), 1, 0); return; }
        if (keys & 0x4000) { crs_step(RS16(0x060A88A4), -1, 0); return; }
    }
    keys = R32(0x060A8810);
    if (keys & 0x100) {                                 /* B: back to the car select */
        W16(0x060A88EA, 0);
        W32(0x060A8800, 0x060225DC);
        return;
    }
    if (keys & 0xE00) {
        W16(0x060AA87E, R16(0x06040018));
        crs_blink();
        snd_call(0x06016FBC, 42, 0, 0);
    }
}

static void crs_select_init(void)                       /* 0x06020D34 */
{
    s32 r2;
    tv_off();
    pad_kind_init();
    init_gspr();
    init_oscroll();
    swclear();
    W32(0x060A001C, 0x06020EA4);
    clr_cmdtblbuf();
    W16(0x25F800F8, 0x604);
    W16(0x25F800FA, 0x404);
    W16(0x25F800FC, 2);
    set_pict_ac_crs_select();
    init_count(11, 60);
    W16(0x060A88BC, 12);
    W16(0x060A8810, 0);
    W8(0x060A88F1, 0);
    pre_fade_inout(255, 0, -34);
    W16(0x060A8878, 2);
    W16(0x060A88EA, 1);
    W16(0x25F800C0, 48);
    W16(0x25F800C2, 40);
    W16(0x25F800C4, 0x280);
    W16(0x25F800C6, 192);
    W16(0x25F800D0, 3);
    r2 = RS16(0x06040018);
    W16(0x060A88A4, r2);
    W8(0x060A88F0, r2);
    if (RS16(0x06040018) == 3) { W16(0x060A8880, 0x260); W16(0x060AA864, 0x260); }
    else { W16(0x060A8880, 0x200); W16(0x060AA864, 0x200); }
    tv_on();
}

static void crs_select_main(void)                       /* 0x06020EA4 */
{
    W32(0x060A8800, 0x06020F68);
    do {
        W16(0x060AA800, 0x0E04);
        slide_ret = 0;
        menu_call(R32(0x060A8800));
        if (slide_ret < 0) W32(0x060A8800, R32(0x060A8808));
        if (!(R8(FLAGS) & 0x20)) W8(FLAGS, R8(FLAGS) | 2);
        W16(0x060AA994, 0);
        intm_sync(4);
    } while (!(R8(FLAGS) & 2));
    W16(0x060AA87E, R16(0x06040018));
    W8(FLAGS, R8(FLAGS) & 0xFD);
    if (RS16(0x060A88EA) <= 0) {                        /* B: back to the car select */
        W32(0x060A001C, 0x06020010);
    } else {                                            /* the race (start_init 0x06015408) */
        W8(0x060AA810, 0);
        W32(0x060A001C, 0x06015408);
    }
}

void menu_arcade_register(void)
{
    menu_reg(0x0602767E, white_out_wait);
    menu_reg(0x0601F978, g_select_init);
    menu_reg(0x0601FAC4, g_select_main);
    menu_reg(0x0601FB44, g_wait_fadein);
    menu_reg(0x0601FB6E, g_check_pad);
    menu_reg(0x0601FEA4, blinkle_init);
    menu_reg(0x0601FF0C, blinkle);
    menu_reg(0x060225DC, fade_inout_work);
    menu_reg(0x06020010, c_select_init);
    menu_reg(0x0602015C, c_select_main);
    menu_reg(0x060202C8, c_wait_fadein);
    menu_reg(0x06020336, c_check_pad);
    menu_reg(0x06020D34, crs_select_init);
    menu_reg(0x06020EA4, crs_select_main);
    menu_reg(0x06020F68, crs_wait_fadein);
    menu_reg(0x06020FA8, crs_check_pad);
    menu_reg(0x06021570, crs_state_slide);
}
