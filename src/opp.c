/* Sega Rally PSP - the other cars (M7), see opp.h.  Every routine names the original's address; the arithmetic is the original's (16.16, 64-bit products, the divider). */
#include <string.h>
#include "opp.h"
#include "oppmath.h"

/* the other cars' slot fields (docs/collision_world_ai.md section 6) */
enum {
    A_FN0 = 0x00, A_FN4 = 0x04, A_ROTX = 0x0C, A_ROTY = 0x10, A_ROTZ = 0x14, A_POS = 0x18, A_VEL = 0x24, A_STEER = 0x30, A_WZ = 0x34, A_WACC = 0x38, A_LOAD = 0x3C, A_WY = 0x4C,
    A_TYPE = 0x5C, A_SEG = 0x60, A_PSEG = 0x62, A_PUSH = 0x64, A_AVEL = 0x70, A_K7C = 0x7C, A_K80 = 0x80, A_K84 = 0x84, A_K88 = 0x88, A_DYN = 0x90, A_SURF = 0xB4, A_RANK = 0xB6,
    A_DIRX = 0xB8, A_DIRZ = 0xBC, A_FLAGS = 0xC6, A_LAP = 0xCA, A_SPD2 = 0xEC, A_BLK = 0xF0, A_STATE = 0xF5, A_SPDST = 0x100, A_LODC = 0x102, A_LODR = 0x104, A_CHG = 0x108, A_GIDX = 0x10A,
    A_LATX = 0x10C, A_GH = 0x110, A_LATZ = 0x114, A_LATT = 0x118, A_HDX = 0x11C, A_HDY = 0x120, A_HDZ = 0x124, A_SPEED = 0x128, A_VX0 = 0x12C, A_VY0 = 0x130, A_VZ0 = 0x134,
    A_SLIP = 0x138, A_SLIPD = 0x13C, A_TGT0 = 0x140, A_TGT1 = 0x144, A_TGT2 = 0x148, A_ACC = 0x14C, A_BRK = 0x150, A_AHEAD = 0x154, A_BEHIND = 0x158, A_LANE = 0x15C, A_GUIDE = 0x160,
    A_TANX = 0x164, A_TANY = 0x168, A_TANZ = 0x16C, A_SPDS = 0x170, A_ACCP = 0x190, A_MASK = 0x194
};
enum { H_FIRST = 0x0600C958, H_EXEC = 0x06009E8C, H_DRAW = 0x06006988, H_NODRAW = 0x06006990 };
enum { G100 = 0x02DEC08F, G16C = 0x03840000 };                        /* [0x060AA100] / [0x060AA16C]: constants of the console's work RAM (init_game_work) */

unsigned opp_stats[4];

typedef struct { const Phys *ph; const Coli *co; Drive *pl; Opp *o; } Ctx;

