/* Sega Rally PSP - the driving core, see drive.h.  Everything here is a hand port of the original SH-2 code (docs/physics.md); function names carry the original's address.
 * Arithmetic follows the SH-2: 32-bit wrapping integers, 16.16 fixed point, the multiplier's 64-bit products and the on-chip divider (64 / 32, truncating, saturating). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "drive.h"
#include "gmath.h"
#include "fx.h"

#ifdef __PSP__
#include <pspiofilemgr.h>
#endif


/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
 * the tables (phys.dat)
 * ------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
int phys_load(Phys *p, const char *path)
{
    memset(p, 0, sizeof *p);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    p->data = (unsigned char *)malloc((size_t)n);
    if (!p->data || (long)fread(p->data, 1, (size_t)n, f) != n) { fclose(f); free(p->data); p->data = NULL; return -1; }
    fclose(f);
    if (n < 8 || memcmp(p->data, "PHY1", 4) != 0) { free(p->data); p->data = NULL; return -2; }
    unsigned nreg = (unsigned)p->data[4] | (unsigned)p->data[5] << 8 | (unsigned)p->data[6] << 16 | (unsigned)p->data[7] << 24;
    if (nreg > 32) { free(p->data); p->data = NULL; return -2; }
    const unsigned char *body = p->data + 8 + 8 * nreg;
    for (unsigned i = 0; i < nreg; i++) {
        const unsigned char *h = p->data + 8 + 8 * i;
        p->reg[i].addr = (unsigned)h[0] | (unsigned)h[1] << 8 | (unsigned)h[2] << 16 | (unsigned)h[3] << 24;
        p->reg[i].size = (unsigned)h[4] | (unsigned)h[5] << 8 | (unsigned)h[6] << 16 | (unsigned)h[7] << 24;
        p->reg[i].p = body;
        body += p->reg[i].size;
    }
    p->nregions = (int)nreg;
    return 0;
}

void phys_free(Phys *p)
{
    free(p->data);
    memset(p, 0, sizeof *p);
}

static const unsigned char *pm_ptr(const Phys *p, unsigned addr, unsigned n)
{
    for (int i = 0; i < p->nregions; i++)
        if (addr >= p->reg[i].addr && addr + n <= p->reg[i].addr + p->reg[i].size) return p->reg[i].p + (addr - p->reg[i].addr);
    fprintf(stderr, "drive: table address %08x is not in phys.dat\n", addr);
    abort();
}
static int pm_r32(const Phys *p, unsigned a) { const unsigned char *q = pm_ptr(p, a, 4); return (int)((u32)q[0] << 24 | (u32)q[1] << 16 | (u32)q[2] << 8 | q[3]); }
static int pm_r16(const Phys *p, unsigned a) { const unsigned char *q = pm_ptr(p, a, 2); return (short)((q[0] << 8) | q[1]); }
static int pm_r16u(const Phys *p, unsigned a) { const unsigned char *q = pm_ptr(p, a, 2); return (q[0] << 8) | q[1]; }
static int win_r16(const MemWin *w, unsigned a)                                        /* a big endian word of a window of the console's memory; 0 outside it */
{
    if (!w->p || a < w->base || a - w->base + 2 > w->size) return 0;
    const unsigned char *q = w->p + (a - w->base);
    return (q[0] << 8) | q[1];
}
static void win_w16(const MemWin *w, unsigned a, int v)
{
    if (!w->p || a < w->base || a - w->base + 2 > w->size) return;
    unsigned char *q = w->p + (a - w->base);
    q[0] = (unsigned char)(v >> 8); q[1] = (unsigned char)v;
}
int phys_r32(const Phys *p, unsigned a) { return pm_r32(p, a); }
int phys_r16s(const Phys *p, unsigned a) { return pm_r16(p, a); }
int phys_r8(const Phys *p, unsigned a) { return *pm_ptr(p, a, 1); }

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
 * the car slot: big endian bytes at the original offsets
 * ------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
/* slot fields (docs/physics.md 2.1) */
enum {
    O_FN0 = 0x00, O_FN4 = 0x04, O_MODE = 0x08, O_STATE = 0x0A, O_ANG = 0x0C, O_YAW = 0x10, O_ROLL = 0x14, O_POS = 0x18, O_VEL = 0x24, O_STEER = 0x30, O_WSTEP = 0x34, O_WROT = 0x38,
    O_LOAD = 0x3C, O_GROUND = 0x4C, O_ID = 0x5C, O_BLIGHT = 0x5D, O_X5F = 0x5F, O_SEG = 0x60, O_PSEG = 0x62, O_PUSH = 0x64, O_AVEL = 0x70, O_K7C = 0x7C, O_K80 = 0x80, O_K84 = 0x84, O_K88 = 0x88,
    O_RPM = 0x8C, O_DYN = 0x90, O_SPEED = 0x94, O_SLIP = 0x98, O_BIAS = 0x9C, O_IN_ACC = 0xA0, O_IN_BRK = 0xA4, O_IN_STR = 0xA8, O_NEGYAW = 0xAC, O_GEAR = 0xB0, O_CONT = 0xB2, O_PCONT = 0xB3,
    O_SURF = 0xB4, O_BSEG = 0xB7, O_DIRX = 0xB8, O_DIRZ = 0xBC, O_REL = 0xC0, O_CKPT = 0xC4, O_HEAD = 0xC5, O_DSPANG = 0xC7, O_LAPNEXT = 0xC8, O_LAPCNT = 0xCA, O_LAPMAX = 0xCC,
    O_BSLIP = 0xD4, O_BSLIPD = 0xD8, O_PLOAD = 0xDC, O_SPEED2 = 0xEC, O_BLK0 = 0xF0, O_BLK1 = 0xF1, O_MK2 = 0xF2, O_TRANS = 0xF4, O_CTRL = 0xF5, O_WALLMASK = 0xF6, O_SECT = 0xF7,
    O_SURFOR = 0xF8, O_SURFIDX = 0xFA, O_WALL = 0x100, O_AXSLIP = 0x104, O_AXSPD = 0x10C, O_INPUT = 0x114, O_PPOS = 0x118, O_PANG = 0x124, O_MAT = 0x130, O_OFF = 0x160, O_FWD = 0x16C,
    O_P16 = 0x190, O_W = 0x1C0, O_WVEC = 0x210, O_CVEL = 0x240, O_SLIPW = 0x270, O_SPDW = 0x280, O_TQ = 0x290, O_FORCE = 0x29C, O_FSCALE = 0x2AC,
    O_STEER0 = 0x2C0, O_STGAIN = 0x2D0, O_STFILT = 0x328, O_HOLD = 0x340, O_FB = 0x344, O_RPMT = 0x348, O_RATIO = 0x34C, O_PSLIP = 0x350, O_LOWREV = 0x354, O_CURVE = 0x358,
    O_SHT0 = 0x35C, O_SHT1 = 0x360, O_INERT = 0x370, O_TQMUL = 0x374, O_FINAL = 0x378, O_GEARS = 0x388, O_R = 0x500
};

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
 * the math module (0x0601B6B0 ..)
 * ------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
void fx_sincos(const Phys *ph, int turns, int *s, int *c)                       /* matrix::sincos 0x06018058: table entry (turns >> 4) & 0xFFF, values x 4 */
{
    unsigned a = 0x060A15A0u + 4u * (((u32)turns >> 4) & 0xFFF);
    *s = pm_r16(ph, a) * 4;
    *c = pm_r16(ph, a + 2) * 4;
}
static int r_sin(const Phys *ph, int rad)                                               /* 0x0601B764 */
{
    int s, c;
    fx_sincos(ph, HM(rad, 0x28BE60DC), &s, &c);
    return s;
}
static int r_atan(int x, int y) { return FM(g_atan(x, y), 0x6487F); }                    /* 0x0601B794 */

static int vecval(const unsigned char *c, int o)                                        /* car::vecval 0x06007BAC */
{
    i64 acc = (i64)g32(c, o) * g32(c, o) + (i64)g32(c, o + 4) * g32(c, o + 4) + (i64)g32(c, o + 8) * g32(c, o + 8);
    return g_sqrt(X16(acc));
}

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
 * pad -> input values
 * ------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
static int get_steer(unsigned char *c)                                                  /* getSteer 0x06008774 */
{
    int in = FM(g32(c, O_IN_STR), 0xD70A);
    int s = g32(c, O_STFILT);
    s = s + FM(in - s, 0xE666);
    p32(c, O_STFILT, s);
    return neg(s);
}

static void getio_pad(Drive *d)                                                         /* pad::getIO_pad 0x0600975C */
{
    unsigned char *c = d->car;
    int word = g16u(c, O_INPUT);
    int s = g32(c, O_IN_STR);
    if (word & 8) {                                                                     /* analog device */
        int a = ((word >> 8) & 0xFF) - 127;
        if (d->cfg.mirror) a = neg(a) - 1;
        s = FM(a * 512, g32(c, O_STGAIN));
        if (s > 0x10000) s = 0x10000;
        if (s < -0x10000) s = -0x10000;
    } else {                                                                            /* digital pad */
        int dir = 0, decay;
        if (word & 0xC0) {
            dir = (word & 0x80) ? 1 : -1;
            if (d->cfg.mirror) dir = -dir;
            int ramp = 0;
            if (s == 0 || (s ^ dir) > 0) ramp = 1;
            if (ramp) {
                int r = FM(g32(c, O_STEER0), iabs(s)) + g32(c, O_STEER0 + 4);
                if (dir < 0) r = neg(r);
                s = s + r;
                if (s > 0x10000) s = 0x10000;
                if (s < -0x10000) s = -0x10000;
                goto steer_done;
            }
            decay = g32(c, O_STEER0 + 0xC);                                             /* counter-steer */
            if (s < 0) decay = neg(decay);
        } else {
            decay = g32(c, O_STEER0 + 8);
            if (s < 0) decay = neg(decay);
        }
        {
            int old = s;
            s = s + decay;
            if ((old ^ s) < 0) s = 0;
        }
    }
steer_done:
    p32(c, O_IN_STR, s);
    /* accelerator and brake ramps (tables 0x06009914 .. 0x06009930) */
    int acc = g32(c, O_IN_ACC) + ((word & 1) ? 0x2000 : -0x10000);
    if (acc > 0x10000) acc = 0x10000;
    if (acc < 0) acc = 0;
    p32(c, O_IN_ACC, acc);
    int brk = g32(c, O_IN_BRK) + ((word & 6) ? 0x10000 : -0x10000);
    if (brk > 0x10000) brk = 0x10000;
    if (brk < 0) brk = 0;
    p32(c, O_IN_BRK, brk);
    if (g8(c, O_TRANS) & 1) {                                                           /* manual transmission */
        int dg = 0;
        if (word & 0x20) dg = 1;
        else if (word & 0x10) dg = -1;
        else goto no_shift;
        int g = g16(c, O_GEAR) + dg;
        if (g > 4) g = 4;
        if (g < 0) g = 0;
        p16(c, O_GEAR, g);
    }
no_shift:;
}

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
 * engine and gearbox (power module)
 * ------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
static int torque_get(Drive *d, int rpm, int thr)                                       /* 0x06008B38 */
{
    unsigned char *c = d->car;
    int ipart = (short)(rpm >> 16);
    int rem = ipart % 100;                                                              /* divider: 32 / 32, remainder */
    int frac = HM(rem << 16, 0x028F5C28);
    int idx = (short)((u32)HM(rpm, 0x0253C825) >> 16);
    unsigned curve = (unsigned)g32(c, O_CURVE);
    int t0 = pm_r32(d->phys, curve + 4u * (unsigned)idx), t1 = pm_r32(d->phys, curve + 4u * (unsigned)idx + 4);
    int t = t0 + FM(frac, t1 - t0);
    t = FM(g32(c, O_TQMUL), t);
    t = FM(thr, t);
    t = FM(t, 0x2C000);
    return HM(g32(c, O_FSCALE), t);
}

static int auto_gear(Drive *d, int thr)                                                 /* 0x06008CE8 */
{
    unsigned char *c = d->car;
    if (g16u(c, O_STATE) == 0) return 1;
    int rpm = g32(c, O_RPM), g = g16(c, O_GEAR);
    int hold = g32(c, O_HOLD) + 1;
    p32(c, O_HOLD, hold);
    if (15 > hold) return g;
    if (rpm > 0x1EF00000 && 0x1999 > g32(c, O_PSLIP)) g++;
    else if (FM(thr, 0x0ABE0000) + 0x0CE40000 > rpm) g--;
    if (1 > g) g = 1;
    else if (g > 4) g = 4;
    if (g16(c, O_GEAR) != g) p32(c, O_HOLD, 0);
    return g;
}

static int brake_force(Drive *d)                                                        /* 0x06008D64 */
{
    unsigned char *c = d->car;
    int b = g32(c, O_IN_BRK);
    p8(c, O_BLIGHT, b != 0);
    if (g16u(c, O_STATE) >= 2) b = 0x8000;
    int r = FM(HM(g32(c, O_FSCALE), 0x05000000), b);
    return r < 0 ? 0 : r;
}

