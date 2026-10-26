/* M8: the title / attract module (module adv 0x0600D204 .. 0x0600DC28, advctrl 0x0600DC28 .. 0x0600E6A4, the title's pictures display3 0x06013C64 .. and init 0x06004BF0), ported from the
 * original and checked tick for tick against it (tools/scn_title.py, scenarios title_*).
 *
 * What it is: the screen the power-on leaves the program in (the main vector adv_init 0x0600D214).  It plays the attract loop: the sports logo (a VDP2 picture), the title ("PRESS START BUTTON"
 * over the fly-over of a course) while the demo run plays on the course, the ranking pages, the thanks-to picture; START leaves for the backup memory check 0x0602F64E (the mode select).
 * The scene is the staff roll's (menu_photo.c): the course data of AD<course>.DAT, the sky bitmap on RBG0, the road blocks, the scenery objects; two cars drive the course: their motion is not
 * computed but replayed from the recorded run of the demo (ADSAFARI.MAD / ADYOSEMI.MAD / ADCORSE.MAD: 80 bytes of car state per pass at 0x00248000 and 0x002CF800), the script of the camera
 * (the table at 0x00277000 + the course: 32 byte entries) says per shot which camera type looks at which car.
 *
 * Flow: adv_init 0x0600D214 (title2_init 0x0600D204 enters it with the sports logo's state), adv_main 0x0600D710 = a blocking loop like the other screens: state handler [0x060AA807] -> picture
 * -> intm_sync(38).  The states are byte offsets into the table at 0x0600D76C: 0 the demo pass (0x0600D7BC), 8 / 10 its fade out (0x0600D880 / 0x0600D890), 12 title init (0x0600E368), 14 the title's
 * wait (0x0600E3B8), 16 its fade out, 18 / 20 / 22 the second picture (the Sega logo), 24 / 26 / 28 the sports logo, 30 START was pressed (0x0600E47C).  2 / 4 / 6 (0x0600D828 / D840 / D868) are
 * the development build's pause: nothing in the shipped program enters them (menu_unported).
 *
 * The 3D pipeline is not ported (like the photo module's): the matrices, the course drawing, the cars and the scenery are the host's.  The view matrix the camera function builds (c_turnv /
 * translate2) is kept in photo_view.mat for the host, the cars' records in adv_cars. */
#include <string.h>
#include "satlib.h"
#include "menu_lib.h"
#include "satmenu.h"
#include "menu_photo.h"
#include "menu_records.h"
#include "menu_ending.h"
#include "fx.h"

#define FLAGS 0x060AA803u
#define ADV   0x060B2800u               /* advctrl's work block: +0 pass counter of the demo, +4 / +24 counters of the shot, +8 / +9 the cars' model numbers, +10 / +12 flags, +16 the shot's entry, +28 the demo's length */
#define CAMW  0x060AA9B0u               /* the camera control work: +0 function, +8 / +12 / +16 eye, +20 / +22 / +24 angle words */
#define CAR_A 0x060B0000u
#define CAR_B 0x060B0600u
#define DUMMY 0x060B2600u               /* the "car" block of the camera: the road search at its eye */
#define AAC4  0x060AAAC4u               /* the picture the title shows (display3 0x06013C64: a byte offset into a table of 7 handlers) */

AdvCars adv_cars;

/* ---- small helpers ---- */
static s32 sqrt16(s32 x)                                /* 0x0601B8CC: Newton iteration with the on-chip divider, a negative argument gives -1.0 */
{
    s32 g, prev, q;
    if (x < 0) return -0x10000;
    if (x == 0) return 0;
    g = x > 0x10000 ? x : 0x10000;
    do {
        prev = g;
        q = div64((i64)x << 16, g);
        g = (s32)(((u32)g + (u32)q) >> 1);
    } while (prev > g);
    return prev;
}

static s32 sqrt_s(i64 S)                                /* FUN_0600BD24's length: the 64 bit sum of squares (32 fractional bits) -> 16.16 */
{
    s32 hi = (s32)(S >> 32);
    if (hi < 0x8000) return sqrt16(X16(S));
    return (s32)(((u32)sqrt16(hi) << 8) + 127);
}

static void c_reg(const s32 v[3], s32 out[3])           /* 0x0600C118: the unit vector, (0, 0, 1) for a zero vector */
{
    s32 L = sqrt_s((i64)v[0] * v[0] + (i64)v[1] * v[1] + (i64)v[2] * v[2]), x, y, z;
    if (L == 0) { out[0] = 0; out[1] = 0; out[2] = 0x10000; return; }
    y = FDIV(v[1], L); z = FDIV(v[2], L); x = FDIV(v[0], L);
    out[0] = x; out[1] = y; out[2] = z;
}

static void turn_y(s32 m[12], s32 q0, s32 q1)           /* FUN_0600BEA8 */
{
    int r;
    for (r = 0; r < 3; r++) {
        s32 a = m[4 * r], b = m[4 * r + 2];
        m[4 * r] = X16((i64)q1 * a + (i64)neg(q0) * b);
        m[4 * r + 2] = X16((i64)q0 * a + (i64)q1 * b);
    }
}

static void turn_x(s32 m[12], s32 q2, s32 q3)           /* FUN_0600BF00 */
{
    int r;
    for (r = 0; r < 3; r++) {
        s32 a = m[4 * r + 1], b = m[4 * r + 2];
        m[4 * r + 1] = X16((i64)q3 * a + (i64)neg(q2) * b);
        m[4 * r + 2] = X16((i64)q2 * a + (i64)q3 * b);
    }
}

static void c_turnv(s32 m[12], const s32 d[3])          /* 0x0600BD0C: the matrix looks along the direction (FUN_0600BDE4 + BF00 + BEA8) */
{
    i64 sxz = (i64)d[0] * d[0] + (i64)d[2] * d[2];
    s32 L = sqrt_s(sxz + (i64)d[1] * d[1]), Lxz = sqrt_s(sxz);
    s32 q0 = neg(FDIV(d[0], Lxz)), q1 = FDIV(d[2], Lxz), q2 = neg(FDIV(d[1], L)), q3 = FDIV(Lxz, L);
    turn_x(m, q2, q3);
    turn_y(m, q0, q1);
}

