/* M8: the ceremony after the goal of the last arcade stage (0x0601E4E8 .. 0x0601EF34), ported from the original.  The ending that follows it is below (section 2).
 *
 * Ceremony: ceremony_init 0x0601E4E8 sets the vectors, loads CEDESERT.DAT + the models (OBJECT.MAD, ENDING.MAD) and creates three actors (the 64 byte slots at 0x060E4000: +0 function,
 * +4 next, +8 previous; the list head / tail are [0x060D0078] / [0x060D007C]): the camera on a B-spline (0x0601E7B4), the player's car (0x0601E9B6) and the animated model of the car
 * (0x0601ED1C); 70 confetti pieces (0x060E4800 + 64 * n) fall around the car.  ceremony_main 0x0601E664 is the blocking pass loop; [0x060D0020] is the state handler of the pass (0x0601E6DA
 * waits for the animation to reach frame 92, then fades out and sounds the jingle; 0x0601E708 waits for frame 103; 0x0601E726 ends the screen), [0x060D0118] the animation frame (16.16)
 * published by the third actor.  The 3D parts (matrices, polygon drawing) are the host's: the port keeps the fixed-point matrix math that decides memory (the confetti motion, the visibility
 * of the animated model, which advances the frame) and records what the original would draw (Cer3D, below).
 *
 * Matrices are 3 x 4 (rows of 4 longs, the 4th the translation), 16.16.  The original's r4 = the matrix pointer (0x060D1000, a stack of 48 byte matrices): `mstk[mtop]` here. */
#include <stdio.h>
#include "satlib.h"
#include "menu_lib.h"
#include "satmenu.h"
#include "menu_post.h"
#include "menu_ending.h"
#include "fx.h"

#define CAR 0x060B0000u
#define SND(id) snd_call(0x06016FBC, (u32)(id), 0, 0)

/* ================================ matrix math (matrix module 0x06017D18 .. 0x06018160, math module) ================================ */
typedef struct { s32 m[12]; } Mat34;
static Mat34 mstk[16];
static int mtop;

static void mat_unit(Mat34 *a)                          /* 0x06017D24 */
{
    memset(a, 0, sizeof *a);
    a->m[0] = a->m[5] = a->m[10] = 0x10000;
}

static void sincos16(u32 ang, s32 *s, s32 *c)           /* 0x06018058: the table at 0x060A15A0, entry (ang >> 4) & 0xFFF = (sin, cos) x 4 */
{
    u32 a = 0x060A15A0u + ((ang >> 2) & 0x3FFCu);
    *s = (s32)((u32)(s32)RS16(a) << 2);
    *c = (s32)((u32)(s32)RS16(a + 2) << 2);
}

static void mat_rotx(Mat34 *a, u32 ang)                 /* 0x06017F76 */
{
    s32 s, c, r;
    sincos16(ang, &s, &c);
    for (r = 0; r < 3; r++) {
        s32 *p = &a->m[4 * r], y = p[1], z = p[2];
        p[1] = X16((s64)c * y + (s64)neg(s) * z);
        p[2] = X16((s64)s * y + (s64)c * z);
    }
}

static void mat_roty(Mat34 *a, u32 ang)                 /* 0x06017FC2 */
{
    s32 s, c, r;
    sincos16(ang, &s, &c);
    for (r = 0; r < 3; r++) {
        s32 *p = &a->m[4 * r], x = p[0], z = p[2];
        p[0] = X16((s64)c * x + (s64)s * z);
        p[2] = X16((s64)neg(s) * x + (s64)c * z);
    }
}

static void mat_rotz(Mat34 *a, u32 ang)                 /* 0x06018012 */
{
    s32 s, c, r;
    sincos16(ang, &s, &c);
    for (r = 0; r < 3; r++) {
        s32 *p = &a->m[4 * r], x = p[0], y = p[1];
        p[0] = X16((s64)c * x + (s64)neg(s) * y);
        p[1] = X16((s64)s * x + (s64)c * y);
    }
}

static u32 rad_turns(s32 rad)                           /* r_rotate_x / y / z (0x06017F70 / 0x06017FBC / 0x0601800C): radians (16.16) -> turns: dmuls with 1 / (2 pi), the high word */
{
    return (u32)HM(rad, 0x28BE60DC);
}

static void mat_translate(Mat34 *a, s32 x, s32 y, s32 z)    /* 0x06017DD0 (translate2 0x06017DBC takes the three values) */
{
    int r;
    for (r = 0; r < 3; r++) {
        s32 *p = &a->m[4 * r];
        s64 acc = (s64)p[0] * x + (s64)p[1] * y + (s64)p[2] * z;
        p[3] = (s32)((u32)X16(acc) + (u32)p[3]);
    }
}

static void mat_point(const Mat34 *a, const s32 v[3], s32 out[3])      /* calc_point2 0x0601B9C4: the rows applied to the vector (no translation) */
{
    int r;
    for (r = 0; r < 3; r++) {
        const s32 *p = &a->m[4 * r];
        out[r] = X16((s64)p[0] * v[0] + (s64)p[1] * v[1] + (s64)p[2] * v[2]);
    }
}

static int mat_visible(const Mat34 *a, s32 radius)      /* FUN_0601D8D4: is a sphere of that radius around the origin of the matrix on the 352 x 224 screen (T flag) */
{
    s32 z = a->m[11], k, x, y, rr, sx, sy, sr, w = 176, h = 112, t;
    if (!(z > 0)) return 0;
    if (!(0x00FA0000 > z)) return 0;
    k = div64(((s64)224 << 32) | 1, z);                 /* DVDNTH = 224, DVDNTL = 1: the 64 bit dividend (224 << 32 | 1) */
    x = a->m[3]; y = a->m[7];
    rr = (s32)((u32)radius >> 1);
    sx = HM(k, x); sy = HM(k, y); sr = HM(k, rr);
    t = sx - sr;
    if (!(w >= t)) return 0;
    t = t + sr + sr;
    if (!(t >= -w)) return 0;
    t = sy - sr;
    if (!(h >= t)) return 0;
    t = t + sr + sr;
    return t >= -h;
}

static s32 atan16(s32 r4, s32 r5)                       /* 0x0601B7C0: the angle (65536 = one turn) of (r4, r5) from the table of 0x060A55A0 (the on-chip divider: (r5 << 16) / r4) */
{
    u32 idx = 0, r2;
    s32 t, q;
    if ((r4 | r5) == 0) return 0;
    if (r4 < 0) { r4 = neg(r4); idx = 8; }
    if (r5 < 0) { r5 = neg(r5); idx += 4; }
    if (!(r4 >= r5)) { t = r5; r5 = r4; r4 = t; idx += 2; }
    q = div64((s64)r5 << 16, r4);
    r2 = (u32)(s32)RS16(0x060A55A0u + (((u32)q >> 6) & 0xFFFFFFFEu));
    switch (idx) {
    case 0: return (s32)r2;
    case 2: return (s32)(0x4000 - r2);
    case 4: return (s32)(0 - r2);
    case 6: return (s32)(r2 - 0x4000);
    case 8: return (s32)(0x8000 - r2);
    case 10: return (s32)(0x4000 + r2);
    case 12: return (s32)(r2 - 0x8000);
    default: return (s32)(0xFFFFC000u - r2);
    }
}

static s32 sqrt16(s32 x)                                /* 0x0601B8CC: Newton iteration with the on-chip divider, a negative argument gives -1.0 */
{
    s32 g, prev, q;
    if (x < 0) return -0x10000;
    if (x == 0) return 0;
    g = x > 0x10000 ? x : 0x10000;
    do {
        prev = g;
        q = div64((s64)x << 16, g);
        g = (s32)(((u32)g + (u32)q) >> 1);
    } while (prev > g);
    return prev;
}

static s32 dist3(const s32 a[3], const s32 b[3])        /* 0x0601B8A4: the distance of two points (16.16): sqrt of the sum of squares */
{
    s64 acc;
    s32 d0 = (s32)((u32)a[0] - (u32)b[0]), d1 = (s32)((u32)a[1] - (u32)b[1]), d2 = (s32)((u32)a[2] - (u32)b[2]);
    acc = (s64)d0 * d0 + (s64)d1 * d1 + (s64)d2 * d2;
    return sqrt16(X16(acc));
}

static void lookat_angle(const s32 cam[3], const s32 tgt[3], s32 *pitch, s32 *yaw)       /* 0x0601BA6C: r0 = pitch, r1 = yaw of the line from the camera to the target */
{
    s32 a, d, c[3], t[3], p;
    a = atan16((s32)((u32)tgt[0] - (u32)cam[0]), (s32)((u32)tgt[2] - (u32)cam[2]));
    c[0] = cam[0] >> 1; c[1] = 0; c[2] = cam[2] >> 1;
    t[0] = tgt[0] >> 1; t[1] = 0; t[2] = tgt[2] >> 1;
    d = dist3(c, t);
    p = atan16(d, (s32)((u32)cam[1] - (u32)tgt[1]) >> 1);        /* the height difference halved */
    *pitch = neg(p);
    *yaw = (s32)((u32)a + 0xFFFFC000u);
}

void get_camera_angle(const s32 a[3], const s32 b[3], s32 *r0, s32 *r1)      /* 0x0601E970 (the ceremony module's; the title demo's advExec calls it): r0 = atan(dz, dy), r1 = atan(dx, dz) - 0x4000 of the line from a to b */
{
    s32 yaw = atan16((s32)((u32)b[0] - (u32)a[0]), (s32)((u32)b[2] - (u32)a[2]));
    *r0 = atan16((s32)((u32)b[2] - (u32)a[2]), (s32)((u32)b[1] - (u32)a[1]));
    *r1 = (s32)((u32)yaw + 0xFFFFC000u);
}

static void bspline(s32 t, s32 w[4])                    /* 0x0601BA04: the four weights of the uniform cubic B-spline at t (16.16) */
{
    s32 t2 = X16((s64)t * t), t3 = X16((s64)t2 * t), u = 0x10000 - t, u2 = X16((s64)u * u), u3 = X16((s64)u2 * u);
    w[0] = HM(u3, 0x2AAAAAAA);
    w[1] = (s32)((u32)(t3 >> 1) - (u32)t2 + 0xAAAA);
    w[2] = (s32)((u32)(neg(t3) >> 1) + (u32)(t2 >> 1) + (u32)(t >> 1) + 0x2AAA);
    w[3] = HM(0x2AAAAAAA, t3);
}