static int power_get(Drive *d)                                                          /* 0x06008810 */
{
    unsigned char *c = d->car;
    int thr = g32(c, O_IN_ACC);
    if (g32(c, O_SHT0) > 0) { p32(c, O_SHT0, g32(c, O_SHT0) - 1); thr = 0x4CCC; }
    if (g32(c, O_SHT1) < 0) p32(c, O_SHT1, g32(c, O_SHT1) + 1);
    if (g16u(c, O_STATE) >= 2) thr = 0;
    int gear = (g8(c, O_TRANS) == 0) ? auto_gear(d, thr) : g16(c, O_GEAR);
    p16(c, O_GEAR, gear);
    int g = 0;
    if (g16u(c, O_STATE) != 0 && g32(c, O_SHT0) == 0) g = gear;
    int ratio = FM(pm_r32(d->phys, (unsigned)g32(c, O_GEARS) + 4u * (unsigned)(g + 1)), g32(c, O_FINAL));
    p32(c, O_RATIO, ratio);
    int R = 0x346DC6 + div64((i64)0x346DC6 << 16, ratio);
    p32(c, O_INERT, R);
    /* the direction the steered wheels point in: Ry(yaw + steer) * (0, 0, 1) */
    int sn, cs;
    fx_sincos(d->phys, HM(g32(c, O_YAW) + g32(c, O_STEER), 0x28BE60DC), &sn, &cs);
    int dir[3] = { neg(sn), 0, cs };
    int vf = X16((i64)g32(c, O_VEL) * dir[0] + (i64)g32(c, O_VEL + 4) * dir[1] + (i64)g32(c, O_VEL + 8) * dir[2]);
    int wrpm = 0;
    if (vf >= 0) wrpm = FM(g32(c, O_RATIO), FM(g32(c, O_SPEED), 0x03BAEDFE));
    if (g != 0) {
        int t = g32(c, O_FB) + wrpm;
        if (t < 0) t = 0;
        else if (!(0x22600000 > t)) t = 0x22600000;
        p32(c, O_RPMT, t);
        int dr = t - g32(c, O_RPM);
        if (0x01900000 > dr) { if (!(dr > -0x01900000)) dr = -0x01900000; } else dr = 0x01900000;
        int rpm = g32(c, O_RPM) + dr;
        p32(c, O_RPM, rpm);
        int e = rpm;
        if (g8(c, O_X5F) == 2) e = FM(0x10000 + FM(g32(c, O_SLIP), 0xF333), e);
        int q = div64((i64)(e - wrpm) << 16, e);
        if (q <= 0) q = neg(q);
        p32(c, O_SLIP, q > 0x10000 ? 0x10000 : q);
    } else {
        p32(c, O_RPM, g32(c, O_RPMT));
        p32(c, O_SLIP, 0);
    }
    p32(c, O_WSTEP, FM(g32(c, O_SPEED), 0x35555));
    p32(c, O_WROT, g32(c, O_WROT) + g32(c, O_WSTEP));
    int rpm = g32(c, O_RPM);
    if (0x03E80000 >= rpm || !(vf > 0)) p32(c, O_SLIP, 0);
    int low = 0x10000;
    if (thr > 0xCCCC && 0x17700000 > rpm) low = 0x10000 - HM(0xAEC33, 0x17700000 - rpm);
    p32(c, O_LOWREV, low);
    p32(c, O_PSLIP, g32(c, O_SLIP));
    int T = torque_get(d, rpm, thr);
    sndev_torque(&d->se, c, T, d->rng);
    int Q = div64((i64)(T - 0x147B) << 32, R);
    int tg = rpm + Q;
    if (tg < 0) tg = 0;
    else if (!(0x22600000 > tg)) tg = 0x22600000;
    p32(c, O_RPMT, tg);
    int h = HM(R, tg - wrpm);
    int F = 0;
    if (g != 0 && 1 >= g16u(c, O_STATE)) F = FM(FM(h, g32(c, O_RATIO)), 0x35555);
    int br = brake_force(d);
    if (vf < 0) br = neg(br);
    return F - br;
}

static void power_use(Drive *d, int applied, int requested)                             /* 0x06008AF8 */
{
    unsigned char *c = d->car;
    int t = FM(g32(c, O_LOWREV), applied) - requested;
    int num = FM(neg(t), 0x5851);
    p32(c, O_FB, div64((i64)num << 32, FM(g32(c, O_INERT), g32(c, O_RATIO))));
}

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
 * the wheels (execCar helpers)
 * ------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
static void push_out(unsigned char *c)                                                  /* FUN_06007954: the car-car collision push vector */
{
    int y = g32(c, O_PUSH + 4);
    if (y < 0) { if (!(0x199A >= neg(y))) y = -0x199A; } else if (!(0x199A >= y)) y = 0x199A;
    p32(c, O_PUSH + 4, y);
    int px = g32(c, O_PUSH), pz = g32(c, O_PUSH + 8);
    i64 acc = (i64)y * y + (i64)px * px + (i64)pz * pz;
    int m2 = X16(acc);                                                                  /* dmuls y,y then two mac.l: y^2 + x^2 + z^2 in one accumulator */
    if ((u32)0x0B73 >= (u32)m2) return;
    p32(c, O_VEL, g32(c, O_VEL) + FM(px, 0x999A));
    p32(c, O_VEL + 8, g32(c, O_VEL + 8) + FM(pz, 0x999A));
    int k = 0x10000 - 0x999A;
    int nx = FM(k, px), nz = FM(k, pz);
    p32(c, O_PUSH, nx);
    p32(c, O_PUSH + 8, nz);
    p32(c, O_POS, g32(c, O_POS) + nx);
    p32(c, O_POS + 8, g32(c, O_POS + 8) + nz);
    p32(c, O_POS + 4, g32(c, O_POS + 4) + g32(c, O_PUSH + 4));
    p32(c, O_PUSH, 0);
    p32(c, O_PUSH + 4, 0);
    p32(c, O_PUSH + 8, 0);
}

/* outer_product_p 0x06007B0C */
static void outer_p(const int a[3], const int b[3], int out[3])
{
    out[0] = (int)(((i64)neg(a[2]) * b[1] + (i64)a[1] * b[2]) >> 24);
    out[1] = (int)(((i64)neg(a[0]) * b[2] + (i64)a[2] * b[0]) >> 24);
    out[2] = (int)(((i64)neg(a[1]) * b[0] + (i64)a[0] * b[1]) >> 24);
}

static void wall_response(unsigned char *c, int w, int off, int wv)                      /* FUN_060078A4 */
{
    int n[3], o[3], t[3], u[3];
    for (int k = 0; k < 3; k++) { n[k] = g32(c, w + 4 * k); o[k] = g32(c, off + 4 * k); }
    outer_p(n, o, t);
    for (int k = 0; k < 3; k++) p32(c, O_MAT + 4 * k, t[k]);
    outer_p(t, n, u);
    for (int k = 0; k < 3; k++) p32(c, O_MAT + 12 + 4 * k, u[k]);
    /* regvect_inner: |u| and (u . o) / |u| */
    int len = g_sqrt(X16((i64)u[0] * u[0] + (i64)u[1] * u[1] + (i64)u[2] * u[2]));
    int proj;
    if (len == 0) proj = o[2];
    else proj = div64((i64)u[0] * o[0] + (i64)u[1] * o[1] + (i64)u[2] * o[2], len);
    int den = g32(c, O_K80) + FM(g32(c, O_K7C), proj);
    int q = div64((i64)0x117C90 << 32, den);
    int r4 = FM(q, g32(c, w + 0xC));
    for (int k = 0; k < 3; k++) p32(c, wv + 4 * k, M24(r4, n[k]));
}

/* FUN_06007718: the four wheel probes, wall contact response */
static void wheel_probe(Drive *d, int px, int py, int pz)
{
    unsigned char *c = d->car;
    const Phys *ph = d->phys;
    int m[12];
    unit_matrix(m);
    t_rotate_y(m, g32(c, 0x128));
    t_rotate_x(m, g32(c, 0x124));
    t_rotate_z(m, g32(c, 0x12C));
    st_mat(c, O_MAT, m);
    int pp[3] = { px << 4, py << 4, pz << 4 };
    for (int i = 0; i < 4; i++) {
        int v[3] = { pm_r32(ph, 0x06007924u + 12u * i), pm_r32(ph, 0x06007924u + 12u * i + 4), pm_r32(ph, 0x06007924u + 12u * i + 8) };
        int o[3];
        calc_xyz(m, v, o);
        for (int k = 0; k < 3; k++) {
            p32(c, O_OFF + 12 * i + 4 * k, o[k] >> 4);
            p32(c, O_P16 + 12 * i + 4 * k, o[k] + pp[k]);
        }
    }
    p32(c, O_WALL, 0);
    int mask = 0;
    for (int i = 0; i < 4; i++) {
        int w = O_W + 0x14 * i, wv = O_WVEC + 12 * i;
        int x4 = g32(c, O_P16 + 12 * i), y4 = g32(c, O_P16 + 12 * i + 4), z4 = g32(c, O_P16 + 12 * i + 8);
        const unsigned char *poly = d->coli ? coli_find(d->coli, x4, z4) : NULL;
        int type = poly ? (short)((u32)poly[2] | (u32)poly[3] << 8) : 1;
        p16(c, w + 0x10, type);
        if (!poly) {                                                                    /* off the map: the original would read address 0; keep the wheel in the air */
            p32(c, w, 0); p32(c, w + 4, 0x1000000); p32(c, w + 8, 0);
            p32(c, w + 0xC, -0x7FFFFFFF);
            for (int k = 0; k < 3; k++) p32(c, wv + 4 * k, 0);
        } else if (type != 0) {
            const unsigned char *pn = poly + 36;
            for (int k = 0; k < 3; k++) p32(c, w + 4 * k, (int)((u32)pn[4 * k] | (u32)pn[4 * k + 1] << 8 | (u32)pn[4 * k + 2] << 16 | (u32)pn[4 * k + 3] << 24));
            p32(c, w + 0xC, coli_y20(poly, x4, z4) - y4);
            for (int k = 0; k < 3; k++) p32(c, wv + 4 * k, 0);
        } else {
            const unsigned char *pn = poly + 36;
            for (int k = 0; k < 3; k++) p32(c, w + 4 * k, (int)((u32)pn[4 * k] | (u32)pn[4 * k + 1] << 8 | (u32)pn[4 * k + 2] << 16 | (u32)pn[4 * k + 3] << 24));
            mask |= 1 << i;
            int dd = coli_wall_d(poly, x4, z4);
            int mx = g32(c, O_WALL);
            if (!(mx >= dd)) { mx = dd; p32(c, O_WALL, mx); }
            p32(c, w + 0xC, mx);
            wall_response(c, w, O_OFF + 12 * i, wv);
        }
    }
    p8(c, O_WALLMASK, mask);
    if (g32(c, O_WALL) == 0) return;
    /* wall contact: the pushes act on the velocity at once and turn the car */
    int sx = g32(c, O_WVEC) + g32(c, O_WVEC + 12) + g32(c, O_WVEC + 24) + g32(c, O_WVEC + 36);
    int vx = g32(c, O_VEL) + HM(sx, 0x00418937);
    p32(c, O_VEL, vx);
    p32(c, O_PPOS, vx + g32(c, O_POS));
    int sz = g32(c, O_WVEC + 8) + g32(c, O_WVEC + 20) + g32(c, O_WVEC + 32) + g32(c, O_WVEC + 44);
    int vz = g32(c, O_VEL + 8) + HM(sz, 0x00418937);
    p32(c, O_VEL + 8, vz);
    p32(c, O_PPOS + 8, vz + g32(c, O_POS + 8));
    int acc9 = 0;
    for (int i = 0; i < 4; i++) {
        int ox = g32(c, O_OFF + 12 * i), oz = g32(c, O_OFF + 12 * i + 8);
        int wx = g32(c, O_WVEC + 12 * i), wz = g32(c, O_WVEC + 12 * i + 8);
        acc9 -= X16((i64)neg(ox) * wz + (i64)oz * wx);
    }
    int ay = g32(c, O_AVEL + 4) + HM(0x01C97E70, acc9);
    p32(c, O_AVEL + 4, ay);
    p32(c, 0x128, (ay >> 4) + g32(c, O_YAW));
}

/* FUN_0600760C: the four body corner points of the car at position pos / angles ang, written to out (x, y, z each) */
static void body_corners(Drive *d, int out, int ang, int pos)
{
    unsigned char *c = d->car;
    int m[12] = { 0x10000, 0, 0, g32(c, pos), 0, 0x10000, 0, g32(c, pos + 4), 0, 0, 0x10000, g32(c, pos + 8) };
    t_rotate_y(m, g32(c, ang + 4));
    t_rotate_x(m, g32(c, ang));
    t_rotate_z(m, g32(c, ang + 8));
    st_mat(c, O_MAT, m);
    for (int i = 0; i < 4; i++) {
        int v[3] = { pm_r32(d->phys, 0x06007674u + 12u * i), pm_r32(d->phys, 0x06007674u + 12u * i + 4), pm_r32(d->phys, 0x06007674u + 12u * i + 8) };
        int o[3];
        calc_point(m, v, o);
        for (int k = 0; k < 3; k++) p32(c, out + 12 * i + 4 * k, o[k]);
    }
}