static void translate2(s32 m[12], s32 x, s32 y, s32 z) /* 0x06017DBC / 0x06017DD0 */
{
    int r;
    for (r = 0; r < 3; r++) m[4 * r + 3] = (s32)((u32)m[4 * r + 3] + (u32)X16((i64)m[4 * r] * x + (i64)m[4 * r + 1] * y + (i64)m[4 * r + 2] * z));
}

static s32 view[12];                                    /* the master's matrix 0x060D1000 of the pass: the view the camera function builds */

/* the picture helpers of the etc module: r7 rows of r6 words at r5, the row distance r8 */
static void boxclr_word(u32 r5, u32 r6, u32 r7, u32 r8) /* 0x06005840 */
{
    u32 r2, r1;
    do {
        r2 = r5;
        for (r1 = r6; r1 > 0; r1--) { W16(r2, 0); r2 += 2; }
        r5 += r8;
    } while (--r7);
}

static void boxwrt_word_nmap(u32 r4, u32 r5, u32 r6, u32 r7, u32 r8, u32 r9)       /* 0x060057F0: the words r4, r4 + 2 .. (+ r9) */
{
    u32 r2, r1;
    do {
        r2 = r5;
        for (r1 = r6; r1 > 0; r1--) { W16(r2, r4 + r9); r4 += 2; r2 += 2; }
        r5 += r8;
    } while (--r7);
}

static void pcm_reload(void)                            /* 0x06004C90: the PCM data of the sound driver is loaded again (to LWRAM 0x002DF000; the driver copies it to the sound RAM) */
{
    if (R8(0x060AA82C)) {
        sat_load_name_at(0x06004CB0);                   /* PCMDATA.DAT */
        W8(0x060AA82C, 0);
    }
}

/* the CD (hardware): 0x0600F756 plays a track, 0x0600F7BE stops, 0x0600F804 seeks a track (the PSP host's CD audio does the work); the status 0x0600F866 never says "seeking" */
static void cd_stop(void) { snd_call(0x0600F7BE, 0, 0, 0); }
static void cd_seek(u32 track) { snd_call(0x0600F804, track, 0, 0); }
static void cd_play(u32 track, u32 times) { snd_call(0x0600F756, track, times, 0); }
static u32 cd_status(void) { return 0; }

/* ---- the title's pictures (display3 0x06013C64: [AAC4] = the picture) ---- */
static void adv_vdp2_display(void)                      /* 0x06013C64 */
{
    u32 r13 = AAC4;
    switch (RS8(AAC4)) {
    case 0:                                             /* 0x06013C80: PRESS START BUTTON: blinks with the pass counter */
        W16(0x060AA8E6, 0x14);
        W16(0x060AA864, 0x200);
        W32(0x060AA868, 0x300);
        W16(0x060AA876, 0);
        if (RS16(0x060AA004) & 8) boxclr_word(0x25E3AC9C, 16, 2, 0x80);
        else boxwrt_word_nmap(0, 0x25E3AC9C, 16, 2, 0x80, 0x4228);
        break;
    case 2:                                             /* 0x06013CC0 */
        if (RS8(r13 + 1) == 0) {
            W8(r13 + 1, 1);
            W8(r13 + 2, 0x96);
            W16(0x25F80020, 0x18);
            W16(0x060AA8E6, 0x18);
        } else {
            u32 c = (R8(r13 + 2) - 1) & 0xFF;
            W8(r13 + 2, c);
            if (c == 0) {
                W16(0x25F80020, 0x14);
                W16(0x060AA8E6, 0x14);
                W8(r13 + 1, 0);
                W8(r13 + 0, 0);
            }
        }
        break;
    case 4:                                             /* 0x06013CF8 */
        if (RS8(r13 + 1) == 0) {
            W8(r13 + 1, 1);
            W8(r13 + 2, 0x96);
            W16(0x060AA876, 0x100);
        } else {
            u32 c = (R8(r13 + 2) - 1) & 0xFF;
            W8(r13 + 2, c);
            if (c == 0) {
                W16(0x060AA876, 0);
                W8(r13 + 1, 0);
                W8(r13 + 0, 0);
            }
        }
        break;
    case 6:                                             /* 0x06013DC4 */
        W16(0x060AA864, 0x200);
        W16(0x060AA868, 0x200);
        W16(0x060AA8E6, 1);
        break;
    case 8:                                             /* 0x06013DE0 */
        W32(0x060AA864, 0);
        W16(0x060AA868, 0x200);
        W16(0x060AA8E6, 1);
        break;
    case 10:                                            /* 0x06013DF8 */
        W32(0x060AA864, 0);
        W16(0x060AA868, 0x300);
        W16(0x060AA8E6, 1);
        break;
    case 12:                                            /* 0x06013D28: the ranking pages (adv_recordact), then the same counter as the others */
        adv_recordact();
        if (RS8(r13 + 1) == 0) {
            W8(r13 + 1, 1);
            W8(r13 + 2, 0xFF);
            W16(0x060AA864, 0x200);
            W32(0x060AA868, 0);
            W16(0x25F80020, 0x11);
            W16(0x060AA8E6, 0x11);
        } else {
            u32 c = (R8(r13 + 2) - 1) & 0xFF;
            W8(r13 + 2, c);
            if (c == 0) {
                W16(0x060AA864, 0x200);
                W32(0x060AA868, 0x300);
                W16(0x25F80020, 0x14);
                W16(0x060AA8E6, 0x14);
                W8(r13 + 1, 0);
                W8(r13 + 0, 0);
            }
        }
        break;
    default:
        menu_unported(0x06013C6A);                      /* an index outside the table (the original jumps into the data behind it) */
    }
}