/* ================================ the actor list (FUN_0601BC20 / FUN_0601BBE8) ================================ */
static u32 rnd(void)                                    /* 0x0601D6CC: the random number generator (state 0x060D0050): r = r * 5 + 0x3537 */
{
    u32 r = R32(0x060D0050);
    r = r * 5 + 0x3537;
    W32(0x060D0050, r);
    return r;
}

static void act_create(u32 fn)                          /* 0x0601BC20: the first free 64 byte slot from 0x060E4000 (word +0 == 0) gets the function and goes to the end of the list */
{
    u32 s = 0x060E4000, tail;
    while (R32(s) != 0) s += 64;
    W32(s + 0, fn);
    tail = R32(0x060D007C);
    if (tail != 0) {
        W32(tail + 4, s);
        W32(0x060D007C, s);
        W32(s + 8, tail);
        W32(s + 4, 0);
    } else {
        W32(0x060D0078, s);
        W32(0x060D007C, s);
        W32(s + 8, 0);
        W32(s + 4, 0);
    }
}

static void act_run_list(void)                          /* 0x0601BBE8: every actor of the list in order, with its slot in gbr / r14 */
{
    u32 s = R32(0x060D0078);
    while (s != 0) {
        u32 fn = R32(s + 0);
        act_cur = s;
        menu_call(fn);
        s = R32(s + 4);
    }
}

/* ================================ what the ceremony draws (for the host) ================================ */
Cer3D cer3d;
unsigned cer3d_tick;

static void cer3d_begin(void)
{
    if (cer3d_tick != sat_ticks) { cer3d_tick = sat_ticks; cer3d.nconf = 0; cer3d.have = 0; cer3d.ending = 0; cer3d.anim = 0; }
}

/* ================================ section 1: the ceremony ================================ */
static void set_grpara2(void)                           /* 0x06015F6E (set_grpara 0x06015F6C loads r13 = 0x060AA9B0): the rotation parameter targets of the sky from the camera's yaw / roll words, r14 = 0x060AA9F0 */
{
    u32 r14 = 0x060AA9F0, r0, r1;
    u32 mir = R8(0x060AA82E);
    s32 roll, r5, r6;
    r0 = R16(0x060AA9C6);
    if (mir != 0) r0 = neg((s32)r0);
    r1 = 0xFFFFFFC0u & r0;
    r1 = (r1 << 8) << 2;
    r1 += R32(r14 + 24);
    r1 &= 0x03FF0000;
    roll = RS16(0x060AA9C8);
    if (mir != 0) roll = neg(roll);
    W32(r14 + 8, r1);
    if (roll == 0) {
        r6 = 0x10000;
        r0 = 0;
        r5 = 0;
    } else {
        s32 sn, cs;
        roll >>= 1;
        r5 = neg(roll);
        sincos16((u32)r5, &sn, &cs);                    /* r5 = the sine, r6 = the cosine (x 4), r0 = -sine: the registers sincos leaves */
        r5 = sn; r6 = cs; roll = neg(sn);
        r0 = (u32)roll;
    }
    W32(r14 + 12, (u32)r6);
    W32(r14 + 16, (u32)r0);
    W32(r14 + 20, (u32)r5);
}

static void init_replay_win(void)                       /* 0x06013AEC: the window words of the replay screens */
{
    u32 k;
    for (k = 0; k < 4; k++) W16(0x25F800C0 + 2 * k, R16(0x06013B6C + 2 * k));
    for (k = 0; k < 4; k++) W16(0x25F800C8 + 2 * k, R16(0x06013B74 + 2 * k));
}

void init_gscroll(void)                                 /* 0x06015B80: the scroll / window set-up of the game screens (and the rotation parameters of the sky, DMA'd to VDP2 VRAM) */
{
    u32 r1, r2, r4;
    W16(0x25F80034, 0x8203);
    W16(0x25F80038, 0xC00C);
    W16(0x25F800F0, 0x202);
    W16(0x25F800F2, 0x202);
    W16(0x25F800F4, 0x202);
    W16(0x25F800F6, 0x202);
    W16(0x25F800F8, 0x604);
    W16(0x25F800FA, 0x606);
    W16(0x25F800FC, 2);
    if (R8(0x060AA810) == 2) {
        init_replay_win();
    } else {
        r1 = 0x06015D90u + ((u32)RS16(0x060AA860) << 2);
        W16(0x25F800C0, R16(r1));
        W16(0x25F800C2, R16(r1 + 2));
        W16(0x25F800C4, R16(r1 + 4));
        W16(0x25F800C6, R16(r1 + 6));
        W16(0x25F800C8, 0x100);
        W16(0x25F800CA, 72);
        W16(0x25F800CC, 0x1B0);
        W16(0x25F800CE, 88);
    }
    if (RS16(0x060AA860) != 10) {
        W16(0x25F800D2, 136);
        rcopy(0x0029819C, 0x060B9000, 24);
        r1 = (u32)RS16(0x060AA882);
        r2 = R32(0x06015DC0 + r1);
        r4 = R32(0x06015DD0 + r1);
        W32(0x060B9044, r2);
        W32(0x060B9048, r4);
        W32(0x060AA9F8, r2);
        W32(0x060AAA08, r2);
        W32(0x060AAAE8, r2);
        W32(0x060AAAF8, r2);
        W32(0x25FE0040, 0x060B9000);
        W32(0x25FE0044, 0x25E7FF00);
        W32(0x25FE0048, 96);
        W32(0x25FE004C, 0x101);
        W32(0x25FE0054, 7);
        W32(0x25FE0050, 0x100);
        W32(0x25FE0050, 0x101);
    } else {
        menu_unported(0x06015C36);                      /* the two player screen's rotation parameters */
    }
    W16(0x25F8000E, (R16(0x25F8000E) & 0x3300) | 0xE0);
}

static void set_car_texture(void)                       /* 0x0601ABA2: the object models (OBJECT.MAD) with their textures */
{
    u32 old = R32(0x060D008C);
    W32(0x060D008C, 0x4F424A20);
    if (old == 0x4F424A20) return;
    if (R8(0x060AA803) & 0x10) sat_cd_request(0x0601ABFC);
    else menu_unported(0x0601ABB6);
    W32(0x060D0048, 0x30002100);
    setup_texture(0x0601AF8C, 0xFDEE0000u);
}

static void set_ceremony_model(void)                    /* 0x0601AE4C: the ceremony / ending models (ENDING.MAD) */
{
    u32 old = R32(0x060D0090);
    W32(0x060D0090, 0x454E4420);
    if (old == 0x454E4420) return;
    if (R8(0x060AA803) & 0x10) sat_cd_request(0x0601AF70);
    else menu_unported(0x0601AE5A);
    W32(0x060D0048, 0x76002A00);
    setup_texture(0x0601B064, 0xFD850000u);
}

/* the camera actor (slot 0x060E4000): +16 angle accumulator, +20 / +24 / +28 the position on the spline, +32 pitch, +36 yaw, +40 roll, +48 .. +60 the spline weights */
static const s32 cam_pts[4][3] = {                      /* the control points (0x0601E940 ..) */
    { 0x0004B5C2, 0x0004028F, (s32)0xFF98D47B }, { 0x0006828F, 0x00030000, (s32)0xFF995EB9 },
    { (s32)0xFFFB0B5D, 0x0001D47A, (s32)0xFF90051F }, { (s32)0xFFFA0F5D, 0x0003147A, (s32)0xFFA568F6 } };

static void act_camera(void)                            /* 0x0601E7CE */
{
    u32 r14 = act_cur;
    s32 t, w[4], tgt[3], cam[3], pitch, yaw, k, r1;
    Mat34 *v;
    r1 = (s32)((u32)RS32(0x060D0118) << 1);
    t = div64((s64)r1 << 16, 0xD2 << 16);               /* DVDNTH:DVDNTL = (frame x 2) << 16, divided by 210.0 */
    if (0x00010000 <= t) t = 0x00010000;
    bspline(t, w);
    for (k = 0; k < 4; k++) W32(r14 + 48 + 4 * k, (u32)w[k]);
    for (k = 0; k < 3; k++) {
        s64 acc = 0;
        int j;
        for (j = 0; j < 4; j++) acc += (s64)cam_pts[j][k] * w[j];
        W32(r14 + 20 + 4 * k, (u32)X16(acc));
    }
    cam[0] = RS32(r14 + 20); cam[1] = RS32(r14 + 24); cam[2] = RS32(r14 + 28);
    tgt[0] = RS32(CAR + 24); tgt[1] = (s32)((u32)RS32(CAR + 28) + 0x10000); tgt[2] = RS32(CAR + 32);
    lookat_angle(cam, tgt, &pitch, &yaw);
    W32(r14 + 32, (u32)pitch);
    W32(r14 + 36, (u32)yaw);
    W16(0x060AA9C4, (u32)neg(pitch));
    W16(0x060AA9C6, (u32)neg(yaw));
    W16(0x060AA9C8, 0);
    mtop = 0;                                           /* initial_matrix + (the mirror course) + rotate_z / x / y of the camera + translate2 of its position -> the view matrix 0x060D1000 */
    v = &mstk[0];
    mat_unit(v);
    if (R8(0x060AA82E) != 0) { v->m[0] = neg(v->m[0]); v->m[4] = neg(v->m[4]); v->m[8] = neg(v->m[8]); }     /* reverse_x */
    mat_rotz(v, (u32)neg(RS32(r14 + 40)));
    mat_rotx(v, (u32)neg(RS32(r14 + 32)));
    mat_roty(v, (u32)neg(RS32(r14 + 36)));
    mat_translate(v, neg(RS32(r14 + 20)), neg(RS32(r14 + 24)), neg(RS32(r14 + 28)));
    W32(r14 + 16, (R32(r14 + 16) + 128) & 0xFFFF);
    cer3d_begin();
    cer3d.have = 1;
    cer3d.cam_pos[0] = RS32(r14 + 20); cer3d.cam_pos[1] = RS32(r14 + 24); cer3d.cam_pos[2] = RS32(r14 + 28);
    cer3d.cam_ang[0] = RS32(r14 + 32); cer3d.cam_ang[1] = RS32(r14 + 36); cer3d.cam_ang[2] = RS32(r14 + 40);
    cer3d.mirror = R8(0x060AA82E);
}