static inline int ph32(const Ctx *x, unsigned a) { return phys_r32(x->ph, a); }
static inline int rd32le(const unsigned char *p) { return (int)((u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24); }
static inline int dyn32(const Ctx *x, const unsigned char *c, int off) { return phys_r32(x->ph, (unsigned)g32(c, A_DYN) + (unsigned)off); }       /* [car+0x90] = the surface dynamics table */
static inline int sra8(int v) { return v >> 8; }                                                                                            /* shlr8 + or 0xFF000000 for negative values */

static unsigned char *car_at(Ctx *x, unsigned addr)
{
    if (addr == OPP_PLAYER_ADDR) return x->pl->car;
    return x->o->car[(addr - OPP_SLOT_ADDR(0)) / OPP_SLOT_BYTES];
}

static void get_vec(const unsigned char *c, int off, int v[3]) { v[0] = g32(c, off); v[1] = g32(c, off + 4); v[2] = g32(c, off + 8); }

/* car::get_block2 0x06008360: the block and the road section of the car's position */
static void get_block2(Ctx *x, unsigned char *c)
{
    int px = g32(c, A_POS), pz = g32(c, A_POS + 8);
    const unsigned char *q = coli_find_quad(x->co, px, pz);
    if (!q) return;
    p8(c, A_BLK, q[3]);
    const unsigned char *s = coli_find_slice(x->co, q[3], px, pz);
    if (!s) return;
    p16(c, A_PSEG, g16u(c, A_SEG));
    p16(c, A_SEG, s[3]);
    p32(c, A_DIRX, rd32le(s + 36));
    p32(c, A_DIRZ, rd32le(s + 40));
}

/* othercar::setMark 0x0600A3F0: where the car is on the guide line.  Fills `M` (3 x 4) with the path frame (the caller's 48-byte stack matrix) */
static void set_mark(Ctx *x, unsigned char *c, int M[12])
{
    unsigned tbl = x->o->guide;
    int idx = g16u(c, A_GIDX);
    unsigned P = tbl + 16u * (unsigned)idx;
    int p0x = ph32(x, P), p0z = ph32(x, P + 8), p1x = ph32(x, P + 16), p1z = ph32(x, P + 24);
    int ax = p1x - p0x, az = p1z - p0z;
    int bx = g32(c, A_POS) - p0x, bz = g32(c, A_POS + 8) - p0z;
    i64 dot = (i64)ax * bx + (i64)az * bz;
    int den = X16((i64)ax * ax + (i64)az * az);
    int t = div64(dot, den);
    if (t < 0) t = 0;
    if (t > 0x10000) {
        int k = (int)((u32)t >> 16);
        int i2 = idx + k;
        if (i2 >= 300) i2 -= 300;
        p16(c, A_GIDX, i2);
        t -= k << 16;
    }
    idx = g16u(c, A_GIDX);
    P = tbl + 16u * (unsigned)idx;
    int y0 = ph32(x, P + 4), y1 = ph32(x, P + 20);
    p32(c, A_GH, FM(y1 - y0, t) + y0);
    int u = t + 0x8000, i3 = idx;
    if (u > 0x10000) {
        i3 += 1;
        u -= 0x10000;
        if (i3 >= 300) i3 = 0;
    }
    unsigned Pb = tbl + 16u * (unsigned)i3;
    int lat = g32(c, A_LATT);
    int u2 = FM(u, u);
    int w1 = u + 0x8000 - u2, w2 = u2 >> 1, w0 = w2 - u + 0x8000;
    int d0 = u - 0x10000, d1 = (neg(u) << 1) + 0x10000, d2 = u;
    int px[3], pz[3];
    for (int k = 0; k < 3; k++) { px[k] = ph32(x, Pb + 16u * (unsigned)k); pz[k] = ph32(x, Pb + 16u * (unsigned)k + 8); }
    int X = X16((i64)w0 * px[0] + (i64)w1 * px[1] + (i64)w2 * px[2]);
    int Z = X16((i64)w0 * pz[0] + (i64)w1 * pz[1] + (i64)w2 * pz[2]);
    int dX = X16((i64)d0 * px[0] + (i64)d1 * px[1] + (i64)d2 * px[2]);
    int dZ = X16((i64)d0 * pz[0] + (i64)d1 * pz[1] + (i64)d2 * pz[2]);
    int tan[3] = { dX, 0, dZ };
    m_unit(M);
    c_turnv(M, tan);
    int tr1[3] = { neg(X), 0, neg(Z) };
    m_translate(M, tr1);
    int M2[12];
    m_unit(M2);
    int tr2[3] = { X, 0, Z };
    m_translate(M2, tr2);
    c_turnd(M2, tan);
    int src[3] = { lat, 0, 0 }, out[3];
    calc_point(M2, src, out);
    p32(c, A_LATX, out[0]);
    p32(c, A_LATZ, out[2]);
    p32(c, A_TANX, dX);
    p32(c, A_TANY, 0);
    p32(c, A_TANZ, dZ);
}

/* othersub::getAngle 0x0600ADCC: the angles (pitch, yaw, roll) of a car moving along `up` (a velocity) over a surface with the normal `dir` */
static void get_angle(const Ctx *x, const int dir[3], const int up[3], int out[3])
{
    int d8[3] = { sra8(dir[0]), sra8(dir[1]), sra8(dir[2]) };
    int u[3], s[3], n[3], m[12], v[3];
    c_reg(up, u);
    cross16(d8, u, s);
    cross16(s, d8, n);
    out[1] = r_atan16(n[2], neg(n[0]));
    m_unit(m);
    r_rotate_y(x->ph, m, neg(out[1]));
    calc_point(m, n, v);
    out[0] = r_atan16(v[2], v[1]);
    m_unit(m);
    r_rotate_x(x->ph, m, neg(out[0]));
    r_rotate_y(x->ph, m, neg(out[1]));
    calc_point(m, s, v);
    out[2] = r_atan16(v[0], v[1]);
}

/* othersub::setVelocity 0x0600AEB4: the car's speed approaches the target speed, its heading the target direction */
static void set_velocity(Ctx *x, unsigned char *c)
{
    if (!(g32(c, A_LOAD) + g32(c, A_LOAD + 4) > 0)) return;                    /* [+0x3C] + [+0x40] */
    int m[12], pos[3], src[3] = { 0, (int)0xFFFF9999, 0x14000 }, out[3];
    m_unit(m);
    get_vec(c, A_POS, pos);
    m_translate(m, pos);
    r_rotate_y(x->ph, m, g32(c, A_ROTY));
    r_rotate_x(x->ph, m, g32(c, A_ROTX));
    r_rotate_z(x->ph, m, g32(c, A_ROTZ));
    calc_point(m, src, out);
    int tx = g32(c, A_LATX) - out[0];
    int tz = g32(c, A_LATZ) - out[2];
    int hv[3] = { tx, 0, tz }, hu[3];
    p32(c, A_HDX, tx); p32(c, A_HDY, 0); p32(c, A_HDZ, tz);
    c_reg(hv, hu);
    p32(c, A_HDX, hu[0]);
    p32(c, A_HDZ, hu[2]);
    int tgt = FM(g32(c, A_TGT2), 0x25E);
    int vv[3] = { g32(c, A_VEL), 0, g32(c, A_VEL + 8) };
    int k = tgt > g32(c, A_SPEED) ? g32(c, A_ACC) : g32(c, A_BRK);
    int len = c_vecval(vv);
    int spd = tgt - FM(k, tgt - len);
    p32(c, A_SPEED, spd);
    int vu[3];
    c_reg(vv, vu);
    int dot = X16((i64)vu[0] * g32(c, A_HDX) + (i64)vu[2] * g32(c, A_HDZ));
    int r2 = 0x10000 - dot, r3 = 0x10000;
    if (r3 >= r2) r3 = r2;
    int nv[3] = { vu[0] + FM(g32(c, A_HDX) - vu[0], r3), 0, vu[2] + FM(g32(c, A_HDZ) - vu[2], r3) }, nu[3];
    c_reg(nv, nu);
    p32(c, A_VEL, FM(spd, nu[0]));
    p32(c, A_VEL + 8, FM(spd, nu[2]));
}

/* the part of othercar::execOtherCar's LOD 0 / 1 start that follows setMark: FUN_0600A374 (the visual steering / wheel turn from the sideways speed) */
static void other_slip(Ctx *x, unsigned char *c)
{
    int m[12], v[3] = { g32(c, A_VEL), 0, g32(c, A_VEL + 8) }, o[3];
    m_unit(m);
    r_rotate_y(x->ph, m, neg(g32(c, A_ROTY)));
    calc_point(m, v, o);                                                       /* c_point_rr_xz: the local x and z of the velocity (rows 0 and 2 only) */
    int lz = o[2];
    p32(c, A_WZ, lz);
    p32(c, A_WACC, g32(c, A_WACC) + lz);
    int slip = g32(c, A_SLIP), as = slip < 0 ? neg(slip) : slip, st = 0;
    if (as > 0x8ED && g32(c, A_SPEED) > 0xBDA1) st = FM(slip, (int)0xFFFF3334);
    p32(c, A_STEER, st);
}

/* othersub::OtherCarCtrl 0x0600AB60: the driver.  M = the path frame of setMark */
static void other_car_ctrl(Ctx *x, unsigned char *c, const int M[12])
{
    int F[34];
    memset(F, 0, sizeof F);
    int pos[3] = { g32(c, A_POS), 0, g32(c, A_POS + 8) }, o[3];
    calc_point(M, pos, o);
    F[10] = o[0]; F[12] = o[2];                                                /* the car in the path frame (x, z) */
    const unsigned char *ahead = car_at(x, (unsigned)g32(c, A_AHEAD));
    int ap[3] = { g32(ahead, A_POS), 0, g32(ahead, A_POS + 8) };
    calc_point(M, ap, o);
    F[13] = o[0]; F[15] = o[2];
    int r9 = 0;
    F[16] = g32(c, A_VEL); F[17] = 0; F[18] = g32(c, A_VEL + 8);
    int u3[3];
    c_reg(&F[16], u3);
    F[16] = u3[0]; F[18] = u3[2];
    unsigned tbl = (unsigned)g32(c, A_GUIDE);
    unsigned r10 = tbl + 16u * (unsigned)g16u(c, A_GIDX);
    for (int k = 0; k < 2; k++, r10 += 32) {
        F[19] = ph32(x, r10 + 16) - ph32(x, r10);
        F[21] = ph32(x, r10 + 24) - ph32(x, r10 + 8);
        F[20] = 0;
        c_reg(&F[19], u3);
        F[19] = u3[0]; F[21] = u3[2];
        r9 += c_outp2(&F[16], &F[19]);
        F[16] = F[19]; F[17] = F[20]; F[18] = F[21];
    }
    r9 >>= 1;
    int curv = r9 < 0 ? neg(r9) : r9;
    F[6] = curv;
    F[9] = g32(c, A_TGT0);
    F[7] = g32(c, A_LATT);
    F[8] = 0x10000;
    const unsigned char *behind = car_at(x, (unsigned)g32(c, A_BEHIND));
    int dx = g32(c, A_POS) - g32(behind, A_POS), dz = g32(c, A_POS + 8) - g32(behind, A_POS + 8);
    int d2 = HM(dx, dx) + HM(dz, dz);
    int lane = g32(c, A_LANE);
    if (!(d2 > 30)) {                                                          /* the car behind is close */
        F[3] = dx; F[4] = 0; F[5] = dz;
        F[0] = g32(behind, A_VEL) - g32(c, A_VEL);
        F[1] = 0;
        F[2] = g32(behind, A_VEL + 8) - g32(c, A_VEL + 8);
        c_reg(&F[0], u3); F[0] = u3[0]; F[2] = u3[2];
        c_reg(&F[3], u3); F[3] = u3[0]; F[5] = u3[2];
        if (inner16(&F[3], &F[0]) > 0) {
            int r3 = c_outp2(&F[3], &F[0]);
            if (r3 < 0) r3 = neg(r3);
            if (r3 > 0x2C8B) { F[7] = r3; F[8] = 0xFF7C; }
        }
    } else {
        F[7] = F[13] >= 0 ? neg(lane) : lane;
        F[8] = 0xFF7C;
    }
    int i20 = g16u(c, A_GIDX) + 20;
    if (i20 >= 300) i20 -= 300;
    int gp[3] = { ph32(x, tbl + 16u * (unsigned)i20), ph32(x, tbl + 16u * (unsigned)i20 + 4), ph32(x, tbl + 16u * (unsigned)i20 + 8) }, cp[3];
    get_vec(c, A_POS, cp);
    int ld = c_getr(cp, gp);
    if (0xC80000 >= ld) { r9 = ld; } else r9 = 0xC80000;
    int st = g16(c, A_SPDST);
    int enter = 0;
    if (st == 0) {
        if (curv > 0x1999 || 0xBE0000 > r9) enter = 1;
        if (enter) {
            int t0 = g32(c, A_TGT0);
            if (t0 > 0) { p32(c, A_TGT1, t0); p16(c, A_SPDST, 1); }
        }
    } else if (st == 1) {
        int r0 = curv * 300;
        r9 >>= 2;
        int r8 = 0x960000 + r9 - r0;
        int r3 = ph32(x, 0x0600D200);
        if (r3 > r8) r8 = r3;
        int t1 = g32(c, A_TGT1);
        if (t1 > r8) p32(c, A_TGT1, r8); else r8 = t1;
        F[9] = r8;
        if (0x1999 > curv) p16(c, A_SPDST, 0);
    }
    if (g32(c, A_LOAD) + g32(c, A_LOAD + 4) > 0) {
        /* [0x060AA02C] is never written: 0 */
        p32(c, A_TGT2, F[9]);
    } else {
        int r4 = F[10], r2 = r4, r1 = lane, r0;
        if (r4 < 0) { r2 = neg(r4); r1 = neg(lane); }
        r0 = (lane > r2) ? r4 : r1;
        p32(c, A_LATT, r0);
    }
}

/* othersub::tinyOther 0x0600A5F4: LOD 2 (and the end of the LOD 3 → 2 move): the car snaps to the road surface */
static void tiny_other(Ctx *x, unsigned char *c)
{
    int F[12], nrm[3], h;
    memset(F, 0, sizeof F);
    int px = g32(c, A_POS) + g32(c, A_VEL), py = g32(c, A_POS + 4), pz = g32(c, A_POS + 8) + g32(c, A_VEL + 8);
    F[5] = px; F[6] = py; F[7] = pz;
    int type = coli_get2(x->co, px, py, pz, nrm, &h);
    F[0] = nrm[0]; F[1] = nrm[1]; F[2] = nrm[2]; F[3] = h;
    int t2 = (type << 4) | type;
    p16(c, A_SURF, (t2 << 8) | t2);
    if (type == 0) {
        int d = F[3], nx = F[0], nz = F[2];
        int r4 = d << 8;
        p32(c, A_PUSH, g32(c, A_PUSH) + (HM(r4, nx) >> 1));
        p32(c, A_PUSH + 8, g32(c, A_PUSH + 8) + (HM(r4, nz) >> 1));
        int r2 = 0xFF3B - FM(d, dyn32(x, c, 48));
        if (r2 < 0) r2 = 0;
        F[11] = FM(r2, dyn32(x, c, 44));
        int tn[3] = { F[0], F[1], F[2] }, tg[3] = { g32(c, A_TANX), g32(c, A_TANY), g32(c, A_TANZ) };
        int oc = c_outp2(tn, tg);
        F[1] = oc < 0 ? neg(oc) : oc;
        F[6] = g32(c, A_GH);
    } else {
        F[6] += F[3] >> 4;
        F[11] = dyn32(x, c, 44);
    }
    p32(c, A_POS, HM(F[0], 0x4CCCC0) + F[5]);
    p32(c, A_POS + 4, HM(F[1], 0x4CCCC0) + F[6]);
    p32(c, A_POS + 8, HM(F[2], 0x4CCCC0) + F[7]);
    for (int k = 0; k < 4; k++) p32(c, A_LOAD + 4 * k, 0x19999);
    int vel[3], ang[3];
    get_vec(c, A_VEL, vel);
    int nn[3] = { F[0], F[1], F[2] };
    get_angle(x, nn, vel, ang);
    F[8] = ang[0]; F[9] = ang[1]; F[10] = ang[2];
    if (g16(c, A_CHG) == 0) {
        int dd;
        dd = regular_angle(g32(c, A_ROTX) - F[8]); F[8] += FM(dd, 0xF333);
        dd = regular_angle(g32(c, A_ROTZ) - F[10]); F[10] += FM(dd, 0xF333);
        dd = regular_angle(g32(c, A_ROTY) - F[9]);
        i64 prod = (i64)dd * 0xE666;
        F[9] += X16(prod);
        F[10] = (int)(prod >> 32);                                             /* the original's yaw block keeps its high word in the register that holds the roll: the roll becomes 0 / -1 */
    }
    p32(c, A_ROTX, F[8]); p32(c, A_ROTY, F[9]); p32(c, A_ROTZ, F[10]);
    int s = r_sin16(x->ph, g32(c, A_ROTX));
    int r0 = s + (s >> 2);
    int y = F[6];
    p32(c, A_WY, y + r0); p32(c, A_WY + 4, y + r0); p32(c, A_WY + 8, y - r0); p32(c, A_WY + 12, y - r0);
    int dmp = F[11];
    p32(c, A_VEL, FM(dmp, g32(c, A_VEL)));
    p32(c, A_VEL + 4, FM(dmp, g32(c, A_VEL + 4)));
    p32(c, A_VEL + 8, FM(dmp, g32(c, A_VEL + 8)));
}

/* othersub::simpleOther 0x0600A7F4: LOD 1 - the car follows the road with one ground probe under its centre */
static void simple_other(Ctx *x, unsigned char *c)
{
    int F[27];
    memset(F, 0, sizeof F);
    int vx = g32(c, A_VEL), vz = g32(c, A_VEL + 8), px = g32(c, A_POS), pz = g32(c, A_POS + 8);
    int r0 = g32(c, A_PUSH) >> 1;
    px += r0; vx += r0;
    r0 = g32(c, A_PUSH + 8) >> 1;
    pz += r0; vz += r0;
    p32(c, A_VEL, vx); p32(c, A_VEL + 8, vz); p32(c, A_POS, px); p32(c, A_POS + 8, pz);
    p32(c, A_PUSH, 0); p32(c, A_PUSH + 4, 0); p32(c, A_PUSH + 8, 0);
    p32(c, A_VX0, vx); p32(c, A_VZ0, vz); p32(c, A_VY0, g32(c, A_VEL + 4));
    set_velocity(x, c);
    int np[3] = { g32(c, A_POS) + g32(c, A_VEL), g32(c, A_POS + 4) + g32(c, A_VEL + 4), g32(c, A_POS + 8) + g32(c, A_VEL + 8) };
    int M[12], src[3] = { 0, (int)0xFFFF9999, 0 }, wp[3], nrm[3], h;
    m_unit(M);
    m_translate(M, np);
    r_rotate_y(x->ph, M, g32(c, A_ROTY));
    r_rotate_x(x->ph, M, g32(c, A_ROTX));
    r_rotate_z(x->ph, M, g32(c, A_ROTZ));
    calc_point(M, src, wp);
    int type = coli_get2(x->co, wp[0], wp[1], wp[2], nrm, &h);
    F[0] = nrm[0]; F[1] = nrm[1]; F[2] = nrm[2]; F[3] = h;
    int t2 = (type << 4) | type;
    p16(c, A_SURF, (t2 << 8) | t2);
    if (type == 0) {
        int d = F[3], nx = sra8(F[0]), nz = sra8(F[2]);
        p32(c, A_PUSH, g32(c, A_PUSH) + (FM(d, nx) >> 1));
        p32(c, A_PUSH + 8, g32(c, A_PUSH + 8) + (FM(d, nz) >> 1));
        int r2 = 0xFF3B - FM(d, dyn32(x, c, 48));
        if (r2 < 0) r2 = 0;
        F[14] = FM(r2, dyn32(x, c, 44));
        int tn[3] = { F[0], F[1], F[2] }, tg[3] = { g32(c, A_TANX), g32(c, A_TANY), g32(c, A_TANZ) };
        int oc = c_outp2(tn, tg);
        F[1] = oc < 0 ? neg(oc) : oc;
        for (int k = 0; k < 4; k++) p32(c, A_LOAD + 4 * k, 0x19999);
        int vel[3], rot[3], nn[3] = { F[0], F[1], F[2] };
        get_vec(c, A_VEL, vel);
        get_angle(x, nn, vel, rot);
        p32(c, A_ROTX, rot[0]); p32(c, A_ROTY, rot[1]); p32(c, A_ROTZ, rot[2]);
    } else {
        F[14] = dyn32(x, c, 44);
        int h7 = F[3];
        if (h7 < 0) {
            for (int k = 0; k < 4; k++) p32(c, A_LOAD + 4 * k, 0);
        } else {
            int k1 = dyn32(x, c, 20), k2 = dyn32(x, c, 24);
            int r3 = HM(G100, h7);
            int r2 = FM(r3, k2 + k1);
            int r12 = r2 * 200;
            if (h7 > 0x33333) {
                int r1 = FM(h7 - 0x33333, g32(c, A_K7C));
                r12 += r1;
                h7 = 0x33333;
            }
            int r0v = g32(c, A_LOAD) - h7;
            int r1 = dyn32(x, c, 28) + dyn32(x, c, 32);
            r1 = FM(r0v, r1);
            r1 = FM(r1, G16C << 1);
            r1 = HM(r1, G100);
            r12 -= r1;
            if (r12 < 0) r12 = 0;
            int ny = F[1] >> 8;
            int q = div64((i64)ny * r12, g32(c, A_K7C));
            for (int k = 0; k < 4; k++) p32(c, A_LOAD + 4 * k, h7);
            p32(c, A_VEL + 4, g32(c, A_VEL + 4) + (q >> 4));
        }
        p32(c, A_VEL + 4, g32(c, A_VEL + 4) - (int)((u32)G100 >> 16));
        int vel[3], rot[3], nn[3] = { F[0], F[1], F[2] };
        get_vec(c, A_VEL, vel);
        get_angle(x, nn, vel, rot);
        int a0 = regular_angle(rot[0]), a1 = regular_angle(rot[1]), a2 = regular_angle(rot[2]);
        int dd;
        dd = regular_angle(g32(c, A_ROTX) - a0); p32(c, A_ROTX, a0 + FM(dd, 0xE666));
        dd = regular_angle(g32(c, A_ROTZ) - a2); p32(c, A_ROTZ, a2 + FM(dd, 0xE666));
        dd = regular_angle(g32(c, A_ROTY) - a1); p32(c, A_ROTY, a1 + FM(dd, 0xE666));
        F[3] >>= 4;
    }
    int s = r_sin16(x->ph, g32(c, A_ROTX));
    int r1 = s + (s >> 2);
    int r2 = F[3] + wp[1];
    p32(c, A_WY, r2 + r1); p32(c, A_WY + 4, r2 + r1); p32(c, A_WY + 8, r2 - r1); p32(c, A_WY + 12, r2 - r1);
    int vel[3], pos[3];
    get_vec(c, A_VEL, vel);
    get_vec(c, A_POS, pos);
    for (int k = 0; k < 3; k++) p32(c, A_POS + 4 * k, pos[k] + vel[k]);
    for (int k = 0; k < 3; k++) p32(c, A_VEL + 4 * k, FM(F[14], vel[k]));
}

/* the LOD 3 block of execOtherCar (0x0600A194): a far car moves along the guide line at the target speed, no collisions */
static void far_car(Ctx *x, unsigned char *c)
{
    p32(c, A_TGT2, g32(c, A_TGT0));
    int px = g32(c, A_POS) + g32(c, A_PUSH), pz = g32(c, A_POS + 8) + g32(c, A_PUSH + 8);
    p32(c, A_POS, px); p32(c, A_POS + 8, pz);
    p32(c, A_PUSH, 0); p32(c, A_PUSH + 4, 0); p32(c, A_PUSH + 8, 0);
    p32(c, A_VX0, g32(c, A_VEL)); p32(c, A_VX0 + 4, g32(c, A_VEL + 4)); p32(c, A_VX0 + 8, g32(c, A_VEL + 8));
    unsigned g = (unsigned)g32(c, A_GUIDE);
    int idx = g16u(c, A_GIDX);
    unsigned P = g + 16u * (unsigned)idx;
    int p0x = ph32(x, P), p0z = ph32(x, P + 8);
    int sx = ph32(x, P + 16) - p0x, sz = ph32(x, P + 24) - p0z;
    int rx = px - p0x, rz = pz - p0z;
    i64 dot = (i64)sx * rx + (i64)rz * sz;
    int len2 = X16((i64)sx * sx + (i64)sz * sz);
    int t = div64(dot, len2);
    if (t < 0) t = 0;
    if (t > 0x10000) {
        int k = (int)((u32)t >> 16);
        idx += k;
        if (idx >= 300) idx -= 300;
        p16(c, A_GIDX, idx);
        t -= k << 16;
    }
    idx = g16u(c, A_GIDX);
    int u = t + 0x8000, i2 = idx + 1;
    if (u > 0x10000) { i2 += 1; u -= 0x10000; }
    int om = 0x10000 - u;
    unsigned Q = g + 16u * (unsigned)i2;
    int F[3];
    F[0] = X16((i64)ph32(x, Q) * om + (i64)ph32(x, Q + 16) * u) - px;
    F[1] = X16((i64)ph32(x, Q + 4) * om + (i64)ph32(x, Q + 20) * u) - g32(c, A_POS + 4);
    F[2] = X16((i64)ph32(x, Q + 8) * om + (i64)ph32(x, Q + 24) * u) - pz;
    int hd[3];
    c_reg(F, hd);
    p32(c, A_HDX, hd[0]); p32(c, A_HDY, hd[1]); p32(c, A_HDZ, hd[2]);
    int spd = FM(g32(c, A_TGT2), 0x25E);
    p32(c, A_SPEED, spd);
    p32(c, A_VEL + 8, FM(hd[2], spd));
    p32(c, A_VEL + 4, FM(hd[1], spd));
    p32(c, A_VEL, FM(hd[0], spd));
    p32(c, A_POS, g32(c, A_POS) + g32(c, A_VEL));
    p32(c, A_POS + 4, g32(c, A_POS + 4) + g32(c, A_VEL + 4));
    p32(c, A_POS + 8, g32(c, A_POS + 8) + g32(c, A_VEL + 8));
    p16(c, A_SURF, 0x1111);
    p32(c, A_ROTY, r_atan16(hd[2], neg(hd[0])));
    int y = g32(c, A_POS + 4) + (int)0xFFFFB333;
    for (int k = 0; k < 4; k++) p32(c, A_WY + 4 * k, y);
}

/* the LOD 2 block of execOtherCar (0x0600A00E) + tinyOther */
static void near_car(Ctx *x, unsigned char *c)
{
    int F[15];
    memset(F, 0, sizeof F);
    set_mark(x, c, F);
    int *M2 = &F[3];
    m_unit(M2);
    r_rotate_y(x->ph, M2, neg(g32(c, A_ROTY)));
    int v[3] = { g32(c, A_VEL), 0, g32(c, A_VEL + 8) }, o[3];
    calc_point(M2, v, o);
    p32(c, A_WZ, o[2]);
    p32(c, A_WACC, g32(c, A_WACC) + o[2]);
    p32(c, A_TGT2, g32(c, A_TGT0));
    int r1 = g32(c, A_PUSH) >> 1, r2 = g32(c, A_PUSH + 8) >> 1;
    p32(c, A_POS, g32(c, A_POS) + r1); p32(c, A_POS + 8, g32(c, A_POS + 8) + r2);
    p32(c, A_VEL, g32(c, A_VEL) + r1); p32(c, A_VEL + 8, g32(c, A_VEL + 8) + r2);
    p32(c, A_PUSH, 0); p32(c, A_PUSH + 4, 0); p32(c, A_PUSH + 8, 0);
    p32(c, A_VX0, g32(c, A_VEL)); p32(c, A_VZ0, g32(c, A_VEL + 8));
    F[0] = g32(c, A_LATX) - g32(c, A_POS);
    F[2] = g32(c, A_LATZ) - g32(c, A_POS + 8);
    int hd[3];
    c_reg(F, hd);
    p32(c, A_HDX, hd[0]); p32(c, A_HDY, hd[1]); p32(c, A_HDZ, hd[2]);
    F[0] = g32(c, A_VEL); F[1] = 0; F[2] = g32(c, A_VEL + 8);
    int len = c_vecval(F);
    int tgt = FM(g32(c, A_TGT2), 0x25E);
    int k = tgt > g32(c, A_SPEED) ? g32(c, A_ACC) : g32(c, A_BRK);
    int d5 = tgt - len;
    p32(c, A_SPEED, tgt - FM(d5, k));
    int vu[3];
    c_reg(F, vu);
    int dot = X16((i64)g32(c, A_HDX) * vu[0] + (i64)g32(c, A_HDZ) * vu[2]);
    int r7 = 0x10000 - dot;
    if (!(0x10000 > r7)) r7 = 0x10000;
    int nu[3];
    F[0] = vu[0] + FM(g32(c, A_HDX) - vu[0], r7);
    F[2] = vu[2] + FM(g32(c, A_HDZ) - vu[2], r7);
    c_reg(F, nu);
    p32(c, A_VEL, FM(nu[0], g32(c, A_SPEED)));
    p32(c, A_VEL + 8, FM(nu[2], g32(c, A_SPEED)));
    tiny_other(x, c);
}

static void set_other_car(Ctx *x, unsigned char *c);

/* othercar::execOtherCar 0x06009E8C: the move handler of the other cars */
static void exec_other_car(Ctx *x, unsigned char *c)
{
    const Phys *ph = x->ph;
    unsigned guide = (unsigned)g32(c, A_GUIDE);
    unsigned acc = (unsigned)g32(c, A_ACCP);
    if (!(0x7684 > g32(c, A_SPEED))) acc += 8;
    p32(c, A_ACC, phys_r32(ph, acc));
    p32(c, A_BRK, phys_r32(ph, acc + 4));
    int w = phys_r8(ph, guide + 16u * (unsigned)g16u(c, A_GIDX) + 12) & 7;
    p32(c, A_LANE, phys_r32(ph, 0x0600A354u + 4u * (unsigned)w));
    p16(c, A_CHG, 0);
    int req = g16(c, A_LODR), cur = g16(c, A_LODC);
    if (req != cur && g32(c, A_LOAD) + g32(c, A_LOAD + 4) > 0) {
        int r9 = req;
        if (cur > req) r9 = cur - 1;
        p16(c, A_CHG, 1);
        switch (r9) {
        case 0:
            p16(c, A_SPDST, 0);
            p32(c, A_AVEL, 0); p32(c, A_AVEL + 4, 0); p32(c, A_AVEL + 8, 0);
            p32(c, A_SLIP, 0); p32(c, A_SLIPD, 0);
            break;
        case 1: break;
        case 2:
            if (cur == 3) {
                int pos[3], nrm[3], h, rot[3], vel[3];
                get_vec(c, A_POS, pos);
                coli_get2(x->co, pos[0], pos[1], pos[2], nrm, &h);
                get_vec(c, A_VEL, vel);
                get_angle(x, nrm, vel, rot);
                p32(c, A_ROTX, rot[0]); p32(c, A_ROTY, rot[1]); p32(c, A_ROTZ, rot[2]);
                p32(c, A_POS, pos[0] + HM(0x666660, nrm[0]));
                p32(c, A_POS + 4, pos[1] + HM(0x666660, nrm[1]) + (rot[0] >> 4));
                p32(c, A_POS + 8, pos[2] + HM(0x666660, nrm[2]));
            }
            break;
        default:
            for (int k = 0; k < 4; k++) p32(c, A_LOAD + 4 * k, 0x19999);
            break;
        }
        p16(c, A_LODC, r9);
    }
    int tgt = 0;
    if (g16u(x->pl->car, 0x0A) != 0) {
        int lap = g16(c, A_LAP);
        if (lap < 0) lap = 0;
        if (lap > 7) lap = 7;
        tgt = g32(c, A_SPDS + 4 * lap);
    }
    p32(c, A_TGT0, tgt);
    switch (g16(c, A_LODC)) {
    case 0: case 1: {
        int M[12];
        set_mark(x, c, M);
        other_slip(x, c);
        other_car_ctrl(x, c, M);
        if (g16(c, A_LODC) == 0) set_other_car(x, c); else simple_other(x, c);
        break; }
    case 2: near_car(x, c); break;
    default: far_car(x, c); break;
    }
    int oldb = (signed char)g8(c, A_BLK);
    get_block2(x, c);
    int newb = (signed char)g8(c, A_BLK);
    if (!(newb >= oldb) && oldb > 125) p16(c, A_LAP, g16(c, A_LAP) + 1);
    p32(c, A_SPD2, g32(c, A_SPEED));
}

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
 * LOD 0: the full model (otherset)
 * ------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
typedef struct { int n[3]; int h; int type; } Rec;                         /* one get_coli2 record: the normal (8.24), the depth / height, the surface type */

static void get_rec(Ctx *x, const int pos[3], Rec *r)
{
    r->type = coli_get2(x->co, pos[0], pos[1], pos[2], r->n, &r->h);
}

/* otherset::otrCarColi 0x0600B9B8: the four body corners against the walls.  pos / ang: the predicted pose; rec: the four ground records.  Returns the deepest wall penetration */
static int otr_car_coli(Ctx *x, unsigned char *c, int pos[3], const int ang[3], Rec rec[4])
{
    int maxd = 0, fx7 = 0, fz9 = 0, tq = 0;
    int M[12], off[4][3], wpos[4][3], push[4][3];
    m_unit(M);
    r_rotate_y(x->ph, M, ang[1]);
    r_rotate_x(x->ph, M, ang[0]);
    r_rotate_z(x->ph, M, ang[2]);
    for (int i = 0; i < 4; i++) {
        int src[3] = { ph32(x, 0x0600BC38u + 12u * (unsigned)i), ph32(x, 0x0600BC38u + 12u * (unsigned)i + 4), ph32(x, 0x0600BC38u + 12u * (unsigned)i + 8) };
        calc_point(M, src, off[i]);
        for (int k = 0; k < 3; k++) wpos[i][k] = off[i][k] + pos[k];
    }
    for (int i = 0; i < 4; i++) {
        get_rec(x, wpos[i], &rec[i]);
        if (rec[i].type == 0 && rec[i].h > 0) {
            int n8[3] = { sra8(rec[i].n[0]), sra8(rec[i].n[1]), sra8(rec[i].n[2]) }, cr[3], t0[3], tu[3];
            cross16(n8, off[i], cr);
            cross16(cr, n8, t0);
            c_reg(t0, tu);
            int dp = inner16(tu, off[i]);
            int d2 = FM(dp, dp);
            int k7c = g32(c, A_K7C), k84 = g32(c, A_K84);
            int r3 = FM(d2, k7c) + k84;
            i64 prod = (i64)(k7c >> 1) * k84;
            int q = div64(prod, r3);
            int d = rec[i].h;
            if (!(maxd > d)) maxd = d;
            int r4 = FM(q, d);
            push[i][0] = FM(n8[0], r4); push[i][1] = FM(n8[1], r4); push[i][2] = FM(n8[2], r4);
        } else {
            push[i][0] = push[i][1] = push[i][2] = 0;
        }
    }
    if (maxd == 0) return 0;
    opp_stats[3]++;
    for (int i = 0; i < 4; i++) {
        off[i][1] = 0; push[i][1] = 0;
        int cr[3];
        cross16(off[i], push[i], cr);
        int len = c_vecval(cr);
        tq -= cr[1] >= 0 ? len : neg(len);
        fx7 += push[i][0];
        fz9 += push[i][2];
    }
    p32(c, A_PUSH, g32(c, A_PUSH) + FDIV(fx7, g32(c, A_K7C)));
    p32(c, A_PUSH + 8, g32(c, A_PUSH + 8) + FDIV(fz9, g32(c, A_K7C)));
    p32(c, A_AVEL + 4, g32(c, A_AVEL + 4) + FDIV(tq, g32(c, A_K84)));
    pos[0] = g32(c, A_POS) + g32(c, A_VEL);
    pos[2] = g32(c, A_POS + 8) + g32(c, A_VEL + 8);
    return maxd;
}

/* otherset::tinyForce 0x0600B874: the spring / damper of one wheel; returns 1 when the wheel carries load */
static int tiny_force(Ctx *x, unsigned char *c, int w, Rec *r, const int wp[3], int *out_h, int *wy, int force[3], int k7c4)
{
    if (r->type == 0) get_rec(x, wp, r);
    int hq = r->h >> 4;
    *out_h = hq;
    *wy = hq + wp[1];
    int nib = r->type & 0xF;
    nib <<= 4 * w;
    p16(c, A_SURF, g16u(c, A_SURF) | nib);
    if (r->type == 0 || r->h < 0) {
        p32(c, A_LOAD + 4 * w, 0);
        force[0] = force[1] = force[2] = 0;
        return 0;
    }
    int k, dmp;
    if (w < 2) { k = dyn32(x, c, 20); dmp = dyn32(x, c, 28); } else { k = dyn32(x, c, 24); dmp = dyn32(x, c, 32); }
    int h = r->h;
    int r1 = HM(FM(k, h), G100) * 100;
    if (h > 0x33333) {
        r1 += FM(h - 0x33333, k7c4);
        h = 0x33333;
    }
    int r0 = g32(c, A_LOAD + 4 * w) - h;
    int r2 = FM(r0, dmp);
    r2 = HM(r2, G100);
    int r3 = FM(r2, G16C);
    r1 -= r3;
    if (r1 < 0) r1 = 0;
    r1 >>= 4;
    p32(c, A_LOAD + 4 * w, h);
    force[0] = FM(r1, sra8(r->n[0]));
    force[1] = FM(r1, sra8(r->n[1]));
    force[2] = FM(r1, sra8(r->n[2]));
    return 1;
}

/* otherset::setOtherCar 0x0600B028: LOD 0 - four wheels with springs, wall corners, a rigid body */
static void set_other_car(Ctx *x, unsigned char *c)
{
    const Phys *ph = x->ph;
    int F[48];                                                              /* the frame: forces F[7..9] (28..36), torques F[10..12] (40..48) */
    memset(F, 0, sizeof F);
    int r1 = g32(c, A_PUSH), r2 = g32(c, A_PUSH + 8);
    int vx = g32(c, A_VEL) + r1, vz = g32(c, A_VEL + 8) + r2;
    p32(c, A_VEL, vx); p32(c, A_VEL + 8, vz);
    p32(c, A_POS, g32(c, A_POS) + (r1 >> 1)); p32(c, A_POS + 8, g32(c, A_POS + 8) + (r2 >> 1));
    p32(c, A_PUSH, 0); p32(c, A_PUSH + 4, 0); p32(c, A_PUSH + 8, 0);
    p32(c, A_VX0, vx); p32(c, A_VY0, g32(c, A_VEL + 4)); p32(c, A_VZ0, vz);
    set_velocity(x, c);
    int pos[3] = { g32(c, A_POS) + g32(c, A_VEL), g32(c, A_POS + 4) + g32(c, A_VEL + 4), g32(c, A_POS + 8) + g32(c, A_VEL + 8) };
    int ang[3] = { g32(c, A_ROTX) + g32(c, A_AVEL), g32(c, A_ROTY) + g32(c, A_AVEL + 4), g32(c, A_ROTZ) + g32(c, A_AVEL + 8) };
    Rec rec[4];
    int ret = otr_car_coli(x, c, pos, ang, rec);
    int M[12], wp[4][3], rear[2][3], W[4][3];
    for (int i = 0; i < 4; i++) for (int k = 0; k < 3; k++) W[i][k] = ph32(x, 0x0600B824u + 12u * (unsigned)i + 4u * (unsigned)k);
    m_unit(M);
    m_translate(M, pos);
    r_rotate_y(ph, M, ang[1]);
    r_rotate_x(ph, M, ang[0]);
    r_rotate_z(ph, M, ang[2]);
    for (int i = 0; i < 4; i++) calc_point(M, W[i], wp[i]);
    int cp[3];
    m_unit(M);
    get_vec(c, A_POS, cp);
    m_translate(M, cp);
    r_rotate_y(ph, M, g32(c, A_ROTY));
    r_rotate_x(ph, M, g32(c, A_ROTX));
    r_rotate_z(ph, M, g32(c, A_ROTZ));
    calc_point(M, W[2], rear[0]);
    calc_point(M, W[3], rear[1]);
    int mv[2][3];
    for (int k = 0; k < 3; k++) { mv[0][k] = wp[2][k] - rear[0][k]; mv[1][k] = wp[3][k] - rear[1][k]; }
    /* the four wheels */
    int k7c4 = g32(c, A_K7C) >> 2, fh[4], wyv[4], frc[4][3], mask = 0;
    p16(c, A_SURF, 0);
    for (int w = 0; w < 4; w++) {
        int rr = tiny_force(x, c, w, &rec[w], wp[w], &fh[w], &wyv[w], frc[w], k7c4);
        p32(c, A_WY + 4 * w, wyv[w]);
        mask = (mask << 1) + rr;
    }
    p8(c, A_MASK, mask);
    /* the body: sideways slip of the rear axle */
    int fwd[3], src[3] = { 0, 0, 0x10000 };
    m_unit(M);
    r_rotate_y(ph, M, g32(c, A_ROTY));
    calc_point(M, src, fwd);
    int sumlen = 0, sumlat = 0;
    for (int k = 0; k < 2; k++) {
        int len = c_vecval(mv[k]);
        sumlen += len;
        if (len > 0) {
            int u[3];
            c_reg(mv[k], u);
            sumlat += FM(fwd[2], u[0]) - FM(fwd[0], u[2]);
        }
    }
    int vel[3];
    get_vec(c, A_VEL, vel);
    int v2 = X16((i64)vel[0] * vel[0] + (i64)vel[1] * vel[1] + (i64)vel[2] * vel[2]);
    if (v2 > 6) {
        int vu[3];
        c_reg(vel, vu);
        int cr = X16((i64)fwd[2] * vu[0] + (i64)neg(vu[2]) * fwd[0]);
        int a = asin_f(cr), lim = 0x1921F, a1 = a, l3 = lim;
        if (a < 0) { a1 = neg(a1); l3 = neg(l3); }
        if (a1 > lim) a = l3;
        int old = g32(c, A_SLIP);
        p32(c, A_SLIP, a);
        p32(c, A_SLIPD, a - old);
    } else {
        p32(c, A_SLIP, 0);
        p32(c, A_SLIPD, 0);
    }
    {
        unsigned gp = (unsigned)g32(c, A_GUIDE) + 16u * (unsigned)g16u(c, A_GIDX);
        int wb = (signed char)phys_r8(ph, gp + 12);
        unsigned ix = ((unsigned)wb >> 2) & 0x1C0;
        int tb = ix == 0 ? ph32(x, 0x0600B854) : 0x10000;                  /* every guide point has a top byte below 0x80: index 0 */
        int r8 = HM(FM(tb * 1000, dyn32(x, c, 4)), G100);
        int r4 = FM(0x3243F, sumlat);
        if (r4 < 0) r4 = neg(r4);
        if (!(0x1921F > r4)) r4 = 0x1921F;
        r8 = FM(r8, r_sin16(ph, r4));
        int r5 = sumlat < 0 ? neg(sumlat) : sumlat;
        if (sumlat >= 0) r8 = neg(r8);
        int l1 = FM(sumlen, r5);
        if (0x51E > l1) {
            l1 = l1 * 50;
            r8 = FM(l1, r8);
        }
        r8 = FM(r8, 0x14000);
        F[11] += r8;
    }
    {
        int dv[3] = { g32(c, A_VEL) - g32(c, A_VX0), 0, g32(c, A_VEL + 8) - g32(c, A_VZ0) };
        int len = c_vecval(dv);
        int mag = FM(g32(c, A_K7C), len >> 1);
        int u[3];
        c_reg(dv, u);
        dv[0] = FM(u[0], mag);
        dv[2] = FM(u[2], mag);
        int o[3];
        m_unit(M);
        r_rotate_y(ph, M, neg(g32(c, A_ROTY)));
        calc_point(M, dv, o);
        F[11] -= FM(0x14000, o[0]);
        m_unit(M);
        r_rotate_y(ph, M, neg(g32(c, A_ROTY)));
        int d2[3] = { g32(c, A_VEL) - g32(c, A_VX0), 0, g32(c, A_VEL + 8) - g32(c, A_VZ0) };
        calc_point(M, d2, o);
        int c1 = FM(g32(c, A_K7C), 0x3333);
        int t4 = F[10] + FM(c1, o[2]);
        int t5 = F[12] - FM(c1, o[0]);
        int r1v = HM(0x7D0000, G100);
        int r6 = FM(r1v, dyn32(x, c, 20));
        int l0 = g32(c, A_LOAD), l1v = g32(c, A_LOAD + 4);
        if (l0 == 0 && l1v > 0) t5 += FM(fh[0], r6);
        if (l1v == 0 && l0 > 0) t5 -= FM(fh[1], r6);
        F[10] = t4; F[12] = t5;
    }
    {
        int T[4][3];
        m_unit(M);
        r_rotate_z(ph, M, neg(g32(c, A_ROTZ)));
        r_rotate_x(ph, M, neg(g32(c, A_ROTX)));
        r_rotate_y(ph, M, neg(g32(c, A_ROTY)));
        for (int k = 0; k < 4; k++) calc_point(M, frc[k], T[k]);
        int fy = T[0][1] + T[1][1], ry = T[2][1] + T[3][1];
        int dy01 = T[0][1] - T[1][1], dy23 = T[2][1] - T[3][1];
        int zt = T[0][2] + T[1][2] + T[2][2] + T[3][2];
        int dz10 = T[1][2] - T[0][2], dz32 = T[3][2] - T[2][2];
        F[10] += X16((i64)fy * 0x14000 + (i64)(int)0xFFFEC000 * ry + (i64)0x3333 * zt);
        int fxs = T[0][0] + T[1][0], rxs = T[2][0] + T[3][0], txs = fxs + rxs;
        F[12] += X16((i64)dy01 * 0xA28F + (i64)0xA8F5 * dy23 + (i64)neg(0x3333) * txs);
        F[11] += X16((i64)rxs * 0x14000 + (i64)(int)0xFFFEC000 * fxs + (i64)dz10 * 0xA28F + (i64)dz32 * 0xA8F5);
        for (int i = 0; i < 3; i++) F[7 + i] += frc[0][i] + frc[1][i] + frc[2][i] + frc[3][i];
        for (int i = 0; i < 3; i++) p32(c, A_AVEL + 4 * i, g32(c, A_AVEL + 4 * i) + FDIV(F[10 + i], g32(c, A_K80 + 4 * i)));
    }
    if (mask != 15) {
        int r2v = 0xCCCC;
        int r0 = g32(c, A_AVEL + 8) ^ g32(c, A_ROTZ);
        if (r0 >= 0) {
            int r1b = g32(c, A_ROTZ), r3 = FM(r2v, FM(r1b, r1b));
            if (r1b < 0) r3 = neg(r3);
            p32(c, A_ROTZ, r1b - r3);
        }
        r0 = g32(c, A_AVEL) ^ g32(c, A_ROTX);
        if (r0 >= 0) {
            int r1b = g32(c, A_ROTX), r3 = FM(r2v, FM(r1b, r1b));
            if (r1b < 0) r3 = neg(r3);
            p32(c, A_ROTX, r1b - r3);
        }
    }
    {
        int dy12 = (int)((u32)dyn32(x, c, 12) >> 4);
        i64 acc = (i64)dy12 * g32(c, A_SLIPD) + (i64)dyn32(x, c, 16) * g32(c, A_AVEL + 4);
        int r1b = X16(acc);
        p32(c, A_AVEL + 4, g32(c, A_AVEL + 4) - (r1b >> 8));
    }
    p32(c, A_ROTX, g32(c, A_ROTX) + g32(c, A_AVEL));
    p32(c, A_ROTY, g32(c, A_ROTY) + g32(c, A_AVEL + 4));
    p32(c, A_ROTZ, g32(c, A_ROTZ) + g32(c, A_AVEL + 8));
    {
        int k7c = g32(c, A_K7C);
        int nvx = g32(c, A_VEL) + FDIV(F[7], k7c);
        p32(c, A_VEL, nvx);
        int nvy = g32(c, A_VEL + 4) + FDIV(F[8], k7c) - (int)((u32)G100 >> 16);
        p32(c, A_VEL + 4, nvy);
        int nvz = g32(c, A_VEL + 8) + FDIV(F[9], k7c);
        p32(c, A_VEL + 8, nvz);
        p32(c, A_POS, g32(c, A_POS) + nvx);
        p32(c, A_POS + 4, g32(c, A_POS + 4) + nvy);
        p32(c, A_POS + 8, g32(c, A_POS + 8) + nvz);
    }
    if (ret >= 0) {
        int r3 = 0xFF3B - FM(ret, dyn32(x, c, 48));
        if (r3 < 0) r3 = 0;
        p32(c, A_VEL, FM(g32(c, A_VEL), r3));
        p32(c, A_VEL + 4, FM(g32(c, A_VEL + 4), r3));
        p32(c, A_VEL + 8, FM(g32(c, A_VEL + 8), r3));
    } else {
        int r1b = mask == 0 ? dyn32(x, c, 44) : 0xFFFF;
        p32(c, A_VEL, FM(g32(c, A_VEL), r1b));
        p32(c, A_VEL + 8, FM(g32(c, A_VEL + 8), r1b));
    }
    p32(c, A_ROTY, regular_angle(g32(c, A_ROTY)));
    int lim = 0x6488;
    int r4 = regular_angle(g32(c, A_ROTX));
    if (!(lim > (r4 < 0 ? neg(r4) : r4))) r4 = r4 < 0 ? neg(lim) : lim;
    p32(c, A_ROTX, r4);
    r4 = regular_angle(g32(c, A_ROTZ));
    if (!(lim > (r4 < 0 ? neg(r4) : r4))) r4 = r4 < 0 ? neg(lim) : lim;
    p32(c, A_ROTZ, r4);
}

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
 * spawn (otherctrl::initOtherGP / initOtherPR) and the first-frame handler
 * ------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
static void first_frame(Ctx *x, unsigned char *c)                                      /* 0x0600C958 */
{
    int cw = 4 * x->pl->cfg.course;
    unsigned tbl = (unsigned)ph32(x, 0x0600CDB8u + (unsigned)cw);
    p32(c, A_GUIDE, (int)tbl);
    p16(c, A_SPDST, 0);
    unsigned P = tbl + 16u * (unsigned)g16u(c, A_GIDX);
    int p0[3] = { ph32(x, P), ph32(x, P + 4), ph32(x, P + 8) }, p1[3] = { ph32(x, P + 16), ph32(x, P + 20), ph32(x, P + 24) };
    int dir[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] }, u[3];
    int tr[3] = { neg(p0[0]), 0, neg(p0[2]) };
    c_reg(dir, u);
    p32(c, A_HDX, u[0]); p32(c, A_HDY, u[1]); p32(c, A_HDZ, u[2]);
    int pos[3], nrm[3], h, rot[3], hd[3];
    get_vec(c, A_POS, pos);
    coli_get2(x->co, pos[0], pos[1], pos[2], nrm, &h);
    get_vec(c, A_HDX, hd);
    get_angle(x, nrm, hd, rot);
    p32(c, A_ROTX, rot[0]); p32(c, A_ROTY, rot[1]); p32(c, A_ROTZ, rot[2]);
    p32(c, A_STEER, 0);
    for (int k = 0; k < 6; k++) p32(c, A_PUSH + 4 * k, 0);
    p32(c, A_SLIP, 0); p32(c, A_SLIPD, 0);
    dir[1] = 0;
    int M[12], o[3];
    m_unit(M);
    c_turnv(M, dir);
    m_translate(M, tr);
    calc_point(M, pos, o);
    p32(c, A_LATT, o[0]);
    set_mark(x, c, M);
    p32(c, A_WZ, 0); p32(c, A_WACC, 0);
    p32(c, A_VX0, 0); p32(c, A_VY0, 0); p32(c, A_VZ0, 0);
    p32(c, A_VEL, 0); p32(c, A_VEL + 4, 0); p32(c, A_VEL + 8, 0);
    for (int k = 0; k < 4; k++) p32(c, A_WY + 4 * k, 0);
    for (int k = 0; k < 4; k++) p32(c, A_LOAD + 4 * k, 0x33333);
    p32(c, A_K7C, 0x03E80000);
    unsigned tb = 0x0600CAB8u + (x->pl->cfg.course == 2 ? 12u : 0u);
    p32(c, A_K80, ph32(x, tb)); p32(c, A_K84, ph32(x, tb + 4)); p32(c, A_K88, ph32(x, tb + 8));
    get_block2(x, c);
    p32(c, A_DYN, ph32(x, 0x060A1490u));
    p32(c, A_FN0, H_EXEC | (g32(c, A_FN0) & 1));
    exec_other_car(x, c);
}