/* ---- the ranking tables of the title's second picture (init_adv_vdp2 0x06004BF0 -> FUN_0603125A: the 16 best times of the arcade ranking over the VDP2 planes 0x25E2C000 / 0x25E2E000) ---- */
static u32 rank_letter(u32 r2, u32 r4)                  /* 0x06031408: one letter of a name as 2 x 2 cells (32 bit) at r4; a blank letter writes nothing and does not advance; returns the next position */
{
    r2 &= 0xFF;
    if (r2 == 0) return r4;
    r2 = (r2 << 2) + 0x00400938u;
    W32(r4, r2); r4 += 4; r2 += 2;
    W32(r4, r2); r4 += 126 + 126; r2 += 106;
    W32(r4, r2); r4 += 4; r2 += 2;
    W32(r4, r2); r4 += (u32)-126 + (u32)-126;
    return r4;
}

static u32 rank_name(u32 r4, u32 r5)                    /* 0x060313E4: the three letters of the name word r5 (bits 16 .. 23, 8 .. 15, 0 .. 7) */
{
    r4 = rank_letter((r5 << 16) | (r5 >> 16), r4);
    r4 = rank_letter(((r5 >> 8) & 0xFF) | ((r5 & 0xFF) << 8) | (r5 & 0xFFFF0000u), r4);
    r4 = rank_letter(r5, r4);
    return r4;
}

static void rank_row(u32 r8, s32 r9, u32 *r7)           /* 0x06031314: a row of the table at r8: rank r9 (< 0: none), the record at *r7 (time, info) */
{
    u32 r4 = r8, r5, r2;
    if (r9 >= 0) putnum_rank2(r8, (u32)r9);
    r4 = r8;
    r5 = R32(*r7); *r7 += 4;
    if (RS16(0x060AA860) == 0 && R8(0x06042A3A) == 1 && !(r5 & 0x00F00000u)) r4 += 4;
    time_disp(r4 + 52, r5, putnum_name2);
    r4 = r8 + 20;
    r5 = R32(*r7); *r7 += 4;
    rank_name(r4, r5);
    r5 >>= 16;
    r5 >>= 8;
    r4 = r5 & 1;
    r2 = 0x003008F4u + r4 * 12;                         /* the transmission flag: 6 cells */
    r4 = r8 + 112;
    W32(r4, r2); r4 += 4; r2 += 2;
    W32(r4, r2); r4 += 4; r2 += 2;
    W32(r4, r2); r4 += 124 + 124; r2 += 2;
    W32(r4, r2); r4 += 4; r2 += 2;
    W32(r4, r2); r4 += 4; r2 += 2;
    W32(r4, r2);
    r5 >>= 2; r5 >>= 2;
    r5 &= 3;
    r2 = 0x0030090Cu;
    if (r5 != 0) r2 += 24;                              /* the car class */
    rec_tile32(r2, r8 + 88, 6, 2);
}

static void rank_tables(void)                           /* 0x0603125A */
{
    u32 r7 = 0x06041220u, r8 = 0x25E2C91Cu;
    s32 r9, r12 = 1, r14 = 1, r10 = 16, r13, r11;
    rec_tile32(0x00300A44, 0x25E2C238, 0x10, 4);
    rec_tile32(0x00300B84, 0x25E2C718, 0x1F, 2);
    rec_tile32(0x00300A44, 0x25E2E238, 0x10, 4);
    rec_tile32(0x00300B84, 0x25E2E718, 0x1F, 2);
    if (R8(0x06042A3A) == 1) r7 = 0x06041320u;
    do {
        r13 = RS32(r7);
        r9 = r12;
        rank_row(r8, r9, &r7);
        r8 += 0x200;
        if (r10 == 9) r8 += 0x1000;
        r14++;
        r11 = RS32(r7);
        if (r13 != r11) r12 = r14;
        r10--;
    } while (r10 != 0);
}

static void colorset(u32 r4)                            /* 0x06004754: n tables of (source, destination, long count) */
{
    u32 n = R32(r4), src, dst, cnt;
    r4 += 4;
    do {
        src = R32(r4); dst = R32(r4 + 4); cnt = R32(r4 + 8);
        r4 += 12;
        do { W32(dst, R32(src)); src += 4; dst += 4; } while (--cnt);
    } while (--n);
}

static void init_adv_vdp2(void)                         /* 0x06004BF0 */
{
    sat_load_name_at(0x06004C80);                       /* CGDATAAD.DAT */
    rank_tables();
    colorset(0x06004C34);
    W32(AAC4, 0);
    W16(0x25F800D2, 0);
    /* init_adv_map 0x06013E10: nothing */
    W16(0x060AA864, 0x200);
    W32(0x060AA868, 0x300);
    W16(0x060AA876, 0);
}

/* ---- the cars (the car control slots 0x060B0000 / 0x060B0600: the function +0 runs on the master, +4 the display) ---- */
static void cars_clear(void)                            /* allclr_car_ctrl_work 0x06005EC4 */
{
    rfillff(0x060B0500, 0, 0x140);
    rfillff(0x060B2600, 0, 0x800);
    rfillff(0x060B2B00, 0, 0x140);
}

static void clr_ctrl_work(u32 r14, u32 fn, u32 nlongs)  /* clr_camera_ctrl_work 0x06005E44 (15 longs) / clr_rscroll_ctrl_work 0x06005E74 (7 longs) */
{
    W32(r14, fn);
    rfillff(r14 + 4 * (nlongs + 1), 0, nlongs);
}

static void car_record(u32 r14)                         /* 0x0600D684: one record of the recording (80 bytes at [slot + 0x508]) into the car block */
{
    u32 r13 = r14 + 0x500, r8 = R32(r13 + 8), k;
    for (k = 0; k < 6; k++) { W32(r14 + 12 + 4 * k, R32(r8)); r8 += 4; }
    W32(r14 + 48, R32(r8)); r8 += 4;
    W32(r14 + 56, R32(r8)); r8 += 4;
    for (k = 0; k < 9; k++) { W32(r14 + 60 + 4 * k, R32(r8)); r8 += 4; }
    W16(r14 + 180, R32(r8)); r8 += 4;
    W32(r14 + 236, R32(r8)); r8 += 4;
    W32(r14 + 36, (u32)RS16(r8) << 4); r8 += 2;
    W32(r14 + 44, (u32)RS16(r8) << 4); r8 += 2;
    W32(r13 + 8, r8);
}