static void act_camera_init(void)                       /* 0x0601E7B4 */
{
    u32 r14 = act_cur;
    W32(r14 + 20, 0);
    W32(r14 + 24, 0x0004028F);
    W32(r14 + 28, 0xFF900000);
    W32(r14 + 32, 0);
    W32(r14 + 36, 0);
    W32(r14 + 40, 0);
    W32(r14 + 16, 0);
    W32(r14 + 0, 0x0601E7CE);
    act_camera();
}

/* the car actor (slot 0x060E4040): dispCar (0x060101E8, r5 = 0) for the player's car block 0x060B0000, then the object of the car (0x0601EA18 + 16 * id: a vector and a model) */
static void disp_car(u32 r5)                            /* 0x060101E8: only the choice of the draw routine (the pipeline is the host's) is memory: [car + 900] = r5, [car + 896] = the polygon routine of the entry r5 */
{
    static const u32 fn[4] = { 0x060186FC, 0x06018908, 0x060186FC, 0x06010654 };
    W32(CAR + 900, r5);
    W32(CAR + 896, fn[r5 & 3]);
    if (R32(0x060D0148) != 0 || R32(0x060D0168) != 0) menu_unported(0x0601D0E6);      /* the tyre marks (dispcar_init(1) of the race), not part of the console memory the host leaves */
}

static void act_car(void)                               /* 0x0601E9D2 */
{
    disp_car(0);
    cer3d_begin();
    cer3d.car_id = R8(CAR + 92);
    memcpy(cer3d.car, sat_ptr(CAR), 0x100);
}

static void act_car_init(void)                          /* 0x0601E9B6 */
{
    u32 r14 = act_cur, r0;
    W32(r14 + 0, 0x0601E9D2);
    r0 = R8(0x060AA80A);
    if (r0 == 2) r0 = 10;
    W8(CAR + 92, r0);
    act_car();
}

/* the animated model of the car (slot 0x060E4080): the frame counter [slot + 12] (16.16) runs on only while the model is on screen (FUN_0601CF44 adds 0x8000 and wraps at the number of
 * frames of the animation table); the visible frame is published at [0x060D0118] */
int menu_anim_log;
#define ANIM_LOG 0x06070000u

/* FUN_0601CF44 (r4 = the matrix, r5 = the animation table, gbr = the actor's slot): for every part the key of this frame and of the next one are mixed by the fraction of the frame counter
 * (positions: (1 - t) * a + t * b of the 16.16 longs; angles: a + t * (b - a) of the 16 bit turns, the difference taken as a 16 bit number), the matrix is the actor's with translate and
 * rotate_z / y / x, and draw_polygon2 draws the part's model with it.  The pipeline is the host's: the matrices are kept for it (and, for the tests, in the memory). */
static void anim_parts(u32 tbl, u32 frame, const Mat34 *base)
{
    u32 np = R16(tbl), parts = R16(tbl + 2), keys = R32(tbl + 4), ka, kb, k;
    s32 f = (s32)(s16)(frame >> 16), t = (s32)(frame & 0xFFFF), u = 0x10000 - t;
    (void)np;
    ka = keys + (u32)(f * (s32)parts * 20);
    kb = keys + (u32)((f + 1) * (s32)parts * 20);
    cer3d.anim_np = 0;
    for (k = 0; k < parts && k < 48; k++, ka += 20, kb += 20) {
        Mat34 m = *base;
        s32 p[3], a[3], j;
        for (j = 0; j < 3; j++) p[j] = X16((s64)u * RS32(ka + 4 * (u32)j) + (s64)t * RS32(kb + 4 * (u32)j));
        for (j = 0; j < 3; j++) {                       /* the words at +12 (z), +14 (y), +16 (x) */
            s32 wa = RS16(ka + 12 + 2 * (u32)j), wb = RS16(kb + 12 + 2 * (u32)j);
            a[j] = (s32)((u32)wa + (u32)HM((s32)((u32)(wb - wa) << 16), t));
        }
        mat_translate(&m, p[0], p[1], p[2]);
        mat_rotz(&m, (u32)a[0]);
        mat_roty(&m, (u32)a[1]);
        mat_rotx(&m, (u32)a[2]);
        memcpy(cer3d.anim_m[k], m.m, sizeof m.m);
        cer3d.anim_model[k] = R32(tbl + 8 + 4 * k);
        if (menu_anim_log) {
            for (j = 0; j < 12; j++) W32(ANIM_LOG + 52 * k + 4 * (u32)j, (u32)m.m[j]);
            W32(ANIM_LOG + 52 * k + 48, cer3d.anim_model[k]);
        }
        cer3d.anim_np = (int)k + 1;
    }
}

static void act_anim(void)                              /* 0x0601ED24 */
{
    u32 r14 = act_cur, tbl = 0;
    s32 id = RS8(CAR + 92), dy, dz;
    Mat34 *m = &mstk[mtop + 1];
    int vis;
    *m = mstk[mtop];
    mtop++;
    dy = (s32)R32(id == 10 ? 0x0601EDD0 : 0x0601EDC8);      /* the offsets of the model against the car (0x0601EDC8 / 0x0601EDD0: y and z) */
    dz = (s32)R32(id == 10 ? 0x0601EDD4 : 0x0601EDCC);
    mat_translate(m, RS32(CAR + 24), (s32)((u32)RS32(CAR + 28) + (u32)dy), (s32)((u32)RS32(CAR + 32) + (u32)dz));
    vis = mat_visible(m, 0x10000);
    if (vis) {
        u32 n;
        s32 cnt;
        tbl = R32(0x0601EDD8 + 4 * (u32)(id == 10 ? 2 : id));      /* FUN_0601CF44's table (index = the car, 10 -> 2: the Celica's, then the one of the Delta and the Stratos): u16 frames, u16 parts, u32 keys, then the model of every part */
        n = R16(tbl);
        mat_rotx(m, rad_turns(RS32(CAR + 12)));                 /* the car's angles (radians): r_rotate_x, r_rotate_z, r_rotate_y of the actor */
        mat_rotz(m, rad_turns(RS32(CAR + 20)));
        mat_roty(m, rad_turns(RS32(CAR + 16)));
        anim_parts(tbl, R32(r14 + 12), m);                      /* FUN_0601CF44 draws with the frame as it was, then advances it */
        cnt = (s32)((u32)RS32(r14 + 12) + 0x8000);
        W32(r14 + 12, (u32)cnt);
        if (((u32)cnt >> 16) >= n) W32(r14 + 12, 0);
    }
    mtop--;
    W32(0x060D0118, R32(r14 + 12));
    cer3d_begin();
    cer3d.anim = vis;
    cer3d.anim_tbl = tbl;
    cer3d.anim_frame = RS32(r14 + 12);
}

static void act_anim_init(void)                         /* 0x0601ED1C */
{
    u32 r14 = act_cur;
    W32(r14 + 12, 0);
    W32(r14 + 0, 0x0601ED24);
    act_anim();
}

/* the confetti (70 pieces of 64 bytes at 0x060E4800): +0 / +4 / +8 position (x, y, z), +12 / +16 / +20 angles, +24 / +28 / +32 their speed, +36 piece, +40 its model (table 0x0601EB74) */
static void conf_spawn(u32 r14)                         /* 0x0601EADE */
{
    u32 r0;
    r0 = (rnd() & 0x000FFFFF) - 0x00080000;
    W32(r14 + 0, r0 + R32(CAR + 24));
    r0 = (rnd() & 0x000FFFFF) - 0x00080000;
    W32(r14 + 8, r0 + R32(CAR + 32));
    W32(r14 + 24, (u32)HM((s32)(rnd() & 0x7FFFFFFF), 0x000192A6));
    W32(r14 + 28, (u32)HM((s32)(rnd() & 0x7FFFFFFF), 0x000192A6));
    W32(r14 + 32, (u32)HM((s32)(rnd() & 0x7FFFFFFF), 0x000192A6));
    W32(r14 + 12, (u32)HM((s32)rnd(), 0x00068DB8));
    W32(r14 + 16, (u32)HM((s32)rnd(), 0x00068DB8));
    W32(r14 + 20, (u32)HM((s32)rnd(), 0x00068DB8));
    r0 = rnd() & 7;
    W32(r14 + 36, r0);
    W32(r14 + 40, R32(0x0601EB74 + (r0 << 2)));
}

static void conf_init(void)                             /* 0x0601EAB2 */
{
    u32 r14 = 0x060E4800;
    int k;
    for (k = 0; k < 70; k++, r14 += 64) {
        conf_spawn(r14);
        W32(r14 + 4, (u32)HM((s32)(rnd() << 16), 0x00068DB8) + 0x10000);
    }
}

static void car_init_fields(void)                       /* 0x0601EA76: the ceremony's own placing of the car block */
{
    W32(CAR + 24, 0x6B85);
    W32(CAR + 28, 0x00021EB8);
    W32(CAR + 32, 0xFF9B4F5D);
    W32(CAR + 16, 0xFFFCDBC1);
    W32(CAR + 12, 0);
    W32(CAR + 20, 0);
    W32(CAR + 36, 0);
    W32(CAR + 40, 0);
    W32(CAR + 44, 0);
    W32(CAR + 48, 0);
    W32(CAR + 52, 0);
    W32(CAR + 56, 0);
    W8(CAR + 92, 0);
    W16(CAR + 180, 0x1111);
    W32(CAR + 60, 0x00023D70);
    W32(CAR + 64, 0x00023D70);
    W32(CAR + 68, 0x00023D70);
    W32(CAR + 64, 0x00023D70);
}