static void progress_init(Ctx *x)                                                      /* FUN_0600C93A: the player's progress word */
{
    const unsigned char *p = x->pl->car;
    x->o->progress = (short)((signed char)g8(p, A_BLK) + (((int)g16(p, A_LAP) << 8) >> 1));
}

static void spawn_one(Ctx *x, unsigned char *c, int rank_idx, int gidx, int speed_w)  /* FUN_0600C78A */
{
    static const unsigned char rank_tab[20] = { 255, 0, 8, 2, 3, 9, 5, 6, 7, 2, 3, 4, 9, 5, 8, 9, 9, 9, 0, 9 };
    int t = rank_tab[rank_idx];
    if (t == 0 && g8(x->pl->car, A_TYPE) == 0) t = 1;
    p8(c, A_TYPE, t);
    int cw = 4 * x->pl->cfg.course;
    unsigned tbl = (unsigned)ph32(x, 0x0600CDB8u + (unsigned)cw);
    if (gidx < 0) { gidx += 300; p16(c, A_LAP, g16(c, A_LAP) - 1); }
    p16(c, A_GIDX, gidx);
    unsigned P = tbl + 16u * (unsigned)gidx;
    p32(c, A_POS, ph32(x, P));
    p32(c, A_POS + 4, ph32(x, P + 4) + 0x4CCC);
    p32(c, A_POS + 8, ph32(x, P + 8));
    p16(c, A_LODR, 3); p16(c, A_LODC, 3);
    for (int k = 0; k < 8; k++) p32(c, A_SPDS + 4 * k, speed_w << 16);
    p32(c, A_FN0, H_FIRST);
    p32(c, A_ACCP, 0x0600D1B0);
    x->o->ncars++;
}