static void car_function(u32 r14)                       /* 0x0600D5FC: the state machine of a car slot: 0 first pass, 2 every pass (AA810 = 2 starts in state 2) */
{
    u32 st = (u32)RS16(r14 + 8);
    if (st == 0) {
        W16(r14 + 8, st + 2);
        car_record(r14);
        W8(FLAGS, R8(FLAGS) | 0x40);
        W8(0x060AA994, R8(0x060AA994) & 0xF7);
        calc_dispang2(r14);                             /* calc_dispang 0x06007D2C (r13 = the camera work: unused later) */
        get_poly_map(r14);
        event_init((u32)RS16(0x060AA87E));
        /* spray_init 0x0601D074 (r4 = 0x01010101): the two spray pools (0x060D0140 ..), not part of the console memory the host leaves */
    } else if (st == 2) {
        car_record(r14);
    } else {
        menu_unported(0x0600D608);                      /* a state outside the table: the AA810 = 2 path's 0x0600D644 / 0x0600D670 are never entered (the table has two entries) */
    }
}

static void disp_car(u32 r14)                           /* 0x060101E8 (r5 = 0): only the choice of the draw routine is memory (entry 0 of the table 0x060186FC / 0x06018908 / 0x060186FC / 0x06010654); the tyre marks are the spray system's */
{
    W32(r14 + 900, 0);
    W32(r14 + 896, 0x060186FC);
}

static void car_display_a(void)                         /* 0x0600D5D4 */
{
    u32 e = R32(ADV + 16);
    if (R8(e + 1) & 1) disp_car(CAR_A);
}

static void car_display_b(void)                         /* 0x0600D5E8 (the slave's) */
{
    u32 e = R32(ADV + 16);
    if (R8(e + 1) & 2) disp_car(CAR_B);
}

static void car_slot_run(u32 slot, int slave)           /* the slot function the master (bit 0 clear) or the slave (bit 0 set) runs */
{
    u32 fn = R32(slot);
    if (fn == 0) return;
    if (slave ? !(fn & 1) : (fn & 1)) return;
    if ((fn & ~1u) == 0x0600D5FC) car_function(slot);
    else menu_unported(fn);
}

static void car_move2p_ctrl(void)                       /* 0x06005EE4: the slave's jobs (s_car_move2p_ctrl 0x06005F18: functions with bit 0 set), then the master's (0x06005EF8) */
{
    car_slot_run(CAR_A, 1);
    car_slot_run(CAR_B, 1);
    car_slot_run(CAR_A, 0);
    car_slot_run(CAR_B, 0);
}

static void car_disp2p_ctrl(void)                       /* 0x06005F44: s_car_disp2p_ctrl 0x06005F70 (the slave's: slot functions +4 with bit 0 set), m_car_disp2p_ctrl 0x06005F50 (the others) */
{
    static const u32 slot[2] = { CAR_A, CAR_B };
    int k, pass;
    for (pass = 0; pass < 2; pass++) {
        for (k = 0; k < 2; k++) {
            u32 fn = R32(slot[k] + 4);
            if (fn == 0) continue;
            if (pass == 0 ? !(fn & 1) || (fn & ~1u) == 0 : (fn & 1)) continue;
            switch (fn & ~1u) {
            case 0x0600D5D4: car_display_a(); break;
            case 0x0600D5E8: car_display_b(); break;
            default: menu_unported(fn);
            }
        }
    }
}

/* ---- the camera control function [CAMW] = 0x0600D9D8 ---- */
static void adv_camera_fn(void);

static void camera_ctrl(void)                           /* 0x06005E20 */
{
    u32 fn = R32(CAMW);
    if (fn == 0x0600D9D8) adv_camera_fn();
    else menu_unported(fn);
}

static void camera_road_search(void)                    /* 0x0600DB94: the road block search at the camera's eye (the type 12's fixed point is the spline's end) */
{
    u32 e = R32(ADV + 16);
    s32 x, z;
    if (R8(e + 0) == 12) { x = (s32)0xFFA60000; z = (s32)0xFDEEDF70; }
    else { x = RS32(CAMW + 8); z = RS32(CAMW + 16); }
    W32(DUMMY + 24, (u32)x);
    W32(DUMMY + 32, (u32)z);
    get_block(DUMMY);
    calc_dispang2(DUMMY);
    get_poly_map(DUMMY);
}

static void cam_yaw(u32 car)                            /* 0x0600DE7A: the angle word of the camera towards the car (the pitch the routine also returns is not used) */
{
    s32 a[3], b[3], r0, r1;
    a[0] = RS32(CAMW + 8); a[1] = RS32(CAMW + 12); a[2] = RS32(CAMW + 16);
    b[0] = RS32(car + 24); b[1] = RS32(car + 28); b[2] = RS32(car + 32);
    W16(CAMW + 28, 0);
    get_camera_angle(a, b, &r0, &r1);
    W16(CAMW + 22, (u32)neg(r1));
}

static void cam_back_off(const s32 dir[3], s32 amount) /* 0x0600DEA8: the eye moves back along the direction by `amount`, the view looks along it */
{
    s32 u[3], eye[3];
    c_turnv(view, dir);
    c_reg(dir, u);
    eye[0] = RS32(CAMW + 8); eye[1] = RS32(CAMW + 12); eye[2] = RS32(CAMW + 16);
    eye[0] = (s32)((u32)eye[0] - (u32)X16((i64)u[0] * amount));
    eye[1] = (s32)((u32)eye[1] - (u32)X16((i64)u[1] * amount));
    eye[2] = (s32)((u32)eye[2] - (u32)X16((i64)u[2] * amount));
    W32(CAMW + 8, (u32)eye[0]);
    W32(CAMW + 12, (u32)eye[1]);
    W32(CAMW + 16, (u32)eye[2]);
    translate2(view, neg(eye[0]), neg((s32)((u32)eye[1] + 0x10000u)), neg(eye[2]));
}