static void conf_update(u32 r14)                        /* 0x0601EBB0 (r4 = the view matrix stack) */
{
    s32 r0 = RS32(r14 + 4), a[3], v[3];
    Mat34 *m;
    int vis;
    if (!(r0 > 0)) {                                    /* 0x0601EC58: the piece has fallen: it comes back above the car */
        W32(r14 + 4, (u32)r0 + 0x000A0000);
        conf_spawn(r14);
        return;
    }
    m = &mstk[mtop + 1];
    *m = mstk[mtop];
    mtop++;
    {
        s32 rad = m->m[11];                             /* translate leaves the matrix's old z translation in r5, which FUN_0601D8D4 takes as the radius */
        mat_translate(m, RS32(r14 + 0), RS32(r14 + 4), RS32(r14 + 8));
        vis = mat_visible(m, rad);
    }
    if (vis && cer3d.nconf < CER3D_CONF) {
        CerConf *c = &cer3d.conf[cer3d.nconf++];
        c->pos[0] = RS32(r14 + 0); c->pos[1] = RS32(r14 + 4); c->pos[2] = RS32(r14 + 8);
        c->ang[0] = RS32(r14 + 12); c->ang[1] = RS32(r14 + 16); c->ang[2] = RS32(r14 + 20);
        c->model = R32(r14 + 40);
    }
    mat_unit(m);
    mat_rotz(m, rad_turns(RS32(r14 + 20)));
    mat_rotx(m, rad_turns(RS32(r14 + 12)));
    mat_roty(m, rad_turns(RS32(r14 + 16)));
    W32(r14 + 12, R32(r14 + 12) + R32(r14 + 24));
    W32(r14 + 16, R32(r14 + 16) + R32(r14 + 28));
    W32(r14 + 20, R32(r14 + 20) + R32(r14 + 32));
    v[0] = RS32(0x0601EC4C); v[1] = RS32(0x0601EC4C + 4); v[2] = RS32(0x0601EC4C + 8);
    mat_point(m, v, a);
    if (!(a[1] > 0)) a[1] = neg(a[1]);
    a[1] = (s32)((u32)a[1] + 0xF5C);
    W32(r14 + 4, R32(r14 + 4) - (u32)a[1]);
    W32(r14 + 0, R32(r14 + 0) + (u32)a[0]);
    W32(r14 + 8, R32(r14 + 8) + (u32)a[2]);
    mtop--;
}

static void conf_run(void)                              /* 0x0601EB94 */
{
    u32 r14 = 0x060E4800;
    int k;
    for (k = 0; k < 70; k++, r14 += 64) conf_update(r14);
}

static void state_cer_a(void)                           /* 0x0601E6DA */
{
    if (92 > RS16(0x060D0118)) return;
    W32(0x060D0020, 0x0601E708);
    pre_fade_inout(0, 255, 12);
    snd_call(0x06016F3C, 0, 0, 48);
}

static void state_cer_b(void)                           /* 0x0601E708 */
{
    if (103 > RS16(0x060D0118)) return;
    W32(0x060D009C, 0);
    W32(0x060D0020, 0x0601E726);
}

static void state_cer_c(void)                           /* 0x0601E726 */
{
    u32 r0 = R32(0x060D009C) - 1;
    W32(0x060D009C, r0);
    if (0x708 > r0) return;
    W8(0x060AA803, R8(0x060AA803) | 2);
}

void ceremony_init(void)                                /* 0x0601E4E8 */
{
    tv_off();
    snd_call(0x060173AC, 5, 82, 0);
    W8(0x060AA804, R8(0x060AA804) | 3);
    W16(0x060AA860, 0);
    W32(0x060A001C, 0x0601E664);
    W32(0x060D0020, 0x0601E6DA);
    init_gspr();
    init_gscroll();
    W16(0x060AA87E, 0);
    W16(0x060AA880, 0);
    W16(0x060AA882, 0);
    W16(0x060AA884, 0);
    sat_load_name_at(R32(0x06004824 + (u32)RS16(0x060AA882)));      /* crs_cer_dataset */
    mkpoly_init();
    set_car_texture();
    set_ceremony_model();
    sat_memset(0x060E4000, 0, 512);                     /* FUN_0601D6BC: 128 longs */
    W32(0x060D0118, 0);
    W32(0x060D0078, 0);
    W32(0x060D007C, 0);
    act_create(0x0601E7B4);
    act_create(0x0601E9B6);
    act_create(0x0601ED1C);
    car_init_fields();
    conf_init();
    pre_fade_inout(255, 0, -12);
    W16(0x25F80020, 16);
    W16(0x060AA8E6, 16);
    tv_on();
}

void ceremony_main(void)                                /* 0x0601E664 */
{
    for (;;) {
        W16(0x060AA800, 0x2404);
        snd_call(0x06017580, 0, 0, 0);
        fade_inout_work();
        s_mkpoly_frame_init();
        m_mkpoly_frame_init();
        set_grpara2();
        act_run_list();
        conf_run();
        if (R32(0x060D0020) != 0) menu_call(R32(0x060D0020));
        W16(0x060AA994, 0);
        W16(0x060AA99A, 0);
        intm_sync(38);
        if (R8(0x060AA803) & 2) break;
    }
    W8(0x060AA803, R8(0x060AA803) & 0xFD);
    W32(0x060A001C, 0x0601EF34);
}


/* ================================ section 2: the ending (0x0601EF34 .. 0x0601F6E0) ================================
 *
 * The ending replays the player's last stage (the lake) with the camera of CAMERA.DAT cutting between fixed cameras while the staff text rolls over it.  Its own code is ported here
 * exactly: ending_init / ending_main, the staff roll (set_staff_cg, staff_act*, the glyph writer), the camera (ending_camera_act: the record of CAMERA.DAT for the player's block, the
 * eye position and the angles towards the car, the perspective keys, calc_camera_pmap), replay_cactl (the pad toggles the cameras), the state handlers of [0x060D0020], the game screen's
 * VDP2 set-up (init_game_vram) and the data loads.
 *
 * What it does not contain is the race engine: the player's car with its physics (the 0x600 byte block at 0x060B0000, driven by the recorded inputs at [0x060B0574]), the rival cars
 * (0x060B0600 + 0x400 * n), the scenery objects (EVENTTBL.MAD, the slots at 0x060E4000), the tyre marks and the HUD.  src/drive.c / opp.c / event.c / camera.c are those on the host's own data;
 * the ending calls them through `engine()` at the points where the original calls the routines: the address is the original routine.  The host binds the hook (menu_engine_hook); the
 * oracle scenarios stub the same routines (tools/scn_ending.py ENGINE), so the port is checked against the original with the engine frozen: its slot memory stays as the scenario
 * patched it. */
void (*menu_engine_hook)(u32 addr);

static void engine(u32 addr)
{
    if (menu_engine_hook) menu_engine_hook(addr);
}

#define CAMW 0x060AA9B0u                /* the camera control work: +0 function, +8 / +12 / +16 eye, +20 / +22 / +24 angle words */
static u32 blk_base = 0x060B2600u;       /* the car block the road search works on: the camera's dummy car 0x060B2600 (the host's ghost car sets its own for a moment) */
#define CARB blk_base
static u32 ghost_pass;                  /* the PSP's ghost car (below): passes since the ending began */
#define FLAGS 0x060AA803u

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

/* bpeset (0x06015978): r4 = four table indices (one per byte, 0 = none); each is a 12-byte entry (source, destination, add) of the table at 0x00299784 */
static void bpeset(u32 r4)
{
    int k;
    for (k = 0; k < 4; k++, r4 >>= 8) {
        u32 i = r4 & 0xFF, e;
        if (i == 0) continue;
        e = 0x00299784u + i * 12;
        bpe_expand(R32(e), R32(e + 4), R32(e + 8));
    }
}


/* ---- the staff roll (module ending: set_staff_cg 0x0601F32A, staff_act_init 0x0601F3F0, staff_act 0x0601F404, 0x0601F42A / F460 / F484) ----
 * STAFF.MAP -> 0x002BE000 (a table of page pointers, the glyph cell lists at 0x002BE450), STAFF.CG -> VDP2 VRAM; the palette words go to the colour RAM shadow.  The page counter lives in
 * 0x060D01C0 (+0 page, +4 frames left, +8 frames per page); the text is drawn into the 16-bit cell plane 0x25E3C000 (64 cells per row, 14 rows). */
#define STAFF 0x060D01C0u

static void set_staff_cg(void)                          /* 0x0601F32A */
{
    if (R8(FLAGS) & 0x10) sat_cd_request(0x0601F378);  /* STAFF.MAP, STAFF.CG */
    else {
        menu_unported(0x0601F334);                      /* the cartridge image path (not on the disc) */
        return;
    }
    rcopy(0x002BE864, 0x060B8200, 128);                 /* 0x0601D694: long copies */
    rcopy(0x002BEA64, 0x060B8400, 32);
}

static u32 staff_glyph(u32 r4, u32 r5, u32 r6)          /* 0x0601F484: one glyph (the cell list r4 of the table at 0x002BE450) to the plane at r5, cell code offset r6; returns twice its width */
{
    s32 w, h, c;
    u32 p, q;
    w = R16(r4); h = R16(r4 + 2);
    r4 += 4;
    (void)p;
    for (; h > 0; h--) {
        q = r5;
        for (c = w; c > 0; c--) { W16(q, R16(r4) + r6); r4 += 2; q += 2; }
        r5 += 0x80;
    }
    return (u32)w << 1;
}

static u32 staff_line(u32 r4, u32 r5)                   /* 0x0601F460: a line of text: u16 offset, u16 cell code offset, then the character bytes (ended by 0); returns the address behind it */
{
    u32 r8, r9, r6;
    r5 += R16(r4); r4 += 2;
    r6 = R16(r4); r4 += 2;
    r8 = r4;
    r9 = r5;
    for (;;) {
        u32 ch = R8(r8);
        r8++;
        if (ch == 0) break;
        ch = (ch - 32);
        r9 += staff_glyph(R32(0x002BE450 + (ch << 2)), r9, r6);
    }
    return r8;
}

static void staff_page(void)                            /* 0x0601F42A: clear the plane (0x380 longs below 0x25E3CE00) and draw the lines of the page [STAFF] */
{
    u32 r1 = 0x25E3CE00, r4;
    int n;
    for (n = 0x380; n > 0; n--) { r1 -= 4; W32(r1, 0); }
    r4 = R32(0x002BE000 + (R32(STAFF) << 2));
    while (RS16(r4) != -1) {
        r4 = staff_line(r4, 0x25E3C000);
        if (r4 & 1) r4++;
    }
}

