/* Sega Rally PSP - the SH-2 arithmetic idioms the game's fixed-point code is made of, and the big-endian field access of its structures (shared by drive.c and camera.c). */
#ifndef FX_H
#define FX_H
#include <string.h>
#include <stdint.h>

typedef int64_t i64;                    /* the same types the PSP SDK calls u32 / u64, so including both is fine */
typedef uint64_t u64;
typedef uint32_t u32;

static inline int g32(const unsigned char *m, int o) { return (int)((u32)m[o] << 24 | (u32)m[o + 1] << 16 | (u32)m[o + 2] << 8 | m[o + 3]); }
static inline void p32(unsigned char *m, int o, int v) { m[o] = (unsigned char)((u32)v >> 24); m[o + 1] = (unsigned char)((u32)v >> 16); m[o + 2] = (unsigned char)((u32)v >> 8); m[o + 3] = (unsigned char)v; }
static inline int g16u(const unsigned char *m, int o) { return (m[o] << 8) | m[o + 1]; }
static inline int g16(const unsigned char *m, int o) { return (short)((m[o] << 8) | m[o + 1]); }
static inline void p16(unsigned char *m, int o, int v) { m[o] = (unsigned char)(v >> 8); m[o + 1] = (unsigned char)v; }
static inline int g8(const unsigned char *m, int o) { return m[o]; }
static inline void p8(unsigned char *m, int o, int v) { m[o] = (unsigned char)v; }


/* the SH-2 multiplier idioms */
static inline int FM(int a, int b) { return (int)(((i64)a * b) >> 16); }                /* dmuls.l; xtrct */
static inline int HM(int a, int b) { return (int)(((i64)a * b) >> 32); }                /* dmuls.l; sts mach */
static inline int M24(int a, int b) { return (int)(((i64)a * b) >> 24); }               /* dmuls.l; shll8 mach | macl >> 24 */
static inline int X16(i64 acc) { return (int)(acc >> 16); }                             /* xtrct of a 64-bit accumulator */
static inline int neg(int a) { return (int)(0u - (u32)a); }
static inline int iabs(int a) { return a < 0 ? neg(a) : a; }

/* the on-chip divider with a 64-bit dividend */
static inline int div64(i64 n, int d)
{
    if (d == 0) return n >= 0 ? 0x7FFFFFFF : (int)0x80000000;
    i64 q = n / d;
    if (q > 0x7FFFFFFFLL) return 0x7FFFFFFF;
    if (q < -0x80000000LL) return (int)0x80000000;
    return (int)q;
}
static inline int FDIV(int a, int b) { return div64((i64)a << 16, b); }                 /* swap.w / exts.w / shll16 into DVDNTH:DVDNTL */


static inline int t_sin(int x)                                 /* 0x0601B6B4: polynomial, radians 16.16 */
{
    if (x == 0) return 0;
    while (0x3243F < x) x = x - 0x6487F;
    while (x < -0x3243F) x = x + 0x6487F;
    int u = iabs(x) - 0x19220;
    int u2 = FM(u, u), u4 = FM(u2, u2), u6 = FM(u2, u4), u8 = FM(u2, u6), u10 = FM(u2, u8), u12 = FM(u2, u10);
    i64 acc = (i64)9 * u12 + (i64)-1184 * u10 + (i64)0x1A01A * u8 + (i64)-5965232 * u6 + (i64)0x0AAAAAAB * u4;
    int r = 0x10000 + ((int)(acc >> 32) - (u2 >> 1));
    return x >= 0 ? r : neg(r);
}
static inline int t_cos(int x) { return t_sin(x + 0x19220); }                                  /* 0x0601B6B0 */


/* 3 x 4 matrices (rows of 4 longs) */
static inline void unit_matrix(int m[12]) { memset(m, 0, 12 * sizeof(int)); m[0] = m[5] = m[10] = 0x10000; }
static inline void rot_cols(int m[12], int ia, int ib, int s, int c)                           /* rotate_x/y/z bodies (0x06017F7E / 06017FCA / 0601801A) */
{
    for (int r = 0; r < 3; r++) {
        int a = m[4 * r + ia], b = m[4 * r + ib];
        m[4 * r + ia] = X16((i64)c * a + (i64)neg(s) * b);
        m[4 * r + ib] = X16((i64)s * a + (i64)c * b);
    }
}
static inline void t_rotate_x(int m[12], int ang) { rot_cols(m, 1, 2, t_sin(ang), t_cos(ang)); }
static inline void t_rotate_y(int m[12], int ang) { rot_cols(m, 0, 2, neg(t_sin(ang)), t_cos(ang)); }      /* rotate_y pushes (cos, sin, -sin, cos): the opposite sense of x and z */
static inline void t_rotate_z(int m[12], int ang) { rot_cols(m, 0, 1, t_sin(ang), t_cos(ang)); }
static inline void st_mat(unsigned char *c, int o, const int m[12]) { for (int i = 0; i < 12; i++) p32(c, o + 4 * i, m[i]); }

/* calc_xyzpoint 0x06007C3C: rows . vector (translation column ignored) */
static inline void calc_xyz(const int m[12], const int v[3], int out[3])
{
    for (int r = 0; r < 3; r++) out[r] = X16((i64)m[4 * r] * v[0] + (i64)m[4 * r + 1] * v[1] + (i64)m[4 * r + 2] * v[2]);
}
/* calc_point 0x0601B97C: rows . vector + translation column */
static inline void calc_point(const int m[12], const int v[3], int out[3])
{
    for (int r = 0; r < 3; r++) out[r] = X16((i64)m[4 * r] * v[0] + (i64)m[4 * r + 1] * v[1] + (i64)m[4 * r + 2] * v[2]) + m[4 * r + 3];
}


#endif
