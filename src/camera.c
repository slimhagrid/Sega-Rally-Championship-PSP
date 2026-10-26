/* Sega Rally PSP - the race camera, see camera.h.  Every routine names the original's address; the arithmetic is the original's (16.16, 64-bit products, the sine table). */
#include <string.h>
#include "camera.h"
#include "fx.h"
#include "gmath.h"

enum { CW_HANDLER = 0x00, CW_EYE = 0x08, CW_ANG = 0x14, CW_EXTRA = 0x1A, CW_EYEOFF = 0x20, CW_PIVOT = 0x2C, CW_CNT = 0x38, CW_5C = 0x5C };
enum { H_CAINIT = 0x0600FA5C, H_CA = 0x0600FAF0, H_ECAMI = 0x0601F4AE, H_ECAM = 0x0601F4C8 };

/* car slot fields the camera reads */
enum { C_PITCH = 0x0C, C_YAW = 0x10, C_ROLL = 0x14, C_POS = 0x18, C_NEGYAW = 0xAC };

static inline int gw(const unsigned char *w, int o) { return (short)((w[o] << 8) | w[o + 1]); }       /* an angle word (mov.w, sign extended) */
static inline void pw(unsigned char *w, int o, int v) { w[o] = (unsigned char)(v >> 8); w[o + 1] = (unsigned char)v; }

/* the matrix module (rows of 4 longs, p' = M p, every operation multiplies M by the new transform from the right) */
static void m_rot(const Phys *ph, int m[12], int axis, int ang)                        /* rotate_x / rotate_y / rotate_z: angle in 16.16 turns, sine table */
{
    int s, c;
    fx_sincos(ph, ang, &s, &c);
    if (axis == 0) rot_cols(m, 1, 2, s, c);
    else if (axis == 1) rot_cols(m, 0, 2, neg(s), c);
    else rot_cols(m, 0, 1, s, c);
}
static void m_rrot(const Phys *ph, int m[12], int axis, int rad) { m_rot(ph, m, axis, HM(rad, 0x28BE60DC)); }          /* r_rotate_*: radians */

static void m_translate(int m[12], const int v[3])                                    /* translate 0x06017DD0 */
{
    for (int r = 0; r < 3; r++) m[4 * r + 3] += X16((i64)m[4 * r] * v[0] + (i64)m[4 * r + 1] * v[1] + (i64)m[4 * r + 2] * v[2]);
}
static void m_scale(int m[12], int sx, int sy, int sz)                                /* scale 0x06017EBC */
{
    for (int r = 0; r < 3; r++) { m[4 * r] = FM(m[4 * r], sx); m[4 * r + 1] = FM(m[4 * r + 1], sy); m[4 * r + 2] = FM(m[4 * r + 2], sz); }
}

static int wrap_pi(int a)                                                              /* 0x06010004 */
{
    if (a > 0) { if (0x3243F >= a) return a; return a - 0x6487E; }
    if (a >= -0x3243F) return a;
    return a + 0x6487E;
}

/* FUN_0600FFE0: the chase view follows the heading with a low-pass (car +0xAC) */
static void lag_filter(unsigned char *car)
{
    int r5 = g32(car, C_NEGYAW);
    int r0 = wrap_pi(g32(car, C_YAW) + r5);
    r0 = HM(r0, 0x0CCCCCCD);
    p32(car, C_NEGYAW, wrap_pi(r5 - r0));
}

/* uniform cubic B-spline of four control points per axis (car::spline 0x06007DDC) */
static void spline(int t, const int *pts, int out[3])
{
    int t2 = FM(t, t), t3 = FM(t, t2), omt = 0x10000 - t;
    int om2 = FM(omt, omt), om3 = FM(omt, om2);
    int B[4];
    B[0] = HM(om3, 0x2AAAAAAB);
    B[1] = (t3 >> 1) - t2 + 0xAAAB;
    B[2] = (t2 >> 1) - (t3 >> 1) + (t >> 1) + 0x2AAB;
    B[3] = HM(t3, 0x2AAAAAAB);
    for (int k = 0; k < 3; k++)
        out[k] = X16((i64)B[0] * pts[4 * k] + (i64)B[1] * pts[4 * k + 1] + (i64)B[2] * pts[4 * k + 2] + (i64)B[3] * pts[4 * k + 3]);
}

