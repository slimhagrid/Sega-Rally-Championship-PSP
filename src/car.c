/* Sega Rally PSP - car display, see car.h.  Mirrors dispcar::dispGhost @0x06010214 and its tiers (0x0601029E, 0x060103BE, 0x0601042C). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "car.h"
#include "mat.h"

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

int cars_load(CarData *d, const char *path)
{
    long size = 0;
    memset(d, 0, sizeof *d);
    d->file = slurp(path, &size);
    if (!d->file) return -1;
    if (size < 0x40 || memcmp(d->file, "CAR1", 4) != 0) { cars_free(d); return -2; }
    unsigned int nt;
    memcpy(&nt, d->file + 4, 4);
    d->ntypes = (int)nt;
    if (size < 0x40 + (long)nt * 4 * (long)sizeof(CarDesc)) { cars_free(d); return -3; }
    memcpy(d->thr, d->file + 8, 12);
    memcpy(&d->shadow_dy, d->file + 0x14, 4);
    memcpy(&d->k_pitch, d->file + 0x18, 4);
    memcpy(&d->k_roll, d->file + 0x1C, 4);
    d->desc = (const CarDesc *)(d->file + 0x40);
    return 0;
}

void cars_free(CarData *d)
{
    if (d->file) free(d->file);
    memset(d, 0, sizeof *d);
}

static int emit(SceneItem *out, int n, int max, const Course *oc, int part, const Mat *a)
{
    if (part < 0 || n >= max) return n;
    out[n].c = oc;
    out[n].clip = 0;
    out[n].part = part;
    memcpy(out[n].m, a->m, sizeof out[n].m);
    return n + 1;
}

int car_tier(const CarData *d, const CarState *c, const float *view)
{
    if (c->type < 0 || c->type >= d->ntypes) return -1;
    Mat car;
    memcpy(car.m, view, sizeof car.m);
    m_translate(&car, c->pos[0], c->pos[1], c->pos[2]);
    if (!m_visible(&car, 3.0f)) return -1;
    m_rot(&car, 1, c->ang[1]);
    m_rot(&car, 0, c->ang[0]);
    m_rot(&car, 2, c->ang[2]);
    float z = car.m[11];
    if (z > d->thr[2]) return -1;
    return z > d->thr[1] ? 2 : (d->thr[0] > z ? 0 : 1);
}

int car_display(const CarData *d, const CarState *c, int mode, const float *view, const Course *oc, SceneItem *out, int max)
{
    int n = 0;
    if (c->type < 0 || c->type >= d->ntypes) return 0;
    Mat car;
    memcpy(car.m, view, sizeof car.m);
    m_translate(&car, c->pos[0], c->pos[1], c->pos[2]);
    if (!m_visible(&car, 3.0f)) return 0;
    m_rot(&car, 1, c->ang[1]);
    m_rot(&car, 0, c->ang[0]);
    m_rot(&car, 2, c->ang[2]);
    float z = car.m[11];
    if (z > d->thr[2]) return 0;
    int tier = z > d->thr[1] ? 2 : (d->thr[0] > z ? 0 : 1);          /* 0 near, 1 middle, 2 far */
    int w = (short)c->damage;
    int damaged = mode == 2 || (((w - 0x1111) & 0x8888) != 0);
    const CarDesc *ds = &d->desc[c->type * 4 + (tier ? 2 : 0) + (damaged ? 1 : 0)];
    if (mode == 3) return 0;                                        /* statistics only */

    n = emit(out, n, max, oc, ds->body, &car);
    if (tier == 0) {
        n = emit(out, n, max, oc, c->brake ? ds->br_on : ds->br_off, &car);
        for (int i = 0; i < 4; i++) {
            Mat t = car;
            m_translate(&t, ds->wheel[i][0], ds->wheel[i][1] + c->susp[i] * (1.0f / 16.0f), ds->wheel[i][2]);
            if (!m_visible(&t, 0.25f)) continue;
            if (i <= 1) m_rot(&t, 1, c->steer);
            m_rot(&t, 0, -c->spin);
            n = emit(out, n, max, oc, ds->tyre[i], &t);
        }
        Mat sh;                                                     /* shadow: in the frame of the camera again, on the ground under the car */
        memcpy(sh.m, view, sizeof sh.m);
        float gy = (c->ground[0] + c->ground[1] + c->ground[2] + c->ground[3]) * 0.25f - d->shadow_dy;
        m_translate(&sh, c->pos[0], gy, c->pos[2]);
        m_rot(&sh, 1, c->ang[1]);
        m_rot(&sh, 0, ((c->ground[0] + c->ground[1]) - (c->ground[2] + c->ground[3])) * d->k_pitch);
        m_rot(&sh, 2, ((c->ground[0] + c->ground[2]) - (c->ground[1] + c->ground[3])) * d->k_roll);
        if (m_visible(&sh, 3.0f)) n = emit(out, n, max, oc, ds->shadow, &sh);
    } else if (tier == 1) {
        for (int i = 0; i < 4; i++) {
            Mat t = car;
            m_translate(&t, ds->wheel[i][0], ds->wheel[i][1] + c->susp[i] * (1.0f / 16.0f), ds->wheel[i][2]);
            if (i <= 1) m_rot(&t, 1, c->steer);
            n = emit(out, n, max, oc, ds->tyre[i], &t);
        }
    } else {
        float avg = (c->susp[0] + c->susp[1] + c->susp[2] + c->susp[3]) * (1.0f / 64.0f);
        for (int i = 0; i < 4; i++) {
            Mat t = car;
            m_translate(&t, ds->wheel[i][0], ds->wheel[i][1] + avg, ds->wheel[i][2]);
            n = emit(out, n, max, oc, ds->tyre[i], &t);
        }
    }
    return n;
}