static void staff_act_init(u32 frames)                  /* 0x0601F3F0: 11 pages over `frames` passes */
{
    s32 per = (s32)(((s64)(s32)0x1745D174 * (s64)(s32)frames) >> 32);
    W32(STAFF + 8, (u32)per);
    W32(STAFF + 4, (u32)per);
    W32(STAFF, 0);
    staff_page();
}

static void staff_act(void)                             /* 0x0601F404 */
{
    s32 left = RS32(STAFF + 4) - 1;
    W32(STAFF + 4, (u32)left);
    if (!(left > 0)) {
        W32(STAFF + 4, R32(STAFF + 8));
        W32(STAFF, R32(STAFF) + 1);
        staff_page();
    }
}


static void set_course_model(void)                      /* 0x0601A8F8 */
{
    u32 r2 = R32(0x0601A970 + (u32)RS16(0x060AA882));
    if (r2 == R32(0x060D0090)) return;
    W32(0x060D0090, r2);
    if (!(R8(FLAGS) & 0x10)) { menu_unported(0x0601A912); return; }
    sat_cd_request(R32(0x0601A998 + (u32)RS16(0x060AA882)));
}

static void load_animal2(void)                          /* 0x0601AD14 (the desert only) */
{
    if (RS16(0x060AA87E) != 0) return;
    if (!(R8(FLAGS) & 0x10)) { menu_unported(0x0601AD24); return; }
    sat_cd_request(0x0601AD68);                         /* ANIMAL2.MAD */
    W32(0x060D0048, 0xF8002EC0);
    setup_texture(0x0601AFFC, 0xFDE68000);
}

static void set_game_texture(void)                      /* 0x0601AB6C */
{
    if (RS16(0x060AA860) == 10) { menu_unported(0x0601AB8E); return; }      /* the 2P course models */
    set_car_texture();
    set_course_model();
    load_animal2();
}


/* ---- the road block search of the camera position (gctrl / car module: get_block 0x06007EAC with 0x06007F3C, calc_dispang2 0x06007D68, atan 0x0601B7C0, get_poly_map 0x060081B8) ---- */
static u32 atan_ang(s32 r4, s32 r5)                     /* 0x0601B7C0: the angle (65536 = one turn) of the vector (r4, r5) from the table of 0x060A55A0 */
{
    u32 idx = 0, r2, quo;
    s32 t;
    if ((r4 | r5) == 0) return 0;
    if (r4 < 0) { r4 = -r4; idx = 8; }
    if (r5 < 0) { r5 = -r5; idx += 4; }
    if (!(r4 >= r5)) { t = r5; r5 = r4; r4 = t; idx += 2; }
    quo = (u32)(((s64)r5 << 16) / (s64)r4);             /* the on-chip divider: (r5 << 16) / r4 */
    r2 = (u32)(s32)RS16(0x060A55A0 + ((quo >> 6) & 0xFFFFFFFEu));
    switch (idx) {
    case 0: return r2;
    case 2: return 0x4000 - r2;
    case 4: return -r2;
    case 6: return r2 - 0x4000;
    case 8: return 0x8000 - r2;
    case 10: return 0x4000 + r2;
    case 12: return r2 - 0x8000;
    default: return 0xFFFFC000u - r2;
    }
}

static u32 find_record(u32 p, s32 x, s32 z, u32 base, u32 size)        /* 0x06007F3C: the list at *p (count, indices): the first record whose four corner edges all have the point on the same side */
{
    u32 list = R32(p);
    s32 cnt = RS8(list);
    u32 r0 = (u32)RS8(list + 1) & 0xFF;
    list += 2;
    for (;;) {
        u32 rec = base + (r0 & 0xFF) * size;
        s32 px[4], pz[4], c[4];
        int k, neg = 0, allneg = 1;
        for (k = 0; k < 4; k++) { px[k] = RS32(rec + 4 + 8 * k) - x; pz[k] = RS32(rec + 8 + 8 * k) - z; }
        for (k = 0; k < 4; k++) {
            int n = (k + 1) & 3;
            s64 sum = (s64)px[k] * (s64)pz[n] + (s64)(-px[n]) * (s64)pz[k];
            c[k] = (s32)(sum >> 32);
            if (c[k] < 0) neg = 1; else allneg = 0;
        }
        if (!neg) return rec;
        if (allneg) menu_unported(0x06007FD8);          /* trapa #9: a point outside the polygon with the wrong winding */
        cnt--;
        r0 = (u32)RS8(list) & 0xFF;
        list++;
        if (cnt == 0) { menu_unported(0x06007FF4); return 0; }          /* trapa #9: the point is in none of the listed polygons */
    }
}

static void get_block(void)                             /* 0x06007EAC (gbr = CARB) */
{
    s32 x = RS32(CARB + 24), z = RS32(CARB + 32);
    u32 r2 = ((u32)x + 0x04000000u) >> 16, r1, r6, rec, id, r0;
    r2 >>= 4;
    r2 >>= 1;
    r1 = (0x04000000u - (u32)z) >> 16;
    r1 &= 0xFFFFFFE0u;
    r1 <<= 1;
    r1 += r2;
    r1 <<= 2;
    r6 = 0x00244000u + r1;
    rec = find_record(r6, x, z, 0x00290000u, 36);
    r0 = (u32)RS8(rec + 0);
    if (RS16(0x060AA860) == 10) r0 = (u32)RS8(rec + 2);
    r0 |= (u32)RS8(rec + 1) << 4;
    W8(CARB + 241, r0);
    W8(CARB + 183, R8(CARB + 240));
    id = (u32)RS8(rec + 3);
    W8(CARB + 240, id);
    rec = find_record(0x00296000u + (id << 2), x, z, 0x00293000u, 44);
    W16(CARB + 98, R16(CARB + 96));
    W32(CARB + 184, R32(rec + 36));
    W32(CARB + 188, R32(rec + 40));
    W8(CARB + 196, R8(rec + 0));
    W8(CARB + 247, R8(rec + 1));
    W16(CARB + 96, (u32)RS8(rec + 3) & 0xFF);
}

static void calc_dispang2(void)                         /* 0x06007D68 */
{
    s32 r0 = (s32)atan_ang(RS32(CARB + 184), RS32(CARB + 188));
    r0 = (s16)(r0 - 0x4000);
    r0 += RS16(0x060AA9C6);
    r0 += RS16(0x060AA9CC);
    r0 = -(s32)(s16)r0;
    r0 <<= 4;
    r0 = (s32)(((u32)r0 << 16) | ((u32)r0 >> 16));
    W8(CARB + 199, (u32)r0 & 15);
}

static void get_poly_map(void)                          /* 0x060081B8: the blocks around the camera's block: polygon list pointers [0x060AA104 ..], block numbers [0x060AA140 ..], count [0x060AA000] / [0x060AA002] */
{
    u32 r1 = (u32)((s32)RS8(CARB + 240) << 2);
    u32 r9 = 0x060AA104, r13 = 0x060AA140, r5, r0, r7, r10;
    s32 r6 = 0;
    r5 = R32(0x0600821C + ((u32)((s32)RS8(CARB + 199)) << 2));
    r0 = (u32)RS8(r5); r5++;
    {
        u32 t = (u32)RS8(CARB + 241);
        if (r0 != 0) t = (t >> 2) >> 2;
        r7 = t & 15;
    }
    r0 = (u32)RS8(r5); r5++;
    r10 = 0;
    for (;;) {
        u32 r3;
        r0 += r1;
        r0 &= 0x1FC;
        r3 = R32(0x06048000u + r0);
        r0 >>= 2;
        r10 += R32(r3) & 0xFFFF;
        if ((s32)r10 > 0x5DC) break;
        W32(r9, r3);
        W8(r13, r0);
        r0 = (u32)RS8(r5); r5++;
        r6++;
        if ((s32)r0 == -1) break;
        r13++;
        r7--;
        r9 += 4;
        if (r7 == 0) break;
    }
    W16(0x060AA002, (u32)r6);
    W16(0x060AA000, (u32)r6);
}


/* ---- fade_color_init (0x0600585C): the fade presets, 10 words each at 0x0600588C + 20 * n ---- */
static void fade_color_init(u32 n)
{
    u32 t = 0x0600588C + n * 20;
    fade_inout_init(RS16(t), RS16(t + 2), RS16(t + 4), RS16(t + 6), RS16(t + 8), RS16(t + 10), RS16(t + 12), RS16(t + 14), RS16(t + 16), RS16(t + 18));
}


/* ---- the scroll control [0x060AA9F0] = normal_endmove 0x06006BD4 -> 0x06015FD4 / set_grpara 0x06015F6C ---- */
static void normal_endmove(void)                        /* 0x06006BD4 */
{
    u32 r14 = 0x060AA9F0, k;
    for (k = 0; k < 7; k++) W32(r14 + 4 + 4 * k, R32(0x060AAAE4 + 4 * k));      /* rcopy 0x06005674 */
    W32(0x060B9044, R32(0x060AA9F8));
    r14 = 0x060AAAE0;
    {                                                   /* set_grpara 0x06015F6C: the road-follow scroll values from the camera yaw */
        s32 mir = RS8(0x060AA82E), r0, r1, r5, r6;
        u32 yaw = (u32)RS16(CAMW + 22) & 0xFFFF;
        if (mir != 0) yaw = (u32)(-(s32)yaw);
        r1 = (s32)(0xFFFFFFC0u & yaw);
        r1 = (s32)((u32)r1 << 10);
        r1 += RS32(r14 + 24);
        r1 &= 0x03FF0000;
        r0 = RS16(CAMW + 24);
        if (mir != 0) r0 = -r0;
        W32(r14 + 8, (u32)r1);
        if (r0 == 0) { r6 = 0x10000; r5 = 0; }
        else {
            u32 idx;
            r0 >>= 1;
            r5 = -r0;
            idx = ((u32)r5 >> 2) & 0x3FFC;
            r5 = (s32)((u32)(s32)RS16(0x060A15A0 + idx) << 2);
            r6 = (s32)((u32)(s32)RS16(0x060A15A0 + idx + 2) << 2);
            r0 = -r5;
        }
        W32(r14 + 12, (u32)r6);
        W32(r14 + 16, (u32)r0);
        W32(r14 + 20, (u32)r5);
    }
}