static void adv_exec(void)                              /* 0x0600DC28: the camera of the shot (the type byte of the shot's entry picks the routine) */
{
    u32 e = R32(ADV + 16);
    s32 r2 = RS8(ADV + 8), r3 = RS8(ADV + 9), d[3];
    u32 car = CAR_A;
    s32 ex, ey, ez;
    if (R8(e + 1) & 0x80) { s32 t = r2; r2 = r3; r3 = t; }
    W8(CAR_A + 0x5C, (u32)r2);
    W8(CAR_B + 0x5C, (u32)r3);
    ex = RS32(e + 12); ey = RS32(e + 16); ez = RS32(e + 20);
    switch (R8(e + 0)) {
    case 0:                                             /* 0x0600DC6C: camera_vr1 (unused: [0x060B280B] is never set) */
        menu_unported(0x0600DA3C);
        return;
    case 4: car = CAR_B;                                /* 0x0600DC84 */
        /* fall through */
    case 2:                                             /* 0x0600DC86: a fixed eye looking at the car */
        W32(CAMW + 8, (u32)ex);
        W32(CAMW + 16, (u32)ez);
        d[0] = (s32)((u32)RS32(car + 24) - (u32)ex);
        d[1] = (s32)((u32)RS32(car + 28) - (u32)ey);
        d[2] = (s32)((u32)RS32(car + 32) - (u32)ez);
        c_turnv(view, d);
        translate2(view, neg(ex), neg((s32)((u32)ey + 0x10000u)), neg(ez));
        cam_yaw(car);
        return;
    case 6: {                                           /* 0x0600DCD0: a fixed eye looking at a point of the table 0x00277020 (16 bytes per entry: the point) */
        u32 p = 0x00277020u + ((u32)RS16(e + 2) << 4);
        s32 t0 = RS32(p), t1 = RS32(p + 4), t2 = RS32(p + 8);
        W32(CAMW + 8, (u32)ex);
        W32(CAMW + 16, (u32)ez);
        d[0] = (s32)((u32)t0 - (u32)ex);
        d[1] = (s32)((u32)t1 - (u32)ey);
        d[2] = (s32)((u32)t2 - (u32)ez);
        c_turnv(view, d);
        translate2(view, neg(ex), neg((s32)((u32)ey + 0x10000u)), neg(ez));
        cam_yaw(p - 24);
        return;
    }
    case 18: car = CAR_B;                               /* 0x0600DD30 */
        /* fall through */
    case 8: {                                           /* 0x0600DD32: the eye follows the car at the offset of the entry, looking along the offset */
        s32 px, py, pz;
        d[0] = ex; d[1] = ey; d[2] = ez;
        c_turnv(view, d);
        px = (s32)((u32)RS32(car + 24) - (u32)ex);
        py = (s32)((u32)RS32(car + 28) - (u32)ey);
        pz = (s32)((u32)RS32(car + 32) - (u32)ez);
        W32(CAMW + 8, (u32)px);
        W32(CAMW + 16, (u32)pz);
        translate2(view, neg(px), neg((s32)((u32)py + 0x10000u)), neg(pz));
        cam_yaw(car);
        return;
    }
    case 12: {                                          /* 0x0600DD70: the eye on a spline (the control points 0x0600E058), looking at a fixed point */
        s32 t = (s32)((u32)R32(ADV + 0) * 0x13Eu);
        s32 w0, w1, w2, w3, tt, ttt, u, uu, uuu;
        if (0x10000 > t) {
            tt = X16((i64)t * t);                       /* spline 0x06007DDC: the weights of the uniform cubic B-spline */
            ttt = X16((i64)tt * t);
            u = 0x10000 - t;
            uu = X16((i64)u * u);
            uuu = X16((i64)uu * u);
            w0 = HM(0x2AAAAAAB, uuu);
            w1 = (s32)((u32)(ttt >> 1) - (u32)tt + 0xAAAB);
            w2 = (s32)((u32)(tt >> 1) - (u32)(ttt >> 1) + (u32)(t >> 1) + 0x2AAB);
            w3 = HM(0x2AAAAAAB, ttt);
            {
                int k;
                for (k = 0; k < 3; k++) {
                    u32 pt = 0x0600E058u + 16u * (u32)k;
                    i64 acc = (i64)w0 * RS32(pt) + (i64)w1 * RS32(pt + 4) + (i64)w2 * RS32(pt + 8) + (i64)w3 * RS32(pt + 12);
                    W32(ADV + 32 + 4 * (u32)k, (u32)X16(acc));
                }
            }
            W32(ADV + 44, (u32)((s32)0xFFB163DE - RS32(ADV + 32)));
            W32(ADV + 48, (u32)((s32)0x000F77AE - RS32(ADV + 36)));
            W32(ADV + 52, (u32)((s32)0xFDECB574 - RS32(ADV + 40)));
        }
        d[0] = RS32(ADV + 44); d[1] = RS32(ADV + 48); d[2] = RS32(ADV + 52);
        c_turnv(view, d);
        ex = RS32(ADV + 32); ey = RS32(ADV + 36); ez = RS32(ADV + 40);
        W32(CAMW + 8, (u32)ex);
        W32(CAMW + 16, (u32)ez);
        translate2(view, neg(ex), neg(ey), neg(ez));
        cam_yaw(ADV + 8);                               /* r13 = ADV + 8: the point is the spline's own (ADV + 32) */
        return;
    }
    case 16: car = CAR_B;                               /* 0x0600DDE8 */
        /* fall through */
    case 14: {                                          /* 0x0600DDEA: a fixed eye looking at the car, backing off when it is nearer than the table's distance */
        s32 dx = (s32)((u32)RS32(car + 24) - (u32)ex), dy = (s32)((u32)RS32(car + 28) - (u32)ey), dz = (s32)((u32)RS32(car + 32) - (u32)ez);
        i64 S = (i64)dx * dx + (i64)dy * dy + (i64)dz * dz;
        s32 hi = (s32)(S >> 32), dist, amount = 0, w = RS16(e + 2);
        W32(CAMW + 8, (u32)ex);
        W32(CAMW + 12, (u32)ey);
        W32(CAMW + 16, (u32)ez);
        if (hi > 0x7FFF) dist = (s32)((u32)sqrt16(hi) << 8);
        else dist = sqrt16(X16(S));
        if (w >= 0) {
            u32 p = 0x00277020u + ((u32)w << 4);
            s32 r2 = RS32(p);
            if (r2 > dist) amount = 0;
            else {
                s32 r3 = RS32(p + 4);
                amount = r2 - X16((i64)dist * r3);
            }
            if (R8(ADV + 10) == 0) amount = 0;
        }
        amount >>= 1;
        d[0] = dx; d[1] = dy; d[2] = dz;
        cam_back_off(d, amount);
        cam_yaw(car);
        return;
    }
    default:                                            /* 10 / 20 / 22: the table's entries are the epilogue itself */
        return;
    }
}

