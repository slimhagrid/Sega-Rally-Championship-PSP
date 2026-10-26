/* Sega Rally PSP - course renderer (M5), see render.h */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspgu.h>
#include <pspgum.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define MAX_POLYS   4096
#define MAX_PARTS_V 1024
#define NEAR_Z      1.25f           /* course near plane (draw_course: set_clip3d_f(0x14000)) */
#define OBJ_NEAR_Z  0.5f            /* cars / objects: template near plane 0x8000, polygons are not clipped */
#define FAR_Z       255.0f          /* depth key limit */
#define SCALE_S     scale_s         /* perspective scale (1P: 224; a replay's camera asks for more: mkpoly::perspective, scene_set_pers) */
static float scale_s = 224.0f;
void scene_set_pers(int p) { scale_s = p > 0 ? (float)p : 224.0f; }
#define HALF_W      176.0f
#define HALF_H      112.0f

typedef struct { float u, v; unsigned int col; float x, y, z; } Vtx;
#define MINF(a, b) ((a) < (b) ? (a) : (b))               /* fminf / fmaxf are libm calls on the PSP (no NaNs here: the depths are finite) */
#define MAXF(a, b) ((a) > (b) ? (a) : (b))

int rcp_load(Course *c, const char *path)
{
    memset(c, 0, sizeof *c);
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return -1;
    long size = (long)sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    c->file = (unsigned char *)memalign(16, (size_t)size + 16);
    if (!c->file) { sceIoClose(fd); return -2; }
    long got = 0;
    while (got < size) {
        int n = sceIoRead(fd, c->file + got, (unsigned)(size - got > 65536 ? 65536 : size - got));
        if (n <= 0) break;
        got += n;
    }
    sceIoClose(fd);
    if (got != size || memcmp(c->file, "RCP1", 4) != 0) { rcp_free(c); return -3; }
    const unsigned int *h = (const unsigned int *)(c->file + 4);
    c->nparts = h[0]; c->ntex = h[1]; c->npoly = h[2]; c->nverts = h[3];
    c->part = (const RcpPart *)(c->file + h[4]);
    c->tex = (const RcpTex *)(c->file + h[5]);
    c->poly = (const RcpPoly *)(c->file + h[6]);
    c->vert = (const float *)(c->file + h[7]);
    c->vis = c->file + h[8];
    c->vis_size = h[9];
    c->kind = h[10];                                    /* header word at offset 44: 0 course, 1 objects */
    sceKernelDcacheWritebackAll();
    return 0;
}

void rcp_free(Course *c)
{
    if (c->file) free(c->file);
    memset(c, 0, sizeof *c);
}