static u32 bcd2bin(u32 r4)                              /* 0x0601D740 */
{
    return ((r4 & 0xF0) >> 4) * 10 + (r4 & 15);
}

static void cam_set_game_pers(void)                     /* 0x0601F6D0: perspective(224): the pipeline's */
{
}


static void dispcar_init(void)                          /* 0x060101B8: the distance steps of the car display (0x060D01E0 ..) */
{
    int k;
    for (k = 0; k < 3; k++) W32(0x060D01E0 + 4 * (u32)k, R32(0x060101DC + 4 * (u32)k));
}

static void smoke_init(u32 r4)                          /* 0x0601D0DC: the tyre marks on / off (the flags of the two mark pools) */
{
    W32(0x060D0148, r4);
    W32(0x060D0168, r4);
}

/* ---- the game screen's set-up (init_game_vram 0x060043C0 with set_tasc_cg / set_tasc_cl / init_navi, the course loads 0x06004770 / 0x0600445C) ---- */
static void init_navi(void)                             /* 0x060130E4: boxclr_word (0x06005840): 4 rows of 4 words at 0x25E3D2A8 */
{
    u32 r5 = 0x25E3D2A8, r2, r7, r1;
    for (r7 = 4; r7 > 0; r7--) {
        r2 = r5;
        for (r1 = 4; r1 > 0; r1--) { W16(r2, 0); r2 += 2; }
        r5 += 0x80;
    }
}

static void crs_dataset(void)                           /* 0x06004770: BG<course>.DAT (the course's collision grid, block lists, sky) */
{
    sat_load_name_at(R32(0x06004780 + (u32)RS16(0x060AA882)));
}

static void crs_game_dataset(void)                      /* 0x0600445C: the course's second file (CR<course>.DAT) by the game type (type 0: the arcade) */
{
    if (RS16(0x060AA860) != 0) { menu_unported(0x0600445C); return; }
    sat_load_name_at(R32(0x06004480 + (u32)RS16(0x060AA882)));
}

static void init_game_vram(void)                        /* 0x060043C0 */
{
    vram_scrmap_a_clr();
    rfillff(0x25E3A000, 0, 0x800);
    rfillff(0x25E3C000, 0, 0x800);
    rfillff(0x25E3E000, 0, 0x800);
    init_navi();
    bpeset(22);                                         /* set_tasc_cg 0x06004B74 */
    rfillff(0x25E00040, 0, 16);
    rfillff(0x25E18040, 0, 16);
    rfillff(0x25E20040, 0, 16);
    W16(0x060B8002, 0);                                 /* set_tasc_cl 0x06004BC0 */
    W16(0x060B800A, 0x7FFF);
    init_nscr_vol();
    init_gscroll();
    if (!(R8(0x060AA825) & 1)) crs_game_dataset();
    W32(0x25FE0020, 0x060B8000);                        /* the colour RAM shadow */
    W32(0x25FE0024, 0x25F00000);
    W32(0x25FE0028, 0);
    W32(0x25FE002C, 0x101);
    W32(0x25FE0034, 7);
    W32(0x25FE0030, 0x100);
    W32(0x25FE0030, 0x101);
    W16(0x060C0000, 0x8000);
    W16(0x25C00000, 0x8000);
    init_vdp1sys();
}

/* ---- the control work (init_game_work 0x06004844 and the small routines it calls) ---- */
static void allclr_car_ctrl_work(void)                  /* 0x06005EC4: the car slots, the other cars and the camera's dummy car */
{
    rfillff(0x060B0500, 0, 0x140);
    rfillff(0x060B2600, 0, 0x800);
    rfillff(0x060B2B00, 0, 0x140);
}

static void normal_ctrl_plset(void)                     /* 0x06005FF4: the player's slot gets the init state (and the presentation of the replay: 0x06006710) */
{
    W32(CAR + 0, 0x06006030);
    W32(CAR + 4, R8(0x060AA810) == 2 ? 0x06006710 : 0x06006638);
}

static void clr_ctrl_work(u32 r14, u32 fn, u32 nlongs)  /* clr_camera_ctrl_work 0x06005E44 (15 longs) / clr_rscroll_ctrl_work 0x06005E74 (7 longs): the function, the rest cleared */
{
    W32(r14, fn);
    rfillff(r14 + 4 * (nlongs + 1), 0, nlongs);
}

static void init_game_work(void)                        /* 0x06004844 (the one player game types) */
{
    s32 type = RS16(0x060AA860);
    u32 r0 = 0, r3 = 4;
    W32(0x060AA168, 0);
    W16(0x060AA004, 0);
    W32(0x060AA178, 0);
    W32(0x060AA190, 0);
    W32(0x060AA180, 0);
    W8(0x060AA301, 0);
    W8(0x060AA302, 0);
    W8(0x060AA304, 0);
    W8(0x060AA305, 0);
    W16(0x060AA024, 0);
    W8(0x060AA821, 0);
    W32(0x060AFE00, 0);
    W32(0x060AFE7C, 0);
    if (type == 0 && RS16(0x060AA87E) != 0) { r3 = 0; r0 = R8(0x060AA01B); }
    W16(0x060AA01A, (r3 << 8) + r0);
    if (type == 10) { menu_unported(0x060048A6); return; }                  /* the two player game */
    allclr_car_ctrl_work();
    normal_ctrl_plset();
    clr_ctrl_work(CAMW, 0x0600FA5C, 15);                                    /* camera: normal_cainit */
    clr_ctrl_work(0x060AA9F0, R8(0x060AA810) == 2 ? 0x06006BD4 : 0x06006BBC, 7);        /* scroll: normal_endmove / normal_rmove */
    W16(CAR + 8, R8(0x060AA810));
    W8(0x060B0500, 0);
    W8(CAR + 244, R8(0x060AA809));
    W8(CAR + 245, 0);
    engine(0x06014270);                                 /* lap_time_init: the R block of the car (the clock, the record pointers) */
    r0 = R8(0x060AA80A);
    if (r0 == 2) r0 = 10;
    W8(CAR + 92, r0);
    W8(CAR + 95, R8(0x060AA811));
    engine(0x06009938);                                 /* carconfig: the car's tuning (the set-up bytes) */
}

static void ending_prepare(void)                        /* 0x0601EFF4: the replay of the last stage: game type 0, play mode 2 (replay), stage 3; the course data */
{
    W8(0x060AA825, R8(0x060AA825) & 0xFE);
    W16(0x060AA860, 0);
    W8(0x060AA810, 2);
    W16(0x060AA87E, 3);
    W16(0x060AA880, 6);
    W16(0x060AA882, 12);
    W16(0x060AA884, 24);
    W8(0x060AA300, R8(0x060AFE02));
    W32(0x060AAA5C, R32(0x060AFE0C));
    W32(0x060B0574, R32(0x060AFE0C));                   /* the recorded inputs of the stage: from here .. + 0xF7FE */
    W32(0x060B0578, R32(0x060AFE0C) + 0xF7FE);
    crs_dataset();
    set_game_texture();                                 /* crs_dataset's saved pr stays on the stack: the tail call runs twice (as in the photo module) */
    set_game_texture();
}

/* ---- the camera ---- */
static void car_corners(void)                           /* 0x0600FF10 (r13 = the car, r11 = 0x002994C0): four points of the car body turned by its angles: the translation column of the matrix at car + 0x130 */
{
    u32 r11 = 0x002994C0, r10 = 0x0600FF58;
    int k, i;
    for (k = 0; k < 4; k++, r10 += 12, r11 += 4) {
        Mat34 m;
        mat_unit(&m);
        mat_roty(&m, rad_turns(RS32(CAR + 16)));
        mat_rotx(&m, rad_turns(RS32(CAR + 12)));
        mat_rotz(&m, rad_turns(RS32(CAR + 20)));
        mat_translate(&m, RS32(r10), RS32(r10 + 4), RS32(r10 + 8));
        for (i = 0; i < 12; i++) W32(CAR + 0x130 + 4 * (u32)i, (u32)m.m[i]);
        W32(r11 + 0, (u32)m.m[3]);
        W32(r11 + 16, (u32)m.m[7]);
        W32(r11 + 32, (u32)m.m[11]);
    }
}

static void normal_cainit(void)                         /* 0x0600FA5C: the camera's first call (r14 = CAMW): in a replay the cut-scene camera takes over (the function keeps its bit 0) */
{
    if (R8(0x060AA810) == 2) {
        W32(CAMW, 0x0601F4AE | (R32(CAMW) & 1));
        smoke_init(0x01010101);
    } else {
        W32(CAMW, 0x0600FAF0 | (R32(CAMW) & 1));
    }
    car_corners();
    W32(CAMW + 32, R32(0x0600FAE4));
    W32(CAMW + 36, R32(0x0600FAE8));
    W32(CAMW + 40, R32(0x0600FAEC));
}

static void camera_ctrl(void)                           /* 0x06005E20: the function of the camera work (bit 0 is not a flag here) */
{
    u32 fn = R32(CAMW);
    act_cur = CAMW;
    if (fn == 0x0601F4AE || fn == 0x0601F4C8 || fn == 0x0600FA5C) menu_call(fn);       /* the ending's cameras */
    else engine(fn);                                    /* the chase camera (normal_ca 0x0600FAF0) is the engine's */
}

static void calc_camera_pmap(void)                      /* 0x0600FD34: the road block search at the eye (the camera's dummy car) */
{
    W32(CARB + 24, R32(CAMW + 8));
    W32(CARB + 32, R32(CAMW + 16));
    get_block();
    calc_dispang2();
    get_poly_map();
}

static void cam_pers(void)                              /* 0x0601F66C: the perspective key (L / R with DOWN and Z held changes the distance 224 .. 768), then perspective(): the pipeline's */
{
    u32 pad = (u32)RS16(0x060AA990), r2 = 0x2010 & pad, v;
    if (r2 == 0x2010) {
        if (pad & 0x80) {
            v = (u32)RS16(0x060D008A) + 16;
            if ((s32)v > 0x300) v = 0x300;
            W16(0x060D008A, v);
        } else if (pad & 0x8) {
            s32 s = RS16(0x060D008A) - 16;
            if (224 > s) s = 224;
            W16(0x060D008A, (u32)s);
        }
    }
}