static void adv_camera_fn(void)                         /* 0x0600D9D8: unit matrix, the shot's camera, the road search at the eye, the number of blocks the shot allows */
{
    memset(view, 0, sizeof view);
    view[0] = view[5] = view[10] = 0x10000;             /* unit_matrix 0x06017D24 */
    if (R8(ADV + 11) != 0) { menu_unported(0x0600DA24); return; }
    adv_exec();
    camera_road_search();
    {
        u32 e = R32(ADV + 16);
        W16(0x060AA000, (u32)((s32)RS16(0x060AA000) - RS8(e + 8)));
    }
}

/* ---- the control of the shots (advctrl: advctrlinit 0x0600DF0C, advFrmCtrl 0x0600DF6C, set_advdata 0x0600E088) ---- */
static void set_advdata(void)                           /* 0x0600E088: the recorded run and the shot table of the course */
{
    if (R8(FLAGS) & 0x10) sat_load_name_at(R32(0x0600E0B4u + ((u32)RS16(0x060AA87E) << 2)));       /* ADSAFARI.MAD / ADYOSEMI.MAD / ADCORSE.MAD */
    else menu_unported(0x0600E0E6);                     /* the cartridge image at 0x02F00000 (not on the disc) */
}

static void advctrlinit(void)                           /* 0x0600DF0C */
{
    u32 r8, r1;
    W32(ADV + 0, 0);
    W32(ADV + 24, 0);
    W8(ADV + 10, 1);
    W8(ADV + 12, 1);
    r8 = R32(0x00277000u + (u32)RS16(0x060AA882));
    if (R8(FLAGS) & 0x10) r8 += 0xFD348000u;
    W32(ADV + 4, (u32)RS16(r8 + 4));
    W32(ADV + 16, r8);
    W8(ADV + 8, 0);
    W8(ADV + 9, 1);
    r1 = 0;
    do {
        r1 += (u32)RS16(r8 + 6);
        r8 += 32;
    } while (RS8(r8) >= 0);
    W32(ADV + 28, r1);
    W32(0x060AA9AC, ADV);
}

static void adv_frm_ctrl(void)                          /* 0x0600DF6C: one pass of the shot clock; the recorded run of the pass goes to the cars' cursors */
{
    u32 r8 = R32(ADV + 16);
    s32 r0, r1, r2;
    W32(ADV + 20, R32(r8 + 0));
    if (R8(ADV + 13) != 0) {
        W32(0x060B9044, R32(0x060AA9F8));               /* clr_hscr_rr 0x06015FBC */
        W8(ADV + 13, 0);
    }
    r1 = RS32(ADV + 4) + 1;
    r2 = RS32(ADV + 24) + 1;
    r0 = RS16(r8 + 6);
    if (r2 > r0 && R8(ADV + 12) != 0) {                 /* the shot is over: the next entry */
        W8(ADV + 13, 1);
        r2 = 0;
        r8 += 32;
        if (RS8(r8) < 0) { W8(0x060AA807, 8); return; } /* the script is over: fade out */
        W32(ADV + 16, r8);
        r1 = RS16(r8 + 4);
    }
    W32(ADV + 4, (u32)r1);
    W32(ADV + 24, (u32)r2);
    if (RS8(r8) == 12) {
        r0 = RS16(r8 + 4);
        r1 = RS32(ADV + 0) - r0;
        r0 = r1 < 0 ? 0 : r1;
    } else {
        r0 = R8(ADV + 12) != 0 ? RS32(ADV + 4) : RS32(ADV + 0);
    }
    r0 = (s32)((u32)r0 * 80u);
    W32(0x060B0508, 0x00248000u + (u32)r0);
    W32(0x060B0B08, 0x002CF800u + (u32)r0);
    r0 = RS32(ADV + 0) + 1;
    W32(ADV + 0, (u32)r0);
    if (r0 == RS32(ADV + 28) - 7) pre_fade_inout(0, 0xFF, 12);
    if (r0 == 0x1C2) W8(AAC4, 12);
    else if (r0 == 0x384) W8(AAC4, 2);
}

/* ---- what the host draws: the view matrix and the cars ---- */
static void adv_scene_fill(void)
{
    int k, n = RS16(0x060AA000);
    u32 slot[2] = { CAR_A, CAR_B };
    u32 e = R32(ADV + 16);
    photo_view.tick = sat_ticks ? sat_ticks : 1;
    photo_view.course = RS16(0x060AA87E) & 3;
    photo_view.stretch = 0;
    photo_view.have_mat = 1;
    memcpy(photo_view.mat, view, sizeof view);
    if (n > 16) n = 16;
    if (n < 0) n = 0;
    photo_view.nparts = n;
    for (k = 0; k < n; k++) photo_view.parts[k] = R8(0x060AA140 + (u32)k);
    adv_cars.tick = sat_ticks ? sat_ticks : 1;
    for (k = 0; k < 2; k++) {
        int i;
        adv_cars.car[k].shown = (R8(e + 1) >> k) & 1;
        adv_cars.car[k].model = RS8(slot[k] + 92);
        for (i = 0; i < 3; i++) {
            adv_cars.car[k].ang[i] = RS32(slot[k] + 12 + 4 * (u32)i);
            adv_cars.car[k].pos[i] = RS32(slot[k] + 24 + 4 * (u32)i);
        }
    }
}