/* FUN_0600FF10: the four fly-by control points, offsets (table 0x0600FF58) turned by the car's start attitude */
static void spline_init(Camera *cam, Drive *d)
{
    static const int pts[4][3] = { { 0, 0x0010599A, (int)0xFFCB8555 }, { 0, 0x000728F6, (int)0xFFDFE17E }, { 0, (int)0xFFFF9C29, 0x00024296 }, { 0, (int)0xFFFF2148, 0x0008307D } };
    unsigned char *car = d->car;
    for (int i = 0; i < 4; i++) {
        int m[12];
        unit_matrix(m);
        m_rrot(d->phys, m, 1, g32(car, C_YAW));
        m_rrot(d->phys, m, 0, g32(car, C_PITCH));
        m_rrot(d->phys, m, 2, g32(car, C_ROLL));
        m_translate(m, pts[i]);
        cam->spline[i] = m[3];
        cam->spline[4 + i] = m[7];
        cam->spline[8 + i] = m[11];
        for (int k = 0; k < 12; k++) p32(car, 0x130 + 4 * k, m[k]);                    /* the original works in the car's scratch matrix */
    }
}

static void set_angles_from_car(unsigned char *w, const unsigned char *car)           /* the rigid attitude: minus the car's angles */
{
    pw(w, CW_ANG, neg(HM(g32(car, C_PITCH), 0x28BE60DB)));
    pw(w, CW_ANG + 2, neg(HM(g32(car, C_YAW), 0x28BE60DB)));
    pw(w, CW_ANG + 4, neg(HM(g32(car, C_ROLL), 0x28BE60DB)));
}

static void view_a(unsigned char *w, const unsigned char *car)                         /* FUN_0600FD68: fixed to the car, pivot 0.75 above its origin */
{
    set_angles_from_car(w, car);
    p32(w, CW_PIVOT, 0); p32(w, CW_PIVOT + 4, 0xC000); p32(w, CW_PIVOT + 8, 0);
    p32(w, CW_EYEOFF, 0); p32(w, CW_EYEOFF + 4, 0); p32(w, CW_EYEOFF + 8, 0);
}

static void view_b(unsigned char *w, const unsigned char *car)                         /* FUN_0600FDAC: the chase view, 6 m behind and 1.3 m up */
{
    pw(w, CW_ANG, HM(0x11DF - (g32(car, C_PITCH) >> 1), 0x28BE60DB));
    pw(w, CW_ANG + 4, 0);
    pw(w, CW_ANG + 2, HM(g32(car, C_NEGYAW), 0x28BE60DB));
    p32(w, CW_PIVOT, 0); p32(w, CW_PIVOT + 4, 0); p32(w, CW_PIVOT + 8, 0);
    p32(w, CW_EYEOFF, 0); p32(w, CW_EYEOFF + 4, 0x14CCD); p32(w, CW_EYEOFF + 8, (int)0xFFFA0000);
}

/* FUN_06010070: where the camera is: the car (plus pivot) turned by the attitude, then the eye offset in that frame */
static void place(Camera *cam, const Drive *d, const int m0[12])
{
    unsigned char *w = cam->w;
    const unsigned char *car = d->car;
    int m[12];
    memcpy(m, m0, sizeof m);
    m[3] = g32(car, C_POS) + g32(w, CW_PIVOT);
    m[7] = g32(car, C_POS + 4) + g32(w, CW_PIVOT + 4);
    m[11] = g32(car, C_POS + 8) + g32(w, CW_PIVOT + 8);
    m_rot(d->phys, m, 1, neg(gw(w, CW_EXTRA + 2) + gw(w, CW_ANG + 2)));
    m_rot(d->phys, m, 0, neg(gw(w, CW_EXTRA) + gw(w, CW_ANG)));
    m_rot(d->phys, m, 2, neg(gw(w, CW_EXTRA + 4) + gw(w, CW_ANG + 4)));
    int off[3] = { g32(w, CW_EYEOFF), g32(w, CW_EYEOFF + 4), g32(w, CW_EYEOFF + 8) };
    m_translate(m, off);
    p32(w, CW_EYE, m[3]); p32(w, CW_EYE + 4, m[7]); p32(w, CW_EYE + 8, m[11]);
}

