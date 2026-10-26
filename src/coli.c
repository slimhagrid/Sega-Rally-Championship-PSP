/* Sega Rally PSP - course collision map, see coli.h.  Layout of the .rct file: tools/conv_track.py (all little endian). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "coli.h"

#ifdef __PSP__
#include <malloc.h>
#include <pspiofilemgr.h>
static unsigned char *slurp(const char *path, long *size)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return NULL;
    long n = (long)sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    unsigned char *b = (unsigned char *)memalign(16, (size_t)n + 16);
    if (!b) { sceIoClose(fd); return NULL; }
    long got = 0;
    while (got < n) {
        int r = sceIoRead(fd, b + got, (unsigned)(n - got));
        if (r <= 0) break;
        got += r;
    }
    sceIoClose(fd);
    *size = got;
    return b;
}
#else
static unsigned char *slurp(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = (unsigned char *)malloc((size_t)n + 16);
    if (!b) { fclose(f); return NULL; }
    *size = (long)fread(b, 1, (size_t)n, f);
    fclose(f);
    return b;
}
#endif

static inline unsigned int rd32(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24); }
static inline unsigned int rd16(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }

int coli_load(Coli *c, const char *path)
{
    long size = 0;
    memset(c, 0, sizeof *c);
    c->file = slurp(path, &size);
    if (!c->file) return -1;
    if (size < 48 || memcmp(c->file, "RCT1", 4) != 0) { free(c->file); c->file = NULL; return -2; }
    return coli_adopt(c, c->file);
}

/* an RCT1 image in memory: c owns it from now on (coli_free) */
int coli_adopt(Coli *c, unsigned char *file)
{
    memset(c, 0, sizeof *c);
    if (memcmp(file, "RCT1", 4) != 0) return -2;
    c->file = file;
    c->npoly = rd32(c->file + 4);
    c->off_poly = rd32(c->file + 16);
    c->off_cgrid = rd32(c->file + 28);
    c->off_pool = rd32(c->file + 40);
    c->off_quad = rd32(c->file + 20);
    c->off_slice = rd32(c->file + 24);
    c->off_bgrid = rd32(c->file + 32);
    c->off_b2s = rd32(c->file + 36);
    return 0;
}

void coli_free(Coli *c)
{
    if (c->file) free(c->file);
    memset(c, 0, sizeof *c);
}

/* 0x06008008: the grid cell holds a list of polygon indices; the first polygon containing the point wins.  The cell index uses the original's unsigned shifts
 * (column = (x + 2^30) >> 25, row = (2^30 - z) >> 25 of the 11.20 values), so a position outside the map reads another cell or none. */
const unsigned char *coli_find(const Coli *c, int x4, int z4)
{
    unsigned int col = ((unsigned)x4 + 0x40000000u) >> 25;
    unsigned int row = (0x40000000u - (unsigned)z4) >> 25;
    unsigned int cell = row * 64 + col;
    if (cell >= 4096) return NULL;
    unsigned int off = rd32(c->file + c->off_cgrid + 4 * cell);
    if (off == 0xFFFFFFFFu) return NULL;
    const unsigned char *list = c->file + c->off_pool + off;
    unsigned int n = rd16(list);
    for (unsigned int k = 0; k < n; k++) {                             /* FUN_060080AA */
        unsigned int idx = rd16(list + 2 + 2 * k);
        if (idx >= c->npoly) continue;
        const unsigned char *p = c->file + c->off_poly + 52 * idx;
        long long px[4], pz[4];
        for (int i = 0; i < 4; i++) {                                  /* 32-bit differences, like the SH-2's sub */
            px[i] = (int)(rd32(p + 4 + 8 * i) - (unsigned)x4);
            pz[i] = (int)(rd32(p + 8 + 8 * i) - (unsigned)z4);
        }
        int tri = (int)(rd32(p) & 1);
        /* edge functions: the sign of the 64-bit sum decides (MACH of the original); wraparound like the SH-2's 64-bit MAC */
#define EDGE(a, b) ((long long)((unsigned long long)(px[a] * pz[b]) - (unsigned long long)(px[b] * pz[a])))
        long long e0 = EDGE(0, 1), e1 = EDGE(1, 2), e2, e3;
        if (tri) { e2 = EDGE(2, 0); e3 = 0; }
        else { e2 = EDGE(2, 3); e3 = EDGE(3, 0); }
#undef EDGE
        int any_neg = (e0 < 0) | (e1 < 0) | (e2 < 0) | (e3 < 0);
        int all_neg = (e0 < 0) & (e1 < 0) & (e2 < 0) & (e3 < 0);
        if (!any_neg) return p;
        if (all_neg) return NULL;                                      /* the original stops here with "none" */
    }
    return NULL;
}

