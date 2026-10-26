/* M8: the result screen of the time attack (0x06032514 ..), ported from the original: the times of the race count up one digit at a time (the four actors 0x06032BB8 / 0x06032C24 on the
 * slots 0x060E6000 + 0x80 * n: n = 0 total, 1 .. 3 the lap times at 0x060B0590 ..), the stage picture sprite (0x06035110, slot 0x060E4180), the turntable car of the player (block
 * 0x060E5180, drawn by the host), the continue / retry choice (left / right) and the fade out.
 *
 * [0x060A8CA0] state (0 fade in, 1 running, 2 fade out), [0x060A8CA1] the choice (0 = back to the time attack menu, 1 = race again), [0x060A8CA4] = 1: no choice (no new record / replay),
 * [0x060A8CA3] = 1: the total time equals the best time of the table (drawn in the new record colour), [0x060A8C38] frame counter of the count-up. */
#include "satlib.h"
#include "menu_lib.h"
#include "satmenu.h"
#include "menu_records.h"
#include "menu_post.h"

#define SND(id) snd_call(0x06016FBC, (u32)(id), 0, 0)

void set_pict_result(void)                              /* 0x06026162: the pictures of the result screen */
{
    u32 t = 0x06026234, base1, base2;
    delete_nbg0();
    W16(0x060AA864, 0);
    W16(0x060AA868, 0);
    if (R8(0x060AA803) & 0x10) {
        base1 = 0xFDA44000u;
        base2 = 0xFDA38200u;
    } else {
        menu_unported(0x060261B4);
        return;
    }
    t = cd_map_set2(t, base1);
    t = cd_color_set(t, base1);
    t = cd_map_set2(t, base1);
    t = cd_map_set(t, base2);
    t = cd_color_set(t, base2);
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

static void set_car(void)                               /* 0x06026510: the two turntable car blocks (0x060E5180 / 0x060E5580) */
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

static void display_car_tm(void)                        /* 0x060266FC with r12 = 128, r13 = 0x060A882C, r14 = 0x060E5180: the turntable car turns by 128 */
{
    W32(0x060E5180 + 16, R32(0x060E5180 + 16) + 128);
    /* set_poly_offset(r4 = [r13]), dispSelectCar2: the original asks for the car here, but the PSP does not show it (a turntable car on top of the lap times; the car belongs to the car select only):
     * no menu3d_car request.  The memory above is what the oracle compares. */
}

/* ---- the sprite of the stage (0x06035110, slot 0x060E4180): +36 the picture, +56 the position, +20 the depth, +68 the state ---- */
static void act_stage_pict(void)
{
    u32 r14 = act_cur, r1;
    s32 st = RS16(r14 + 68);
    r1 = (u32)st << 2;
    switch (st) {
    case 0: {                                           /* 0x06035144 */
        u32 r2 = 0xFF780050u;
        W32(r14 + 36, R32(0x06035180 + ((u32)RS16(0x060AA87E) << 2)));
        W32(r14 + 20, 0x00010000);
        if (RS16(0x060AA860) == 10) r2 += 4;
        W32(r14 + 56, r2);
        W16(r14 + 68, 1);
        return;
    }
    case 1:                                             /* 0x06035190 */
        W32(r14 + 4, 24);
        break;
    case 2:                                             /* 0x06035198 */
        W32(r14 + 56, R32(r14 + 56) + 0xFFEC0000u);
        W32(r14 + 4, r1);
        break;
    default:
        menu_unported(0x06035138);
    }
    draw_sprite(R32(r14 + 56), R32(r14 + 36), R32(r14 + 20));      /* 0x06035122 */
}

/* ---- the count-up of the times: slot n counts the BCD time of 0x060B058C + 4 * n up, one nibble after the other ---- */
static void act_result(void);

static void act_result_init(void)                       /* 0x06032BB8 */
{
    u32 r14 = act_cur, r3;
    s32 n = RS16(r14 + 70);
    r3 = R32(0x060B058C + (u32)(n << 2)) & 0x00FFFFFF;
    W32(r14 + 56, r3);
    W32(r14 + 60, 0);
    W32(r14 + 24, 1);
    W32(r14 + 28, 15);
    W32(r14 + 32, 20);
    if (RS8(0x060B00F5) != 8) {                         /* not a goal: no time to show for the total / beyond the last lap */
        if (n == 0) {
            W32(r14 + 28, 0x00FFFFFF);
            r3 = 0x000CCCCC;
            W32(r14 + 56, r3);
            W32(r14 + 60, r3);
        }
        if (n > RS16(0x060B00C8)) {
            W32(r14 + 28, 0x00FFFFFF);
            r3 = 0x000CCCCC;
            W32(r14 + 56, r3);
            W32(r14 + 60, r3);
        }
    }
    if (R8(0x060AA810) == 2) {
        W32(r14 + 28, 0x00FFFFFF);
        W32(r14 + 60, r3);
    }
    W8(0x060A8C38, 0);
    W32(r14 + 0, 0x06032C24);
    act_result();                                       /* the code runs on into the loop body (0x06032C24) */
}

static void act_result(void)                            /* 0x06032C24 */
{
    u32 r14 = act_cur, r13 = 0x060A8C38, r4, r6, r5, r0;
    s32 n = RS16(r14 + 70);
    s32 r2 = RS32(r14 + 32) - 1, r3, r1;
    if (r2 != 0) {
        W32(r14 + 32, (u32)r2);
        goto show;
    }
    r1 = RS32(r14 + 56);
    r2 = RS32(r14 + 28) & r1;
    r3 = RS32(r14 + 60);
    r4 = R32(r14 + 24);
    if (r3 == r2) {
        r2 = RS32(r14 + 28);
        if (r2 == 0x00FFFFFF) {                         /* all digits are there */
            r0 = R8(r13) >> 4;
            if (r0 != 0) {                              /* the blink phase of the new record mark is over / running */
                if (r0 != 1) W8(r13, 0);
                box_fill16(0, 0x25E3858A, 0x00040002);
            } else {
                s32 rank = RS8(0x060A8C62);
                if (rank != -1) {                       /* the row of the new entry in the table: its rank mark */
                    u32 p = R32(0x060A8C74) - 4, ref = R32(p), q;
                    s32 k = rank + 1;
                    do { k--; p -= 8; } while (R32(p) == ref);
                    q = (u32)k << 3;
                    rec_tile16(0x2056 + q, 0x25E3858A, 4, 1);
                    rec_tile16(0x207E + q, 0x25E3860A, 4, 1);
                }
            }
            goto show;
        }
        r2 = (r2 << 4) + 15;
        W32(r14 + 28, (u32)r2);
        r4 <<= 4;
        W32(r14 + 24, r4);
        r1 = RS32(r14 + 56);
        r2 &= r1;
        if (r3 == r2) goto show;
    }
    if (!(R8(r13) & 1)) {                               /* every second pass the digit counts up */
        r3 += (s32)r4;
        W32(r14 + 60, (u32)r3);
    }
show:
    r4 = 0x25E38594;
    if (R32(r14 + 60) & 0x00F00000) r4 -= 2;
    r6 = 0;
    r5 = R32(r14 + 60);
    {
        RecPutNum put = putnum_Grn;
        if (n == 0) {
            if (R8(0x060A8CA3) == 1 && R8(0x060AA82E) != 1) put = putnum_Grn2;
        } else {
            r4 += (u32)(n + 1) << 8;
            if (R32(r14 + 56) == R32(0x060B0530) && R8(0x060AA82E) != 1) put = putnum_Grn2;
        }
        time_disp(r4, r5, put);
    }
    (void)r6;
}

/* ---- the times next to the record table (0x06032B10): the best time of the table and the player's rows ---- */
static void result_times(void)
{
    RecPair p;
    u32 r10, r5, r4;
    RecPutNum put;
    W8(0x060A8CA3, 0);
    p = record_connect(1);
    r5 = R32(p.r0);
    put = putnum_Grn;
    if (r5 == R32(0x060B058C) && R8(0x060AA82E) != 1) { put = putnum_Grn2; W8(0x060A8CA3, 1); }
    time_disp(0x25E3C9B4, r5, put);
    p = record_connect(0);
    r10 = p.r0;
    r5 = R32(r10);
    put = putnum_Grn;
    if (r5 == R32(0x060B058C) && R8(0x060AA82E) != 1) { put = putnum_Grn2; W8(0x060A8CA3, 1); }
    time_disp(0x25E3C5B4, r5, put);
    r5 = R32(r10 + 40);
    put = R8(0x060A8C63) == 1 ? putnum_Grn : putnum_Grn2;
    time_disp(0x25E3C7B4, r5, put);
    (void)r4;
}

static void result_tail(int full)                       /* 0x06032970 / 0x06032986: the times, the count-up actors, the stage sprite and the car */
{
    if (full) {
        result_times();
        W8(0x060A8C38, R8(0x060A8C38) + 1);
        actor_run(0x060E6000, 4);
    }
    act_cur = 0x060E4180;
    act_stage_pict();
    display_car_tm();
}

void result_screen(void)                                /* 0x06032514 */
{
    u32 r11;
    int i;
    W32(0x060A001C, 0x060262A4);
    W16(0x060A88EA, 1);
    W8(0x060AA825, 0);
    W8(0x060AA80D, 1);
    snd_call(0x060173AC, 2, 60, 0);
    init_gspr2();
    init_oscroll();
    set_select_model();
    mkpoly_init();
    /* rotate_light(0xA000, 0, 0): the light vector of the pipeline */
    W16(0x25F80038, 0);
    for (i = 0; i < 8; i++) W16(0x25F80050 + 2 * (u32)i, R16(0x0603263C + 2 * (u32)i));
    if (R8(0x060AA810) == 2) W8(0x060B00F5, 8);
    W8(0x060A8CA4, 0);
    if (RS8(0x060B00F5) >= 10 || RS32(0x060B058C) > 0x00100000 || R8(0x060A8C17) == 1) W8(0x060A8CA4, 1);
    if (R8(0x060AA803) & 0x10) {
        sat_cd_request(0x06032620);                     /* RESULT.DAT */
        r11 = 0x038B0000;
    } else {
        menu_unported(0x0600F550);
        r11 = 0;
    }
    rec_cg_set(0x06032AB8, r11);
    rec_pal_set(0x06032ADC, r11);
    rec_vdp2_set_w(0x06032B08);
    copy_l(0x0283837C + r11, 0x060EE800, 16);
    copy_l(0x028383BC + r11, 0x060EEA00, 16);
    set_pict_result();
    W16(0x060AA8B0, 0);
    W16(0x060AA8B2, 0);
    for (i = 0; i < 160; i++) W32(0x060E4280 - 4 * (u32)(i + 1), 0);
    W32(0x060E4004, 0x060B8400);
    W32(0x060E4008, 0x060EE800);
    W32(0x060E400C, 0x060B8E00);
    W32(0x060E4010, 32);
    W32(0x060E4014, 0);
    W32(0x060E4018, 0x060EEA00);
    W32(0x060E401C, 0);
    rec_tile16(0x20A6, 0x25E38492, 9, 2);
    rec_tile16(0x203A, 0x25E38694, 7, 2);
    rec_tile16(0x203A, 0x25E3C4B2, 8, 2);
    rec_tile16(0x208A, 0x25E3C6B2, 9, 2);
    rec_tile16(0x205A, 0x25E3C8B2, 12, 2);
    if (R8(0x060A8CA4) != 1) {
        rec_tile16(0x20DA, 0x25E3CC2A, 8, 2);
        rec_tile16(0x20FA, 0x25E3CC3C, 4, 2);
        rec_tile16(0x70CA, 0x25E3CC46, 4, 2);
    }
    W16(0x25F800F8, 0x0406);
    W16(0x25F800FA, 0x0604);
    W16(0x25F800FC, 2);
    actor_init(0x060E6000, 0x06032BB8, 4);
    W16(0x060E4180 + 68, 0);
    for (i = 0; i < 64; i++) W32(0x060E5280 - 4 * (u32)(i + 1), 0);
    set_car();
    W32(0x060E5180 + 32, 0x00078000);
    W8(0x060E5180 + 92, R8(0x060AA80A));                /* put_modeltype */
    W32(0x060A882C, 0xFFC50014);
    W8(0x060A8CA1, 0);
    W16(0x25F80110, 127);
    W16(0x25F80114, 255);
    W16(0x25F80116, 255);
    W16(0x25F80118, 255);
    W32(0x060A8C30, 255);
    W16(0x060B9048, 224);
    tv_on();
    W16(0x060AA994, 0);
    W8(0x060A8CA0, 0);
    W8(0x060A8CA5, 2);
    for (;;) {
        snd_call(0x06017580, 0, 0, 0);
        m_mkpoly_frame_init();
        switch (R8(0x060A8CA0)) {
        case 0: {                                       /* 0x0603299E: fade in */
            s32 r0 = RS32(0x060A8C30) - 10;
            if (r0 < 0) { W8(0x060A8CA0, 1); r0 = 0; }
            W16(0x25F80114, (u32)r0);
            W16(0x25F80116, (u32)r0);
            W16(0x25F80118, (u32)r0);
            W32(0x060A8C30, (u32)r0);
            result_tail(1);
            break;
        }
        case 1: {                                       /* 0x060328CC */
            u32 r14 = (u32)RS16(0x060AA994);
            W16(0x060AA994, 0);
            m_mkpoly_frame_init();
            if (R8(0x060A8CA4) != 1 && (r14 & 0xC000)) {
                SND(61);
                if (R8(0x060A8CA1) == 0) {
                    W8(0x060A8CA1, 1);
                    W32(0x060A001C, 0x06015408);
                    W8(0x060AA810, 2);
                    rec_tile16(0x70FA, 0x25E3CC3C, 4, 2);
                    rec_tile16(0x20CA, 0x25E3CC46, 4, 2);
                } else {
                    W8(0x060A8CA1, 0);
                    W32(0x060A001C, 0x060262A4);
                    W8(0x060AA810, 0);
                    rec_tile16(0x20FA, 0x25E3CC3C, 4, 2);
                    rec_tile16(0x70CA, 0x25E3CC46, 4, 2);
                }
            }
            if (r14 & 0xE00) {
                SND(65);
                W8(0x060A8CA0, 2);
                W32(0x060A8C30, 0);
                snd_call(0x06016F3C, 0, 0, 32);
            }
            result_tail(1);
            break;
        }
        case 2: {                                       /* 0x060329BE: fade out */
            s32 r0 = 0;
            int go = 1;
            if (!(RS16(0x060AA994) & 0xE00)) {
                r0 = RS32(0x060A8C30) + 20;
                if (r0 < 255) go = 0;
            }
            if (go) {
                W8(0x060AA803, R8(0x060AA803) | 2);
                W8(0x060AA80D, 1);
                tv_off();
                box_fill16(0, 0x25E3848A, 0x000D000C);
                box_fill16(0, 0x25E3C4AA, 0x00120011);
                r0 = 0;
            }
            W16(0x25F80114, (u32)r0);
            W16(0x25F80116, (u32)r0);
            W16(0x25F80118, (u32)r0);
            W32(0x060A8C30, (u32)r0);
            result_tail(0);
            break;
        }
        default:
            menu_unported(0x060328C0);
        }
        menu_pal_anim(0x060E4000);
        intm_sync(46);
        if (R8(0x060AA803) & 2) break;
    }
    W8(0x060AA803, R8(0x060AA803) & 0xFD);
}

void menu_result_register(void)
{
    menu_reg(0x06032514, result_screen);
    menu_reg(0x06026162, set_pict_result);
    menu_reg(0x06035110, act_stage_pict);
    menu_reg(0x06032BB8, act_result_init);
    menu_reg(0x06032C24, act_result);
}