/* FUN_060100D8: the view matrix of a camera at the placed pose (the inverse chain) */
static void view_matrix(Camera *cam, const Drive *d, int m[12])
{
    unsigned char *w = cam->w;
    const unsigned char *car = d->car;
    m_scale(m, 0x11000, 0x10000, 0x10000);
    m[3] = neg(g32(w, CW_EYEOFF)); m[7] = neg(g32(w, CW_EYEOFF + 4)); m[11] = neg(g32(w, CW_EYEOFF + 8));
    m_rot(d->phys, m, 2, gw(w, CW_EXTRA + 4) + gw(w, CW_ANG + 4));
    m_rot(d->phys, m, 0, gw(w, CW_EXTRA) + gw(w, CW_ANG));
    m_rot(d->phys, m, 1, gw(w, CW_EXTRA + 2) + gw(w, CW_ANG + 2));
    int t[3] = { neg(g32(car, C_POS) + g32(w, CW_PIVOT)), neg(g32(car, C_POS + 4) + g32(w, CW_PIVOT + 4)), neg(g32(car, C_POS + 8)) };
    t[1] += g32(w, CW_PIVOT + 8);                                                       /* sic: the original adds the pivot's z to y here (the pivot z is 0 in both views) */
    m_translate(m, t);
}

/* mode 4, the fly-by: FUN_0600FF88 (eye on the spline, minus the car) and its matrix FUN_0601016C */
static void flyby(Camera *cam, Drive *d, int m[12])
{
    unsigned char *w = cam->w;
    const unsigned char *car = d->car;
    int t = g32(w, CW_CNT), eye[3];
    spline(t, cam->spline, eye);
    for (int k = 0; k < 3; k++) p32(w, CW_EYE + 4 * k, neg(eye[k] + g32(car, C_POS + 4 * k)));
    set_angles_from_car(w, car);
    int nt = t + 0x312;
    p32(w, CW_CNT, nt);
    if (nt == 0xE646) cam->fade = 3;
    else if (nt >= 0x10000) { p32(w, CW_CNT, 3); d->cam_mode = 6; }
    m_scale(m, 0x11000, 0x10000, 0x10000);
    m_rot(d->phys, m, 2, gw(w, CW_ANG + 4));
    m_rot(d->phys, m, 0, gw(w, CW_ANG));
    m_rot(d->phys, m, 1, gw(w, CW_ANG + 2));
    int eyev[3] = { g32(w, CW_EYE), g32(w, CW_EYE + 4), g32(w, CW_EYE + 8) };
    m_translate(m, eyev);
}

/* mode 2, the goal orbit: FUN_0600FE84 and its matrix FUN_06010148 */
static void orbit(Camera *cam, Drive *d, int m[12])
{
    unsigned char *w = cam->w;
    unsigned char *car = d->car;
    int t[12];
    unit_matrix(t);
    int cnt = gw(w, CW_CNT);
    pw(w, CW_ANG + 2, cnt + 0x7FFF);
    m_rot(d->phys, t, 1, neg(cnt + 0x7FFF));
    int s, c;
    fx_sincos(d->phys, cnt << 1, &s, &c);
    int pitch = HM(FM(s, 0x199A) + 0x2836, 0x28BE60DB);
    pw(w, CW_ANG, pitch);
    m_rot(d->phys, t, 0, neg(pitch));
    static const int off[3] = { 0, (int)0xFFFF4000, 0x38000 };
    m_translate(t, off);
    for (int k = 0; k < 12; k++) p32(car, 0x130 + 4 * k, t[k]);                       /* the car's scratch matrix again */
    int e1 = t[7] - g32(car, C_POS + 4);
    if (g8(w, CW_5C)) e1 -= 0xB333;
    p32(w, CW_EYE, t[3] - g32(car, C_POS));
    p32(w, CW_EYE + 4, e1);
    p32(w, CW_EYE + 8, t[11] - g32(car, C_POS + 8));
    pw(w, CW_CNT, cnt + 0xD0);
    m_scale(m, 0x11000, 0x10000, 0x10000);
    m_rot(d->phys, m, 0, gw(w, CW_ANG));
    m_rot(d->phys, m, 1, gw(w, CW_ANG + 2));
    int eyev[3] = { g32(w, CW_EYE), g32(w, CW_EYE + 4), g32(w, CW_EYE + 8) };
    m_translate(m, eyev);
}

