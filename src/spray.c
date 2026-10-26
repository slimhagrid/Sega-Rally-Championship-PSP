/* Sega Rally PSP - the dust / spray objects, see spray.h.  Every routine names the original's address (0RALLY.DAT, 0x0601D074 .. 0x0601D58C). */
#include <string.h>
#include "spray.h"
#include "fx.h"
#include "mat.h"

enum { H_COUNT = 0x00, H_NFREE = 0x04, H_ENABLE = 0x08, H_ACT = 0x0C, H_FREE = 0x10, H_OBJS = 0x14, N_OBJ = 28 };
enum { ACT_ARRAY = 0x18, FREE_ARRAY = 0x88, OBJ_ARRAY = 0xF8 };                   /* offsets in Spray.m */
enum { ADDR_ACT = 0x060D3000, ADDR_FREE = 0x060D3100, ADDR_OBJS = 0x060D3200 };
enum { A_WATER = 0x060F9210, A_SAND = 0x060F91D8 };                                  /* the sprite descriptors of obj +36: water_poly / sand_poly of OBJECT.MDL */

/* car fields */
enum { C_PITCH = 0x0C, C_YAW = 0x10, C_ROLL = 0x14, C_POS = 0x18, C_VX = 0x24, C_VZ = 0x2C, C_STEER = 0x30, C_WHEEL = 0x3C, C_TYPE = 0x5C, C_DAMAGE = 0xB4, C_EC = 0xEC,
       C_LOCAL = 0x3CC, C_ABSLAT = 0x3D0, C_NIB = 0x3D8, C_SPEED = 0x3DC, C_SIDE = 0x3E0, C_PT = 0x3C0 };

static unsigned sp_rand(unsigned *rng) { *rng = *rng * 5u + 0x3537u; return *rng; }       /* 0x0601D6CC */

static unsigned char *obj_at(Spray *s, unsigned addr) { return s->m + OBJ_ARRAY + (addr - ADDR_OBJS); }

void spray_init(Spray *s, unsigned enable)                                                   /* 0x0601D094 for the master list */
{
    unsigned char *m = s->m;
    p32(m, H_ENABLE, (int)enable);
    p32(m, H_ACT, ADDR_ACT); p32(m, H_FREE, ADDR_FREE); p32(m, H_OBJS, ADDR_OBJS);
    for (int i = 0; i < N_OBJ; i++) {
        p32(m, FREE_ARRAY + 4 * i, ADDR_OBJS + 64 * i);
        p32(m, ACT_ARRAY + 4 * i, 0);
    }
    p32(m, H_NFREE, N_OBJ);
    p32(m, H_COUNT, 0);
}

/* the matrix module, fixed point (as camera.c) */
static void m_rrot(const Phys *ph, int m[12], int axis, int rad)                           /* r_rotate_x / y / z: radians 16.16 */
{
    int s, c;
    fx_sincos(ph, HM(rad, 0x28BE60DC), &s, &c);
    if (axis == 0) rot_cols(m, 1, 2, s, c);
    else if (axis == 1) rot_cols(m, 0, 2, neg(s), c);
    else rot_cols(m, 0, 1, s, c);
}
static void mi_translate(int m[12], int x, int y, int z)                                    /* 0x06017DD0 / 0x06017DBC */
{
    for (int r = 0; r < 3; r++) m[4 * r + 3] += X16((i64)m[4 * r] * x + (i64)m[4 * r + 1] * y + (i64)m[4 * r + 2] * z);
}