static s32 cam_lookup(u32 *rec)                         /* 0x0601F51C: the camera of the player's block: type (0 chase, 1 fixed eye looking at the car, 2 fixed eye and angles) and its record */
{
    u32 r1 = 0x002A2000u + ((u32)R8(0x060D0089) << 3);
    u32 r3 = R32(r1), r4 = R32(r1 + 4), r0, r2;
    r0 = (u32)(s32)RS16(0x060B0060);
    r0 = (u32)(s32)RS8(r0 + r3);
    r0 <<= 2;
    r2 = r4 + r0;
    *rec = r4 + (u32)(s32)RS16(r2 + 2);
    return RS16(r2);
}

static void cam_eye_lookat(u32 rec)                     /* 0x0601F542: the eye from the record, the angle words towards the car (pitch, yaw, roll 0) */
{
    s32 cam[3], tgt[3], pitch, yaw;
    W32(CAMW + 8, R32(rec));
    W32(CAMW + 12, R32(rec + 4));
    W32(CAMW + 16, R32(rec + 8));
    cam[0] = RS32(CAMW + 8); cam[1] = RS32(CAMW + 12); cam[2] = RS32(CAMW + 16);
    tgt[0] = RS32(CAR + 24); tgt[1] = RS32(CAR + 28); tgt[2] = RS32(CAR + 32);
    lookat_angle(cam, tgt, &pitch, &yaw);
    W16(CAMW + 20, (u32)neg(pitch));
    W16(CAMW + 22, (u32)neg(yaw));
    W16(CAMW + 24, 0);
}

static void cam_matrix(void)                            /* 0x0601F56E */
{
    int k, n = RS16(0x060AA000);
    photo_view.tick = sat_ticks ? sat_ticks : 1;        /* the scene for the host: the same record as the photo module's (menu_host.c draws the course, the scenery and the text) */
    photo_view.course = RS16(0x060AA87E) & 3;
    for (k = 0; k < 3; k++) photo_view.pos[k] = RS32(CAMW + 8 + 4 * k);
    photo_view.stretch = 0;
    photo_view.pitch = neg(RS16(CAMW + 20));            /* initial_matrix, rotate_x(word), rotate_y(word), translate2(-eye): the host's view is rotate_x(-pitch), rotate_y(-yaw) */
    photo_view.yaw = neg(RS16(CAMW + 22));
    if (n > 16) n = 16;
    if (n < 0) n = 0;
    photo_view.nparts = n;
    photo_view.have_mat = 0;
    for (k = 0; k < n; k++) photo_view.parts[k] = R8(0x060AA140 + (u32)k);
}

static void act_end_camera(void)                        /* 0x0601F4C8 */
{
    u32 rec;
    s32 type;
    cam_pers();
    type = cam_lookup(&rec);
    switch (type) {
    case 0:                                             /* the chase camera of the replay */
        engine(0x0600FC5C);
        break;
    case 1:
        cam_eye_lookat(rec);
        calc_camera_pmap();
        cam_matrix();
        break;
    case 2:                                             /* 0x0601F4F8 */
        W32(CAMW + 8, R32(rec));
        W32(CAMW + 12, R32(rec + 4));
        W32(CAMW + 16, R32(rec + 8));
        W16(CAMW + 20, R16(rec + 12));
        W16(CAMW + 22, R16(rec + 14));
        W16(CAMW + 24, R16(rec + 16));
        calc_camera_pmap();
        cam_matrix();
        break;
    default:
        menu_unported(0x0601F4DC);
    }
}

static void act_end_camera_init(void)                   /* 0x0601F4AE */
{
    u32 rec;
    W16(0x060D008A, 0x200);
    cam_lookup(&rec);
    cam_eye_lookat(rec);
    W32(CAMW, 0x0601F4C8);
    W16(CAMW + 58, 0);
    act_end_camera();                                   /* the init code runs on into the pass (0x0601F4C6 -> 0x0601F4C8) */
}

static void replay_cactl(void)                          /* 0x0600FC94: in the replay the pad changes the camera (A / B / C: cut scenes <-> chase camera, X / Y / Z: the view) and picks the scroll control */
{
    u32 pad, v;
    if (R8(0x060AA810) != 2) return;
    switch (R8(0x060AA01A)) {                           /* the byte at the view word: 0 = the toggles, 2 = the chase camera is set, 4 nothing (a table of word offsets indexed by the byte) */
    case 0:                                             /* 0x0600FCE0 */
        pad = R16(0x060AA00A);
        if (pad & 0x0700) {
            v = (R8(0x060AFE7C) + 1) & 1;
            W8(0x060AFE7C, v);
            if (v != 0) {
                W32(CAMW, 0x0600FAF0);
                cam_set_game_pers();
                smoke_init(0x101);
            } else {
                W32(CAMW, 0x0601F4AE);
                smoke_init(0x01010101);
            }
        } else if (R8(0x060AFE7C) != 0 && (pad & 0x70)) {
            W8(0x060AA01B, (R8(0x060AA01B) + 2) & 2);
        }
        break;
    case 2:                                             /* 0x0600FCCC */
        W32(CAMW, 0x0600FAF0);
        cam_set_game_pers();
        smoke_init(0x101);
        break;
    case 4:
        break;
    default:
        menu_unported(0x0600FCC0);
    }
    W32(0x060AA9F0, R8(0x060AFE7C) != 0 ? 0x06006BC4 : 0x06006BD4);
}

/* ---- sync_ctrl 0x060067D0: the other cars' positions for the HUD ---- */
static void sync_entry(u32 r13, u32 *r14)               /* 0x060067EA: position (x, z), +36 and +44 of a car block */
{
    W32(*r14, R32(r13 + 24)); *r14 += 4;
    W32(*r14, R32(r13 + 32)); *r14 += 4;
    W32(*r14, R32(r13 + 36)); *r14 += 4;
    W32(*r14, R32(r13 + 44)); *r14 += 4;
}

static void sync_ctrl(void)
{
    u32 r14 = 0x060AFD00, r13;
    int k;
    W16(0x060AA02E, R16(CAR + 10));
    sync_entry(CAR, &r14);
    for (k = 0, r13 = 0x060B0600; k < 8; k++, r13 += 0x200) sync_entry(r13, &r14);     /* the original steps 0x200, not the slot size 0x400 */
}

static void replay_rmove(void)                          /* 0x06006BC4: the scroll control of the chase camera: set_grpara, then the second block's copy (set_tp_rr 0x06015FC8) */
{
    int k;
    set_grpara2();
    for (k = 0; k < 7; k++) W32(0x060AAAE4 + 4 * (u32)k, R32(0x060AA9F4 + 4 * (u32)k));
}

static void rscroll_ctrl(void)                          /* 0x06005E54 */
{
    u32 fn = R32(0x060AA9F0);
    if (fn == 0x06006BD4) normal_endmove();
    else if (fn == 0x06006BC4) replay_rmove();
    else menu_unported(fn);
}

static void ending_stats(void)                          /* 0x0601D756: the pipeline's counters of the master / slave halves */
{
    u32 m = 0x060D1400, s = 0x060D1C00, r1 = 0x060D0034;
    u32 a = R16(m + 170), b = R16(m + 134), c = R16(m + 168);
    W16(r1 + 4, a);
    W16(r1 + 6, b);
    W16(r1 + 8, R16(s + 170));
    W16(r1 + 0, R16(s + 170) + a);
    W16(r1 + 10, R16(s + 134));
    W16(r1 + 2, R16(s + 134) + b);
    W16(r1 + 12, R16(s + 168) + c);
}

/* ---- the pass of the race engine (game_ctrl 0x06005B78: the one player path 0x06005B90) ---- */
static void game_ctrl(void)
{
    u32 r14;
    if (RS16(0x060AA860) != 0 && RS16(0x060AA860) != 2) { menu_unported(0x06005B78); return; }
    s_mkpoly_frame_init();
    m_mkpoly_frame_init();
    engine(R32(CAR + 0));                               /* the player's state function (fn0) */
    replay_cactl();
    camera_ctrl();
    engine(R32(CAR + 4));                               /* the player's presentation (fn4) */
    for (r14 = 0x060B0600; r14 < 0x060B2200 + 0x400; r14 += 0x400) {
        u32 fn = R32(r14);
        if (fn == 0) continue;
        engine(fn);
        fn = R32(r14 + 4);
        if (fn == 0) continue;
        engine(fn);
    }
    sync_ctrl();
    engine(0x0600CAD0);                                 /* otherCtrl: the rival cars together */
    engine(0x0601BCB8);                                 /* the scenery objects */
    if (RS8(0x060B00F5) < 8) engine(0x0601D410);        /* the HUD */
    rscroll_ctrl();
    W16(0x060D0040, 0);                                 /* wait_slave_end: 0 */
    ending_stats();
}

/* ---- the staff roll's states ([0x060D0020]) ---- */
static u32 staff_time(void)                             /* 0x0601F2E8: the length of the roll from the total time of the run: 30 passes per second, at most 0xA8C + 90 */
{
    u32 r9;
    r9 = bcd2bin(R8(0x060AFE55)) * 0x708u;
    r9 += bcd2bin(R8(0x060AFE56)) * 30u;
    r9 += (u32)(((u64)bcd2bin(R8(0x060AFE57)) * 0x4CCCCCCDull) >> 32);
    if (!(0xA8C > r9)) r9 = 0xA8C;
    return r9 + 90;
}

static void state_end_a(void)                           /* 0x0601F230: (the pass counter test compares the counter's address: it always goes on) */
{
    u32 t;
    W32(0x060D009C, R32(0x060D009C) + 1);
    W32(0x060D009C, 0);
    t = staff_time();
    staff_act_init(t);
    W32(0x060D0020, 0x0601F25E);
}

static void state_end_b(void)                           /* 0x0601F25E: the roll runs while the camera does not hold it */
{
    if (RS16(0x060AA9EA) != 0) return;
    staff_act();
    if (RS32(STAFF + 0) > 10) W32(0x060D0020, 0x0601F27A);
}

