/* Sega Rally PSP - the game's integer math (math module, 0x0601B6B0 ..): sqrt and atan with the same steps and truncation as the original, so results match bit for bit
 * (tools/test_gmath.c checks them against tools/oracle_fn.py's vectors).  16.16 fixed point unless noted. */
#ifndef GMATH_H
#define GMATH_H
#include <math.h>

/* 0x0601B8CC: Newton iteration with the hardware divider; negative input returns -1.0 */
static inline int g_sqrt(int x)
{
    if (x < 0) return -0x10000;
    if (x == 0) return 0;
    int g = x > 0x10000 ? x : 0x10000;
    int prev;
    do {
        prev = g;
        int q = (int)((((long long)x) << 16) / g);
        g = (int)(((unsigned)g + (unsigned)q) >> 1);
    } while (prev > g);
    return prev;
}

/* 0x0601B7C0: angle of the vector (x, y) in 16.16 "turns" (65536 = one turn), counter-clockwise from +x, via a 513-entry table of trunc(atan(i / 512) / (2 pi) * 65536) */
static inline int g_atan(int x, int y)
{
    static unsigned short tab[513];
    static int ready;
    if (!ready) {
        for (int i = 0; i <= 512; i++) tab[i] = (unsigned short)(int)(atan((double)i / 512.0) * (65536.0 / 6.283185307179586));
        ready = 1;
    }
    if (x == 0 && y == 0) return 0;
    int code = 0;
    if (x < 0) { x = -x; code = 8; }
    if (y < 0) { y = -y; code += 4; }
    if (x < y) { int t = x; x = y; y = t; code += 2; }
    int q = (int)((((long long)y) << 16) / x);
    int r = tab[q >> 7];
    switch (code) {
    case 0: return r;
    case 2: return 0x4000 - r;
    case 4: return -r;
    case 6: return r - 0x4000;
    case 8: return 0x8000 - r;
    case 10: return 0x4000 + r;
    case 12: return r - 0x8000;
    default: return -0x4000 - r;
    }
}

/* r_rotate_*: radians in 16.16 -> turns (x 1/(2 pi), 0x28BE60DC / 2^32) */
static inline int g_rad_to_turns(int rad16) { return (int)(((long long)rad16 * 0x28BE60DCLL) >> 32); }

/* the fixed-point product the game uses everywhere (dmuls + xtrct): (a * b) >> 16 */
static inline int g_mul(int a, int b) { return (int)(((long long)a * b) >> 16); }

#endif