/* ---- the replay's cameras (play mode 2): ending::ending_camera_act 0x0601F4AE / 0x0601F4C8, the camera of CAMERA.DAT for the player's road slice ---------------------------------- */
static int cd_r32(const Camera *cam, unsigned a)
{
    const CamDat *c = &cam->camdat;
    if (!c->p || a < c->base || a - c->base + 4 > c->size) return 0;
    const unsigned char *q = c->p + (a - c->base);
    return (int)((u32)q[0] << 24 | (u32)q[1] << 16 | (u32)q[2] << 8 | q[3]);
}
static int cd_r16(const Camera *cam, unsigned a)                                       /* mov.w: sign extended */
{
    const CamDat *c = &cam->camdat;
    if (!c->p || a < c->base || a - c->base + 2 > c->size) return 0;
    const unsigned char *q = c->p + (a - c->base);
    return (short)((q[0] << 8) | q[1]);
}
static int cd_r8(const Camera *cam, unsigned a)                                        /* mov.b: sign extended */
{
    const CamDat *c = &cam->camdat;
    if (!c->p || a < c->base || a - c->base + 1 > c->size) return 0;
    return (signed char)c->p[a - c->base];
}

static int cam_lookup(const Camera *cam, const Drive *d, unsigned *rec)                /* 0x0601F51C: the camera of the player's road slice: its type (0 chase, 1 fixed eye looking at the car, 2 fixed eye and angles) and record */
{
    unsigned hdr = cam->camdat.base + 8u * (unsigned)d->cfg.course;
    unsigned tbl = (unsigned)cd_r32(cam, hdr), recs = (unsigned)cd_r32(cam, hdr + 4);
    int idx = cd_r8(cam, tbl + (unsigned)(short)g16u(d->car, 0x60));
    unsigned e = recs + (unsigned)(idx << 2);
    *rec = recs + (unsigned)cd_r16(cam, e + 2);
    return cd_r16(cam, e);
}

static void cam_eye_lookat(Camera *cam, const Drive *d, unsigned rec)                  /* 0x0601F542: the eye from the record, the angle words towards the car (0x0601BA6C get_lookat_angle) */
{
    unsigned char *w = cam->w;
    for (int k = 0; k < 3; k++) p32(w, CW_EYE + 4 * k, cd_r32(cam, rec + 4u * (unsigned)k));
    int cx = g32(w, CW_EYE), cy = g32(w, CW_EYE + 4), cz = g32(w, CW_EYE + 8);
    int tx = g32(d->car, C_POS), ty = g32(d->car, C_POS + 4), tz = g32(d->car, C_POS + 8);
    int a = g_atan(tx - cx, tz - cz);
    int ax = cx >> 1, az = cz >> 1, bx = tx >> 1, bz = tz >> 1;
    i64 acc = (i64)(ax - bx) * (ax - bx) + (i64)(az - bz) * (az - bz);
    int dd = g_sqrt(X16(acc));
    int p = g_atan(dd, (cy - ty) >> 1);
    int pitch = neg(p), yaw = a - 0x4000;
    pw(w, CW_ANG, neg(pitch));
    pw(w, CW_ANG + 2, neg(yaw));
    pw(w, CW_ANG + 4, 0);
}

static void calc_camera_pmap(Camera *cam)                                              /* 0x0600FD34: the road block at the eye (the dummy car) -> the part list of the pass */
{
    Drive *dm = cam->dummy;
    unsigned char *w = cam->w;
    if (!dm) return;
    p32(dm->car, C_POS, g32(w, CW_EYE));
    p32(dm->car, C_POS + 8, g32(w, CW_EYE + 8));
    drive_get_block(dm);
    dm->cam_ang1 = gw(w, CW_ANG + 2);                                                   /* calc_dispang2 reads the camera work's words */
    dm->cam_ang2 = gw(w, CW_EXTRA + 2);
    drive_calc_dispang(dm);
    cam->pm[0] = dm->car[0xF0]; cam->pm[1] = dm->car[0xF1]; cam->pm[2] = dm->car[0xC7];
    cam->pm_on = 1;
}

static void cam_matrix(Camera *cam, Drive *d)                                          /* 0x0601F56E: unit matrix, reverse_x on the mirror course, rotate_x / rotate_y of the angle words, translate2(-eye) */
{
    unsigned char *w = cam->w;
    int m[12];
    unit_matrix(m);
    if (d->cfg.mirror) { m[0] = neg(m[0]); m[4] = neg(m[4]); m[8] = neg(m[8]); }
    m_rot(d->phys, m, 0, gw(w, CW_ANG));
    m_rot(d->phys, m, 1, gw(w, CW_ANG + 2));
    int t[3] = { neg(g32(w, CW_EYE)), neg(g32(w, CW_EYE + 4)), neg(g32(w, CW_EYE + 8)) };
    m_translate(m, t);
    memcpy(cam->view, m, sizeof m);
    memcpy(cam->view_copy, m, sizeof m);
}