static void state_end_d(void)                           /* 0x0601F29C: 60 passes of fade, then the screen ends */
{
    u32 r0 = R32(0x060D009C) + 1;
    W32(0x060D009C, r0);
    if (60 > r0) return;
    W8(FLAGS, R8(FLAGS) | 2);
}

static void state_end_c(void)                           /* 0x0601F27A: the fade out and the jingle (the code runs on into the next state's body) */
{
    pre_fade_inout(0, 255, 12);
    snd_call(0x0601734C, 10, 0, 0);
    W32(0x060D0020, 0x0601F29C);
    W32(0x060D009C, 0);
    state_end_d();
}

/* ---- the engine's set-up of the replay (FUN_0601F048 at the end of ending_init) ---- */
static void ending_setup(void)
{
    W32(CAR + 4, 0x060066F8);                           /* ending_plmt_start: the replay car's presentation */
    engine(0x06005E84);                                 /* car_move_ctrl: the state functions of the car slots (normal_plinit) */
    engine(0x06006214);                                 /* normal_otinit: the rival cars (initOtherGP) */
    camera_ctrl();                                      /* normal_cainit */
    sync_ctrl();
    W32(CAMW, 0x0601F4AE);                              /* ending_camera_act */
    engine(0x06005E84);
    camera_ctrl();                                      /* the cut-scene camera's first call */
    engine(0x06006668);                                 /* calc_pmap_sub: the player's road blocks */
    sync_ctrl();
    engine(0x0600CAD0);
    fade_color_init(0);                                 /* other_set_mors 0x0600602C is a bare rts */
}

void ending_init(void)                                  /* 0x0601EF34 */
{
    ghost_pass = 0;
    tv_off();
    snd_call(0x060173AC, 0, 0, 0);
    init_gspr();
    ending_prepare();
    init_game_work();
    init_game_vram();
    swclear();
    W32(0x060AA9AC, 0x060AA9B8);
    W8(0x060AA807, 0);
    W16(0x25D00004, 2);
    W32(0x060A001C, 0x0601F0A0);
    W8(0x060AA806, R8(0x060AA806) & 0xFE);
    W8(0x060A0001, 2);
    W8(0x060A0013, 2);
    set_staff_cg();
    W32(0x060D0020, 0x0601F230);
    W8(0x060D0089, 3);
    W32(0x060D009C, 0);
    ending_setup();
    snd_call(0x0600F756, 12, 15, 0);                    /* the CD track of the ending */
    mkpoly_init();
    dispcar_init();
    W16(0x25F80020, 31);
    W16(0x060AA8E6, 31);
    tv_on();
}

void ending_main(void)                                  /* 0x0601F0A0 */
{
    for (;;) {
        W16(0x060AA800, 0x2404);
        snd_call(0x06017580, 0, 0, 0);
        W32(0x060AA168, R32(0x060AA168) + 1);
        W16(0x060AA004, R16(0x060AA004) + 1);
        fade_inout_work();
        snd_call(0x06017374, 0, 0, 0);                  /* _gfc_madload: the CD streaming of the course data */
        if (R32(0x060D0020) != 0) menu_call(R32(0x060D0020));
        game_ctrl();
        W16(0x060AA994, 0);
        W16(0x060AA99A, 0);
        intm_sync(38);
        if (R8(FLAGS) & 2) break;
    }
    W8(FLAGS, R8(FLAGS) & 0xFD);
    W8(0x060AA80D, R8(0x060AA80D) | 1);
    W8(0x060AA825, R8(0x060AA825) & 0xFE);
    W8(0x060AA810, 0);
    engine(0xFFFF0001u);                                /* (not an address of the original: the host lets go of the engine's course data) */
    W32(0x060A001C, R32(0x060D0024));                   /* the callback of the caller of the ceremony (post_goal: init_game_over, then the name entry / ranking) */
}


/* ================================ the PSP's stand-in for the race engine of the ending ================================
 * The original replays the recorded inputs of the last stage on the physics (src/drive.c would do that with the inputs, which play.c does not record yet).  Until it does, the host binds
 * this to menu_engine_hook: a ghost of the player's car drives the guide line of the lake (the rivals' path, PRGDATA.DAT: 303 records of (x, y, z, w) at 0x0029D7A8) once in about the
 * length of the staff roll; the camera cuts follow its road block ([0x060B0060], found by the original's own get_block).  Nothing of this is checked against the oracle. */
static void ghost_pose(u32 pass, s32 pos[3], s32 *yaw_turns)
{
    const u32 g = 0x0029D7A8u;
    u32 fx = pass * 0x1EB8u, k = (fx >> 16) % 300, f = fx & 0xFFFF, a = g + 16 * k, b = g + 16 * (k + 1);
    int i;
    for (i = 0; i < 3; i++) pos[i] = RS32(a + 4 * (u32)i) + (s32)(((s64)((s32)((u32)RS32(b + 4 * (u32)i) - (u32)RS32(a + 4 * (u32)i))) * f) >> 16);
    *yaw_turns = atan16((s32)((u32)RS32(b) - (u32)RS32(a)), (s32)((u32)RS32(b + 8) - (u32)RS32(a + 8)));
}

static s32 turns_to_rad(s32 turns) { return (s32)(((s64)turns * 411775) >> 16); }      /* 2 pi * 65536 */

static void ghost_step(void)                            /* the player's state function of every pass (normal_plinit's place) */
{
    s32 pos[3], yaw;
    ghost_pose(ghost_pass++, pos, &yaw);
    W32(CAR + 24, (u32)pos[0]);
    W32(CAR + 28, (u32)pos[1] + 0x6000);                /* the car's centre above the road */
    W32(CAR + 32, (u32)pos[2]);
    W32(CAR + 12, 0);
    W32(CAR + 16, (u32)turns_to_rad(yaw));
    W32(CAR + 20, 0);
    blk_base = CAR;
    get_block();                                        /* the road block of the car: [car + 96] picks the camera of CAMERA.DAT */
    blk_base = 0x060B2600u;
}

static void ghost_chase_camera(void)                    /* the chase camera (normal_ca / replay_ca): behind and above the car, looking at it */
{
    s32 car[3], cam[3], pitch, yaw, sn, cs, t;
    int i;
    for (i = 0; i < 3; i++) car[i] = RS32(CAR + 24 + 4 * i);
    t = (s32)(((s64)RS32(CAR + 16) * 0x28BE60DC) >> 32);          /* the car's yaw in turns again */
    sincos16((u32)t, &sn, &cs);                         /* rotate_y(yaw) turns the car's forward (0, 0, 1) to (sin, 0, cos) of the original's sense */
    cam[0] = (s32)((u32)car[0] - (u32)((s64)sn * 8 >> 0));
    cam[1] = (s32)((u32)car[1] + 0x30000);
    cam[2] = (s32)((u32)car[2] - (u32)((s64)cs * 8 >> 0));
    W32(CAMW + 8, (u32)cam[0]);
    W32(CAMW + 12, (u32)cam[1]);
    W32(CAMW + 16, (u32)cam[2]);
    lookat_angle(cam, car, &pitch, &yaw);
    W16(CAMW + 20, (u32)neg(pitch));
    W16(CAMW + 22, (u32)neg(yaw));
    W16(CAMW + 24, 0);
    calc_camera_pmap();
    cam_matrix();
}

void ending_present_car(void)                           /* the car's presentation (ending_plmt_start's place): what the host draws */
{
    cer3d_begin();
    cer3d.ending = 1;
    cer3d.car_id = R8(CAR + 92);
    memcpy(cer3d.car, sat_ptr(CAR), 0x100);
}

void ending_chase_done(void)                            /* the chase camera (the engine's) wrote the eye and the angle words: the part list at the eye and the photo scene's view follow */
{
    calc_camera_pmap();
    cam_matrix();
    photo_view.stretch = 1;                             /* the race's chase camera builds its view with FUN_060100D8: scale(1.0625, 1, 1) first (the cut-scene cameras' matrices have none) */
}

void ending_ghost_engine(u32 addr)
{
    switch (addr) {
    case 0x06006030: ghost_step(); break;
    case 0x060066F8: ending_present_car(); break;
    case 0x0600FAF0: case 0x0600FC5C: ghost_chase_camera(); break;
    default: break;                                     /* the rivals, the scenery objects, the HUD, the tyre marks: not run */
    }
}

void init_game_over(void)                               /* 0x06011EA4: GMGOVER.DAT, then its text box (37 x 4 cells of 32 bits: byte * 2 + 0x00600822) at 0x25E30C0C (boxwrt_a4 0x060056C4 -> boxwrt_long4) */
{
    u32 src = 0x002001FC, dst = 0x25E30C0C, r1, r7;
    sat_load_name_at(0x06011EC0);
    for (r7 = 4; r7 > 0; r7--) {
        u32 d = dst;
        for (r1 = 37; r1 > 0; r1--) {
            W32(d, ((u32)R8(src) << 1) + 0x00600822);
            src++;
            d += 4;
        }
        dst += 0x100;
    }
}

void menu_ending_register(void)
{
    menu_reg(0x0601E4E8, ceremony_init);
    menu_reg(0x0601E664, ceremony_main);
    menu_reg(0x0601E6DA, state_cer_a);
    menu_reg(0x0601E708, state_cer_b);
    menu_reg(0x0601E726, state_cer_c);
    menu_reg(0x0601E7B4, act_camera_init);
    menu_reg(0x0601E7CE, act_camera);
    menu_reg(0x0601E9B6, act_car_init);
    menu_reg(0x0601E9D2, act_car);
    menu_reg(0x0601ED1C, act_anim_init);
    menu_reg(0x0601ED24, act_anim);
    menu_reg(0x0601EF34, ending_init);
    menu_reg(0x0601F0A0, ending_main);
    menu_reg(0x0601F230, state_end_a);
    menu_reg(0x0601F25E, state_end_b);
    menu_reg(0x0601F27A, state_end_c);
    menu_reg(0x0601F29C, state_end_d);
    menu_reg(0x0601F4AE, act_end_camera_init);
    menu_reg(0x0601F4C8, act_end_camera);
    menu_reg(0x0600FA5C, normal_cainit);
}