/* FUN_06007080: wheels that touched a wall are probed again at the body corners; then the corner velocities */
static void reprobe(Drive *d)
{
    unsigned char *c = d->car;
    int mask = g8(c, O_WALLMASK);
    if (mask) {
        for (int i = 0; i < 4; i++) {
            if (!((mask >> i) & 1)) continue;
            int x4 = g32(c, O_P16 + 12 * i) << 4, y4 = g32(c, O_P16 + 12 * i + 4) << 4, z4 = g32(c, O_P16 + 12 * i + 8) << 4;
            const unsigned char *poly = d->coli ? coli_find(d->coli, x4, z4) : NULL;
            int w = O_W + 0x14 * i;
            if (!poly) continue;
            int type = (short)((u32)poly[2] | (u32)poly[3] << 8);
            p16(c, w + 0x10, type);
            if (type != 0) {
                const unsigned char *pn = poly + 36;
                for (int k = 0; k < 3; k++) p32(c, w + 4 * k, (int)((u32)pn[4 * k] | (u32)pn[4 * k + 1] << 8 | (u32)pn[4 * k + 2] << 16 | (u32)pn[4 * k + 3] << 24));
                p32(c, w + 0xC, coli_y20(poly, x4, z4) - y4);
            }
        }
    }
    for (int i = 0; i < 12; i++) p32(c, O_CVEL + 4 * i, g32(c, O_P16 + 4 * i) - g32(c, O_CVEL + 4 * i));
}

/* FUN_0600755C: body slip angle, body forward vector and the steered wheel direction */
static void slip_angle(Drive *d)
{
    unsigned char *c = d->car;
    int vx = g32(c, O_VEL), vy = g32(c, O_VEL + 4), vz = g32(c, O_VEL + 8);
    int v2 = X16((i64)vx * vx + (i64)vy * vy + (i64)vz * vz);
    int m[12];
    unit_matrix(m);
    t_rotate_y(m, g32(c, O_YAW));
    st_mat(c, O_MAT, m);
    int fx = m[2] + m[3], fy = m[6] + m[7], fz = m[10] + m[11];
    p32(c, O_FWD, fx); p32(c, O_FWD + 4, fy); p32(c, O_FWD + 8, fz);
    if (0x199A >= v2) {
        p32(c, O_BSLIP, 0);
        p32(c, O_BSLIP + 4, 0);
    } else {
        int lat = X16((i64)neg(fx) * vz + (i64)fz * vx);
        int lat2 = FM(lat, lat);
        int a = r_atan(v2 - lat2, lat2);
        if (lat < 0) a = neg(a);
        if (a > 0x19220) a = 0x19220;
        else if (-0x19220 > a) a = -0x19220;
        int old = g32(c, O_BSLIP);
        p32(c, O_BSLIP, a);
        p32(c, O_BSLIP + 4, a - old);
    }
    t_rotate_y(m, g32(c, O_STEER));
    st_mat(c, O_MAT, m);
    p32(c, O_OFF, m[2] + m[3]); p32(c, O_OFF + 4, m[6] + m[7]); p32(c, O_OFF + 8, m[10] + m[11]);
}

/* FUN_060074EC: sine of the slip angle of a wheel (corner velocity vs the wheel direction) */
static int wheel_slip(unsigned char *c, int cv, int dir, int *zero_speed)
{
    int vx = g32(c, cv), vz = g32(c, cv + 8), dx = g32(c, dir), dz = g32(c, dir + 8);
    int vxz2 = X16((i64)vx * vx + (i64)vz * vz);
    if (vxz2 == 0) { *zero_speed = 1; return 0; }
    int dot = X16((i64)vx * dx + (i64)vz * dz);
    int ratio = div64((i64)dot * dot, vxz2);
    i64 cross = (i64)vx * dz + (i64)neg(vz) * dx;
    int hi = (int)(cross >> 32);
    if (ratio >= 0x10000) return 0;
    int r = g_sqrt(0x10000 - ratio);
    if (hi < 0) r = neg(r);
    return r;
}

/* FUN_060073C8: suspension of one wheel; returns 1 when the wheel carries load */
static int suspension(Drive *d, int i, int kst, int kd, int prev_load_other)
{
    unsigned char *c = d->car;
    int w = O_W + 0x14 * i, wv = O_WVEC + 12 * i, lo = O_LOAD + 4 * i, cvp = O_CVEL + 12 * i;
    int h = g32(c, w + 0xC);
    p32(c, O_GROUND + 4 * i, g32(c, O_P16 + 12 * i + 4) + (h >> 4));
    int type = g16u(c, w + 0x10);
    if (type == 0 || h < 0) {
        p32(c, lo, 0);
        for (int k = 0; k < 3; k++) p32(c, wv + 4 * k, 0);
        if (prev_load_other > 0) p32(c, wv + 4, FM(HM(0x0C000000, g32(c, w + 4)), h));
        p32(c, O_SLIPW + 4 * i, 0);
        p32(c, O_SPDW + 4 * i, 0);
        return 0;
    }
    int F = g32(c, O_FSCALE);
    int K = FM(0x640000, kst);
    i64 prod = (i64)h * M24(K, F);
    int hc = h, Fs;
    if (h > 0x33333) {
        Fs = (int)(prod >> 24);
        Fs += FM(h - 0x33333, 0xFA0000);
        hc = 0x33333;
        p32(c, w + 0xC, 0x33333);
    } else {
        Fs = (int)(prod >> 24);
    }
    int cd = HM(F, FM(kd, 0x03840000));
    int dh = g32(c, lo) - hc;
    if (!(iabs(dh) >= 0x80)) dh = iabs(dh);
    p32(c, lo, hc);
    int Fn = Fs - FM(cd, dh);
    if (Fn < 0) Fn = 0;
    for (int k = 0; k < 3; k++) p32(c, wv + 4 * k, M24(g32(c, w + 4 * k), Fn));
    int spd = vecval(c, cvp);
    if (!(spd > 0)) {
        p32(c, O_SLIPW + 4 * i, 0);
        p32(c, O_SPDW + 4 * i, 0);
    } else {
        int zero = 0;
        p32(c, O_SPDW + 4 * i, spd);
        int sl = wheel_slip(c, cvp, (i & 2) ? O_FWD : O_OFF, &zero);
        if (zero) p32(c, O_SPDW + 4 * i, 0);
        p32(c, O_SLIPW + 4 * i, sl);
    }
    return 1;
}

/* FUN_06007304: surface types of the wheels and the suspension loop */
static void suspension_all(Drive *d)
{
    unsigned char *c = d->car;
    int or_mask = 0, nib = 0;
    for (int i = 3; i >= 0; i--) {
        int type = g16u(c, O_W + 0x14 * i + 0x10);
        nib = (nib << 4) | type;
        or_mask |= pm_r16u(d->phys, 0x06008544u + 2u * (unsigned)type);
    }
    p16(c, O_SURFOR, or_mask);
    p16(c, O_SURF, nib);
    p8(c, O_PCONT, g8(c, O_CONT));
    p8(c, O_CONT, 0);
    unsigned dyn = (unsigned)g32(c, O_DYN);
    const unsigned char *dy = d->dyn + (dyn - 0x060A9400u);
    for (int i = 0; i < 4; i++) {
        int rear = (i & 2) != 0;
        int kst = g32(dy, 0x14 + (rear ? 4 : 0)), kd = g32(dy, 0x1C + (rear ? 4 : 0));
        int other = g32(c, O_PLOAD + 4 * (i ^ 1));
        if (suspension(d, i, kst, kd, other)) p8(c, O_CONT, g8(c, O_CONT) | (1 << i));
    }
    for (int i = 0; i < 4; i++) p32(c, O_PLOAD + 4 * i, g32(c, O_LOAD + 4 * i));
}

/* FUN_0600716C: spring forces into the body frame -> torques, total spring force */
static void spring_torques(Drive *d)
{
    unsigned char *c = d->car;
    int m[12];
    unit_matrix(m);
    t_rotate_z(m, neg(g32(c, O_ROLL)));
    t_rotate_x(m, neg(g32(c, O_ANG)));
    t_rotate_y(m, neg(g32(c, O_YAW)));
    st_mat(c, O_MAT, m);
    for (int i = 0; i < 4; i++) {
        int v[3] = { g32(c, O_WVEC + 12 * i), g32(c, O_WVEC + 12 * i + 4), g32(c, O_WVEC + 12 * i + 8) }, o[3];
        calc_xyz(m, v, o);
        for (int k = 0; k < 3; k++) p32(c, O_P16 + 12 * i + 4 * k, o[k]);
    }
    int Fx[4], Fy[4], Fz[4];
    for (int i = 0; i < 4; i++) { Fx[i] = g32(c, O_P16 + 12 * i); Fy[i] = g32(c, O_P16 + 12 * i + 4); Fz[i] = g32(c, O_P16 + 12 * i + 8); }
    i64 acc = (i64)(Fy[0] + Fy[1] - Fy[2] - Fy[3]) * 0x0159999A + (i64)(Fz[0] + Fz[1] + Fz[2] + Fz[3]) * 0x00333333;
    p32(c, O_TQ, (int)(acc >> 24));                                                   /* pitch */
    int sumx = neg(Fx[0] + Fx[1] + Fx[2] + Fx[3]);
    acc = (i64)sumx * 0x00333333 + (i64)(Fy[0] - Fy[1]) * 0x00A28F5C + (i64)(Fy[2] - Fy[3]) * 0x00A8F5C3;
    int roll = (int)(acc >> 24);
    p32(c, O_TQ + 8, roll);
    acc = (i64)(Fx[2] + Fx[3] - Fx[0] - Fx[1]) * 0x0159999A + (i64)(Fz[1] - Fz[0]) * 0x00A28F5C + (i64)(Fz[3] - Fz[2]) * 0x00A8F5C3;
    for (int k = 0; k < 3; k++)
        p32(c, O_FORCE + 4 * k, g32(c, O_WVEC + 4 * k) + g32(c, O_WVEC + 12 + 4 * k) + g32(c, O_WVEC + 24 + 4 * k) + g32(c, O_WVEC + 36 + 4 * k));
    p32(c, O_TQ + 4, (int)(acc >> 24));                                                /* yaw */
    int depth = g32(c, O_WALL);
    if (depth != 0) {
        int v[3] = { g32(c, O_VEL), g32(c, O_VEL + 4), g32(c, O_VEL + 8) }, vb[3];
        calc_xyz(m, v, vb);
        for (int k = 0; k < 3; k++) p32(c, O_P16 + 4 * k, vb[k]);
        int t = FM(FM(vb[2], 0x01400000), depth);
        p32(c, O_TQ, g32(c, O_TQ) - t);
        t = FM(FM(vb[0], 0x03200000), depth);
        p32(c, O_TQ + 8, g32(c, O_TQ + 8) + t);
    }
}

/* FUN_06009718: rotate (x, z) of a vector by an angle (radians 16.16) with the sine table */
static void rotate_xz(Drive *d, int vec[3], int ang)
{
    int s, cs;
    fx_sincos(d->phys, HM(ang, 0x28BE60DB), &s, &cs);
    int x = vec[0], z = vec[2];
    vec[0] = X16((i64)z * neg(s) + (i64)x * cs);
    vec[2] = X16((i64)z * cs + (i64)x * s);
}