/* the wheel's spray object, variant A (0x0601D1EA: water): a fountain of drops thrown out sideways */
static void spawn_a(const Phys *ph, unsigned char *c, unsigned *rng, const int car_m[12], int w, int r12, unsigned tab, unsigned char *o)
{
    int t[12];
    memcpy(t, car_m, sizeof t);
    mi_translate(t, phys_r32(ph, tab + 12 * w), phys_r32(ph, tab + 12 * w + 4) + r12, phys_r32(ph, tab + 12 * w + 8));
    m_rrot(ph, t, 1, g32(c, C_STEER));
    p32(o, 4, t[3]); p32(o, 8, t[7]); p32(o, 12, t[11]);
    int v[3] = { g32(c, C_LOCAL) + g32(c, C_SIDE), FM(g32(c, C_SPEED), r12), 0 }, out[3];
    calc_point(t, v, out);
    p32(c, C_PT, out[0]); p32(c, C_PT + 4, out[1]); p32(c, C_PT + 8, out[2]);
    int r6 = iabs(g32(c, C_PT + 4) - g32(o, 8));
    int nib = g32(c, C_NIB);
    o[1] = 0; o[2] = (unsigned char)nib;
    p32(o, 16, g32(c, C_PT) - g32(o, 4));
    p32(o, 20, r6 >> 1);
    p32(o, 24, g32(c, C_PT + 8) - g32(o, 12));
    p32(o, 32, 0);
    p32(o, 28, (int)((u32)((sp_rand(rng) & 3) + 1) * (u32)r6));
    p32(o, 36, A_WATER);
}

/* variant B (0x0601D296: sand / dust): a puff that rises */
static void spawn_b(const Phys *ph, unsigned char *c, unsigned *rng, const int car_m[12], int w, int r12, unsigned tab, unsigned char *o)
{
    int t[12];
    memcpy(t, car_m, sizeof t);
    mi_translate(t, phys_r32(ph, tab + 12 * w), phys_r32(ph, tab + 12 * w + 4) + r12, phys_r32(ph, tab + 12 * w + 8));
    p32(o, 4, t[3]); p32(o, 8, t[7]); p32(o, 12, t[11]);
    int v[3] = { 0, HM(g32(c, C_SPEED), 0x19999999) + g32(c, C_ABSLAT), 0 }, out[3];
    calc_point(t, v, out);
    p32(c, C_PT, out[0]); p32(c, C_PT + 4, out[1]); p32(c, C_PT + 8, out[2]);
    int r6 = iabs(g32(c, C_PT + 4) - g32(o, 8)) >> 1;
    int nib = g32(c, C_NIB);
    o[1] = 1; o[2] = (unsigned char)nib; o[3] = 15;
    p32(o, 16, 0); p32(o, 24, 0);
    p32(o, 20, HM(r6, 0x4CCCCCCD));
    p32(o, 28, (int)((u32)((sp_rand(rng) & 3) + 1) * (u32)r6));
    p32(o, 36, A_SAND);
}

void spray_spawn(Spray *s, Drive *d, unsigned *rng) { spray_spawn_car(s, d->phys, d->car, rng); }

void spray_spawn_car(Spray *s, const Phys *ph, unsigned char *c, unsigned *rng)
{
    unsigned char *m = s->m;
    if (g32(m, H_ENABLE) == 0) return;
    int v = g32(c, C_EC);
    if (v <= 0) return;
    p32(c, C_SPEED, v);
    int a[12];                                                                               /* the car's yaw undone: its velocity in the car frame (+0x3CC .. +0x3D4) */
    unit_matrix(a);
    m_rrot(ph, a, 1, neg(g32(c, C_YAW)));
    int vel[3] = { g32(c, C_VX), 0, g32(c, C_VZ) }, lv[3];
    calc_xyz(a, vel, lv);
    p32(c, C_LOCAL, lv[0]); p32(c, C_ABSLAT, lv[1]); p32(c, C_ABSLAT + 4, lv[2]);
    v = FM(g32(c, C_LOCAL), 0x9999);
    p32(c, C_LOCAL, v);
    p32(c, C_ABSLAT, iabs(v));
    unsigned tab = (unsigned)phys_r32(ph, 0x0601D340 + 4 * (signed char)c[C_TYPE]);          /* wheel offsets of the car type */
    int car_m[12];                                                                           /* the car's own matrix: position, yaw, pitch, roll */
    unit_matrix(car_m);
    mi_translate(car_m, g32(c, C_POS), g32(c, C_POS + 4), g32(c, C_POS + 8));
    m_rrot(ph, car_m, 1, g32(c, C_YAW));
    m_rrot(ph, car_m, 0, g32(c, C_PITCH));
    m_rrot(ph, car_m, 2, g32(c, C_ROLL));
    unsigned r10 = (unsigned)g16u(c, C_DAMAGE);
    for (int w = 0; w < 4; w++, r10 >>= 4) {
        if (m[H_ENABLE + w] == 0) continue;
        p32(c, C_NIB, (int)(r10 & 0xF));
        int r12 = g32(c, C_WHEEL + 4 * w);
        if (r12 <= 0) continue;
        r12 >>= 4;
        int side = r12 >> 1;
        p32(c, C_SIDE, (w & 1) ? side : neg(side));
        int count = g32(m, H_COUNT);
        if (!(N_OBJ > count)) continue;
        int fn = phys_r16s(ph, 0x0601D328 + 2 * (int)(r10 & 0xF));
        if (fn == 0) continue;
        int nfree = g32(m, H_NFREE) - 1;
        p32(m, H_NFREE, nfree);
        unsigned obj = (unsigned)g32(m, FREE_ARRAY + 4 * nfree);
        p32(m, ACT_ARRAY + 4 * count, (int)obj);
        p32(m, H_COUNT, count + 1);
        if (fn == 0x14) spawn_a(ph, c, rng, car_m, w, r12, tab, obj_at(s, obj));
        else spawn_b(ph, c, rng, car_m, w, r12, tab, obj_at(s, obj));
    }
}