static void spawn_gp(Ctx *x)                                                           /* otherctrl::initOtherGP 0x0600C664 */
{
    Opp *o = x->o;
    unsigned char *pl = x->pl->car;
    o->ncars = 0;
    o->frame = 0;
    int cw = 4 * x->pl->cfg.course;
    int level = x->pl->cfg.difficulty, flag = x->pl->cfg.laps == 3 ? 1 : 0;
    int step = ph32(x, 0x0600D064u + 16u * (unsigned)(level + 3 * flag) + (unsigned)cw);
    o->guide = (unsigned)ph32(x, 0x0600CDB8u + (unsigned)cw);
    int rank = g8(pl, A_RANK);
    if (cw == 12) { rank = 1; p16(pl, A_RANK, 1); }
    unsigned set = (unsigned)ph32(x, 0x0600D0C4u + 4u * (unsigned)(level * 2 + flag));
    unsigned ahead = (unsigned)ph32(x, set + 2u * (unsigned)cw);
    int slot = 0, acc = 0, r11 = rank - 1;
    while (phys_r16s(x->ph, ahead) >= 0 && r11 > 0) {
        acc += step;
        int gi = (short)((u32)acc >> 16) + phys_r16s(x->ph, ahead + 2);
        spawn_one(x, o->car[slot++], r11, gi, phys_r16s(x->ph, ahead));
        r11--;
        ahead += 4;
    }
    o->w2b2 = (r11 + 1) & 0xFFFF;
    unsigned behind = (unsigned)ph32(x, set + 4u + 2u * (unsigned)cw);
    acc = 0;
    r11 = rank + 1;
    while (phys_r16s(x->ph, behind) >= 0 && !(r11 > 15)) {
        acc -= step;
        int gi = (short)((u32)acc >> 16) + phys_r16s(x->ph, behind + 2);
        unsigned char *c = o->car[slot++];
        spawn_one(x, c, r11, gi, phys_r16s(x->ph, behind));
        if (rank == 1 && r11 == 2) p8(c, A_TYPE, g8(pl, A_TYPE) == 0 ? 1 : 0);
        r11++;
        behind += 4;
    }
    progress_init(x);
}

