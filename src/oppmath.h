/* Sega Rally PSP - the vector / matrix library of the other cars' code: the `copro` module (0x0600BC68 .. 0x0600C197) and the matrix module routines it calls
 * (0x06017D24 ..), ported with the SH-2's rounding (docs/collision_world_ai.md section 5).  3 x 4 matrices are rows of 4 longs, p' = M p + t. */
#ifndef OPPMATH_H
#define OPPMATH_H
#include "fx.h"
#include "gmath.h"
#include "drive.h"

/* the length conversion of c_getr / c_vecval / c_reg / FUN_0600BD24: the 64-bit sum of squares S (32 fractional bits) -> a 16.16 length */
static inline int sqrt_s(i64 S)
{
    int hi = (int)(S >> 32);
    if (hi < 0x8000) return g_sqrt(X16(S));
    return (g_sqrt(hi) << 8) + 127;
}

static inline int c_outp2(const int a[3], const int b[3]) { return X16((i64)neg(a[0]) * b[2] + (i64)a[2] * b[0]); }            /* 0x0600BC68 */
static inline int c_getr(const int a[3], const int b[3])                                                                       /* 0x0600BC7C: |b - a| */
{
    int dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
    return sqrt_s((i64)dz * dz + (i64)dy * dy + (i64)dx * dx);
}
static inline int c_vecval(const int v[3]) { return sqrt_s((i64)v[0] * v[0] + (i64)v[1] * v[1] + (i64)v[2] * v[2]); }          /* 0x0600BCBC */
static inline void c_reg(const int v[3], int out[3])                                                                            /* 0x0600C118: the unit vector, (0, 0, 1) for a zero vector */
{
    int L = c_vecval(v);
    if (L == 0) { out[0] = 0; out[1] = 0; out[2] = 0x10000; return; }
    int y = FDIV(v[1], L), z = FDIV(v[2], L), x = FDIV(v[0], L);
    out[0] = x; out[1] = y; out[2] = z;
}
static inline int inner16(const int a[3], const int b[3]) { return X16((i64)a[0] * b[0] + (i64)a[1] * b[1] + (i64)a[2] * b[2]); }       /* math::inner_product 0x0601B910 */
static inline void cross16(const int a[3], const int b[3], int out[3])                                                         /* math::outer_product 0x0601B924 */
{
    int x = X16((i64)a[1] * b[2] + (i64)neg(a[2]) * b[1]);
    int y = X16((i64)a[2] * b[0] + (i64)neg(a[0]) * b[2]);
    int z = X16((i64)a[0] * b[1] + (i64)neg(a[1]) * b[0]);
    out[0] = x; out[1] = y; out[2] = z;
}
static inline int r_atan16(int x, int y) { return FM(g_atan(x, y), 0x6487F); }                                                  /* math::r_atan 0x0601B794 */
static inline int r_sin16(const Phys *ph, int rad)                                                                             /* math::r_sin 0x0601B764 */
{
    int s, c;
    fx_sincos(ph, HM(rad, 0x28BE60DC), &s, &c);
    return s;
}
static inline int regular_angle(int a)                                                                                          /* 0x0600BF58 */
{
    int lim = 0x3243F, s = lim, m = a;
    if (a < 0) { m = neg(a); s = neg(s); }
    if (!(lim > m)) a -= s << 1;
    return a;
}
static inline int asin_f(int x)                                                                                                /* 0x0600BF78 */
{
    int m = x < 0 ? neg(x) : x;
    if (m > 0x10000) return 0;
    if (m == 0x10000) return x > 0 ? 0x1921F : neg(0x1921F);
    int x2 = FM(x, x);
    int r = g_sqrt(0x10000 - x2);
    return r_atan16(0x10000, FDIV(x, r));
}

/* the matrix module */
static inline void m_unit(int m[12]) { unit_matrix(m); }
static inline void m_translate(int m[12], const int v[3])                                                                       /* translate 0x06017DD0 */
{
    for (int r = 0; r < 3; r++) m[4 * r + 3] += X16((i64)m[4 * r] * v[0] + (i64)m[4 * r + 1] * v[1] + (i64)m[4 * r + 2] * v[2]);
}
static inline void m_scale(int m[12], int sx, int sy, int sz)                                                                   /* scale 0x06017EBC */
{
    for (int r = 0; r < 3; r++) { m[4 * r] = FM(m[4 * r], sx); m[4 * r + 1] = FM(m[4 * r + 1], sy); m[4 * r + 2] = FM(m[4 * r + 2], sz); }
}
static inline void m_rot_turns(const Phys *ph, int m[12], int axis, int turns)
{
    int s, c;
    fx_sincos(ph, turns, &s, &c);
    if (axis == 0) rot_cols(m, 1, 2, s, c);
    else if (axis == 1) rot_cols(m, 0, 2, neg(s), c);
    else rot_cols(m, 0, 1, s, c);
}
static inline void r_rot(const Phys *ph, int m[12], int axis, int rad) { m_rot_turns(ph, m, axis, HM(rad, 0x28BE60DC)); }      /* r_rotate_x / y / z 0x06017F70 / 06017FBC / 0601800C */
#define r_rotate_x(ph, m, a) r_rot(ph, m, 0, a)
#define r_rotate_y(ph, m, a) r_rot(ph, m, 1, a)
#define r_rotate_z(ph, m, a) r_rot(ph, m, 2, a)

/* c_turnd / c_turnv: turn the matrix's columns to look along (or away from) a direction */
static inline void turn_y(int m[12], int q0, int q1)                                                                            /* FUN_0600BEA8 */
{
    for (int r = 0; r < 3; r++) {
        int a = m[4 * r], b = m[4 * r + 2];
        m[4 * r] = X16((i64)q1 * a + (i64)neg(q0) * b);
        m[4 * r + 2] = X16((i64)q0 * a + (i64)q1 * b);
    }
}
static inline void turn_x(int m[12], int q2, int q3)                                                                            /* FUN_0600BF00 */
{
    for (int r = 0; r < 3; r++) {
        int a = m[4 * r + 1], b = m[4 * r + 2];
        m[4 * r + 1] = X16((i64)q3 * a + (i64)neg(q2) * b);
        m[4 * r + 2] = X16((i64)q2 * a + (i64)q3 * b);
    }
}
static inline void c_turnd(int m[12], const int d[3])                                                                           /* 0x0600BCF0 (FUN_0600BD24 + BEA8 + BF00) */
{
    i64 sxz = (i64)d[0] * d[0] + (i64)d[2] * d[2];
    int L = sqrt_s(sxz + (i64)d[1] * d[1]), Lxz = sqrt_s(sxz);
    int q0 = FDIV(d[2], Lxz), q1 = FDIV(d[0], Lxz), q2 = FDIV(d[1], L), q3 = FDIV(Lxz, L);
    turn_y(m, q0, q1);
    turn_x(m, q2, q3);
}
static inline void c_turnv(int m[12], const int d[3])                                                                           /* 0x0600BD0C (FUN_0600BDE4 + BF00 + BEA8) */
{
    i64 sxz = (i64)d[0] * d[0] + (i64)d[2] * d[2];
    int L = sqrt_s(sxz + (i64)d[1] * d[1]), Lxz = sqrt_s(sxz);
    int q0 = neg(FDIV(d[0], Lxz)), q1 = FDIV(d[2], Lxz), q2 = neg(FDIV(d[1], L)), q3 = FDIV(Lxz, L);
    turn_x(m, q2, q3);
    turn_y(m, q0, q1);
}

#endif