static unsigned int abgr_from_rgb555(unsigned int c)
{
    unsigned int r = (c & 31) << 3, g = ((c >> 5) & 31) << 3, b = ((c >> 10) & 31) << 3;
    r |= r >> 5; g |= g >> 5; b |= b >> 5;
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

/* ---- per-frame work buffers ------------------------------------------------------------------------------------------------- */
/* a colour offset (the VDP2 fades: R, G, B added to every colour of the items, -255 .. 255) for the next scene_draw calls: flat colours are offset on the CPU, a texture's palette is copied with the offset
 * applied (the GE reads it when it runs the display list: a ring of copies) */
static int coff_rgb[3], coff_on;
#define CLUT_RING 96
static unsigned int __attribute__((aligned(16))) clut_ring[CLUT_RING][16];
static int clut_pos;
void scene_set_coloff(int r, int g, int b) { coff_rgb[0] = r; coff_rgb[1] = g; coff_rgb[2] = b; coff_on = (r | g | b) != 0; clut_pos = 0; }       /* called once per picture: the last picture's palettes are done with */

static unsigned int off_abgr(unsigned int c)
{
    int r, g, b;
    if (!coff_on) return c;
    r = (int)(c & 255) + coff_rgb[0]; g = (int)((c >> 8) & 255) + coff_rgb[1]; b = (int)((c >> 16) & 255) + coff_rgb[2];
    r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
    return (c & 0xFF000000u) | ((unsigned)b << 16) | ((unsigned)g << 8) | (unsigned)r;
}

#define MAX_ITEMS 512
#define VPOOL 16384                                 /* transformed vertices of all items of a frame */
static unsigned int ent_poly[MAX_POLYS];             /* polygon numbers (within the item's pack) that passed the tests */
static unsigned short ent_bucket[MAX_POLYS];
static unsigned short ent_item[MAX_POLYS];
static unsigned short sorted[MAX_POLYS];             /* entry numbers, far to near */
static unsigned int bucket_count[8193];
/* The GE reads the vertices when it runs the display list, which is after every scene_draw of the frame has returned (the menus draw up to four cars into one list): each call gets its own
 * part of a ring, never the same memory as an earlier call of the frame (the PSP showed the first cars with the last car's vertices; PPSSPP reads the data at the call). */
#define VRING (MAX_POLYS * 6 * 3)
static Vtx __attribute__((aligned(16))) vring[VRING];
static int vring_pos;
static Vtx *vbuf;
static float vpool[VPOOL * 3];                       /* transformed vertices (view space), item i at voff[i] */
static int voff[MAX_ITEMS];
static signed char vlev[VPOOL];                      /* kind 2 (menu models): the brightness level of every transformed vertex, -16 .. 15 */
static float scene_light[3] = { 0.0f, 0.0f, 1.0f };  /* kind 2: the light direction in view space (unit vector) */

static void project_matrix(ScePspFMatrix4 *m, float n)
{
    const float f = 300.0f;
    memset(m, 0, sizeof *m);
    float *a = (float *)m;                           /* column-major */
    a[0] = SCALE_S / HALF_W;                         /* ndc x = x * S / (176 z) */
    a[5] = SCALE_S / HALF_H;                         /* ndc y = y * S / (112 z) (y up) */
    a[10] = (f + n) / (f - n);
    a[11] = 1.0f;                                    /* w = z */
    a[14] = -2.0f * f * n / (f - n);
}

#define POLY_TYPE(p) (((p)->flags >> 5) & 15)
#define IS_MESH(p)   (POLY_TYPE(p) == 8)

/* development: where scene_draw spends its time (the menus' perf log reads these: microseconds summed over the calls, the calls, vertices, polygons) */
unsigned long long rs_t_xform, rs_t_sort, rs_t_emit, rs_t_ge;
unsigned rs_calls, rs_verts, rs_polys, rs_tris;

static float scene_dx, scene_dy;
static const short *scene_cols;                      /* kind 2: a colour (RGB555) per item that replaces the polygon colours of that item; -1 = the model's own */
void scene_set_offset(float dx, float dy) { scene_dx = dx; scene_dy = dy; }
void scene_set_colors(const short *cols) { scene_cols = cols; }
void scene_set_light(float x, float y, float z) { scene_light[0] = x; scene_light[1] = y; scene_light[2] = z; }

void scene_draw(const SceneItem *items, int nitems, const RenderRect *rect, RenderStats *st)
{
    int nent = 0, vused = 0, skipped = 0;
    unsigned long long tp0 = sceKernelGetSystemTimeWide(), tp1, tp2, tp3;
    if (nitems > MAX_ITEMS) nitems = MAX_ITEMS;
    for (int pi = 0; pi < nitems; pi++) {
        const Course *c = items[pi].c;
        const float *view = items[pi].m;
        const RcpPart *pt = &c->part[items[pi].part];
        const float near = c->kind ? OBJ_NEAR_Z : NEAR_Z;
        float *vv = vpool + 3 * vused;
        voff[pi] = vused;
        if (pt->nverts > MAX_PARTS_V || vused + pt->nverts > VPOOL) { voff[pi] = -1; skipped++; continue; }
        vused += pt->nverts;
        const float *vs = c->vert + 3 * pt->first_vert;
        for (int i = 0; i < pt->nverts; i++) {
            float x = vs[3 * i], y = vs[3 * i + 1], z = vs[3 * i + 2];
            vv[3 * i]     = view[0] * x + view[1] * y + view[2] * z + view[3];
            vv[3 * i + 1] = view[4] * x + view[5] * y + view[6] * z + view[7];
            vv[3 * i + 2] = view[8] * x + view[9] * y + view[10] * z + view[11];
            if (c->kind == 2) {                             /* the game's FUN_060190B4: level = integer part of (R * normal) . L (L has length 16), clamped to [-16, 15] */
                const short *nn = (const short *)c->vis + 3 * (pt->first_vert + (unsigned)i);
                float nx = nn[0] * (1.0f / 16384.0f), ny = nn[1] * (1.0f / 16384.0f), nz = nn[2] * (1.0f / 16384.0f);
                float d = 16.0f * ((view[0] * nx + view[1] * ny + view[2] * nz) * scene_light[0] + (view[4] * nx + view[5] * ny + view[6] * nz) * scene_light[1] + (view[8] * nx + view[9] * ny + view[10] * nz) * scene_light[2]);
                int lv = (int)(d + 64.0f) - 64;                 /* floor (|d| < 64: the bias keeps the argument positive; floorf is a libm call, once per vertex) */
                vlev[vused - pt->nverts + i] = (signed char)(lv < -16 ? -16 : lv > 15 ? 15 : lv);
            }
        }
        for (int k = 0; k < pt->npolys && nent < MAX_POLYS; k++) {
            unsigned int pn = pt->first_poly + k;
            const RcpPoly *p = &c->poly[pn];
            float z0 = vv[3 * p->kv[0] + 2], z1 = vv[3 * p->kv[1] + 2], z2 = vv[3 * p->kv[2] + 2], z3 = vv[3 * p->kv[3] + 2];
            int tri = p->flags & 1;
            int behind = 0;
            for (int j = 0; j < (tri ? 3 : 4); j++)
                if (vv[3 * p->kv[j] + 2] < near) behind++;
            /* course polygons are clipped at the near plane (dropped only when entirely behind), object polygons are not clipped: any vertex behind drops them */
            if ((c->kind && !items[pi].clip) ? behind != 0 : behind == (tri ? 3 : 4)) continue;
            float key;
            switch ((p->flags >> 1) & 3) {
            case 0: key = (z0 + z1 + z2 + z3) * 0.25f; break;
            case 1: key = MINF(MINF(z0, z1), MINF(z2, z3)); break;
            case 2: key = MAXF(MAXF(z0, z1), MAXF(z2, z3)); break;
            default: key = MAXF(MAXF(z0, z1), MAXF(z2, z3)) + 4.0f; break;     /* mode 3 = max + 4 (measured on the game's chain; the notes said min + 4) */
            }
            if (key > FAR_Z) continue;
            if ((p->flags & 0x10) && !behind) {             /* single sided: cull when 2*area >= 0 (screen y down) */
                /* the sign of the projected area without the six divisions: with every z > 0 it is the sign of -det[(z, x, y) of the three corners] */
                const float *a = &vv[3 * p->kv[0]], *b = &vv[3 * p->kv[1]], *d = &vv[3 * p->kv[2]];
                float det = a[2] * (b[0] * d[1] - b[1] * d[0]) - a[0] * (b[2] * d[1] - b[1] * d[2]) + a[1] * (b[2] * d[0] - b[0] * d[2]);
                if (det <= 0.0f) continue;
            }
            int bk = key <= 0.0f ? 0 : (int)(key * 32.0f);
            if (bk > 8191) bk = 8191;
            ent_poly[nent] = pn;
            ent_bucket[nent] = (unsigned short)bk;
            ent_item[nent] = (unsigned short)pi;
            nent++;
        }
    }
    /* counting sort by bucket (stable); drawn from the farthest bucket down to 0 */
    tp1 = sceKernelGetSystemTimeWide();
    memset(bucket_count, 0, sizeof bucket_count);
    for (int i = 0; i < nent; i++) bucket_count[ent_bucket[i] + 1]++;
    for (int b = 0; b < 8192; b++) bucket_count[b + 1] += bucket_count[b];
    for (int i = 0; i < nent; i++) sorted[bucket_count[ent_bucket[i]]++] = (unsigned short)i;

    /* ---- emit triangles far -> near; a run = consecutive polygons with the same pack / texture / mesh flag ---- */
    tp2 = sceKernelGetSystemTimeWide();
    int nv = 0;
    if (vring_pos + nent * 6 > VRING) vring_pos = 0;
    vbuf = vring + vring_pos;
    static const Course *run_pack[MAX_POLYS];
    static int run_tex[MAX_POLYS], run_first[MAX_POLYS], run_cnt[MAX_POLYS], run_mesh[MAX_POLYS];
    int nrun = 0;
    int tris = 0;
    for (int s = nent - 1; s >= 0; s--) {
        int e = sorted[s];
        const Course *c = items[ent_item[e]].c;
        const RcpPoly *p = &c->poly[ent_poly[e]];
        const float *vv = vpool + 3 * voff[ent_item[e]];
        unsigned int col = 0xFFFFFFFFu;
        float uv[4][2] = { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };
        int tex = (p->tex == 0xFFFF) ? -1 : p->tex;
        int mesh = IS_MESH(p);
        if (tex >= 0) {
            const RcpTex *t = &c->tex[tex];
            float w = t->w, h = t->h;
            float u[4] = { 0, w, w, 0 }, v[4] = { 0, 0, h, h };
            if (p->flags & 0x1000) for (int k = 0; k < 4; k++) u[k] = w - u[k];
            if (p->flags & 0x2000) for (int k = 0; k < 4; k++) v[k] = h - v[k];
            const float itw = 1.0f / (float)t->tw, ith = 1.0f / (float)t->th;              /* power-of-two sizes: the reciprocal is exact */
            for (int k = 0; k < 4; k++) { uv[k][0] = u[k] * itw; uv[k][1] = v[k] * ith; }
        } else {
            col = off_abgr(abgr_from_rgb555(p->colr));
        }
        if (mesh) col = (col & 0x00FFFFFFu) | 0x80000000u;       /* VDP1 mesh = every other pixel: about half transparent */
        static const int T[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
        int first = nv;
        for (int ti = 0; ti < 2; ti++) {
            int a = p->cv[T[ti][0]], b = p->cv[T[ti][1]], d = p->cv[T[ti][2]];
            if (a == b || b == d || a == d) continue;
            int cs[3] = { T[ti][0], T[ti][1], T[ti][2] };
            int vi[3] = { a, b, d };
            for (int k = 0; k < 3; k++) {
                Vtx *o = &vbuf[nv++];
                o->u = uv[cs[k]][0]; o->v = uv[cs[k]][1]; o->col = col;
                if (c->kind == 2) {                         /* flat colour + the vertex brightness added to every 5 bit channel (VDP1 gouraud) */
                    int pc = (scene_cols && scene_cols[ent_item[e]] >= 0) ? scene_cols[ent_item[e]] : p->colr;
                    int lv = vlev[voff[ent_item[e]] + vi[k]], r = (pc & 31) + lv, g = ((pc >> 5) & 31) + lv, b = ((pc >> 10) & 31) + lv;
                    r = r < 0 ? 0 : r > 31 ? 31 : r; g = g < 0 ? 0 : g > 31 ? 31 : g; b = b < 0 ? 0 : b > 31 ? 31 : b;
                    o->col = off_abgr(abgr_from_rgb555((unsigned)(r | (g << 5) | (b << 10))));
                }
                o->x = vv[3 * vi[k]]; o->y = vv[3 * vi[k] + 1]; o->z = vv[3 * vi[k] + 2];
            }
            tris++;
        }
        if (nv == first) continue;
        if (nrun > 0 && run_pack[nrun - 1] == c && run_tex[nrun - 1] == tex && run_mesh[nrun - 1] == mesh) run_cnt[nrun - 1] += nv - first;
        else { run_pack[nrun] = c; run_tex[nrun] = tex; run_mesh[nrun] = mesh; run_first[nrun] = first; run_cnt[nrun] = nv - first; nrun++; }
    }
    sceKernelDcacheWritebackRange(vbuf, sizeof(Vtx) * (unsigned)nv);
    vring_pos += (nv + 3) & ~3;
    tp3 = sceKernelGetSystemTimeWide();

    /* ---- GE state ---- */
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGuOffset(2048 - 240, 2048 - 136);
    sceGuViewport(2048 - 240 + rect->x0 + rect->w / 2 + (int)(scene_dx * (float)rect->w / 352.0f), 2048 - 136 + rect->y0 + rect->h / 2 + (int)(scene_dy * (float)rect->h / 224.0f), rect->w, rect->h);
    sceGuScissor(rect->x0, rect->y0, rect->w, rect->h);     /* sceGuScissor takes x, y, width, height */
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuAlphaFunc(GU_GREATER, 0, 0xff);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuShadeModel(GU_FLAT);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    int draws = 0, cur_kind = -1, cur_mesh = 0;
    for (int r = 0; r < nrun; r++) {
        const Course *c = run_pack[r];
        if ((int)c->kind != cur_kind) {                            /* course and objects have different near planes */
            ScePspFMatrix4 proj;
            project_matrix(&proj, c->kind ? OBJ_NEAR_Z : NEAR_Z);
            sceGumMatrixMode(GU_PROJECTION);
            sceGumLoadMatrix(&proj);
            sceGumMatrixMode(GU_MODEL);
            cur_kind = (int)c->kind;
            sceGuShadeModel(c->kind == 2 ? GU_SMOOTH : GU_FLAT);
        }
        if (run_mesh[r] != cur_mesh) {
            if (run_mesh[r]) sceGuEnable(GU_BLEND); else sceGuDisable(GU_BLEND);
            cur_mesh = run_mesh[r];
        }
        if (run_tex[r] < 0) {
            sceGuDisable(GU_TEXTURE_2D);
        } else {
            const RcpTex *t = &c->tex[run_tex[r]];
            sceGuEnable(GU_TEXTURE_2D);
            if (coff_on && clut_pos < CLUT_RING) {                  /* a fade: the palette with the offset applied (the alpha bit of an entry stays) */
                unsigned int *dst = clut_ring[clut_pos++];
                const unsigned short *src = (const unsigned short *)(c->file + t->co);
                for (int k = 0; k < 16; k++) dst[k] = (off_abgr(abgr_from_rgb555(src[k])) & 0x00FFFFFFu) | ((src[k] & 0x8000) ? 0xFF000000u : 0u);
                sceKernelDcacheWritebackRange(dst, sizeof clut_ring[0]);
                sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
                sceGuClutLoad(2, dst);
            } else {
                sceGuClutMode(GU_PSM_5551, 0, 0xff, 0);
                sceGuClutLoad(2, c->file + t->co);
            }
            sceGuTexMode(GU_PSM_T4, 0, 0, 0);
            sceGuTexImage(0, t->tw, t->th, t->tw, c->file + t->po);
        }
        sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, run_cnt[r], 0, vbuf + run_first[r]);
        draws++;
    }
    sceGuDisable(GU_BLEND);
    {
        unsigned long long tp4 = sceKernelGetSystemTimeWide();
        rs_t_xform += tp1 - tp0; rs_t_sort += tp2 - tp1; rs_t_emit += tp3 - tp2; rs_t_ge += tp4 - tp3;
        rs_calls++; rs_verts += (unsigned)vused; rs_polys += (unsigned)nent; rs_tris += (unsigned)tris;
    }
    if (st) { st->polys_in = nent; st->polys_drawn = nent; st->tris = tris; st->draws = draws; st->skipped = skipped; st->vused = vused; }
}

void rcp_draw(const Course *c, const float *view, const unsigned char *parts, int nparts, const RenderRect *rect, RenderStats *st)
{
    SceneItem it[16];
    if (nparts > 16) nparts = 16;
    for (int i = 0; i < nparts; i++) {
        it[i].c = c;
        it[i].part = parts[i] & 127;
        it[i].clip = 0;
        memcpy(it[i].m, view, 12 * sizeof(float));
    }
    scene_draw(it, nparts, rect, st);
}

/* ---- VDP2 sky --------------------------------------------------------------------------------------------------------------- */
int sky_load(Sky *s, const char *path)
{
    memset(s, 0, sizeof *s);
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return -1;
    long size = (long)sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    s->file = (unsigned char *)memalign(16, (size_t)size + 16);
    if (!s->file) { sceIoClose(fd); return -2; }
    long got = 0;
    while (got < size) {
        int n = sceIoRead(fd, s->file + got, (unsigned)(size - got > 65536 ? 65536 : size - got));
        if (n <= 0) break;
        got += n;
    }
    sceIoClose(fd);
    if (got != size || memcmp(s->file, "RBG1", 4) != 0) { sky_free(s); return -3; }
    memcpy(&s->base_x, s->file + 0x10, 4);
    memcpy(&s->base_y, s->file + 0x14, 4);
    memcpy(&s->yaw_ppu, s->file + 0x18, 4);
    memcpy(&s->roll_scale, s->file + 0x1C, 4);
    s->back = *(unsigned short *)(s->file + 0x28);
    memcpy(&s->clut_off, s->file + 0x2C, 4);
    memcpy(&s->tex_off[0], s->file + 0x30, 4);
    memcpy(&s->tex_off[1], s->file + 0x34, 4);
    sceKernelDcacheWritebackAll();
    return 0;
}

void sky_free(Sky *s)
{
    if (s->file) free(s->file);
    memset(s, 0, sizeof *s);
}

typedef struct { float x, y, z; } SkyPt;           /* x, y screen (352x224 space); z unused */

/* convex polygon (<= 8 points) clipped against lo <= u <= hi where u = ua + ux * x + uy * y */
static int clip_strip(const float *px, const float *py, int n, float ua, float ux, float uy, float lo, float hi, float *ox, float *oy)
{
    float tx[16], ty[16];
    int m = 0;
    for (int pass = 0; pass < 2; pass++) {
        const float *ix = pass == 0 ? px : tx, *iy = pass == 0 ? py : ty;
        int in_n = pass == 0 ? n : m;
        float *dx = pass == 0 ? tx : ox, *dy = pass == 0 ? ty : oy;
        float sign = pass == 0 ? 1.0f : -1.0f, lim = pass == 0 ? lo : hi;
        int k = 0;
        for (int i = 0; i < in_n; i++) {
            int j = (i + 1) % in_n;
            float di = sign * ((ua + ux * ix[i] + uy * iy[i]) - lim), dj = sign * ((ua + ux * ix[j] + uy * iy[j]) - lim);
            if (di >= 0) { dx[k] = ix[i]; dy[k] = iy[i]; k++; }
            if ((di >= 0) != (dj >= 0)) {
                float t = di / (di - dj);
                dx[k] = ix[i] + (ix[j] - ix[i]) * t; dy[k] = iy[i] + (iy[j] - iy[i]) * t; k++;
            }
        }
        if (pass == 0) m = k; else return k;
    }
    return 0;
}

static void sky_draw_px(const Sky *s, float px, float ct, float st, const RenderRect *rect);

void sky_draw(const Sky *s, unsigned int yaw16, int roll16, const RenderRect *rect)
{
    float theta = (float)roll16 * s->roll_scale * 6.2831853f / 65536.0f;
    sky_draw_px(s, s->base_x + (float)(yaw16 & 0xFFFF) * s->yaw_ppu, cosf(theta), sinf(theta), rect);
}

void sky_draw_table(const Sky *s, int mx, int a, int b, const RenderRect *rect)            /* the console's own rotation parameters (src/camera.c camera_sky_pass): Mx, A = E, B in 16.16 */
{
    sky_draw_px(s, (float)(mx & 0x03FFFFFF) / 65536.0f, (float)a / 65536.0f, (float)b / 65536.0f, rect);
}

static void sky_draw_px(const Sky *s, float px, float ct, float st, const RenderRect *rect)
{
    const float sxs = (float)rect->w / 352.0f, sys = (float)rect->h / 224.0f;
    /* panorama coordinates of a screen point: u = ua + ct*x + st*y ; v = va - st*x + ct*y */
    float ua = px + 176.0f - ct * 176.0f - st * 112.0f;
    float va = s->base_y + 112.0f + st * 176.0f - ct * 112.0f;

    sceGuOffset(2048 - 240, 2048 - 136);
    sceGuScissor(rect->x0, rect->y0, rect->w, rect->h);     /* sceGuScissor takes x, y, width, height */
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_CLIP_PLANES);
    /* back colour: a flat rectangle */
    {
        unsigned int c = abgr_from_rgb555(s->back);
        Vtx *q = (Vtx *)sceGuGetMemory(6 * sizeof(Vtx));
        float x0 = (float)rect->x0, y0 = (float)rect->y0, x1 = (float)(rect->x0 + rect->w), y1 = (float)(rect->y0 + rect->h);
        const float xs[6] = { x0, x1, x1, x0, x1, x0 }, ys[6] = { y0, y0, y1, y0, y1, y1 };
        for (int i = 0; i < 6; i++) { q[i].u = 0; q[i].v = 0; q[i].col = c; q[i].x = xs[i]; q[i].y = ys[i]; q[i].z = 0; }
        sceGuDisable(GU_TEXTURE_2D);
        sceGuDisable(GU_ALPHA_TEST);
        sceGuShadeModel(GU_FLAT);
        sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 6, 0, q);
    }
    sceGuEnable(GU_ALPHA_TEST);
    sceGuAlphaFunc(GU_GREATER, 0, 0xff);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuClutMode(GU_PSM_5551, 0, 0xff, 0);
    sceGuClutLoad(32, s->file + s->clut_off);
    sceGuTexMode(GU_PSM_T8, 0, 0, 0);
    const float cx[4] = { 0, 352, 352, 0 }, cy[4] = { 0, 0, 224, 224 };
    for (int tex = 0; tex < 2; tex++) {
        sceGuTexImage(0, 512, 512, 512, s->file + s->tex_off[tex]);
        for (int k = -1; k <= 2; k++) {                       /* the 1024-pixel panorama repeats */
            float lo = 1024.0f * k + 512.0f * tex, hi = lo + 512.0f;
            float ox[16], oy[16];
            int n = clip_strip(cx, cy, 4, ua, ct, st, lo, hi, ox, oy);
            if (n < 3) continue;
            int nv = 0;
            Vtx *fan = (Vtx *)sceGuGetMemory((unsigned)((n - 2) * 3) * sizeof(Vtx));      /* in the display list: the GE reads it later */
            for (int i = 1; i + 1 < n; i++) {
                const int idx[3] = { 0, i, i + 1 };
                for (int j = 0; j < 3; j++) {
                    float x = ox[idx[j]], y = oy[idx[j]];
                    Vtx *o = &fan[nv++];
                    o->u = (ua + ct * x + st * y - lo);                    /* through mode: texel units */
                    o->v = (va - st * x + ct * y);
                    o->col = 0xFFFFFFFFu;
                    o->x = rect->x0 + x * sxs; o->y = rect->y0 + y * sys; o->z = 0;
                }
            }
            sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, nv, 0, fan);
        }
    }
}