static void spawn_pr(Ctx *x)                                                           /* otherctrl::initOtherPR 0x0600C824 */
{
    Opp *o = x->o;
    unsigned char *pl = x->pl->car;
    o->frame = 0;
    int ghost = o->ghost;
    ghost = 0;                                                                         /* [0x060AA810] != 2: the secret ghost condition (byte 0x06040062 & 4 and the pad word 0x0210) is not part of the port */
    o->ghost = ghost;
    int cw = 4 * x->pl->cfg.course;
    o->guide = (unsigned)ph32(x, 0x0600CDB8u + (unsigned)cw);
    unsigned rec = (unsigned)ph32(x, 0x0600CDDCu + (unsigned)cw);
    int slot = 0;
    for (;;) {
        unsigned char *c = o->car[slot];
        int t = ghost ? 10 : (g8(pl, A_TYPE) == 0 ? 1 : 0);
        p8(c, A_TYPE, t);
        int lst = ph32(x, rec);
        rec += 4;
        if (!(lst > 0)) break;
        if (ghost) lst += 0x18;
        int spd = phys_r16s(x->ph, (unsigned)lst + 2u * (unsigned)x->pl->cfg.difficulty);
        for (int k = 0; k < 8; k++) p32(c, A_SPDS + 4 * k, spd << 16);
        p16(c, A_GIDX, 0);
        p32(c, A_POS, ph32(x, rec)); p32(c, A_POS + 4, ph32(x, rec + 4)); p32(c, A_POS + 8, ph32(x, rec + 8));
        rec += 12;
        p16(c, A_LODR, 0); p16(c, A_LODC, 0);
        p16(c, A_LAP, 0xFFFF);
        p32(c, A_FN0, H_FIRST | (g32(c, A_FN0) & 1));
        p32(c, A_ACCP, 0x0600D1C0 + 4 * cw);
        p32(c, A_AHEAD, OPP_PLAYER_ADDR); p32(c, A_BEHIND, OPP_PLAYER_ADDR);
        slot++;
    }
    progress_init(x);
}

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
 * car-car collisions (carcoli) and otherCtrl
 * ------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