/* carctrl::carControl 0x06009124: tyres, engine, brakes -> force and torque accumulators */
static void car_control(Drive *d)
{
    unsigned char *c = d->car;
    const Phys *ph = d->phys;
    int or_mask = g16u(c, O_SURFOR), idx = 10, bit = 0x0400;
    while (!(or_mask & bit)) { if (--idx == 0) break; bit >>= 1; }
    p16(c, O_SURFIDX, idx);
    unsigned dynp = (unsigned)pm_r32(ph, 0x06009D80u + 4u * (unsigned)idx) + (d->slot1 ? 0xD0u : 0u);          /* (every slot but slot 0 uses the second player's block) */
    p32(c, O_DYN, (int)dynp);
    const unsigned char *dy = d->dyn + (dynp - 0x060A9400u);
    int Fdrive = power_get(d);
    int l0 = g32(c, O_LOAD), l1 = g32(c, O_LOAD + 4), l2 = g32(c, O_LOAD + 8), l3 = g32(c, O_LOAD + 12);
    int Nf = l0 + l1, Nr = l2 + l3;
    int xferF = Nf ? div64((i64)(l1 - l0) * 0x119999, Nf) : 0;
    int xferR = Nr ? div64((i64)(l3 - l2) * 0x119999, Nr) : 0;
    int Fdf = 0, Fdr = 0;
    if (Nf != 0 && Nr != 0) {
        int split = (g8(c, O_X5F) == 2) ? 0x3333 : g32(dy, 0x08);
        Fdf = FM(Fdrive, split);
        Fdr = Fdrive - Fdf;
    }
    int spdfac = FM(g32(c, O_SPEED), 0x2666) + 0x10000;
    int gripF = Nf, gripR = Nr;
    if (Nf + Nr == 0) p32(c, O_BIAS, 0);
    else {
        int bias = div64((i64)((Nf - Nr) << 4) << 16, Nf + Nr);
        int delta = bias - (g32(c, O_BIAS) & 0xFE);
        int gain = delta > 0 ? g32(dy, 0x24) : g32(dy, 0x28);
        int b2 = bias - FM(gain, delta);
        int sn = r_sin(ph, HM(b2, 0x3243F000));
        int w = b2 + FM(b2 > 0 ? 0x147AE : 0, sn);
        p32(c, O_BIAS, w);
        gripF = FM(g32(dy, 0), 0x100000 + w);
        gripR = FM(g32(dy, 4), 0x100000 - w);
    }
    int F = g32(c, O_FSCALE);
    /* front axle */
    int sF = g32(c, O_AXSLIP);
    if (0x28F > iabs(sF)) sF = 0;
    else sF = sF >= 0 ? sF - 0x28F : sF + 0x28F;
    p32(c, O_AXSLIP, sF);
    int capF = HM(0x3E8000, FM(spdfac, FM(F, (int)((u32)gripF >> 4)))) << 4;
    int a = iabs(FM(0x2182A, sF));
    if (a > 0x19220) a = 0x19220;
    int latF = FM(r_sin(ph, a), capF);
    if (sF >= 0) latF = neg(latF);
    int mag = g_sqrt(FM(Fdf, Fdf) + FM(latF, latF));
    int lim = FM(mag, g32(c, O_SLIP) + 0x10000);
    if (lim > capF) {
        int rho = div64((i64)capF << 16, lim);
        int g = FM(0x13333, g_sqrt(rho)) + FM(0xFFFFCCCD, FM(rho, rho));
        Fdf = FM(g, Fdf);
        latF = FM(latF, FM(g, 0x11999));
    }
    int r6 = FM(iabs(sF), g32(c, O_AXSPD));
    if (0x51E > r6) latF = FM(50 * r6, latF);
    if (Fdf < 0) xferF = 0;
    /* rear axle */
    int capR = HM(0x3E8000, FM(spdfac, FM(F >> 2, gripR))) << 2;
    int sR = g32(c, O_AXSLIP + 4);
    a = iabs(FM(0x3243F, sR));
    if (a > 0x1921F) a = 0x1921F;
    int latR = FM(capR, r_sin(ph, a));
    mag = g_sqrt(FM(Fdr, Fdr) + FM(latR, latR));
    lim = FM(mag, g32(c, O_SLIP) + 0x10000);
    if (lim > capR) {
        int rho = div64((i64)capR << 16, lim);
        int g = FM(0x6666, g_sqrt(rho)) + FM(0x999A, FM(rho, rho));
        Fdr = FM(g, Fdr);
        latR = FM(latR, FM(g, 0x11999));
    }
    if (sR >= 0) latR = neg(latR);
    r6 = FM(iabs(sR), g32(c, O_AXSPD + 4));
    if (0x51E > r6) latR = FM(50 * r6, latR);
    if (Fdr < 0) xferR = 0;
    power_use(d, Fdf + Fdr, Fdrive);
    if (Fdf == 0 || Fdr == 0) p32(c, O_SLIP, 0);
    int bs = iabs(g32(c, O_BSLIP));
    if (g8(c, O_X5F) == 2) {
        if (0x2666 > g32(c, O_SLIP)) p32(c, O_SLIP, (bs > 0x6666 && g32(c, O_AXSPD + 4) > 0x3333) ? 0x2666 : 0);
    } else if (g32(c, O_SLIP) == 0 && bs > 0x6666 && g32(c, O_AXSPD + 4) > 0x3333) {
        p32(c, O_SLIP, 0x28F);
    }
    /* the force of the tyres in the car frame (x16), then the torques */
    int r9 = Fdf, r10 = Fdr;
    if (r9 < 0) { r10 += r9; r9 = 0; }
    r9 <<= 4;
    r10 <<= 4;
    int vec[3] = { latF << 4, 0, r9 };
    rotate_xz(d, vec, g32(c, O_STEER));
    int t1 = FM(r10, 0xA8F5C) >> 4;
    int t2 = FM(xferF, 0xA28F5) >> 4;
    int yawTq = (FM(xferR, t1) >> 4) - (FM(r9, t2) >> 4);
    int lr = latR << 4, vx = vec[0];
    yawTq += (lr + (lr >> 2)) - (vx + (vx >> 2));
    vec[0] = lr + vx;
    vec[2] += r10;
    p32(c, O_TQ, g32(c, O_TQ) + (FM(0x33333, vec[2]) >> 4));
    p32(c, O_TQ + 8, g32(c, O_TQ + 8) - (FM(0x33333, vec[0]) >> 4));
    p32(c, O_TQ + 4, g32(c, O_TQ + 4) + yawTq);
    rotate_xz(d, vec, g32(c, O_YAW));
    for (int k = 0; k < 3; k++) p32(c, O_FORCE + 4 * k, g32(c, O_FORCE + 4 * k) + vec[k]);
}

static void torque_to_avel(unsigned char *c)                                           /* FUN_06007020 */
{
    p32(c, O_AVEL, g32(c, O_AVEL) + HM(g32(c, O_TQ), 0x1C97E7));
    p32(c, O_AVEL + 4, g32(c, O_AVEL + 4) + HM(g32(c, O_TQ + 4), 0x1C97E7));
    p32(c, O_AVEL + 8, g32(c, O_AVEL + 8) + HM(g32(c, O_TQ + 8), 0x369D03));
    for (int k = 0; k < 3; k++) {
        int v = g32(c, O_AVEL + 4 * k);
        if (!(0x33333 >= iabs(v))) p32(c, O_AVEL + 4 * k, v >= 0 ? 0x33333 : -0x33333);
    }
}

static void air_damping(Drive *d)                                                      /* FUN_06006FA4 */
{
    unsigned char *c = d->car;
    if (g8(c, O_CONT) != 0x0F) {
        int pitch = g32(c, O_ANG);
        int t = FM(pitch, FM(0x6B85, pitch));
        if (pitch < 0) t = neg(t);
        p32(c, O_ANG, pitch - t);
        int roll = g32(c, O_ROLL);
        if (((g32(c, O_AVEL + 8) ^ roll) & 0x80000000u) == 0) {
            int k = g16u(c, O_SURFIDX);
            k = (k == 6 || k == 7) ? 0x8000 : 0x13330;
            t = FM(roll, FM(k, roll));
            if (roll < 0) t = neg(t);
            p32(c, O_ROLL, roll - t);
        }
    }
    const unsigned char *dy = d->dyn + ((unsigned)g32(c, O_DYN) - 0x060A9400u);               /* the yaw stabiliser: always */
    i64 acc = (i64)g32(dy, 0x0C) * g32(c, O_BSLIPD) + (i64)g32(dy, 0x10) * g32(c, O_AVEL + 4);
    p32(c, O_AVEL + 4, g32(c, O_AVEL + 4) - (int)(acc >> 24));
}

static void wall_assist(unsigned char *c)                                              /* FUN_06006F60: a car stuck at a wall turns towards the course */
{
    if (g32(c, O_WALL) == 0 || g32(c, O_SPEED) > 0x17B4) return;
    i64 acc = (i64)neg(g32(c, O_FWD)) * g32(c, O_DIRZ) + (i64)g32(c, O_FWD + 8) * g32(c, O_DIRX);
    int v = (int)(acc >> 12);
    p32(c, O_AVEL + 4, g32(c, O_AVEL + 4) - HM(v, 0x30413EB6));
}

static void integrate(unsigned char *c)                                                /* FUN_06006EE4 */
{
    p32(c, O_VEL, g32(c, O_VEL) + (int)(((i64)g32(c, O_FORCE) * 0x00418937) >> 36));
    i64 acc = (i64)g32(c, O_FORCE + 4) * 0x00418937 + (i64)g32(c, O_FSCALE) * (i64)(int)0xFFF00000;
    p32(c, O_VEL + 4, g32(c, O_VEL + 4) + (int)(acc >> 36));
    p32(c, O_VEL + 8, g32(c, O_VEL + 8) + (int)(((i64)g32(c, O_FORCE + 8) * 0x00418937) >> 36));
    for (int k = 0; k < 3; k++) p32(c, O_ANG + 4 * k, g32(c, O_ANG + 4 * k) + (g32(c, O_AVEL + 4 * k) >> 4));
}

static void drag(Drive *d)                                                             /* FUN_06006E24 */
{
    unsigned char *c = d->car;
    int depth = g32(c, O_WALL);
    unsigned dyn = (unsigned)g32(c, O_DYN);
    const unsigned char *dy = d->dyn + (dyn - 0x060A9400u);
    if (depth > 0) {
        int f;
        if (d->cfg.course == 3) f = 0xFD71 - FM(0xF000, depth);
        else f = 0xFEF9 - FM(g32(dy, 0x30), depth);
        if (f < 0) f = 0;
        for (int k = 0; k < 3; k++) p32(c, O_VEL + 4 * k, FM(f, g32(c, O_VEL + 4 * k)));
    } else if (g8(c, O_CONT) != 0) {
        int f = g32(dy, 0x2C);
        p32(c, O_VEL, FM(f, g32(c, O_VEL)));
        p32(c, O_VEL + 8, FM(f, g32(c, O_VEL + 8)));
    }
    if (g16u(c, O_STATE) == 2) {
        p32(c, O_VEL, FM(g32(c, O_VEL), 0xF700));
        p32(c, O_VEL + 8, FM(g32(c, O_VEL + 8), 0xF700));
    }
}

static void speed_cap(unsigned char *c)                                                /* FUN_06006DAC: 250 km/h */
{
    int sp = vecval(c, O_VEL);
    if (0x25097 >= sp) return;
    int v[3] = { g32(c, O_VEL), g32(c, O_VEL + 4), g32(c, O_VEL + 8) };
    for (int k = 0; k < 3; k++) p32(c, O_VEL + 4 * k, div64((i64)0x25097 * v[k], sp));
}

static void wrap_angles(unsigned char *c)                                              /* FUN_06006DF4 */
{
    for (int k = 0; k < 3; k++) {
        int a = g32(c, O_ANG + 4 * k);
        if (a < 1) { if (a < -0x3243F) p32(c, O_ANG + 4 * k, a + 0x6487F); }
        else if (0x3243F < a) p32(c, O_ANG + 4 * k, a - 0x6487F);
    }
}

/* player::execCar 0x06006C4C: one step of the car */
static void exec_car(Drive *d)
{
    unsigned char *c = d->car;
    getio_pad(d);
    p32(c, O_STEER, g8(c, O_CTRL) == 8 ? 0 : get_steer(c));
    push_out(c);
    int x = g32(c, O_POS) + g32(c, O_VEL), y = g32(c, O_POS + 4) + g32(c, O_VEL + 4), z = g32(c, O_POS + 8) + g32(c, O_VEL + 8);
    p32(c, O_PPOS, x); p32(c, O_PPOS + 4, y); p32(c, O_PPOS + 8, z);
    p32(c, O_PANG, (g32(c, O_AVEL) >> 4) + g32(c, O_ANG));
    p32(c, O_PANG + 4, (g32(c, O_AVEL + 4) >> 4) + g32(c, O_YAW));
    p32(c, O_PANG + 8, (g32(c, O_AVEL + 8) >> 4) + g32(c, O_ROLL));
    wheel_probe(d, x, y, z);
    body_corners(d, O_P16, O_PANG, O_PPOS);
    body_corners(d, O_CVEL, O_ANG, O_POS);
    reprobe(d);
    slip_angle(d);
    suspension_all(d);
    int mk = 3;
    for (int k = 0; k < 2; k++) {
        int s = g32(c, O_SLIPW + 8 * k) + g32(c, O_SLIPW + 8 * k + 4);
        int w = g32(c, O_SPDW + 8 * k) + g32(c, O_SPDW + 8 * k + 4);
        if ((g8(c, O_CONT) & mk) == mk) { s >>= 1; w >>= 1; }
        p32(c, O_AXSLIP + 4 * k, s);
        p32(c, O_AXSPD + 4 * k, w);
        mk <<= 2;
    }
    spring_torques(d);
    int sp = vecval(c, O_VEL);
    p32(c, O_SPEED, sp);
    p32(c, O_SPEED2, sp);
    car_control(d);
    torque_to_avel(c);
    air_damping(d);
    wall_assist(c);
    integrate(c);
    if (g8(c, O_CONT) == 0) p32(c, O_VEL + 4, g32(c, O_VEL + 4) - 0x7C);
    if (g16u(c, O_STATE) == 0) { p32(c, O_VEL, 0); p32(c, O_VEL + 8, 0); }
    for (int k = 0; k < 3; k++) p32(c, O_POS + 4 * k, g32(c, O_POS + 4 * k) + g32(c, O_VEL + 4 * k));
    drag(d);
    speed_cap(c);
    wrap_angles(c);
}


/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
 * course position (get_block), wrong-way detection
 * ------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