/* 0x0601D526: one step of an object; true = it lives on */
static int sp_update(unsigned char *o)
{
    if (o[1] == 0) {
        int v = g32(o, 20) - 0x51E;
        p32(o, 20, v);
        p32(o, 4, g32(o, 4) + g32(o, 16));
        int size = g32(o, 28) + v;
        p32(o, 28, size);
        p32(o, 12, g32(o, 12) + g32(o, 16));
        p32(o, 8, g32(o, 8) + g32(o, 32));
        return size >= 0;
    }
    int v = g32(o, 20);
    int dec = v > 0 ? 0x51F : 0x83;
    v -= dec;
    p32(o, 20, v);
    int size = g32(o, 28) + v;
    p32(o, 28, size);
    if (size < 0) return 0;
    int left = (signed char)o[3] - 1;
    o[3] = (unsigned char)left;
    return left > 0;
}

int spray_frame(Spray *s, const float view[12], const Course *objects, int part_sand, int part_water, SceneItem *out, int max)
{
    unsigned char *m = s->m;
    int n = 0, count = g32(m, H_COUNT);
    if (count == 0) return 0;
    int r12 = count - 1, r13 = count - 1;                                                    /* indices into the active list: the one being visited and the last live one */
    for (int k = count; k > 0; k--, r12--) {
        unsigned addr = (unsigned)g32(m, ACT_ARRAY + 4 * r12);
        unsigned char *o = obj_at(s, addr);
        {   /* 0x0601D4FC: draw */
            Mat t;
            memcpy(t.m, view, sizeof t.m);
            for (int r = 0; r < 3; r++)
                t.m[4 * r + 3] += t.m[4 * r] * ((float)g32(o, 4) * (1.0f / 65536.0f)) + t.m[4 * r + 1] * ((float)g32(o, 8) * (1.0f / 65536.0f)) + t.m[4 * r + 2] * ((float)g32(o, 12) * (1.0f / 65536.0f));
            if (m_visible(&t, 1.0f) && n < max) {
                int part = (unsigned)g32(o, 36) == A_WATER ? part_water : part_sand;
                if (part >= 0) {
                    out[n].c = objects; out[n].part = part; out[n].clip = 0;
                    memset(out[n].m, 0, sizeof out[n].m);
                    out[n].m[0] = 1.0f; out[n].m[5] = (float)g32(o, 28) * (1.0f / 65536.0f); out[n].m[10] = 1.0f;       /* draw_splash_polygon: y scaled by r6 / 65536 */
                    out[n].m[3] = t.m[3]; out[n].m[7] = t.m[7]; out[n].m[11] = t.m[11];
                    n++;
                }
            }
        }
        if (!sp_update(o)) {
            p32(m, ACT_ARRAY + 4 * r12, g32(m, ACT_ARRAY + 4 * r13));
            int nfree = g32(m, H_NFREE);
            p32(m, FREE_ARRAY + 4 * nfree, (int)addr);
            p32(m, H_NFREE, nfree + 1);
            p32(m, H_COUNT, g32(m, H_COUNT) - 1);
            r13--;
        }
    }
    return n;
}
