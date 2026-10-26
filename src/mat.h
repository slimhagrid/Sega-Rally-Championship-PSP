/* Sega Rally PSP - the game's matrix operations (3x4, row-major, p' = R p + t), shared by car.c and event.c.
 * Every operation is a post-multiplication M' = M * T like the game's matrix module (docs/render_pipeline.md 2.1). */
#ifndef MAT_H
#define MAT_H
#include <math.h>

typedef struct { float m[12]; } Mat;

static inline void m_translate(Mat *a, float x, float y, float z)
{
    float *m = a->m;
    for (int r = 0; r < 3; r++) m[4 * r + 3] += m[4 * r] * x + m[4 * r + 1] * y + m[4 * r + 2] * z;
}

/* axis 0 = x, 1 = y, 2 = z; ang in radians.  The game's r_rotate_x/y/z turn the opposite way to the formulas of render_pipeline.md 2.1 (measured: tools/test_car.c). */
static inline void m_rot_cs(Mat *a, int axis, float c, float s)       /* c = cos, s = -sin of the angle */
{
    float *m = a->m;
    for (int r = 0; r < 3; r++) {
        float *row = m + 4 * r;
        float p, q;
        switch (axis) {
        case 0: p = row[1]; q = row[2]; row[1] = c * p + s * q; row[2] = -s * p + c * q; break;
        case 1: p = row[0]; q = row[2]; row[0] = c * p - s * q; row[2] = s * p + c * q; break;
        default: p = row[0]; q = row[1]; row[0] = c * p + s * q; row[1] = -s * p + c * q; break;
        }
    }
}

static inline void m_rot(Mat *a, int axis, float ang)
{
    m_rot_cs(a, axis, cosf(ang), -sinf(ang));
}

/* angle given as the game's 16.16 "turns" value (65536 = one turn), used by the entry points 0x06017F76 / 0x06017FC2 / 0x06018012 */
static inline void m_rot_turns(Mat *a, int axis, int turns16)
{
    /* the game looks sin / cos up in a 4096-entry table: index = (angle >> 4) & 0xFFF (here the same values, computed once with sinf / cosf of the same angle: the libm calls were most of the
     * name entry's matrix work on the PSP) */
    static float tc[4096], ts[4096];
    static int init;
    int idx = (turns16 >> 4) & 0xFFF;
    if (!init) {
        for (int i = 0; i < 4096; i++) { float ang = (float)i * (6.2831853071795864f / 4096.0f); tc[i] = cosf(ang); ts[i] = -sinf(ang); }
        init = 1;
    }
    m_rot_cs(a, axis, tc[idx], ts[idx]);
}

/* matrix::scale: scales the columns of the rotation part */
static inline void m_scale(Mat *a, float sx, float sy, float sz)
{
    float *m = a->m;
    for (int r = 0; r < 3; r++) { m[4 * r] *= sx; m[4 * r + 1] *= sy; m[4 * r + 2] *= sz; }
}

/* FUN_0601d8d4: is a sphere of the given radius around the origin of matrix a on screen?  Same fixed-point steps as the game: K = (224 << 32) / z (16.16 z, hardware divider),
 * screen position and radius = (value * K) >> 32 with truncation, radius halved, picture 352x224, depth 0..250. */
static inline int m_visible(const Mat *a, float radius)
{
    long long zr = llroundf(a->m[11] * 65536.0f);
    if (zr <= 0 || zr >= 250LL * 65536) return 0;
    long long k = (long long)((((unsigned long long)224 << 32) | 1ULL) / (unsigned long long)zr);
    long long sx = ((long long)llroundf(a->m[3] * 65536.0f) * k) >> 32;
    long long sy = ((long long)llroundf(a->m[7] * 65536.0f) * k) >> 32;
    long long rr = (((long long)llroundf(radius * 65536.0f) >> 1) * k) >> 32;
    return 176 >= sx - rr && sx + rr >= -176 && 112 >= sy - rr && sy + rr >= -112;
}

#endif