/* ---- one demo pass (0x0600D908) ---- */
static void adv_frame(void)
{
    car_move2p_ctrl();                                  /* the slave's jobs, the master's */
    camera_ctrl();
    s_mkpoly_frame_init();
    m_mkpoly_frame_init();
    car_disp2p_ctrl();
    /* p2sync_ctrl 0x060067A4: slot +8 of 0x060B0600 is not 4 */
    adv_scene_fill();
    W8(0x060D0114, RS8(0x060B00C5) == 0);               /* 0x0601BCB8 -> 0x0601BD3E: the object dispatcher's side flag (the objects' state functions are the host's) */
    /* 0x0601D410: the spray list; draw_course 0x060187C0 (pipeline) */
    normal_endmove();                                   /* rscroll_ctrl 0x06005E54 */
    W16(0x060D0040, 0);                                 /* wait_slave_end 0x06017BAC leaves 0 in r0: the store of the epilogue (the word at 0x060D0034 + 12) */
}

/* ---- the module's set-up ---- */
static void init_adv_actors(void)                       /* 0x0600D3F0: the actors of the demo: two cars (the same slot function, their recording cursors), the camera function, the scroll control */
{
    W32(0x060AA168, 0);
    W16(0x060AA004, 0);
    W32(0x060AA178, 0);
    W32(0x060AA180, 0);
    W8(0x060AA301, 0);
    W8(0x060AA302, 0);
    W16(0x060AA01A, 0);
    cars_clear();
    W32(CAR_A + 0, 0x0600D5FC);
    W32(CAR_A + 4, 0x0600D5D4);
    W8(CAR_A + 92, 0);
    W32(0x060B0508, 0x00248000);
    W8(CAR_B + 92, 1);
    W32(CAR_B + 0, 0x0600D5FC);
    W32(CAR_B + 4, 0x0600D5E9);
    W32(0x060B0B08, 0x002CF800);
    clr_ctrl_work(CAMW, 0x0600D9D8, 15);
    clr_ctrl_work(0x060AA9F0, 0x06006BD4, 7);
    W16(CAR_A + 8, R8(0x060AA810));
    W8(0x060B0500, 0);
    W8(CAR_A + 244, 0);
    W8(CAR_A + 245, 0);
    W8(CAR_A + 0x5F, 0);
    /* carconfig 0x06009938: the car's tuning (the race engine's: stubbed in the oracle scenarios, nothing to replay here) */
}

static void car_ctrl_pass(void)                         /* 0x0600D398: the cars and the camera once (twice for the cars) */
{
    car_move2p_ctrl();
    car_move2p_ctrl();
    camera_ctrl();
}

static void init_adv_stage(void)                        /* 0x0600D2B2: the actors, the shot script, the first pass of the cars and the camera */
{
    init_gscroll();
    W16(0x060C0000, 0x8000);
    W16(0x25C00000, 0x8000);
    init_adv_actors();
    set_advdata();
    advctrlinit();
    swclear();
    W8(FLAGS, R8(FLAGS) & 0xBF);
    car_ctrl_pass();
    mkpoly_init();
    adv_recordact_init();
    /* set_slave_exec(0x0600D38C): the slave's cache control only */
    pad_kind_init();
}

static void init_adv_work_start(void)                   /* 0x0600D3B4: the course's data and textures */
{
    W8(0x060AA808, 1);
    W8(0x060AA80C, 1);
    W16(0x060AA8E0, 0);
    W32(0x060AAA5C, 0x002CF800);
    W32(0x060AAA60, 0x002DF000);
    {
        u32 c = (u32)RS16(0x060AA87E);
        W16(0x060AA880, c * 2);
        W16(0x060AA882, c * 4);
        W16(0x060AA884, c * 8);
    }
    crs_adv_dataset();
    set_game_texture();                                 /* crs_adv_dataset keeps its saved pr on the stack: the tail call runs twice (as in the photo module) */
    set_game_texture();
}

static void adv_init_body(void)                         /* 0x0600D228 */
{
    tv_off();
    W8(0x060AA82B, 1);
    snd_call(0x060173AC, 3, 0, 0);
    W32(0x060AA9AC, ADV);
    W8(0x060AA80D, 2);
    snd_call(0x06016FA4, 0xFFFFFFC0u, 0, 0);
    snd_call(0x06017580, 0, 0, 0);
    pad_kind_init();
    init_gspr();
    photo_scene_vram();                                 /* init_adv_vram 0x0600D490 */
    init_adv_vdp2();
    W16(0x25D00004, 2);
    W32(0x060A001C, 0x0600D710);
    W8(0x060AA806, R8(0x060AA806) & 0xFE);
    W8(0x060A0001, 2);
    W8(0x060A0013, 2);
    W16(0x25F80020, 0x11);
    W16(0x060AA8E6, 0x11);
    if (R8(0x060AA807) == 24) init_adv_work_start();
    tv_on();
}

static void adv_init(void)                              /* 0x0600D214: the first time after the power-on the sports logo, afterwards the title */
{
    W8(0x060AA807, R8(0x060AA82B) == 0 ? 12 : 24);
    W8(0x060AA82B, 1);
    adv_init_body();
}

static void title2_init(void)                           /* 0x0600D204 */
{
    W8(0x060AA807, 18);
    adv_init_body();
}

/* ---- the states of the attract loop ---- */
static void title_init(void)                            /* 0x0600E368: state 12 */
{
    W32(ADV + 0, 0);
    W8(AAC4, 6);
    W8(0x060AA807, 14);
    adv_recordact_init();
    snd_call(0x060173AC, 3, 76, 0);
    pre_fade_inout(0xFF, 0, -3);
    adv_vdp2_display();
}

static int title_start_pressed(void)                    /* 0x0600E4DC: T flag = 1 when there is no START */
{
    u32 w;
    if (R8(0x060AA9A9) == 0) return 0;
    w = R16(0x060AA994);
    W16(0x060AA00A, w);                                 /* pl1p_or_switch 0x060085EC */
    return ((w >> 8) & 0x08) != 0;
}