int coli_y20(const unsigned char *p, int x4, int z4)
{
    int nx = (int)rd32(p + 36), ny = (int)rd32(p + 40), nz = (int)rd32(p + 44), d = (int)rd32(p + 48);
    /* y(2^20) = (d * 2^22 - x * nx - z * nz) / ny with the hardware divider (64 / 32 -> 32 bit, truncating) */
    long long num = (long long)((unsigned long long)((long long)d << 22) - (unsigned long long)((long long)x4 * nx) - (unsigned long long)((long long)z4 * nz));
    return ny ? (int)(num / ny) : 0;
}

/* FUN_06007F3C: the first record of a byte list that contains the point (four edge tests like the collision polygons, quads only) */
static const unsigned char *scan_records(const unsigned char *list, const unsigned char *base, int stride, int x, int z)
{
    int n = list[0];
    for (int k = 0; k < n; k++) {
        const unsigned char *p = base + stride * list[1 + k];
        long long px[4], pz[4], e[4];
        for (int i = 0; i < 4; i++) {
            px[i] = (int)(rd32(p + 4 + 8 * i) - (unsigned)x);
            pz[i] = (int)(rd32(p + 8 + 8 * i) - (unsigned)z);
        }
        for (int i = 0; i < 4; i++) {
            int j = (i + 1) & 3;
            e[i] = (long long)((unsigned long long)(px[i] * pz[j]) - (unsigned long long)(px[j] * pz[i]));
        }
        if ((e[0] | e[1] | e[2] | e[3]) >= 0) return p;                   /* no negative edge: inside */
    }
    return NULL;
}

const unsigned char *coli_find_quad(const Coli *c, int x16, int z16)
{
    unsigned int col = ((unsigned)x16 + 0x04000000u) >> 21;
    unsigned int row = ((0x04000000u - (unsigned)z16) >> 16) >> 5;
    unsigned int cell = row * 64 + col;
    if (cell >= 4096) return NULL;
    unsigned int off = rd32(c->file + c->off_bgrid + 4 * cell);
    if (off == 0xFFFFFFFFu) return NULL;
    return scan_records(c->file + c->off_pool + off, c->file + c->off_quad, 36, x16, z16);
}

const unsigned char *coli_find_slice(const Coli *c, int quad_id, int x16, int z16)
{
    if ((unsigned)quad_id >= 128) return NULL;
    unsigned int off = rd32(c->file + c->off_b2s + 4 * (unsigned)quad_id);
    if (off == 0xFFFFFFFFu) return NULL;
    return scan_records(c->file + c->off_pool + off, c->file + c->off_slice, 44, x16, z16);
}

/* get_d (0x06008060): wall polygons - the 2-D cross product of the first two vertices with the point, divided by the polygon constant (edge length) with the hardware divider, |q| >> 6 */
int coli_wall_d(const unsigned char *p, int x4, int z4)
{
    int x0 = (int)(rd32(p + 4) - (unsigned)x4), z0 = (int)(rd32(p + 8) - (unsigned)z4);
    int x1 = (int)(rd32(p + 12) - (unsigned)x4), z1 = (int)(rd32(p + 16) - (unsigned)z4);
    long long acc = (long long)((unsigned long long)((long long)x0 * z1) + (unsigned long long)((long long)z0 * (int)(0u - (unsigned)x1)));
    int dv = (int)rd32(p + 48), q;
    if (dv == 0) q = acc >= 0 ? 0x7FFFFFFF : (int)0x80000000;
    else {
        long long qq = acc / dv;                                     /* the on-chip divider: 64 / 32, truncating, saturating */
        q = qq > 0x7FFFFFFFLL ? 0x7FFFFFFF : qq < -0x80000000LL ? (int)0x80000000 : (int)qq;
    }
    if (q < 0) q = (int)(0u - (unsigned)q);
    return (int)((unsigned)q >> 6);
}

int coli_ground(const Coli *c, int x, int y, int z, int *alt, int normal[3])
{
    int x4 = (int)((unsigned)x << 4), z4 = (int)((unsigned)z << 4);
    const unsigned char *p = coli_find(c, x4, z4);
    if (!p) return 0;
    int type = (short)(rd32(p) >> 16);
    if (type == 0) return 0;
    *alt = y - (coli_y20(p, x4, z4) >> 4);
    if (normal) { normal[0] = (int)rd32(p + 36); normal[1] = (int)rd32(p + 40); normal[2] = (int)rd32(p + 44); }
    return type;
}

int coli_get2(const Coli *c, int x, int y, int z, int normal[3], int *h)
{
    int x4 = (int)((unsigned)x << 4), z4 = (int)((unsigned)z << 4);
    const unsigned char *p = coli_find(c, x4, z4);
    normal[0] = normal[1] = normal[2] = 0;
    *h = 0;
    if (!p) return 0;                                              /* the original reads address 0 here; the other cars only drive on the map */
    int type = (short)(rd32(p) >> 16);
    normal[0] = (int)rd32(p + 36); normal[1] = (int)rd32(p + 40); normal[2] = (int)rd32(p + 44);
    if (type == 0) *h = coli_wall_d(p, x4, z4);
    else *h = coli_y20(p, x4, z4) - (int)((unsigned)y << 4);
    return type;
}