static void chase_pose(Camera *cam, Drive *d, int m[12])                                /* replay_ca 0x0600FC5C up to the part list: the heading's low-pass, view B, where the eye is */
{
    lag_filter(d->car);
    unit_matrix(m);
    if (d->cfg.mirror) { m[0] = neg(m[0]); m[4] = neg(m[4]); m[8] = neg(m[8]); }
    view_b(cam->w, d->car);
    place(cam, d, m);
}

void camera_chase_pose(Camera *cam, Drive *d)                                           /* the ending's chase camera (menu_ending.c runs the rest: the part list at the eye and the view) */
{
    int m[12];
    chase_pose(cam, d, m);
}

static void replay_pass(Camera *cam, Drive *d)
{
    unsigned char *w = cam->w;
    unsigned rec;
    cam->fade = -1;
    cam->pm_on = 0;
    int h = g32(w, CW_HANDLER) & ~1;
    if (h == H_CAINIT) {                                                                /* normal_cainit 0x0600FA5C, play mode 2: the cut-scene camera takes over */
        p32(w, CW_HANDLER, H_ECAMI | (g32(w, CW_HANDLER) & 1));
        spline_init(cam, d);
        p32(w, CW_EYEOFF, 0); p32(w, CW_EYEOFF + 4, 0); p32(w, CW_EYEOFF + 8, 0);
        return;
    }
    if (h == H_ECAMI) {                                                                 /* 0x0601F4AE: the first pass runs on into the pass body */
        cam->pers = 0x200;
        cam_lookup(cam, d, &rec);
        cam_eye_lookat(cam, d, rec);
        p32(w, CW_HANDLER, H_ECAM);
        pw(w, 0x3A, 0);
        h = H_ECAM;
    }
    if (h != H_ECAM) return;
    int type = cam_lookup(cam, d, &rec);                                                /* 0x0601F4C8 */
    switch (type) {
    case 1:
        cam_eye_lookat(cam, d, rec);
        calc_camera_pmap(cam);
        cam_matrix(cam, d);
        break;
    case 2:
        for (int k = 0; k < 3; k++) p32(w, CW_EYE + 4 * k, cd_r32(cam, rec + 4u * (unsigned)k));
        for (int k = 0; k < 3; k++) pw(w, CW_ANG + 2 * k, cd_r16(cam, rec + 12u + 2u * (unsigned)k));
        calc_camera_pmap(cam);
        cam_matrix(cam, d);
        break;
    default: {                                                                          /* 0: the chase camera of the replay (replay_ca 0x0600FC5C): view B, and the part list of the eye's block */
        int m[12];
        chase_pose(cam, d, m);
        calc_camera_pmap(cam);
        view_matrix(cam, d, m);
        memcpy(cam->view, m, sizeof m);
        memcpy(cam->view_copy, m, sizeof m);
        break; }
    }
    d->cam_ang1 = gw(w, CW_ANG + 2);
    d->cam_ang2 = gw(w, CW_EXTRA + 2);
}

void camera_replay_keys(Camera *cam, Drive *d, int abc, int xyz)                        /* camera::replay_cactl 0x0600FC94: A / B / C toggle the cut-scene cameras and the chase camera, X / Y / Z change the chase camera's view */
{
    unsigned char *w = cam->w;
    if (!cam->replay || d->cam_mode == 2) return;
    if (abc) {
        d->rp_chase ^= 1;
        if (d->rp_chase) { p32(w, CW_HANDLER, H_CA | (g32(w, CW_HANDLER) & 1)); cam->cut = 0; cam->pers = 224; cam->pm_on = 0; }      /* normal_ca, set_game_pers */
        else { p32(w, CW_HANDLER, H_ECAMI | (g32(w, CW_HANDLER) & 1)); cam->cut = 1; }
    } else if (d->rp_chase && xyz) d->g1b = (d->g1b + 2) & 2;
}

void camera_replay_setup(Camera *cam, const CamDat *dat, Drive *dummy)
{
    cam->replay = cam->cut = 1;
    cam->camdat = *dat;
    cam->dummy = dummy;
    cam->pers = 0x200;
}

void camera_init(Camera *cam)
{
    memset(cam, 0, sizeof *cam);
    p32(cam->w, CW_HANDLER, H_CAINIT);
    cam->fade = -1;
}