static void hit_check(Ctx *x, unsigned char *a, const unsigned char *b)                /* carcoli::hitCheck 0x0600C368: the player (a) is pushed away from an other car (b) */
{
    int pa[3], pb[3], d[3];
    get_vec(a, A_POS, pa); get_vec(b, A_POS, pb);
    for (int k = 0; k < 3; k++) d[k] = pa[k] - pb[k];
    i64 S = (i64)d[0] * d[0] + (i64)d[2] * d[2] + (i64)d[1] * d[1];
    if (!(9 > (int)(S >> 32))) return;
    int dist = g_sqrt(X16(S));
    int n[3], M[12], v[3], w[3];
    c_reg(d, n);
    m_unit(M);
    m_scale(M, 0xE666, 0x3333, 0x1E666);
    r_rotate_z(x->ph, M, neg(g32(b, A_ROTZ)));
    r_rotate_x(x->ph, M, neg(g32(b, A_ROTX)));
    r_rotate_y(x->ph, M, neg(g32(b, A_ROTY)));
    calc_point(M, n, v);
    int rb = c_vecval(v);
    for (int k = 0; k < 3; k++) n[k] = neg(n[k]);
    m_unit(M);
    r_rotate_z(x->ph, M, neg(g32(a, A_ROTZ)));
    r_rotate_x(x->ph, M, neg(g32(a, A_ROTX)));
    r_rotate_y(x->ph, M, neg(g32(a, A_ROTY)));
    int r[3];
    calc_point(M, n, r);
    v[0] = FM(0xE666, r[0]); v[1] = FM(0x3333, r[1]); v[2] = FM(0x1E666, r[2]);
    int ra = c_vecval(v);
    int sum = rb + ra;
    if (!(sum > dist)) return;
    int pen = (sum - dist) >> 1;
    opp_stats[0]++;
    int T[12];
    memcpy(T, M, sizeof T);
    { int t; t = T[1]; T[1] = T[4]; T[4] = t; t = T[2]; T[2] = T[8]; T[8] = t; t = T[6]; T[6] = T[9]; T[9] = t; }
    int np = neg(pen);
    int q[3] = { FM(np, r[0]), FM(np, r[1]), FM(np, r[2]) };
    calc_point(T, q, w);
    p32(a, A_PUSH, g32(a, A_PUSH) + w[0]);
    p32(a, A_PUSH + 4, g32(a, A_PUSH + 4) + w[1]);
    p32(a, A_PUSH + 8, g32(a, A_PUSH + 8) + w[2]);
    p8(a, A_FLAGS, g8(a, A_FLAGS) | 2);
}