static void title_wait(void)                            /* 0x0600E3B8: state 14: START leaves, 240 passes go on to the next picture */
{
    if (!title_start_pressed()) {
        u32 c = R32(ADV) + 1;
        W32(ADV, c);
        if (240 > (s32)c) return;
        W8(0x060AA807, 16);
        fade_color_init(3);
        return;
    }
    W16(0x060AA994, 0);
    if (R8(FLAGS) & 0x20) {                             /* a fade runs: end it and go back to counting */
        pre_fade_inout(0, 0xFF, 51);
        W8(FLAGS, R8(FLAGS) & 0xDF);
        W32(ADV, 0);
        return;
    }
    snd_call(0x06016FBC, 75, 0, 0);
    snd_call(0x06017580, 0, 0, 0);
    pcm_reload();
    W8(0x060AA807, 30);
    {
        s32 c = RS32(ADV);
        if (c > 30) fade_color_init(3);
    }
}

static void start_wait(void)                            /* 0x0600E47C: state 30: after START the picture fades out, then the backup memory check */
{
    s32 c = RS32(ADV) + 1;
    W32(ADV, (u32)c);
    if (c > 60) {
        if (!(R8(FLAGS) & 0x20)) {
            W8(FLAGS, R8(FLAGS) | 2);
            W32(0x060A001C, 0x0602F64E);
            W8(0x060AA82B, 0);
            vram_scrmap_a_clr();
        }
    } else if (c == 60) fade_color_init(3);
}

static void title_fout(void)                            /* 0x0600E504: state 16 */
{
    if (!(R8(FLAGS) & 0x20)) W8(0x060AA807, 18);
}

static void segalogo_init(void)                         /* 0x0600E520: state 18 */
{
    W32(ADV, 0);
    W8(AAC4, 8);
    W8(0x060AA807, 20);
    fade_color_init(0);
    cd_stop();
    adv_vdp2_display();
}

static void segalogo_wait(void)                         /* 0x0600E560: state 20 */
{
    if (R8(FLAGS) & 0x20) return;
    W16(0x060AA800, 0x202);
    init_adv_work_start();
    W8(0x060AA807, 22);
    pre_fade_inout(0, (s32)0xFFFFFF01, -48);
}

static void segalogo_fout(void)                         /* 0x0600E5A4: state 22 */
{
    if (!(R8(FLAGS) & 0x20)) W8(0x060AA807, 24);
}

static void sportslogo_init(void)                       /* 0x0600E5C0: state 24 */
{
    W32(ADV, 0);
    W8(AAC4, 10);
    W8(0x060AA807, 26);
    pre_fade_inout((s32)0xFFFFFF01, 0, 48);
    adv_vdp2_display();
}

static void sportslogo_wait(void)                       /* 0x0600E5FC: state 26 */
{
    if (R8(FLAGS) & 0x20) return;
    W16(0x060AA800, 0x202);
    init_adv_stage();
    W8(0x060AA807, 28);
    fade_color_init(3);
    cd_seek(13);
}

static void sportslogo_fout(void)                       /* 0x0600E644: state 28: when the fade and the CD seek are over the demo starts and the CD plays */
{
    if (cd_status() == 4) return;
    if (R8(FLAGS) & 0x20) return;
    W8(AAC4, 0);
    W8(0x060AA807, 0);
    cd_play(13, 0);
    sync_vout();
    fade_color_init(0);
}

static void demo_run(void)                              /* 0x0600D7BC: state 0 */
{
    W32(0x060AA168, R32(0x060AA168) + 1);
    W16(0x060AA004, R16(0x060AA004) + 1);
    adv_frm_ctrl();
    adv_frame();
    photo_stats();
    /* game_zsort 0x0601B284: the pipeline's */
    if (!(R8(FLAGS) & 0x20) && (R8(FLAGS) & 0x40)) {
        u32 w = R16(0x060AA994);
        W16(0x060AA00A, w);                             /* pl1p_or_switch */
        if ((w >> 8) & 0x08) {                          /* START: fade out */
            W8(0x060AA807, 8);
            fade_color_init(1);
            cd_stop();
        }
    }
    W16(0x060AA994, 0);
}

static void demo_fade_start(void)                       /* 0x0600D880: state 8 */
{
    W16(0x060AA994, 0);
    W8(0x060AA807, 10);
}

static void demo_fade_wait(void)                        /* 0x0600D890: state 10 */
{
    W16(0x060AA994, 0);
    if (!(R8(FLAGS) & 0x20)) W8(FLAGS, R8(FLAGS) | 2);
}

static void course_next(void)                           /* 0x0600DC02: the next demo course (desert, forest, mountain, [lake]) */
{
    u32 lim = (R8(FLAGS) & 0x10) ? 2 : 1, c = (u32)RS16(0x060AA87E);
    W16(0x060AA87E, c >= lim ? 0 : c + 1);
}

static void adv_main(void)                              /* 0x0600D710 */
{
    for (;;) {
        W16(0x060AA800, 0x2404);
        fade_inout_work();
        adv_vdp2_display();
        switch (R8(0x060AA807)) {
        case 0: demo_run(); break;
        case 8: demo_fade_start(); break;
        case 10: demo_fade_wait(); break;
        case 12: title_init(); break;
        case 14: title_wait(); break;
        case 16: title_fout(); break;
        case 18: segalogo_init(); break;
        case 20: segalogo_wait(); break;
        case 22: segalogo_fout(); break;
        case 24: sportslogo_init(); break;
        case 26: sportslogo_wait(); break;
        case 28: sportslogo_fout(); break;
        case 30: start_wait(); break;
        default: menu_unported(0x0600D828); return;     /* 2 / 4 / 6: the development pause */
        }
        intm_sync(38);
        if (R8(FLAGS) & 2) break;
    }
    snd_call(0x06017080, 0, 0, 0);
    W8(FLAGS, R8(FLAGS) & 0xFD);
    W8(0x060AA80D, R8(0x060AA80D) | 1);
    W8(0x060AA807, 12);
    W8(0x060AA80D, R8(0x060AA80D) | 2);
    course_next();
}

void menu_adv_register(void)
{
    menu_reg(0x0600D204, title2_init);
    menu_reg(0x0600D214, adv_init);
    menu_reg(0x0600D710, adv_main);
}