/* ---- the off-screen picture ------------------------------------------------------------------------------------------------- */
#define VIEW_OFF 0x110000                  /* VRAM: after the two 512 x 272 x 4 screen buffers (main.c / play.c) */
const RenderRect VIEW_RECT = { 0, 0, 352, 224 };

void view_begin(void)
{
    sceGuDrawBufferList(GU_PSM_8888, (void *)VIEW_OFF, 512);
    sceGuOffset(2048 - 240, 2048 - 136);
    sceGuViewport(2048 - 240 + 176, 2048 - 136 + 112, 352, 224);
    sceGuScissor(0, 0, 352, 224);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuClearColor(0xff000000);
    sceGuClear(GU_COLOR_BUFFER_BIT);
}

void view_end(int draw_off, int mode)
{
    sceGuDrawBufferList(GU_PSM_8888, (void *)(unsigned)draw_off, 512);
    sceGuOffset(2048 - 240, 2048 - 136);
    sceGuViewport(2048, 2048, 480, 272);
    sceGuScissor(0, 0, 480, 272);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_CLIP_PLANES);
    sceGuClearColor(0xff000000);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    float x0 = 64.0f, y0 = 24.0f, x1 = 64.0f + 352.0f, y1 = 24.0f + 224.0f;           /* mode 0: pixel for pixel */
    if (mode != 0) { x0 = 26.0f; x1 = 454.0f; y0 = 0.0f; y1 = 272.0f; }               /* the picture's own shape (352 : 224) at the screen height */
    sceGuTexFlush();
    sceGuTexSync();
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, 512, 256, 512, (void *)(0x04000000u + VIEW_OFF));
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
    sceGuTexFilter(mode == 2 ? GU_LINEAR : GU_NEAREST, mode == 2 ? GU_LINEAR : GU_NEAREST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuShadeModel(GU_FLAT);
    Vtx *q = (Vtx *)sceGuGetMemory(2 * sizeof(Vtx));
    q[0].u = 0; q[0].v = 0; q[0].col = 0xFFFFFFFFu; q[0].x = x0; q[0].y = y0; q[0].z = 0;
    q[1].u = 352; q[1].v = 224; q[1].col = 0xFFFFFFFFu; q[1].x = x1; q[1].y = y1; q[1].z = 0;
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, q);
}

void view_fade(int offset)
{
    if (offset == 0) return;
    if (offset > 255) offset = 255;
    if (offset < -255) offset = -255;
    unsigned v = (unsigned)(offset < 0 ? -offset : offset);
    unsigned col = v | v << 8 | v << 16;
    sceGuScissor(0, 0, 352, 224);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CLIP_PLANES);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(offset < 0 ? GU_REVERSE_SUBTRACT : GU_ADD, GU_FIX, GU_FIX, 0xFFFFFFu, 0xFFFFFFu);                 /* dst + colour  or  dst - colour (both factors 1) */
    sceGuShadeModel(GU_FLAT);
    Vtx *q = (Vtx *)sceGuGetMemory(2 * sizeof(Vtx));
    q[0].u = 0; q[0].v = 0; q[0].col = 0xFF000000u | col; q[0].x = 0; q[0].y = 0; q[0].z = 0;
    q[1].u = 0; q[1].v = 0; q[1].col = 0xFF000000u | col; q[1].x = 352; q[1].y = 224; q[1].z = 0;
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, q);
    sceGuDisable(GU_BLEND);
}