static void hit_check2(Ctx *x, unsigned char *a, const unsigned char *b)               /* carcoli::hitCheck2 0x0600C198 (practice: the rival) */
{
    int acc[3] = { 0, 0, 0 }, maxpen = 0;
    int pa[3], pb[3], d[3];
    get_vec(a, A_POS, pa); get_vec(b, A_POS, pb);
    for (int k = 0; k < 3; k++) d[k] = pa[k] - pb[k];
    i64 S = (i64)d[0] * d[0] + (i64)d[2] * d[2] + (i64)d[1] * d[1];
    if (!(9 > (int)(S >> 32))) return;
    int dist = g_sqrt(X16(S));
    int n[3], M[12], v[3];
    c_reg(d, n);
    m_unit(M);
    m_scale(M, 0xE666, 0x3333, 0x1E666);
    r_rotate_z(x->ph, M, neg(g32(b, A_ROTZ)));
    r_rotate_x(x->ph, M, neg(g32(b, A_ROTX)));
    r_rotate_y(x->ph, M, neg(g32(b, A_ROTY)));
    calc_point(M, n, v);
    int rb = c_vecval(v);
    for (int k = 0; k < 3; k++) n[k] = neg(n[k]);
    m_unit(M);
    r_rotate_z(x->ph, M, neg(g32(a, A_ROTZ)));
    r_rotate_x(x->ph, M, neg(g32(a, A_ROTX)));
    r_rotate_y(x->ph, M, neg(g32(a, A_ROTY)));
    int r[3];
    calc_point(M, n, r);
    v[0] = FM(0xE666, r[0]); v[1] = FM(0x3333, r[1]); v[2] = FM(0x1E666, r[2]);
    int ra = c_vecval(v);
    int sum = ra + rb;
    if (sum > dist) {
        int pen = (int)((u32)(sum - dist) >> 1);
        if (pen > maxpen) maxpen = pen;
        for (int k = 0; k < 3; k++) acc[k] -= FM(pen, r[k]);
    }
    if (!(maxpen > 0)) return;
    opp_stats[1]++;
    int T[12], w[3];
    memcpy(T, M, sizeof T);
    { int t; t = T[1]; T[1] = T[4]; T[4] = t; t = T[2]; T[2] = T[8]; T[8] = t; t = T[6]; T[6] = T[9]; T[9] = t; }
    calc_point(T, acc, w);
    p32(a, A_PUSH, g32(a, A_PUSH) + w[0]);
    p32(a, A_PUSH + 4, g32(a, A_PUSH + 4) + w[1]);
    p32(a, A_PUSH + 8, g32(a, A_PUSH + 8) + w[2]);
    p8(a, A_FLAGS, g8(a, A_FLAGS) | 2);
}

static void car_coli_gp(Ctx *x)                                                        /* carcoli::carColiGP 0x0600C510 */
{
    unsigned char *pl = x->pl->car;
    int pb = (signed char)g8(pl, A_BLK);
    for (int k = 0; k < x->o->ncars; k++) {
        unsigned char *c = x->o->car[k];
        int dbk = (signed char)g8(c, A_BLK) - pb;
        if (dbk < 0) dbk = neg(dbk);
        if (!(dbk >= 2)) hit_check(x, pl, c);
    }
}