static int rd32le(const unsigned char *p) { return (int)((u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24); }

static void get_block(Drive *d)                                                        /* car::get_block 0x06007EAC */
{
    unsigned char *c = d->car;
    int x = g32(c, O_POS), z = g32(c, O_POS + 8);
    const unsigned char *q = d->coli ? coli_find_quad(d->coli, x, z) : NULL;
    if (!q) return;                                                                     /* the original executes a debug trap here */
    int a = (d->cfg.game_type == 10) ? q[2] : q[0];
    p8(c, O_BLK1, a | (q[1] << 4));
    p8(c, O_BSEG, g8(c, O_BLK0));
    p8(c, O_BLK0, q[3]);
    const unsigned char *s = coli_find_slice(d->coli, q[3], x, z);
    if (!s) return;
    p16(c, O_PSEG, g16u(c, O_SEG));
    p32(c, O_DIRX, rd32le(s + 36));
    p32(c, O_DIRZ, rd32le(s + 40));
    p8(c, O_CKPT, s[0]);
    p8(c, O_SECT, s[1]);
    p16(c, O_SEG, s[3]);
}

static int calc_rad(const int A[3], const int B[3])                                    /* car::calc_rad 0x06007C7C */
{
    int a2 = X16((i64)A[0] * A[0] + (i64)A[1] * A[1] + (i64)A[2] * A[2]);
    int b2 = X16((i64)B[0] * B[0] + (i64)B[1] * B[1] + (i64)B[2] * B[2]);
    int prod = FM(a2, b2);
    int dot = X16((i64)A[0] * B[0] + (i64)A[1] * B[1] + (i64)A[2] * B[2]);
    int dot2 = FM(dot, dot);
    int rad = r_atan(prod - dot2, dot2);
    i64 cross = (i64)neg(A[2]) * B[0] + (i64)A[0] * B[2];
    if (dot < 0) rad = neg(rad);
    int r = 0x19220 - rad;
    if ((int)(cross >> 32) < 0) r = neg(r);
    return r;
}

static void calc_reverse(Drive *d)                                                     /* car::calc_reverse 0x06007CF8 */
{
    unsigned char *c = d->car;
    p32(c, O_P16, g32(c, O_DIRX));
    p32(c, O_P16 + 4, 0);
    p32(c, O_P16 + 8, g32(c, O_DIRZ));
    int A[3] = { g32(c, O_P16), 0, g32(c, O_P16 + 8) };
    int B[3] = { g32(c, O_FWD), g32(c, O_FWD + 4), g32(c, O_FWD + 8) };
    int rad = calc_rad(A, B);
    p32(c, O_REL, rad);
    int turns = HM(rad, 0x28BE60DB);
    p8(c, O_HEAD, ((turns - 0x6000) >> 14) & 3);
}

static void calc_dispang2(Drive *d)                                                    /* car::calc_dispang2 0x06007D68 */
{
    unsigned char *c = d->car;
    int a = g_atan(g32(c, O_DIRX), g32(c, O_DIRZ));
    int r = (short)(a - 0x4000);
    r = (short)(r + d->cam_ang1 + d->cam_ang2);
    r = neg(r);
    p8(c, O_DSPANG, ((r << 4) >> 16) & 0xF);
}

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
 * start-up: carconfig, mycar_init, the state functions
 * ------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
enum { FN0_PLINIT = 0x06006030, FN0_WAIT = 0x06006228, FN0_MOVE = 0x060062F4, FN0_GOALI = 0x0600640C, FN0_GOAL = 0x06006470, FN0_GOVER = 0x060063F0,
       FN4_START = 0x06006638, FN4_MT = 0x06006754, FN4_GOAL = 0x060068E0,
       FN4_RSTART = 0x06006710, FN4_RMT = 0x0600680C, FN4_ESTART = 0x060066F8, FN4_EMT = 0x06006848 };                                  /* the replay's (play mode 2) count-down and running presentations: count_down_start_replay 0x06013F30, replay_plmt */

static void set_fn(unsigned char *c, int slot_off, int fn) { p32(c, slot_off, fn | (g32(c, slot_off) & 1)); }       /* keeps the bit-0 flag like the original's shlr / movt / or */

static void carconfig(Drive *d)                                                        /* pad::carconfig 0x06009938 (player 1; the second player, d->slot1, in the game types above 2) */
{
    unsigned char *c = d->car;
    const Phys *ph = d->phys;
    const int *su = d->cfg.setup;
    unsigned char *dy = d->dyn + (d->slot1 ? 0xD0 : 0);                                 /* the second player (the ghost) has its own block behind player 1's */
    int idx = su[0];
    p32(c, O_STGAIN, pm_r32(ph, 0x06009A98u + 4u * (unsigned)idx));
    for (int k = 0; k < 4; k++) p32(c, O_STEER0 + 4 * k, pm_r32(ph, 0x06009AACu + 16u * (unsigned)idx + 4u * k));
    if (d->cfg.car_type == 2) {                                                         /* the special car: its own table, no tuning */
        for (int k = 0; k < 0x34; k++) p32(dy, 4 * k, pm_r32(ph, 0x06009DBCu + 4u * k));
        return;
    }
    for (int k = 0; k < 0x34; k++) p32(dy, 4 * k, pm_r32(ph, 0x060A14D0u + 4u * k));
    if (d->cfg.setup_mode != 1) return;
    for (int k = 0; k < 4; k++) {                                                       /* tyres: grip */
        int v = pm_r32(ph, 0x06009AFCu + 16u * (unsigned)su[1] + 4u * k);
        p32(dy, 0x34 * k + 0, v);
        p32(dy, 0x34 * k + 4, v);
    }
    int a = su[2], b = su[3];
    for (int k = 0; k < 4; k++) {                                                       /* springs, dampers */
        p32(dy, 0x34 * k + 0x14, pm_r32(ph, 0x06009B4Cu + 16u * (unsigned)a + 4u * k));
        p32(dy, 0x34 * k + 0x18, pm_r32(ph, 0x06009B4Cu + 16u * (unsigned)b + 4u * k));
        p32(dy, 0x34 * k + 0x1C, pm_r32(ph, 0x06009B9Cu + 16u * (unsigned)a + 4u * k));
        p32(dy, 0x34 * k + 0x20, pm_r32(ph, 0x06009B9Cu + 16u * (unsigned)b + 4u * k));
    }
    unsigned row = 0x06009BECu + 16u * (unsigned)(a * 5 + b);
    int r2 = pm_r32(ph, row), r3 = pm_r32(ph, row + 4), r4 = pm_r32(ph, row + 8), r5 = pm_r32(ph, row + 12);
    for (int k = 0; k < 4; k++) {
        p32(dy, 0x34 * k + 0x0C, r2);
        p32(dy, 0x34 * k + 0x10, k == 3 ? r4 : r3);
        if (r5) p32(dy, 0x34 * k + 0x08, r5);
    }
}

static void mycar_init(Drive *d)                                                       /* plctrl::mycar_init 0x0600609C */
{
    unsigned char *c = d->car;
    const Phys *ph = d->phys;
    p16(c, O_STATE, 0);
    p8(c, O_CTRL, 4);
    unsigned pt = 0x002996D0u + 16u * (unsigned)d->cfg.course;
    p32(c, O_POS, pm_r32(ph, pt)); p32(c, O_POS + 4, pm_r32(ph, pt + 4)); p32(c, O_POS + 8, pm_r32(ph, pt + 8));
    p32(c, O_YAW, pm_r32(ph, pt + 12));
    p32(c, O_NEGYAW, neg(pm_r32(ph, pt + 12)));
    p32(c, O_K7C, 0x03E80000);
    p32(c, O_K80, 0x08F40000);
    p32(c, O_K84, 0x08F40000);
    p32(c, O_K88, 0x04B00000);
    for (int k = 0; k < 4; k++) p32(c, O_LOAD + 4 * k, 0x33333);
    p32(c, O_FSCALE, g8(c, O_X5F) == 2 ? 0x039670B2 : 0x02DEC08F);
    p32(c, O_RPM, 0x03200000);
}

static void power_init(Drive *d)                                                       /* power::PowerInit 0x0600879C */
{
    unsigned char *c = d->car;
    p32(c, O_HOLD, 60);
    p32(c, O_SHT0, 0);
    p32(c, O_SHT1, 0);
    p32(c, 0x364, 0); p32(c, 0x368, 0); p32(c, 0x36C, 0);
    p32(c, O_RPMT, 0x03200000);
    p32(c, O_PSLIP, 0);
    p32(c, O_LOWREV, 0);
    p32(c, O_CURVE, (g8(c, O_TRANS) == 0) ? 0x06008DD4 : 0x06008F68);
    int id = g8(c, O_ID);
    int sp = (id == 10 || id == 13);
    p32(c, O_TQMUL, sp ? 0x11000 : 0x10000);
    p32(c, O_GEARS, sp ? 0x06008DBC : 0x06008DA4);
    p32(c, O_FINAL, 0x30000);
}

static void init_car_control(Drive *d)                                                 /* carctrl::initCarControl 0x060090FC */
{
    unsigned char *c = d->car;
    p32(c, O_DYN, 0x060A9400);
    p32(c, O_BIAS, 0);
    p32(c, O_SLIP, 0);
    power_init(d);
}

/* car::make_gswdata 0x06008568, by the play mode (+8): 0 = config_keyset builds the input word and its 16-bit game part is recorded at R+0x74; 2 / 4 = playback (a replay, the ghost): the next recorded word
 * becomes the upper half of the input word (the view bits of the lower half stay); 6 = like 0 without recording */
static void make_gswdata(Drive *d, const DriveIn *in)
{
    unsigned char *c = d->car;
    int mode = g16u(c, O_MODE);
    unsigned wp = (unsigned)g32(c, O_R + 0x74);
    if (mode == 2 || mode == 4) {
        p16(c, O_INPUT, win_r16(d->slot1 ? &d->cfg.ghost_win : &d->cfg.rec_win, wp));
        p32(c, O_R + 0x74, (int)(wp + 2));
        return;
    }
    int g1 = 8 | (in->accel ? 1 : 0) | (in->brake ? 2 : 0) | (in->shift > 0 ? 0x20 : in->shift < 0 ? 0x10 : 0);
    int word = (int)(((u32)(in->axis & 0xFF) << 24) | ((u32)g1 << 16) | ((u32)(in->view & 0x70) << 8));
    p32(c, O_INPUT, word);
    if (mode == 6) return;
    if (!((int)wp >= g32(c, O_R + 0x78))) {
        win_w16(&d->cfg.rec_win, wp, (int)((u32)word >> 16));
        p32(c, O_R + 0x74, (int)(wp + 2));
    }
}

static void view_changer(Drive *d, const DriveIn *in)                                  /* car::view_changer1p 0x06008604 */
{
    if (in->view & 0x70) d->g1b = (d->g1b + 2) & 2;
}

static void check_lap(Drive *d);
static void race_tail(Drive *d);
static void time_calc(Drive *d);
static void goali_move(Drive *d, const DriveIn *in);
static void goal_move(Drive *d, const DriveIn *in);
static void gover_move(Drive *d, const DriveIn *in);

static void plstart_wait(Drive *d, const DriveIn *in)                                  /* plctrl::normal_plstart_wait 0x06006228 */
{
    make_gswdata(d, in);
    view_changer(d, in);
    exec_car(d);
    sndev_game_se(&d->se, d->car, &d->msg45);
    get_block(d);
    check_lap(d);
    calc_reverse(d);
}

static void plmove(Drive *d, const DriveIn *in)                                        /* plctrl::normal_plmove 0x060062F4 */
{
    make_gswdata(d, in);
    view_changer(d, in);
    exec_car(d);
    sndev_game_se(&d->se, d->car, &d->msg45);
    get_block(d);
    check_lap(d);
    if (g16u(d->car, O_MODE) != 2) time_calc(d);
    calc_reverse(d);
    race_tail(d);
}

static void plinit(Drive *d, const DriveIn *in)                                        /* plctrl::normal_plinit 0x06006030 */
{
    unsigned char *c = d->car;
    set_fn(c, O_FN0, FN0_WAIT);
    mycar_init(d);
    init_car_control(d);
    p8(c, 0xB6, d->g300);
    plstart_wait(d, in);
    calc_dispang2(d);
}

static void count_down(Drive *d)                                                       /* display3::count_down_start 0x0601310C (the car and race state part) */
{
    unsigned char *c = d->car;
    int idx = 3, size = 0, rp = d->cfg.replay;                                                              /* what it hands to dispCountDown: numeral 3 / 2 / 1 / 0 = GO and its size parameter */
    switch (d->ret_state) {
    case 0:
        if (rp) { sndev_replay_start(&d->se); if (d->cfg.course == 0) SET_MSG(d, 38); }  /* (0x06013F50: the replay starts the stage's music / message instead of the voice) */
        else sndev_kansei(&d->se);
        p8(c, O_CTRL, 4);
        d->ret_state = 2;
        d->cd_timer = d->cam_mode ? 104 : 25;
        if (rp) d->cam_mode = 0;
        break;
    case 2:
        if (--d->cd_timer == 0) {
            d->g803 |= 0x40;
            if (!rp) SET_MSG(d, 32);
            d->ret_state = 4;
        }
        break;
    case 4: case 6: case 8: {
        idx = d->ret_state == 4 ? 3 : d->ret_state == 6 ? 2 : 1;
        int t = d->cd_timer + 0x800;
        if (0x10000 >= t) { d->cd_timer = t; size = t; break; }
        d->cd_timer = 0;
        if (d->ret_state == 4) { d->ret_state = 6; if (!rp) SET_MSG(d, 33); }
        else if (d->ret_state == 6) { d->ret_state = 8; if (!rp) SET_MSG(d, 34); }
        else {
            d->g180 = 2;
            p16(c, O_STATE, 1);
            d->ret_state = 10;
            set_fn(c, O_FN0, FN0_MOVE);
            p8(c, O_CTRL, 6);
            if (!rp) SET_MSG(d, 35); else sndev_replay_go(&d->se);
        }
        break; }
    case 10: {
        idx = 0;
        int t = d->cd_timer + 0x800;
        if (0x4000 >= t) { d->cd_timer = t; size = t; break; }
        d->cd_timer = 0;
        if (!rp) sndev_go(&d->se);
        d->g180 = 6;
        set_fn(c, O_FN4, rp ? FN4_RMT : FN4_MT);
        d->ret_state = 0;
        break; }
    }
    d->cd_idx = idx;
    d->cd_size = size;
}

static void count_down_ending(Drive *d)                                                /* display3::count_down_start_ending 0x06013E50: the ending's replay starts like the others, without sound or numerals */
{
    unsigned char *c = d->car;
    int t;
    switch (d->ret_state) {
    case 0: p8(c, O_CTRL, 4); d->ret_state = 2; d->cd_timer = 25; break;               /* 0x06013E68 */
    case 2: if (--d->cd_timer == 0) d->ret_state = 4; break;                            /* 0x06013E78 */
    case 4: case 6: case 8:                                                             /* 0x06013E90 / A8 / C0 */
        t = d->cd_timer + 0x800;
        if (0x10000 >= t) { d->cd_timer = t; break; }
        d->cd_timer = 0;
        if (d->ret_state != 8) { d->ret_state += 2; break; }
        p16(c, O_STATE, 1);
        d->ret_state = 10;
        set_fn(c, O_FN0, FN0_MOVE);
        p8(c, O_CTRL, 6);
        break;
    case 10:                                                                            /* 0x06013EEC */
        t = d->cd_timer + 0x800;
        if (0x4000 >= t) { d->cd_timer = t; break; }
        d->cd_timer = 0;
        set_fn(c, O_FN4, FN4_EMT);
        d->ret_state = 0;
        break;
    }
}

void drive_move(Drive *d, const DriveIn *in)                                           /* slot function 0 */
{
    unsigned char *c = d->car;
    int fn0 = g32(c, O_FN0);
    if (fn0 & 1) return;
    switch (fn0) {
    case FN0_PLINIT: plinit(d, in); break;
    case FN0_WAIT: plstart_wait(d, in); break;
    case FN0_MOVE: plmove(d, in); break;
    case FN0_GOALI: goali_move(d, in); break;
    case FN0_GOAL: goal_move(d, in); break;
    case FN0_GOVER: gover_move(d, in); break;
    default: break;
    }
}

void drive_present(Drive *d)                                                           /* slot function 4: the grid countdown, then the per-pass map and angle upkeep */
{
    unsigned char *c = d->car;
    int fn4 = g32(c, O_FN4);
    if (fn4 && !(fn4 & 1)) {
        if (fn4 == FN4_ESTART) { count_down_ending(d); return; }                        /* ending_plmt_start / ending_plmt: dispCar and get_other_map only */
        if (fn4 == FN4_EMT) return;
        if (fn4 == FN4_START || fn4 == FN4_RSTART) count_down(d);
        if (!(d->cfg.replay && fn4 != FN4_GOAL && !d->rp_chase)) calc_dispang2(d);       /* (replay_plmt 0x0600680C with the cut-scene cameras: no calc_dispang2 / get_poly_map for the player) */
    }
}

void drive_calc_dispang(Drive *d) { calc_dispang2(d); }
void drive_get_block(Drive *d) { get_block(d); }
void drive_dummy_init(Drive *dst, const Drive *src)
{
    memset(dst, 0, sizeof *dst);
    dst->phys = src->phys;
    dst->coli = src->coli;
    dst->cfg = src->cfg;
}

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
 * the ghost ("myline", game type 4 with a stored best run): the car of slot 1 replays the stored input words on the same physics (docs/timeattack.md)
 * ------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
enum { FN0_GINIT = 0x06006078, FN0_GWAIT = 0x06006254, FN0_GMOVE = 0x06006340, FN4_GPRES = 0x06006784 };

static void lap_myline(Drive *g)                                                       /* laptime::lap_myline 0x06014B4C: the ghost's lap count; the end of its last lap stops it (both state functions 0) */
{
    unsigned char *c = g->car;
    int dseg = (short)g16u(c, O_SEG) - (short)g16u(c, O_PSEG);
    if (!(100 >= dseg)) { p16(c, O_LAPCNT, g16u(c, O_LAPCNT) - 1); return; }
    if (dseg >= -100) {
        if ((short)g16u(c, O_LAPNEXT) == (short)g16u(c, O_LAPCNT) && !((short)g16u(c, O_LAPMAX) >= (short)g16u(c, O_SEG))) p16(c, O_LAPMAX, g16u(c, O_SEG));
        return;
    }
    p16(c, O_LAPCNT, g16u(c, O_LAPCNT) + 1);
    int next = (short)g16u(c, O_LAPNEXT) + 1;
    if ((short)g16u(c, O_LAPCNT) != next) return;
    if (next == g->total_laps) { p32(c, O_FN0, 0); p32(c, O_FN4, 0); return; }
    p16(c, O_LAPNEXT, next);
    p16(c, O_LAPMAX, 0);
}

static void ghost_body(Drive *g)                                                       /* 0x06006254 / 0x06006340: the recorded input, the car's step, the road block, the lap count, the wrong-way angle */
{
    DriveIn none;
    memset(&none, 0, sizeof none);
    make_gswdata(g, &none);
    exec_car(g);
    get_block(g);
    lap_myline(g);
    calc_reverse(g);
}

void drive_ghost_init(Drive *g, const Drive *p)                                        /* init_game_work's carconfig for the second player, game_init's myline_set 0x060049B8 */
{
    memset(g, 0, sizeof *g);
    g->phys = p->phys;
    g->coli = p->coli;
    g->cfg = p->cfg;
    g->slot1 = 1;
    g->total_laps = p->total_laps;
    g->cfg.car_type = p->cfg.ghost_car;
    g->cfg.setup_mode = p->cfg.ghost_mode;
    g->cfg.manual = p->cfg.ghost_manual;
    for (int k = 0; k < 4; k++) g->cfg.setup[k] = p->cfg.ghost_setup[k];
    memcpy(g->dyn, p->dyn, 0xD0);                                                       /* (the ghost's first step still uses player 1's block: initCarControl points there) */
    carconfig(g);
    unsigned char *c = g->car;
    p32(c, O_FN0, FN0_GINIT);
    p16(c, O_MODE, 4);
    p8(c, O_TRANS, g->cfg.manual);
    p8(c, O_ID, g->cfg.car_type == 2 ? 10 : g->cfg.car_type);
    p8(c, O_X5F, g->cfg.setup_mode);
    p32(c, O_R + 0x74, (int)g->cfg.ghost_start);
    p32(c, O_R + 0x78, (int)(g->cfg.ghost_start + 0xF7FEu));
}

void drive_ghost_move(Drive *g, const Drive *p)                                        /* the ghost's slot function 0 */
{
    unsigned char *c = g->car;
    int fn0 = g32(c, O_FN0);
    if (fn0 & 1) return;
    switch (fn0) {
    case FN0_GINIT:                                                                     /* 0x06006078: like normal_plinit, then the ghost's wait step */
        set_fn(c, O_FN0, FN0_GWAIT);
        mycar_init(g);
        init_car_control(g);
        p8(c, 0xB6, p->g300);
        ghost_body(g);
        break;
    case FN0_GWAIT: case FN0_GMOVE: ghost_body(g); break;
    default: break;
    }
}

int drive_ghost_present(Drive *g)                                                      /* slot function 4 (0x06006784): dispCar's mode, alternating 0 / 3 every pass unless the option keeps the ghost solid; -1 = no presentation */
{
    if (g32(g->car, O_FN4) != FN4_GPRES) return -1;
    if (g->cfg.ghost_solid) return 0;
    g->ghost_cnt++;
    return (g->ghost_cnt & 1) ? 3 : 0;
}

void drive_p2sync(Drive *p, Drive *g)                                                  /* plctrl::p2sync_ctrl 0x060067A4: the count-down's end starts the ghost, the end of the "GO" sign shows it */
{
    unsigned char *c = g->car;
    if (g16u(c, O_MODE) != 4) return;
    int s = p->g180;
    if (s == 2) { p32(c, O_FN0, FN0_GMOVE); p16(c, O_STATE, 1); p->g180 = 4; return; }
    if (s == 6) { p32(c, O_FN4, FN4_GPRES); p->g180 = 4; }
}

int drive_part_limit(const Drive *d) { return (g32(d->car, O_FN4) & ~1) == FN4_GOAL ? 5 : 16; }

void drive_pass(Drive *d, const DriveIn *in)
{
    drive_move(d, in);
    drive_present(d);
}

static void lap_time_init(Drive *d);

void drive_init(Drive *d, const Phys *phys, const Coli *coli, const DriveCfg *cfg)
{
    memset(d, 0, sizeof *d);
    d->phys = phys;
    d->coli = coli;
    d->cfg = *cfg;
    d->se.course = cfg->course; d->se.game_type = cfg->game_type;
    unsigned char *c = d->car;
    d->g300 = cfg->init_g300;                                           /* a work RAM byte: it survives from one stage to the next */
    if (cfg->init_r) { memcpy(c + O_R, cfg->init_r, SLOT_BYTES - O_R); p8(c, O_R, 0); }
    if (cfg->init_rec) memcpy(d->rec, cfg->init_rec, sizeof d->rec);
    d->cam_mode = (cfg->game_type == 0 && cfg->course != 0) ? 0 : 4;
    set_fn(c, O_FN0, FN0_PLINIT);
    set_fn(c, O_FN4, cfg->ending ? FN4_ESTART : cfg->replay ? FN4_RSTART : FN4_START);
    p16(c, O_MODE, cfg->replay ? 2 : 0);
    p8(c, O_TRANS, cfg->manual);
    p8(c, O_CTRL, 0);
    p8(c, O_ID, cfg->car_type == 2 ? 10 : cfg->car_type);
    p8(c, O_X5F, cfg->setup_mode);
    lap_time_init(d);
    carconfig(d);
    if (cfg->game_type != 0 || cfg->course == 0 || !cfg->init_r) {                       /* lap_time_init: the record pointers start at [0x060AAA5C] (a later stage of the arcade chain goes on where the stage before ended: the R block it was given) */
        unsigned st = cfg->rec_start ? cfg->rec_start : 0x060F0000u;                      /* (the oracle's environment: its scratch buffer) */
        p32(c, O_R + 0x74, (int)st);
        p32(c, O_R + 0x78, (int)(cfg->rec_end ? cfg->rec_end : cfg->rec_start ? st + 0xF7FEu : 0x060F8000u));
    }
}


/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
 * lap, time limit, goal (docs/physics.md 5.3 - 5.5); the split / lap time records (R+0x04 .. R+0x6F) are not ported yet
 * ------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
static void time_extension(Drive *d, int ckpt)                                         /* 0x06014BB0: add the seconds of the (lap, checkpoint) cell to the time limit */
{
    unsigned char *c = d->car;
    const Phys *ph = d->phys;
    unsigned t1 = (unsigned)pm_r32(ph, 0x06014BE4u + 4u * (unsigned)(d->cfg.game_type / 2));
    unsigned t2 = (unsigned)pm_r32(ph, t1 + 4u * (unsigned)d->cfg.difficulty);
    unsigned row = (unsigned)pm_r32(ph, t2 + 4u * (unsigned)g16u(c, O_LAPNEXT));
    int secs = pm_ptr(ph, row + 4u * (unsigned)d->cfg.course + (unsigned)(ckpt & 0x7F), 1)[0];
    p32(c, O_R + 0x34, g32(c, O_R + 0x34) + (secs << 8));
}

static int rec_get(const Drive *d, int a);
static void init_record_pointers(Drive *d, int fresh)                                  /* 0x060143C4 (game type 0: only the first stage starts the buffers) / 0x06014448 (game type 2: always) */
{
    unsigned char *c = d->car;
    if (fresh || d->cfg.course == 0) p32(c, O_R + 0x48, 0x060AA400);
    p32(c, O_R + 0x6C, g32(c, O_R + 0x48));
    p32(c, O_R + 0x4C, 0x060AA520);
    p32(c, O_R + 0x50, 0x060AA520);
    if (fresh || d->cfg.course == 0) { p32(c, O_R + 0x54, 0x060AA538); p8(c, O_R + 2, 0); }
    p32(c, O_R + 0x58, 0x060AA538 + 0x120);
    p32(c, O_R + 0x5C, 0x060AA538 + 0x120);
    int opt = d->cfg.compare, r1 = opt;                                                  /* the compare with a best run: the option, and only if the reference list (the run's list R+84 for 1, the best lap's R+88 for 2) holds a time */
    if (opt && rec_get(d, g32(c, O_R + (opt == 1 ? 84 : 88))) == 0) r1 = 0;
    p8(c, O_R + 1, r1);
    if (fresh || d->cfg.course == 0) memset(d->rec, 0, 0x138);                           /* rfillff: the current run's lists 0x060AA400 .. 0x060AA538 (the type 0 branch of a later stage only the last 6 longs) */
    else memset(d->rec + 0x120, 0, 0x18);
}

/* --- the race clock (laptime 0x06014C08 .. 0x06015100): times are BCD longs 0x00MMSSHH (minutes, seconds, hundredths), the records go to buffers at 0x060AA400 .. ---------------------- */
#define REC_BASE 0x060AA400u

static int rec_get(const Drive *d, int a) { unsigned o = (unsigned)a - REC_BASE; return o <= sizeof d->rec - 4 ? g32(d->rec, (int)o) : 0; }
static void rec_put(Drive *d, int a, int v) { unsigned o = (unsigned)a - REC_BASE; if (o <= sizeof d->rec - 4) p32(d->rec, (int)o, v); }

/* 0x06014E96 / E90: one digit-pair addition (a + b + carry), `lim` = the limit of the tens digit; *carry is the carry out */
static int bcd_add(int a, int b, int *carry, int lim, int with_carry)
{
    a &= 0xFF; b &= 0xFF;
    if (with_carry) a += *carry;
    *carry = 0;
    int u = (a & 15) + (b & 15), ta = (int)((u32)a >> 4), tb = (int)((u32)b >> 4);
    if (u >= 10) { u -= 10; ta += 1; }
    tb += ta;
    if (tb >= lim) { tb -= lim; *carry = 1; }
    return u + (tb << 4);
}

/* 0x06014E5A: dst = a + b (hundredths, seconds, minutes); a minutes overflow gives 0x00595999 */
static void bcd_sum(unsigned char *dst, int a, int b)
{
    int cy;
    dst[3] = (unsigned char)bcd_add(a, b, &cy, 10, 0);
    dst[2] = (unsigned char)bcd_add(a >> 8, b >> 8, &cy, 6, 1);
    int m = bcd_add(a >> 16, b >> 16, &cy, 6, 1);
    if (cy) p32(dst, 0, 0x00595999); else dst[1] = (unsigned char)m;
}

/* 0x06014F28 / F0C: one digit-pair subtraction (a - b + borrow); *borrow is 0 or -1 */
static int bcd_sub1(int a, int b, int *borrow, int lim, int with_borrow)
{
    a &= 0xFF; b &= 0xFF;
    if (with_borrow) {
        a += *borrow;
        if (a >= 0) { if ((a & 15) == 15) a -= 6; *borrow = 0; }
        else { *borrow = -1; a = 89; }
    } else *borrow = 0;
    int u = (a & 15) - (b & 15), ta = (int)((u32)a >> 4), tb = (int)((u32)b >> 4);
    if (u < 0) { u += 10; ta -= 1; }
    ta -= tb;
    if (ta < 0) { ta += lim; *borrow = -1; }
    return u + (ta << 4);
}

/* 0x06014ED4: dst = a - b; a minutes underflow gives 0x00095999 */
static void bcd_diff(unsigned char *dst, int a, int b)
{
    int bw;
    dst[3] = (unsigned char)bcd_sub1(a, b, &bw, 10, 0);
    dst[2] = (unsigned char)bcd_sub1(a >> 8, b >> 8, &bw, 6, 1);
    int m = bcd_sub1(a >> 16, b >> 16, &bw, 6, 1);
    if (bw) p32(dst, 0, 0x00095999); else dst[1] = (unsigned char)m;
}

/* 0x06014EC4: dst = |a - b| with the sign (0x80) in byte 0 */
static void bcd_cmp(unsigned char *dst, int a, int b)
{
    int sign = 0;
    if (!(a >= b)) { int t = a; a = b; b = t; sign = 0x80; }
    dst[0] = (unsigned char)sign;
    bcd_diff(dst, a, b);
}

static int rd32le_(const unsigned char *p) { return (int)((u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24); }

/* 0x060150B4: how far (0..3 hundredths-steps) the car is past the line of its road section when the pass counted it: the distance to the section's edge, divided by the speed */
static int time_adjust(Drive *d)
{
    unsigned char *c = d->car;
    const Phys *ph = d->phys;
    unsigned tbl = (unsigned)pm_r32(ph, 0x0601513Cu + 4u * (unsigned)d->cfg.course);
    int r0 = pm_r32(ph, tbl + 4u * (unsigned)((int)(signed char)g8(c, O_SECT) - 1));
    int r5 = FM(r0, 0x4CCD);
    int r6 = FM(g32(c, 0x94), r5);
    if (r6 == 0) return 0;
    const unsigned char *s = d->coli->file + d->coli->off_slice + 44u * (unsigned)g16u(c, O_SEG);
    int px = g32(c, O_POS), pz = g32(c, O_POS + 8);
    i64 acc = (i64)(rd32le_(s + 4) - px) * (i64)(rd32le_(s + 32) - pz);
    acc += (i64)neg(rd32le_(s + 28) - px) * (i64)(rd32le_(s + 8) - pz);
    int q = div64(acc, r6);
    int a = q < 0 ? neg(q) : q;
    if ((short)a < 0) a = (int)((u32)a + 0x10000u);
    a = (int)((u32)a >> 16);
    return 4 > a ? a : 3;
}

/* 0x06014D58: the comparison with the best run (only when the compare mode byte R+1 is set) */
static void rec_tail(Drive *d)
{
    unsigned char *c = d->car;
    if (d->cfg.game_type == 10) return;
    int m = g8(c, O_R + 1);
    if (!m) return;
    p8(c, O_R + 2, 2);
    if (m != 2) bcd_cmp(c + O_R + 100, rec_get(d, g32(c, O_R + 0x48) - 4), rec_get(d, g32(c, O_R + 0x54) - 4));
    else        bcd_cmp(c + O_R + 104, rec_get(d, g32(c, O_R + 0x4C) - 4), rec_get(d, g32(c, O_R + 0x58) - 4));
}

static int lap_slot(const unsigned char *c, int lap) { return g32(c, O_R + 0x10 + 4 * lap); }
static int is_t0_lake(const Drive *d) { return d->cfg.game_type == 0 && d->cfg.course == 3; }

static void rec_push(Drive *d, int ptr_off, int v)
{
    unsigned char *c = d->car;
    int a = g32(c, O_R + ptr_off);
    rec_put(d, a, v);
    p32(c, O_R + ptr_off, a + 4);
}

static void split_record(Drive *d)                                                     /* 0x06014C08: a section mark was passed */
{
    unsigned char *c = d->car;
    int adj = time_adjust(d);
    p32(c, O_R + 0x58, g32(c, O_R + 0x58) + 4);
    p32(c, O_R + 0x54, g32(c, O_R + 0x54) + 4);
    int t = lap_slot(c, g16u(c, O_LAPNEXT));
    int lake = is_t0_lake(d);
    if (adj != 0 && t < g32(c, 0x2A8)) {
        bcd_diff(c + O_R + 0x60, t, adj);
        rec_push(d, 0x4C, g32(c, O_R + 0x60));
        if (!lake) bcd_diff(c + O_R + 0x60, g32(c, O_R + 12), adj);
        rec_push(d, 0x48, g32(c, O_R + 0x60));
    } else {
        rec_push(d, 0x4C, t);
        rec_push(d, 0x48, lake ? t : g32(c, O_R + 12));
    }
    rec_tail(d);
}

static void lap_end_record(Drive *d, int lap)                                          /* 0x06014CE8: the lap `lap` is over */
{
    unsigned char *c = d->car;
    int t = lap_slot(c, lap);
    rec_push(d, 0x4C, t);
    p32(c, O_R + 0x58, g32(c, O_R + 0x58) + 4);
    rec_push(d, 0x48, is_t0_lake(d) ? t : g32(c, O_R + 8));
    p32(c, O_R + 0x54, g32(c, O_R + 0x54) + 4);
    rec_tail(d);
    p32(c, O_R + 0x4C, g32(c, O_R + 0x50));
    p32(c, O_R + 0x58, g32(c, O_R + 0x5C));
    /* with bit 7 of R+0x30 set the original also calls 0x06005674 (r4 = 6): the new-best jingle */
}

static void best_lap_flag(Drive *d, int t)                                             /* a lap faster than R+0x30 becomes the best lap (flag bit 31 = new) */
{
    unsigned char *c = d->car;
    if (t < g32(c, O_R + 0x30) && !d->cfg.mirror) p32(c, O_R + 0x30, t | (int)0x80000000);
}

static void goal_record(Drive *d)                                                      /* 0x06014F58: the goal line was crossed */
{
    unsigned char *c = d->car;
    int adj = time_adjust(d);
    int lap = g16u(c, O_LAPNEXT), mx = g32(c, 0x2A8);
    if (adj != 0) {
        int t = lap_slot(c, lap);
        if (t < mx) {
            bcd_diff(c + O_R + 0x10 + 4 * lap, t, adj);
            if (!is_t0_lake(d)) {
                int total = g32(c, O_R + 12);
                if (!(d->cfg.game_type == 10 && total >= mx)) bcd_diff(c + O_R + 12, total, adj);
            }
        }
    }
    best_lap_flag(d, lap_slot(c, lap));
    unsigned char *dst = c + O_R + 0x38 + 4 * d->cfg.course;
    p32(dst, 0, 0);
    for (int k = 0; k < d->total_laps; k++) bcd_sum(dst, lap_slot(c, k), g32(dst, 0));
}

/* 0x06015014: a new lap starts (the finished lap is `lap - 1`).  Returns the value of r3 on exit: 1 when it is the R block (the caller then clears the "new best" flag byte), 0 when it is the
 * small number the adjustment leaves there (the caller's store then goes to the low addresses of the console's ROM and has no effect) */
static int newlap_record(Drive *d)
{
    unsigned char *c = d->car;
    int adj = time_adjust(d);
    int prev = g16u(c, O_LAPNEXT) - 1;
    int mx = g32(c, 0x2A8);
    if (adj != 0) {
        int t = lap_slot(c, prev);
        if (t < mx) {
            int total = g32(c, O_R + 8);
            bcd_diff(c + O_R + 0x10 + 4 * prev, t, adj);
            best_lap_flag(d, lap_slot(c, prev));
            bcd_diff(c + O_R + 8, total, adj);
            if (g8(c, O_SECT)) lap_end_record(d, prev);
            if (d->cfg.game_type == 6 || d->cfg.game_type == 10) return 1;
            d->lap_redraw = prev + 1;                                                   /* the original shows the corrected lap time (0x060121BE) and leaves r13 in r3 */
            return 0;
        }
    }
    best_lap_flag(d, lap_slot(c, prev));
    if (g8(c, O_SECT)) lap_end_record(d, prev);
    return 1;
}

static void lap_time_calc(Drive *d)                                                    /* 0x06014DDC: the stop-watch of the running lap (R+5 minutes, R+6 seconds, R+7 frames) */
{
    unsigned char *c = d->car;
    const Phys *ph = d->phys;
    unsigned char *slot = c + O_R + 0x10 + 4 * g16u(c, O_LAPNEXT);
    if (g8(c, O_R + 5) == 10) { p32(slot, 0, g32(c, 0x2A8)); return; }
    int f = g8(c, O_R + 7) + 1;
    if (f != 30) { p8(c, O_R + 7, f); slot[3] = *pm_ptr(ph, 0x060A1248u + (unsigned)f, 1); return; }
    p8(c, O_R + 7, 0); slot[3] = 0;
    int s = g8(c, O_R + 6) + 1;
    if (s != 60) { p8(c, O_R + 6, s); slot[2] = *pm_ptr(ph, 0x060A1208u + (unsigned)s, 1); return; }
    p8(c, O_R + 6, 0); slot[2] = 0;
    int m = g8(c, O_R + 5) + 1;
    p8(c, O_R + 5, m);
    slot[1] = *pm_ptr(ph, 0x060A1208u + (unsigned)m, 1);
}

static void total_time_calc(Drive *d)                                                  /* 0x06014E34: total = previous laps + the running lap */
{
    unsigned char *c = d->car;
    if (is_t0_lake(d)) return;
    bcd_sum(c + O_R + 12, lap_slot(c, g16u(c, O_LAPNEXT)), g32(c, O_R + 8));
}

static void lap_time_init(Drive *d)                                                    /* FUN_06014270 (single-player game types 0 and 2) */
{
    unsigned char *c = d->car;
    p32(c, 0x2A8, 0x00095999);
    if (d->cfg.game_type == 0) {
        d->total_laps = d->cfg.laps;
        p8(c, O_R + 0x7E, 0);
        for (int k = 1; k < 2; k++) p32(c, O_R + 4 * k, 0);
        for (int k = 4; k < 9; k++) p32(c, O_R + 4 * k, 0);
        init_record_pointers(d, 0);
        if (d->cfg.course == 0) {
            d->g300 = 15;
            p32(c, O_R + 8, 0);
            p32(c, O_R + 12, 0);
            p32(c, O_R + 0x34, 29);
        } else {
            p32(c, O_R + 12, g32(c, O_R + 8));
            p8(c, O_R + 0x37, 29);
        }
    } else if (d->cfg.game_type == 2) {                                                 /* game type 2 */
        d->g300 = 2;
        p8(c, O_R + 0x7E, 0);
        d->total_laps = d->cfg.course == 0 ? 3 : 2;
        for (int k = 1; k < 9; k++) p32(c, O_R + 4 * k, 0);
        init_record_pointers(d, 1);
        p32(c, O_R + 0x34, 29);
    } else {                                                                            /* time attack: 4 = three laps (0x0601434E), 6 = free run (0x06014322); no time limit, no time extension */
        static const signed char free_laps[4] = { 4, 5, 4, 5 };                         /* the table at 0x06014348 */
        if (d->cfg.game_type == 6) {
            p8(c, O_R + 125, 0); p8(c, O_R + 126, 0); p8(c, O_R + 124, 1);
            d->total_laps = free_laps[d->cfg.course & 3];
        } else {
            d->total_laps = 3;
            p8(c, O_R + 126, 0);
        }
        for (int k = 1; k < 9; k++) p32(c, O_R + 4 * k, 0);
        p32(c, O_R + 0x34, 29);
        init_record_pointers(d, 1);
        return;
    }
    time_extension(d, 0);
}

static void check_lap_std(Drive *d)                                                    /* laptime::check_lap 0x0601465C, game types 0 and 2 */
{
    unsigned char *c = d->car;
    int dseg = g16u(c, O_SEG) - g16u(c, O_PSEG);
    if (!(100 >= dseg)) { p16(c, O_LAPCNT, g16u(c, O_LAPCNT) - 1); return; }            /* moved backwards over the line */
    if (dseg < -100) {                                                                  /* the segment index wrapped: crossed the line forward */
        int cnt = (g16u(c, O_LAPCNT) + 1) & 0xFFFF;
        p16(c, O_LAPCNT, cnt);
        int next = (g16u(c, O_LAPNEXT) + 1) & 0xFFFF;
        if (cnt != next) return;
        if (next == d->total_laps) {                                                    /* goal */
            set_fn(c, O_FN0, FN0_GOALI);
            d->g300 = g8(c, 0xB6);
            p8(c, O_CTRL, 8);
            return;
        }
        p16(c, O_LAPNEXT, next);                                                        /* new lap */
        p16(c, O_LAPMAX, 0);
        if (g16u(c, O_MODE) == 2) return;
        p32(c, O_R + 4, 0);
        p32(c, O_R + 8, g32(c, O_R + 12));
        se_call(&d->se, SND_PLAY, 1, 0, 0);
        SET_MSG(d, 29);
        time_extension(d, 0);
        if (newlap_record(d)) p8(c, O_R + 0x30, 0);
        return;
    }
    if (g16u(c, O_LAPNEXT) == g16u(c, O_LAPCNT) && g16u(c, O_LAPMAX) < g16u(c, O_SEG)) {
        p16(c, O_LAPMAX, g16u(c, O_SEG));
        if (g16u(c, O_SEG) == 0 || g16u(c, O_MODE) == 2) return;
        if (g8(c, O_SECT)) split_record(d);
        int ck = (signed char)g8(c, O_CKPT);
        if (ck == 0) return;
        if (ck > 0) { p8(c, O_MK2, ck); return; }
        se_call(&d->se, SND_PLAY, 1, 0, 0);
        SET_MSG(d, 29);
        time_extension(d, ck);                                                          /* extension checkpoint */
    }
}

/* the time attack variants of check_lap (the table at 0x06014668): type 4 (three laps, 0x06014750) and type 6 (free run, 0x06014810).  FUN_06015014 returns the original's r3: the R block
 * (newlap_record() = 1) or the small correction number (0: the bytes the original then reads and writes at 48 + r3 are in the console's ROM, no effect, read as 0 here like the oracle does) */
static void hud_retime(Drive *d, int rb)                                               /* re_1x1time_ft 0x06011CDC: a new best lap lights the time of the lap */
{
    if (rb && g8(d->car, O_R + 0x30)) { d->g301 = 2; d->g302 |= 1; }
}

static void check_lap_t4(Drive *d)
{
    unsigned char *c = d->car;
    int dseg = g16u(c, O_SEG) - g16u(c, O_PSEG);
    if (!(100 >= dseg)) { p16(c, O_LAPCNT, g16u(c, O_LAPCNT) - 1); return; }
    if (dseg < -100) {
        int cnt = (g16u(c, O_LAPCNT) + 1) & 0xFFFF;
        p16(c, O_LAPCNT, cnt);
        int next = (g16u(c, O_LAPNEXT) + 1) & 0xFFFF;
        if (cnt != next) return;
        if (next == d->total_laps) {                                                    /* goal (the rank byte [0x060AA300] is not touched here) */
            set_fn(c, O_FN0, FN0_GOALI);
            p8(c, O_CTRL, 8);
            return;
        }
        p16(c, O_LAPNEXT, next);
        p16(c, O_LAPMAX, 0);
        if (g16u(c, O_MODE) == 2) return;
        p32(c, O_R + 4, 0);
        p32(c, O_R + 8, g32(c, O_R + 12));
        int rb = newlap_record(d);
        hud_retime(d, rb);
        if (rb && g8(c, O_R + 0x30)) { p8(c, O_R + 0x30, 0); SET_MSG(d, 37); }
        se_call(&d->se, SND_PLAY, 1, 0, 0);
        return;
    }
    if (g16u(c, O_LAPNEXT) == g16u(c, O_LAPCNT) && g16u(c, O_LAPMAX) < g16u(c, O_SEG)) {
        p16(c, O_LAPMAX, g16u(c, O_SEG));
        if (g16u(c, O_SEG) == 0 || g16u(c, O_MODE) == 2) return;
        if (g8(c, O_SECT)) {
            split_record(d);
            if (g8(c, O_R + 1)) se_call(&d->se, SND_PLAY, 1, 0, 0);
        }
        int ck = (signed char)g8(c, O_CKPT);
        if (ck > 0) p8(c, O_MK2, ck);
    }
}

static void check_lap_t6(Drive *d)                                                     /* free run: every crossing of the line starts a new lap 0, there is no goal */
{
    unsigned char *c = d->car;
    int dseg = g16u(c, O_SEG) - g16u(c, O_PSEG);
    if (!(100 >= dseg)) { p16(c, O_LAPCNT, g16u(c, O_LAPCNT) - 1); return; }
    if (dseg < -100) {
        int cnt = (g16u(c, O_LAPCNT) + 1) & 0xFFFF;
        p16(c, O_LAPCNT, cnt);
        int next = (g16u(c, O_LAPNEXT) + 1) & 0xFFFF;
        if (cnt != next) return;
        p16(c, O_LAPNEXT, next);
        p16(c, O_LAPMAX, 0);
        p32(c, O_R + 4, 0);
        p8(c, O_R + 124, 1);
        p32(c, O_R + 8, g32(c, O_R + 12));
        int rb = newlap_record(d);
        hud_retime(d, rb);
        d->hud_lapnum = 1;                                                              /* lap_rewrite 0x06012304 draws R+8, or the ROM bytes at 8 + r3 (zeros) */
        for (int k = 0; k < 4; k++) d->hud_laptime[k] = rb ? g8(c, O_R + 8 + k) : 0;
        if (rb) {                                                                       /* FUN_06014910: the lap counters start again */
            p16(c, O_LAPNEXT, 0); p16(c, O_LAPCNT, 0);
            p8(c, O_R + 125, 1);
            if (!g8(c, O_R + 1)) p8(c, O_R + 2, 2);
            if (g8(c, O_R + 0x30)) { p8(c, O_R + 0x30, 0); SET_MSG(d, 37); }
            p32(c, O_R + 8, 0);
            int t = g32(c, O_R + 16);
            p32(c, O_R + 16, 0);
            p8(c, O_R + 126, t >= g32(c, 0x2A8) ? 1 : 0);
            p32(c, O_R + 72, 0x060AA400);
            p32(c, O_R + 84, 0x060AA538);
        }
        se_call(&d->se, SND_PLAY, 1, 0, 0);
        return;
    }
    if (g16u(c, O_LAPNEXT) == g16u(c, O_LAPCNT) && g16u(c, O_LAPMAX) < g16u(c, O_SEG)) {
        p16(c, O_LAPMAX, g16u(c, O_SEG));
        if (g16u(c, O_SEG) == 0) return;
        if (g8(c, O_SECT)) {
            p8(c, O_R + 0x7C, g8(c, O_SECT));
            split_record(d);
            p8(c, O_R + 0x7E, 0);
            d->hud_area = 1;                                                            /* area_rewrite 0x06012334: the section time just stored, in the row of lap R+124 - 2 */
            d->hud_arearow = (signed char)g8(c, O_R + 124) - 2;
            for (int k = 0; k < 4; k++) d->hud_areatime[k] = d->rec[(unsigned)(g32(c, O_R + 0x4C) - 4 - 0x060AA400u + (unsigned)k) & 0x3FF];
            se_call(&d->se, SND_PLAY, 1, 0, 0);
        }
        int ck = (signed char)g8(c, O_CKPT);
        if (ck > 0) p8(c, O_MK2, ck);
    }
}

static void check_lap(Drive *d)
{
    if (d->cfg.game_type == 4) check_lap_t4(d);
    else if (d->cfg.game_type == 6) check_lap_t6(d);
    else check_lap_std(d);
}

static void time_calc(Drive *d)                                                        /* normal_plmove's three calls: the stop-watch, the total, the time limit (extend_time_calc 0x0601459C) */
{
    unsigned char *c = d->car;
    lap_time_calc(d);
    total_time_calc(d);
    if (d->cfg.game_type > 2) return;
    int t = g32(c, O_R + 0x34);
    if (t == 28) { p16(c, O_STATE, 2); return; }
    t -= 1;
    if ((t & 0x7F) == 127) t = (int)(((u32)t >> 8 << 8) + 29);
    p32(c, O_R + 0x34, t);
    p16(c, O_STATE, 1);
}

static void race_tail(Drive *d)                                                        /* 0x06015380: time up and nearly stopped -> game over */
{
    unsigned char *c = d->car;
    if (g16u(c, O_STATE) == 2 && !(g32(c, O_SPEED) > 0xD74)) {
        p8(c, O_CTRL, 10);
        set_fn(c, O_FN0, FN0_GOVER);
        d->g300 = g8(c, 0xB6);
    }
}

static void goal_car_move(Drive *d)                                                    /* player::goal_car_move 0x06007AB0: the car is put on the goal pose */
{
    unsigned char *c = d->car;
    unsigned a = 0x00299520u + 36u * (unsigned)d->cfg.course;
    const Phys *ph = d->phys;
    for (int k = 0; k < 3; k++) p32(c, O_POS + 4 * k, pm_r32(ph, a + 4u * k));
    for (int k = 0; k < 3; k++) p32(c, O_ANG + 4 * k, pm_r32(ph, a + 12 + 4u * k));
    for (int k = 0; k < 3; k++) p32(c, O_VEL + 4 * k, pm_r32(ph, a + 24 + 4u * k));
    for (int k = 0; k < 3; k++) p32(c, O_PUSH + 4 * k, 0);
    for (int k = 0; k < 3; k++) p32(c, O_AVEL + 4 * k, 0);
    p32(c, O_STEER, 0);
    for (int k = 0; k < 4; k++) p32(c, O_LOAD + 4 * k, 0x23D70);
}

static void goal_move(Drive *d, const DriveIn *in)                                     /* plctrl::goal_plmove 0x06006470 */
{
    (void)in;
    d->cam_mode = 2;
    exec_car(d);
    sndev_game_se(&d->se, d->car, &d->msg45);
    get_block(d);
    calc_reverse(d);
}

static void goali_move(Drive *d, const DriveIn *in)                                    /* plctrl::goali_plmove 0x0600640C (falls through into goal_plmove) */
{
    unsigned char *c = d->car;
    p16(c, O_STATE, 2);
    if (g16u(c, O_MODE) != 2) {
        goal_record(d);
        p32(c, O_R + 8, g32(c, O_R + 12));
        lap_end_record(d, g16u(c, O_LAPNEXT));
        if (d->cfg.game_type == 4) {                                                    /* time attack: a new best lap lights its time (re_1x1time_ft), the corrected last lap and the total are drawn (laptime_displayp) */
            hud_retime(d, 1);
            d->lap_redraw = g16u(c, O_LAPNEXT) + 1;
        }
        p8(c, O_R + 0x30, 0);
    }
    d->g9e8 = 0;
    set_fn(c, O_FN0, FN0_GOAL);
    set_fn(c, O_FN4, FN4_GOAL);
    goal_car_move(d);
    goal_move(d, in);
}

static void gover_move(Drive *d, const DriveIn *in)                                    /* plctrl::gover_plmove 0x060063F0 */
{
    (void)in;
    get_block(d);
    calc_reverse(d);
}