void camera_pass(Camera *cam, Drive *d)
{
    unsigned char *w = cam->w;
    unsigned char *car = d->car;
    if (cam->cut) {
        if (d->cam_mode != 2) { replay_pass(cam, d); return; }
        cam->cut = 0;                                                                   /* the goal: the orbit of the chase camera's handler (normal_ca) takes over */
        cam->pm_on = 0;
        p32(w, CW_HANDLER, H_CA | (g32(w, CW_HANDLER) & 1));
    }
    cam->fade = -1;
    int h = g32(w, CW_HANDLER) & ~1;
    if (h == H_CAINIT) {                                                                /* normal_cainit 0x0600FA5C */
        p32(w, CW_HANDLER, H_CA | (g32(w, CW_HANDLER) & 1));
        spline_init(cam, d);
        p32(w, CW_EYEOFF, 0); p32(w, CW_EYEOFF + 4, 0); p32(w, CW_EYEOFF + 8, 0);
        return;
    }
    if (h != H_CA) return;
    lag_filter(car);                                                                    /* normal_ca 0x0600FAF0 */
    int m[12];
    unit_matrix(m);
    if (d->cfg.mirror) { m[0] = neg(m[0]); m[4] = neg(m[4]); m[8] = neg(m[8]); }       /* matrix::reverse_x */
    switch (d->cam_mode) {
    case 4: flyby(cam, d, m); break;
    case 2: orbit(cam, d, m); break;
    default: {
        int sel = d->g1b;
        if (d->cam_mode == 6) {                                                         /* the 3-pass pause after the fly-by */
            int n = g32(w, CW_CNT) - 1;
            p32(w, CW_CNT, n);
            if (n == 0) { cam->fade = 0; d->cam_mode = 0; sel = 0; }                    /* (the view byte is read from the mode byte in that pass: view A) */
        }
        if (sel == 0) view_a(w, car);
        else view_b(w, car);
        place(cam, d, m);
        view_matrix(cam, d, m);
        break; }
    }
    memcpy(cam->view, m, sizeof m);
    memcpy(cam->view_copy, m, sizeof m);
    d->cam_ang1 = gw(w, CW_ANG + 2);                                                    /* [0x060AA9C6] / [0x060AA9CC]: what calc_dispang2 reads */
    d->cam_ang2 = gw(w, CW_EXTRA + 2);
}

/* ---- the sky (RBG0 rotation parameters), scroll module 0x06015F6E .. 0x06016040 ---------------------------------------------------------------------------------- */
void camera_sky_init(Camera *cam, int course)
{
    static const int base[4] = { 960, 960, 784, 960 };                                  /* mova table 0x06015DC0 (px) */
    cam->sky_base = base[course & 3] << 16;
    cam->sky[0] = cam->sky[3] = 0x10000; cam->sky[1] = cam->sky[2] = 0;
    cam->sky[4] = cam->sky_base;
    cam->sky_t[0] = cam->sky_base; cam->sky_t[1] = 0x10000; cam->sky_t[2] = cam->sky_t[3] = 0;
}

void camera_sky_pass(Camera *cam, const Drive *d)
{
    const unsigned char *w = cam->w;
    int yaw = gw(w, 0x16) & 0xFFFF, roll = gw(w, 0x18);
    if (d->cfg.mirror) { yaw = (-yaw) & 0xFFFF; roll = -roll; }                          /* (the original negates the word, then extends it unsigned) */
    cam->sky_t[0] = (int)(((((u32)(yaw & 0xFFC0)) << 10) + (u32)cam->sky_base) & 0x03FF0000u);              /* set_grpara2: px = base + (yaw >> 6), wrapped at 1024 */
    if (roll == 0) { cam->sky_t[1] = 0x10000; cam->sky_t[2] = 0; cam->sky_t[3] = 0; }
    else {
        int ang = -(roll >> 1), s, c;
        fx_sincos(d->phys, ang, &s, &c);                                                 /* matrix::sincos of -(roll / 2) */
        cam->sky_t[1] = c; cam->sky_t[2] = -s; cam->sky_t[3] = s;
    }
    /* set_grpara_int: the table moves half way to the targets, Mx around the 1024 px circle */
    int cur = cam->sky[4], tg = cam->sky_t[0], diff = cur - tg;
    if (diff < 0) diff = -diff;
    if (diff > 0x02000000) cur += 0x04000000;
    cam->sky[4] = ((tg + cur) >> 1) & 0x03FF0000;
    cam->sky[0] = cam->sky[3] = (cam->sky[0] + cam->sky_t[1]) >> 1;
    cam->sky[1] = (cam->sky[1] + cam->sky_t[2]) >> 1;
    cam->sky[2] = (cam->sky[2] + cam->sky_t[3]) >> 1;
}