static void car_coli_otr(Ctx *x)                                                       /* carcoli::carColiOtr 0x0600C550 */
{
    Opp *o = x->o;
    for (int k = 0; k < o->ncars; k++) {
        unsigned a = o->list[k], b = o->list[k + 1];
        if (a == OPP_PLAYER_ADDR) continue;
        unsigned char *ca = car_at(x, a);
        if (g16(ca, A_LODC) == 3) continue;
        if (b == OPP_PLAYER_ADDR) continue;
        unsigned char *cb = car_at(x, b);
        int dbk = (signed char)g8(ca, A_BLK) - (signed char)g8(cb, A_BLK);
        if (dbk < 0) dbk = neg(dbk);
        if (dbk > 2) continue;
        int d[3] = { g32(ca, A_POS) - g32(cb, A_POS), g32(ca, A_POS + 4) - g32(cb, A_POS + 4), g32(ca, A_POS + 8) - g32(cb, A_POS + 8) };
        i64 S = (i64)d[0] * d[0] + (i64)d[1] * d[1] + (i64)d[2] * d[2];
        if ((int)(S >> 32) >= 4) continue;
        int dist = g_sqrt(X16(S));
        int pen = (0x20000 - dist) >> 1;
        opp_stats[2]++;
        int u[3];
        c_reg(d, u);
        for (int i = 0; i < 3; i++) p32(ca, A_PUSH + 4 * i, g32(ca, A_PUSH + 4 * i) + FM(u[i], pen));
    }
}

static void progress_update(Ctx *x)                                                    /* FUN_0600CCCC: the player's progress follows the block id (the ids wrap at 128) */
{
    const unsigned char *pl = x->pl->car;
    int d = (signed char)g8(pl, A_BLK) - (signed char)g8(pl, 0xB7), r3 = -1, r1 = d;
    int r0 = d;
    if (d < 0) { r0 = neg(d); r3 = neg(r3); }
    if (r0 > 64) r1 = r3;
    x->o->progress = (short)(x->o->progress + r1);
}

static unsigned tie_break(Ctx *x, unsigned char *ai)                                   /* FUN_0600CCF8: who is ahead when the progress is equal */
{
    unsigned char *pl = x->pl->car;
    i64 acc = (i64)(g32(pl, A_POS) - g32(ai, A_POS)) * g32(pl, A_DIRX) + (i64)(g32(pl, A_POS + 8) - g32(ai, A_POS + 8)) * g32(pl, A_DIRZ);
    return (int)(acc >> 32) >= 0 ? OPP_PLAYER_ADDR : 0;
}

static void sort_cars(Ctx *x)                                                          /* otherctrl 0x0600CD20: sort by block id, then every car's neighbours */
{
    Opp *o = x->o;
    int n = o->ncars;
    o->list[0] = OPP_PLAYER_ADDR;
    for (int k = 0; k < n; k++) o->list[k + 1] = OPP_SLOT_ADDR(k);
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j <= n; j++) {
            int bi = (signed char)g8(car_at(x, o->list[i]), A_BLK), bj = (signed char)g8(car_at(x, o->list[j]), A_BLK);
            if (bj > bi) { unsigned t = o->list[i]; o->list[i] = o->list[j]; o->list[j] = t; }
        }
    }
    o->list[n + 1] = o->list[0];
    o->list[n + 2] = o->list[1];
    const unsigned *ext = o->list;
    for (int k = 0; k <= n; k++) {
        unsigned mid = ext[k + 1];
        if (mid == OPP_PLAYER_ADDR) continue;
        unsigned char *c = car_at(x, mid);
        p32(c, A_AHEAD, (int)ext[k]);
        p32(c, A_BEHIND, (int)ext[k + 2]);
    }
}

void opp_map(Opp *o, const Drive *d)                                                   /* car::get_other_map 0x0600829C */
{
    static const signed char off[10] = { 0, 1, -1, 2, -2, 3, 4, 5, 6, 7 };
    int blk = (signed char)d->car[A_BLK];
    for (int k = 0; k < 10; k++) o->otmap[k] = (unsigned char)((blk + off[k]) & 0x7F);
    o->notmap = 10;
}

static int find_blk(const unsigned char *list, int n, int blk)
{
    for (int k = 0; k < n; k++) if ((signed char)list[k] == blk) return k;
    return -1;
}

static void other_ctrl_gp(Ctx *x)                                                      /* 0x0600CB1C */
{
    Opp *o = x->o;
    unsigned char *pl = x->pl->car;
    progress_update(x);
    int r8 = o->progress, r11 = o->w2b2;
    for (int k = 0; k < o->ncars; k++) {
        unsigned char *c = o->car[k];
        int prog = (((int)g16(c, A_LAP) << 8) >> 1) + (signed char)g8(c, A_BLK);
        if (!(r8 > prog)) {
            if (prog > r8) r11++;
            else if (tie_break(x, c) != OPP_PLAYER_ADDR) r11++;
        }
    }
    p8(pl, A_RANK, r11);
    if (o->frame >= 2) {
        static const unsigned char lodtab[10] = { 0, 0, 0, 0, 0, 1, 1, 1, 2, 2 };
        for (int k = 0; k < o->ncars; k++) {
            unsigned char *c = o->car[k];
            int i = find_blk(o->otmap, o->notmap, (signed char)g8(c, A_BLK));
            p16(c, A_LODR, i < 0 ? 3 : lodtab[i]);
        }
        for (int k = 0; k < o->ncars; k++) {
            unsigned char *c = o->car[k];
            int i = find_blk(o->vis, o->nvis, (signed char)g8(c, A_BLK));
            p32(c, A_FN4, (i < 0 ? H_NODRAW : H_DRAW) | (g32(c, A_FN4) & 1));
        }
    }
    sort_cars(x);
    car_coli_otr(x);
    car_coli_gp(x);
}

static void other_ctrl_pr(Ctx *x)                                                      /* 0x0600CC40 */
{
    Opp *o = x->o;
    unsigned char *pl = x->pl->car, *c = o->car[0];
    if (o->frame >= 2) {
        progress_update(x);
        int prog = (((int)g16(c, A_LAP) << 8) >> 1) + (signed char)g8(c, A_BLK);
        int rank = 1;
        if (!(o->progress > prog)) {
            if (prog > o->progress) rank = 2;
            else if (tie_break(x, c) == OPP_PLAYER_ADDR) rank = 1; else rank = 2;
        }
        p8(pl, A_RANK, rank);
        int i = find_blk(o->vis, o->nvis, (signed char)g8(c, A_BLK));
        p32(c, A_FN4, (i < 0 ? H_NODRAW : H_DRAW) | (g32(c, A_FN4) & 1));
    }
    unsigned char *g = o->frame >= 2 ? c : pl;                                         /* GBR is the first other car after the draw handler search, the player before it */
    p32(g, A_AHEAD, OPP_PLAYER_ADDR);
    p32(g, A_BEHIND, OPP_PLAYER_ADDR);
    hit_check2(x, pl, c);
}

static void other_ctrl(Ctx *x)                                                         /* otherctrl::otherCtrl 0x0600CAD0 */
{
    Opp *o = x->o;
    if (g8(x->pl->car, A_STATE) == 8) {
        for (int k = 0; k < OPP_SLOTS; k++) { p32(o->car[k], A_FN0, 0); p32(o->car[k], A_FN4, 0); }
        return;
    }
    o->frame++;
    if (x->pl->cfg.game_type == 0) other_ctrl_gp(x); else other_ctrl_pr(x);
}

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
 * the public routines
 * ------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
static Ctx make_ctx(Opp *o, Drive *d) { Ctx x; x.ph = d->phys; x.co = d->coli; x.pl = d; x.o = o; return x; }

void opp_init(Opp *o) { memset(o, 0, sizeof *o); }

int opp_visible(const Opp *o, int k) { return g32(o->car[k], A_FN0) != 0 && (g32(o->car[k], A_FN4) & ~1) == H_DRAW; }

static void sync_mem(Ctx *x)                                                           /* the globals live in the console's work RAM next to the cars: the player's slot at +0x2B0, the first other car's at +0x1A0 */
{
    Opp *o = x->o;
    unsigned char *pl = x->pl->car;
    p16(pl, 0x2B0, o->ncars); p16(pl, 0x2B2, o->w2b2); p16(pl, 0x2B6, o->progress); p32(pl, 0x2B8, (int)o->guide);
    for (int k = 0; k < 16; k++) p32(o->car[0], 0x1A0 + 4 * k, (int)o->list[k]);
    p32(o->car[0], 0x1E0, o->frame);
}

void opp_spawn(Opp *o, Drive *d)
{
    Ctx x = make_ctx(o, d);
    if (d->cfg.game_type == 0) spawn_gp(&x); else spawn_pr(&x);
    sync_mem(&x);
}

void opp_move(Opp *o, Drive *d)
{
    Ctx x = make_ctx(o, d);
    for (int k = 0; k < OPP_SLOTS; k++) {
        unsigned char *c = o->car[k];
        int fn = g32(c, A_FN0);
        if (!fn) continue;
        if (fn == H_FIRST) first_frame(&x, c);
        else if ((fn & ~1) == H_EXEC) exec_other_car(&x, c);
    }
}

void opp_pass(Opp *o, Drive *d)
{
    opp_move(o, d);
    Ctx x = make_ctx(o, d);
    other_ctrl(&x);
    sync_mem(&x);
}

void opp_other_ctrl(Opp *o, Drive *d)
{
    Ctx x = make_ctx(o, d);
    other_ctrl(&x);
    sync_mem(&x);
}
