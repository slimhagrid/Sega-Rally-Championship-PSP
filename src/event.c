/* Sega Rally PSP - event objects, see event.h.  The state functions mirror the original's (addresses in the comments: 0RALLY.DAT, 0x0601C06C .. 0x0601CCAC). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "event.h"
#include "mat.h"
#include "gmath.h"

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
    b[got] = 0;
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
    b[*size] = 0;
    fclose(f);
    return b;
}
#endif

/* ---- parts used by name (object.names) ---------------------------------------------------------------------------------------- */
#define N_PEOPLE 26                 /* spectator table 0x0601C614: 22 OBJECT.MDL figures, then the ANIMAL2 sprites (zebra, elephant) */
enum {
    P_KATA1 = 0,                    /* 0 .. 25: the people table */
    P_CHECKPOINT_S = 26, P_CHECKPOINT_L, P_FINISH_L, P_POLE_S, P_POLE_M, P_POLE_L, P_AM3KANBAN, P_MEGAKANBAN, P_STARTOBJ, P_PILON, P_SWAN,
    P_FLAG1_00 = 40,                /* 40 + n: flag1_nn (n = 0..31) */
    P_FLAG2_00 = 72,                /* 72 + n: flag2_nn */
    P_HELI1 = 104, P_HELI2, P_HELI3, P_HELI_KAGE, P_SAND,      /* helicopter body, main rotor, tail rotor, shadow; rotor dust */
    P_TACHO = 109, P_GO, P_ONE, P_TWO, P_THREE,                /* HUD sprites */
    P_WATER = 114,                                             /* spray drops (water_poly) */
    P_COUNT = 115
};

static const char *const people_names[N_PEOPLE] = { "kata1", "kata2", "kata3", "sohe1", "sohe2", "sho1", "sho2", "arai", "ken1", "ken2", "ken3", "matu1", "matu2", "matu3", "matu4", "matu5",
                                                    "matu6", "oja1", "oja2", "gucchi", "yamatan", "ken4", "shima1_2d", "shima2_2d", "zou_2d", "zou_2d" };
static const char *const heli_names[5] = { "heli1", "heli2", "heli3", "heli_kage", "sand" };
static const char *const hud_names[5] = { "tachometer", "go", "one", "two", "three" };
static const char *const misc_names[11] = { "checkpoint_s", "checkpoint_l", "finish_l", "pole_s", "pole_m", "pole_l", "am3kanban", "megakanban", "startobj", "pilon", "swan" };

static int name_index(const unsigned char *txt, const char *name)
{
    char key[64];
    snprintf(key, sizeof key, " %s_poly", name);
    size_t kl = strlen(key);
    const char *p = (const char *)txt;
    while ((p = strstr(p, key)) != NULL) {
        char nx = p[kl];
        if (nx == 0 || nx == 10 || nx == 13) {              /* whole name (the file has CR LF line ends when written on Windows) */
            const char *ls = p;
            while (ls > (const char *)txt && ls[-1] != 10) ls--;
            return atoi(ls);
        }
        p += kl;
    }
    return -1;
}

int ev_load(Events *e, const char *events_dat, const char *names_txt, const Course *objects)
{
    long size = 0, nsize = 0;
    memset(e, 0, sizeof *e);
    e->objects = objects;
    e->file = slurp(events_dat, &size);
    if (!e->file) return -1;
    if (size < 8 || memcmp(e->file, "EVT1", 4) != 0) return -2;
    unsigned char *names = slurp(names_txt, &nsize);
    if (!names) return -3;
    for (int i = 0; i < P_COUNT; i++) e->parts[i] = -1;
    for (int i = 0; i < N_PEOPLE; i++) e->parts[P_KATA1 + i] = name_index(names, people_names[i]);
    for (int i = 0; i < 11; i++) e->parts[P_CHECKPOINT_S + i] = name_index(names, misc_names[i]);
    for (int i = 0; i < 5; i++) e->parts[P_HELI1 + i] = name_index(names, heli_names[i]);
    for (int i = 0; i < 5; i++) e->parts[P_TACHO + i] = name_index(names, hud_names[i]);
    e->parts[P_WATER] = name_index(names, "water");
    for (int i = 0; i < 32; i++) {
        char nm[32];
        snprintf(nm, sizeof nm, "flag1_%02d", i);
        e->parts[P_FLAG1_00 + i] = name_index(names, nm);
        snprintf(nm, sizeof nm, "flag2_%02d", i);
        e->parts[P_FLAG2_00 + i] = name_index(names, nm);
    }
    free(names);
    {   /* motion section */
        const unsigned char *f = e->file;
        unsigned int mo, nt, a;
        memcpy(&mo, f + 8 + 48, 4);
        memcpy(&nt, f + mo, 4);
        a = mo + 4;
        const unsigned char *tabs[7];
        for (unsigned int i = 0; i < nt && i < 7; i++) {
            unsigned int n, k, off;
            memcpy(&n, f + a, 4); memcpy(&k, f + a + 4, 4); memcpy(&off, f + a + 8, 4);
            a += 12;
            e->anim[i].n = (int)n; e->anim[i].nparts = (int)k;
            for (unsigned int j = 0; j < k && j < 25; j++) { short v; memcpy(&v, f + a + 2 * j, 2); e->anim[i].parts[j] = v; }
            a += 2 * k + ((4 - ((2 * k) & 3)) & 3);
            tabs[i] = (const unsigned char *)(size_t)off;
        }
        unsigned int blen;
        memcpy(&blen, f + a, 4);
        const unsigned char *block = f + a + 4;
        for (unsigned int i = 0; i < nt && i < 7; i++) e->anim[i].data = block + (size_t)tabs[i];
        e->motion = block;
        memcpy(e->flag_anim, block + blen, sizeof e->flag_anim);
    }
    return 0;
}

int ev_part_sand(const Events *e) { return e->parts[P_SAND]; }
int ev_part_water(const Events *e) { return e->parts[P_WATER]; }

void ev_free(Events *e)
{
    if (e->file) free(e->file);
    memset(e, 0, sizeof *e);
}

/* ---- slot access (big-endian layout) --------------------------------------------------------------------------------------------- */
static inline int g32(const EvSlot *s, int o) { return (int)(((unsigned)s->m[o] << 24) | ((unsigned)s->m[o + 1] << 16) | ((unsigned)s->m[o + 2] << 8) | s->m[o + 3]); }
static inline void p32(EvSlot *s, int o, int v) { s->m[o] = (unsigned char)((unsigned)v >> 24); s->m[o + 1] = (unsigned char)((unsigned)v >> 16); s->m[o + 2] = (unsigned char)((unsigned)v >> 8); s->m[o + 3] = (unsigned char)v; }
static inline int g16(const EvSlot *s, int o) { return (short)((s->m[o] << 8) | s->m[o + 1]); }
static inline int gu16(const EvSlot *s, int o) { return (s->m[o] << 8) | s->m[o + 1]; }
static inline void p16(EvSlot *s, int o, int v) { s->m[o] = (unsigned char)(v >> 8); s->m[o + 1] = (unsigned char)v; }
static inline int g8(const EvSlot *s, int o) { return (signed char)s->m[o]; }
static inline void p8(EvSlot *s, int o, int v) { s->m[o] = (unsigned char)v; }

enum { ST_FREE = 0, ST_T1_INIT, ST_T3_INIT, ST_CROWD_INIT, ST_CROWD_RUN, ST_T21_INIT, ST_T21_RUN, ST_NOP, ST_DELETE,
       ST_GATE_INIT, ST_GATE_RUN, ST_POLE_INIT, ST_POLE_RUN, ST_FLAG_INIT, ST_FLAG_RUN, ST_START_INIT, ST_START_RUN, ST_KANBAN_INIT, ST_KANBAN_RUN,
       ST_T1_RUN, ST_T3_RUN, ST_T5_INIT, ST_T5_RUN, ST_T9_INIT, ST_T9_RUN, ST_T10_INIT, ST_T10_RUN };

enum { A_BIRD1_BODY21, A_BIRD1_BODY32, A_BIRD1_WINGS, A_BIRD2, A_ELEPHANT, A_ZEBRA_BODY, A_ZEBRA_TAIL };

/* type number -> initial state (table at 0x0601BFEC) */
static int type_state(int type)
{
    switch (type) {
    case 1: return ST_T1_INIT;
    case 2: return ST_DELETE;
    case 3: return ST_T3_INIT;
    case 4: return ST_CROWD_INIT;
    case 6: case 7: case 8: return ST_NOP;
    case 16: return ST_GATE_INIT;
    case 17: return ST_POLE_INIT;
    case 18: return ST_FLAG_INIT;
    case 19: return ST_START_INIT;
    case 20: return ST_KANBAN_INIT;
    case 5: return ST_T5_INIT;
    case 9: return ST_T9_INIT;
    case 10: return ST_T10_INIT;
    case 21: return ST_T21_INIT;
    case 22: return ST_DELETE;
    default: return ST_FREE;
    }
}

void ev_set_collision(Events *e, const Coli *c) { e->coli = c; }

static void heli_start(Events *e, int course, int game_type);

void ev_start(Events *e, int course, int game_type)
{
    heli_start(e, course, game_type);
    const unsigned char *f = e->file;
    const unsigned int *ce = (const unsigned int *)(f + 8 + 12 * (course & 3));
    e->nobjects = (int)ce[0];
    e->blocks = f + ce[1];
    e->recs = f + ce[2];
    memset(e->slots, 0, sizeof e->slots);
    e->flag[0] = e->flag[1] = e->flag[2] = 0;
    int n = 0;
    for (int b = 0; b < 128; b++) {
        const unsigned int *bt = (const unsigned int *)(e->blocks + 8 * b);
        e->block_first[b] = n;
        e->block_count[b] = (int)bt[1];
        for (unsigned int k = 0; k < bt[1] && n < EV_MAX_SLOTS; k++, n++) {
            const int *r = (const int *)(e->recs + 28 * (bt[0] + k));      /* type, w0, L1, L2, x, y, z */
            EvSlot *s = &e->slots[n];
            p32(s, 0, type_state(r[0]));
            p16(s, 0x0C, (n + 1) & 1);
            p32(s, 0x10, r[2]);
            p32(s, 0x24, r[3]);
            p32(s, 0x14, r[4]);
            p32(s, 0x18, r[5]);
            p32(s, 0x1C, r[6]);
        }
    }
}

/* ---- drawing helpers ------------------------------------------------------------------------------------------------------------ */
typedef struct {
    Events *e;
    EvWorld *w;
    SceneItem *out;
    int n, max;
    const Mat *view;
} Ctx;

static unsigned int ev_rand(Events *e)                 /* 0x0601D6CC */
{
    e->rng = e->rng * 5u + 0x3537u;
    return e->rng;
}

static inline float fx(int v) { return (float)v * (1.0f / 65536.0f); }

static void emit(Ctx *c, int part, const Mat *m, int clip)
{
    if (part < 0 || c->n >= c->max) return;
    c->out[c->n].c = c->e->objects;
    c->out[c->n].part = part;
    c->out[c->n].clip = clip;
    if (c->n < 512) c->e->slot_of[c->n] = (short)c->e->cur_slot;
    memcpy(c->out[c->n].m, m->m, sizeof c->out[c->n].m);
    c->n++;
}

/* draw_people_polygon: the quad is screen aligned at the view-space position of the pushed matrix; scale -1 mirrors it */
static void draw_people(Ctx *c, int part, const Mat *m, int scale)
{
    Mat b;
    memset(&b, 0, sizeof b);
    b.m[0] = (float)scale; b.m[5] = 1.0f; b.m[10] = 1.0f;
    b.m[3] = m->m[3]; b.m[7] = m->m[7]; b.m[11] = m->m[11];
    emit(c, part, &b, 0);
}

/* pushed matrix at the object's position (translate of FUN_06017DD0 with &slot[0x14]) */
static void at_object(const Ctx *c, const EvSlot *s, Mat *m)
{
    *m = *c->view;
    m_translate(m, fx(g32(s, 0x14)), fx(g32(s, 0x18)), fx(g32(s, 0x1C)));
}

/* ---- animals: keyframe animator and mover --------------------------------------------------------------------------------------- */
static inline int be32(const unsigned char *p) { return (int)(((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]); }
static inline int be16s(const unsigned char *p) { return (short)((p[0] << 8) | p[1]); }

/* FUN_0601CE8C: draw frame |counter| of a keyframe animation (every part: translate, rotate z, y, x, draw_polygon) relative to `base`, then advance the counter
 * (byte +0x0E) by the step (byte +0x0F).  Past the last frame: mode 0 restarts at 0, mode 1 turns around (counter = 1 - n), mode 2 holds the last frame.  Returns |counter|. */
static int anim(Ctx *c, EvSlot *s, const Mat *base, int table, int mode)
{
    const EvAnim *t = &c->e->anim[table];
    int cnt = g8(s, 0x0E);
    int a = cnt < 0 ? -cnt : cnt;
    const unsigned char *d = t->data + (size_t)a * (size_t)t->nparts * 20;
    for (int i = 0; i < t->nparts; i++, d += 20) {
        Mat m = *base;
        m_translate(&m, fx(be32(d)), fx(be32(d + 4)), fx(be32(d + 8)));
        m_rot_turns(&m, 2, be16s(d + 12));
        m_rot_turns(&m, 1, be16s(d + 14));
        m_rot_turns(&m, 0, be16s(d + 16));
        emit(c, t->parts[i], &m, 0);
    }
    cnt += g8(s, 0x0F);
    p8(s, 0x0E, cnt);
    a = cnt < 0 ? -cnt : cnt;
    if (a < t->n) return a;
    if (mode == 0) { p8(s, 0x0E, 0); return 0; }
    if (mode == 1) { p8(s, 0x0E, 1 - t->n); return 1 - t->n; }
    p8(s, 0x0E, t->n - 1);
    return t->n - 1;
}

/* FUN_0601CD40: advance the position by (0, +0x38, +0x3C) rotated about y by the heading (+0x24) */
static void move_obj(EvSlot *s)
{
    Mat r;
    memset(&r, 0, sizeof r);
    r.m[0] = r.m[5] = r.m[10] = 1.0f;
    m_rot_turns(&r, 1, g32(s, 0x24));
    float vy = fx(g32(s, 0x38)), vz = fx(g32(s, 0x3C));
    p32(s, 0x14, g32(s, 0x14) + (int)floorf((r.m[1] * vy + r.m[2] * vz) * 65536.0f + 0.5f));
    p32(s, 0x18, g32(s, 0x18) + (int)floorf((r.m[5] * vy + r.m[6] * vz) * 65536.0f + 0.5f));
    p32(s, 0x1C, g32(s, 0x1C) + (int)floorf((r.m[9] * vy + r.m[10] * vz) * 65536.0f + 0.5f));
}

/* FUN_0601CABE: a bird takes off when the car comes within 64 m (|dx| + |dz|) and flies off in the direction the car faces */
static void scare(Ctx *c, EvSlot *s)
{
    int dx = g32(s, 0x14) - c->w->car_x, dz = g32(s, 0x1C) - c->w->car_z;
    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;
    if ((long long)dx + dz > 0x400000LL) return;
    p16(s, 0x12, 8);
    int ang = 0x2000 - (int)(ev_rand(c->e) & 0x3FFF);
    ang += (int)(((long long)c->w->car_yaw * 0x28BE60DCLL) >> 32);
    p32(s, 0x24, ang);
}

static int c7d8_car_is_p1(Ctx *c) { (void)c; return 1; }

/* ---- the state functions ------------------------------------------------------------------------------------------------------- */
static void crowd_run(Ctx *c, EvSlot *s)               /* 0x0601C4F8 */
{
    Mat m;
    at_object(c, s, &m);
    if (m_visible(&m, 1.0f)) {
        int scale = 1;
        if ((gu16(s, 0x12) ^ c->w->side) & 1) scale = -1;
        draw_people(c, g32(s, 0x2C), &m, scale);
    }
}

static int people_part(Ctx *c, EvSlot *s)             /* 0x0601C5FC: part = table 0x0601C614[word +0x10] */
{
    int idx = gu16(s, 0x10);
    return idx < N_PEOPLE ? c->e->parts[P_KATA1 + idx] : -1;
}

/* the sound library calls the objects make (src/sndev.h: SND_PLAY / SND_WORD); the host forwards them after the dispatch */
static void ev_snd(Events *e, int kind, int val)
{
    if (e->snd.n < 4) { e->snd.kind[e->snd.n] = kind; e->snd.val[e->snd.n] = val; e->snd.n++; }
}

/* 0x06008C9C: one byte per pair of values of [0x060AA810]: the object sounds only play in the modes that have a 1 */
static int ev_sound_mode(const EvWorld *w)
{
    static const unsigned char tab[6] = { 1, 0, 0, 1, 1, 9 };
    int i = (w->mode810 & 0xFF) >> 1;
    return i < 6 ? tab[i] : 0;
}

/* the start object's push-back and sound (0x0601C3CE .. 0x0601C468): a box 6 m wide and 65 m long at the desert's start line keeps the car out (the push goes on the car's +0x64 / +0x6C),
 * and while it pushes the sound of a hit (id 2) plays every 15 passes */
static void start_push(Ctx *c, EvSlot *s)
{
    EvWorld *w = c->w;
    int r6 = (int)((unsigned)w->car_x + 0x01710000u), r7 = (int)((unsigned)w->car_z + 0x01280000u);
    int a6 = r6 > 0 ? r6 : -r6, a7 = r7 > 0 ? r7 : -r7;
    if (!(a6 > 0x30000)) return;
    if (!(0x208000 > a7)) return;
    r6 = -r6 + (r6 > 0 ? 0x30000 : -0x30000);
    r7 = -r7 + (r7 > 0 ? 0x210000 : -0x210000);
    a6 = r6 > 0 ? r6 : -r6; a7 = r7 > 0 ? r7 : -r7;
    if (a6 > a7) r6 = 0; else r7 = 0;
    w->push_x += r6;
    w->push_z += r7;
    if (!(15 > g32(s, 0x30))) {
        p32(s, 0x30, 0);
        if (w->car_state != 0 && ev_sound_mode(w)) ev_snd(c->e, 0, 2);
    }
}

static void exec_slot(Ctx *c, EvSlot *s)
{
    Events *e = c->e;
    EvWorld *w = c->w;
    Mat m;
    switch (g32(s, 0)) {
    case ST_FREE: case ST_NOP: break;
    case ST_DELETE: p32(s, 0, ST_FREE); break;
    case ST_CROWD_INIT:                                   /* 0x0601C4E4 */
        p32(s, 0x2C, people_part(c, s));
        p32(s, 0, ST_CROWD_RUN);
        crowd_run(c, s);
        break;
    case ST_CROWD_RUN: crowd_run(c, s); break;
    case ST_T21_INIT:                                     /* 0x0601C530: arcade only */
        if (w->game_type != 0) { p32(s, 0, ST_FREE); break; }
        p32(s, 0x2C, people_part(c, s));
        p32(s, 0, ST_T21_RUN);
        p16(s, 0x10, 0);
        if (!e->flag[2]) crowd_run(c, s);
        break;
    case ST_T21_RUN:                                      /* 0x0601C554 */
        p16(s, 0x10, 0);
        if (!e->flag[2]) crowd_run(c, s);
        break;
    case ST_GATE_INIT:                                    /* 0x0601C06C */
        e->flag[0] = 0; e->flag[1] = 0;
        p32(s, 0, ST_GATE_RUN);
        /* fall through */
    case ST_GATE_RUN: {                                   /* 0x0601C078 */
        static const struct { float sx, tx, ty; int routine; } tab[16] = {
            { 1.30999755859375f, 0.0099945068359375f, 0.0f, 2 }, { 0.9589996337890625f, -1.089996337890625f, 0.0f, 0 },
            { 0.99798583984375f, -0.0399932861328125f, 0.0f, 2 }, { 1.06298828125f, 0.1399993896484375f, 0.0f, 1 },
            { 1.037994384765625f, -0.7299957275390625f, 0.0f, 1 }, { 1.0609893798828125f, -0.019989013671875f, 0.0f, 0 },
            { 1.135986328125f, -0.079986572265625f, 0.0f, 2 }, { 1.3799896240234375f, 0.04998779296875f, 0.0f, 1 },
            { 0.732986450195f, 0.119995117188f, 0.0f, 0 }, { 0.724990844727f, 0.0f, -1.0f, 2 },
            { 1.0f, 0.0f, 0.0f, 0 }, { 1.0f, 0.0f, 0.0f, 0 }, { 1.0f, 0.0f, 0.0f, 0 }, { 1.0f, 0.0f, 0.0f, 0 }, { 1.0f, 0.0f, 0.0f, 0 }, { 1.0f, 0.0f, 0.0f, 0 } };
        at_object(c, s, &m);
        m_rot_turns(&m, 1, g32(s, 0x24));
        int ix = gu16(s, 0x12);
        if (ix > 15) break;
        m_translate(&m, tab[ix].tx, tab[ix].ty, 0.0f); m_scale(&m, tab[ix].sx, 1.0f, 1.0f);
        int part_idx = gu16(s, 0x10);
        static const int gate_a[4] = { P_CHECKPOINT_S, P_CHECKPOINT_L, P_FINISH_L, P_FINISH_L };
        static const int gate_b[2] = { P_FINISH_L, P_FINISH_L };
        int routine = tab[ix].routine;
        if (routine == 0) {                               /* 0x0601C0D0 */
            e->flag[0] = 1;
            if (c7d8_car_is_p1(c) && w->car_c8 == w->nseg_word - 1) e->flag[1] = 1;
            routine = 1;
        }
        if (routine == 1) {                               /* 0x0601C0F0 */
            if (m_visible(&m, 4.0f) && part_idx < 4) emit(c, e->parts[gate_a[part_idx]], &m, 1);
            e->flag[2] = 1;
        } else {                                          /* 0x0601C116 */
            if (m_visible(&m, 4.0f)) {
                if (e->flag[1]) { if (part_idx < 2) emit(c, e->parts[gate_b[part_idx]], &m, 1); }
                else if (e->flag[0] && part_idx < 4) emit(c, e->parts[gate_a[part_idx]], &m, 1);
            }
        }
        break;
    }
    case ST_POLE_INIT: {                                  /* 0x0601C2A0: poles */
        static const int pole[6] = { P_POLE_S, P_POLE_S, P_POLE_M, P_POLE_M, P_POLE_L, P_POLE_L };
        int idx = gu16(s, 0x10);
        p32(s, 0x2C, idx < 6 ? e->parts[pole[idx]] : -1);
        p32(s, 0, ST_POLE_RUN);
    }   /* fall through */
    case ST_POLE_RUN:                                     /* 0x0601C2AE */
        at_object(c, s, &m);
        m_rot_turns(&m, 1, g32(s, 0x24));
        if (m_visible(&m, 4.0f)) emit(c, g32(s, 0x2C), &m, 1);
        break;
    case ST_FLAG_INIT:                                    /* 0x0601C2F4 */
        if (w->game_type != 0) { p32(s, 0, ST_FREE); break; }
        p16(s, 0x30, (int)(ev_rand(e) & 0x7C));
        p16(s, 0x18, gu16(s, 0x18) + 1);
        p32(s, 0x2C, gu16(s, 0x10) == 0 ? 0 : 1);        /* which animation table: flag1 (0) / flag2 */
        p32(s, 0, ST_FLAG_RUN);
        /* fall through */
    case ST_FLAG_RUN:                                     /* 0x0601C326 */
        if (gu16(s, 0x12) == 0 && e->flag[2]) break;
        at_object(c, s, &m);
        m_rot_turns(&m, 1, g32(s, 0x24));
        if (m_visible(&m, 2.0f)) emit(c, e->flag_anim[g32(s, 0x2C) ? 1 : 0][gu16(s, 0x30) >> 2], &m, 0);
        p16(s, 0x30, (gu16(s, 0x30) + 4) & 0x7C);            /* the animation runs whenever the object is executed, drawn or not */
        break;
    case ST_KANBAN_INIT: {                                /* 0x0601C470 */
        int idx = gu16(s, 0x10) & 3;
        int part = idx == 2 ? e->parts[P_AM3KANBAN] : (idx == 3 ? e->parts[P_MEGAKANBAN] : -1);
        if (part < 0) { p32(s, 0, ST_FREE); break; }
        p32(s, 0x2C, part);
        p32(s, 0, ST_KANBAN_RUN);
    }   /* fall through */
    case ST_KANBAN_RUN: {                                 /* 0x0601C488 */
        at_object(c, s, &m);
        m_rot_turns(&m, 1, g32(s, 0x24));
        float sc = 1.0f + (float)g16(s, 0x12) * 0.1f * (1.0f);
        m_scale(&m, sc, sc, sc);
        if (m_visible(&m, 3.0f)) emit(c, g32(s, 0x2C), &m, 0);
        break;
    }
    case ST_START_INIT:                                   /* 0x0601C370: start object (arcade) */
        if (w->game_type != 0) { p32(s, 0, ST_FREE); break; }
        p16(s, 0x30, 0);
        e->flag[2] = 0;
        p32(s, 0x30, 0);
        p32(s, 0x1C, g32(s, 0x1C) + 0x80000);
        p32(s, 0, ST_START_RUN);
        /* fall through */
    case ST_START_RUN:                                    /* 0x0601C39A */
        if (!e->flag[2]) {
            at_object(c, s, &m);
            m_rot_turns(&m, 1, g32(s, 0x24));
            m_scale(&m, 0.89998626708984375f, 0.89998626708984375f, 1.0f);
            emit(c, e->parts[P_STARTOBJ], &m, 1);
            p32(s, 0x30, g32(s, 0x30) + 1);
            start_push(c, s);
        }
        break;
    case ST_T1_INIT:                                      /* 0x0601C814: zebra */
        p16(s, 0x10, 0);
        p16(s, 0x12, (int)(ev_rand(e) & 0xFF));
        p16(s, 0x30, (int)(ev_rand(e) & 0xFF));
        p8(s, 0x0E, 0); p8(s, 0x0F, 1);
        p32(s, 0, ST_T1_RUN);
        /* fall through */
    case ST_T1_RUN:                                       /* 0x0601C83C: body and tail are two animations sharing the counter word, which is swapped in and out */
        at_object(c, s, &m);
        if (!m_visible(&m, 2.0f)) break;
        m_rot_turns(&m, 1, g32(s, 0x24));
        if (gu16(s, 0x12) != 0) {
            p16(s, 0x12, gu16(s, 0x12) - 1);
            p8(s, 0x0F, 0);
            anim(c, s, &m, A_ZEBRA_BODY, 1);
        } else {
            p8(s, 0x0F, 1);
            int r = anim(c, s, &m, A_ZEBRA_BODY, 1);
            if (r == 29 || r == 0) p16(s, 0x12, (int)(ev_rand(e) & 0x7F) + 0x40);
        }
        {
            int saved = gu16(s, 0x0E);
            p16(s, 0x0E, gu16(s, 0x10));
            if (gu16(s, 0x30) != 0) {
                p16(s, 0x30, gu16(s, 0x30) - 1);
                p8(s, 0x0F, 0);
                anim(c, s, &m, A_ZEBRA_TAIL, 1);
            } else {
                p8(s, 0x0F, 1);
                int r = anim(c, s, &m, A_ZEBRA_TAIL, 1);
                if (r == 0) p16(s, 0x30, (int)(ev_rand(e) & 0x7F));
            }
            p16(s, 0x10, gu16(s, 0x0E));
            p16(s, 0x0E, saved);
        }
        break;
    case ST_T3_INIT:                                      /* 0x0601C96C: bird on the ground */
        if (w->game_type == 6) { p32(s, 0, ST_FREE); break; }
        p16(s, 0x12, 0); p16(s, 0x30, 0);
        p32(s, 0x34, 0); p32(s, 0x38, 0); p32(s, 0x3C, 0);
        p8(s, 0x0E, 0); p8(s, 0x0F, 1);
        p32(s, 0x24, (int)(ev_rand(e) & 0xFFFF));
        p32(s, 0, ST_T3_RUN);
        /* fall through */
    case ST_T3_RUN:                                       /* 0x0601C9A2 */
        if (w->car_ca != 0) break;
        at_object(c, s, &m);
        if (m_visible(&m, 1.0f)) {
            m_rot_turns(&m, 1, g32(s, 0x24) + 0x8000);
            switch (gu16(s, 0x12)) {
            case 0: {                                     /* 0x0601C9E4: hop */
                p32(s, 0x3C, 0);
                int r = anim(c, s, &m, A_BIRD1_BODY21, 0);
                if (r != 0) {
                    p32(s, 0x3C, 0x3851);
                    if ((ev_rand(e) & 3) == 0) p16(s, 0x12, gu16(s, 0x12) + 4);
                    else p32(s, 0x24, g32(s, 0x24) + 0x1C71);
                }
                scare(c, s);
                break;
            }
            case 4: {                                     /* 0x0601CA58: peck */
                p32(s, 0x3C, 0);
                int r = anim(c, s, &m, A_BIRD1_BODY32, 0);
                if (r == 20 && (ev_rand(e) & 3) == 0) p16(s, 0x12, gu16(s, 0x12) - 4);
                scare(c, s);
                break;
            }
            case 8:                                       /* 0x0601CA84: flying away */
                if (g16(s, 0x30) <= 0x5A) {
                    anim(c, s, &m, A_BIRD1_WINGS, 0);
                    if (0x4444 > g32(s, 0x38)) p32(s, 0x38, g32(s, 0x38) + 0x222);
                    if (0x11111 > g32(s, 0x3C)) p32(s, 0x3C, g32(s, 0x3C) + 0xBF2);
                    p16(s, 0x30, g16(s, 0x30) + 1);
                }
                break;
            default: break;
            }
        }
        move_obj(s);
        break;
    case ST_T5_INIT:                                      /* 0x0601CC2C: elephant, 1800 frames */
        p8(s, 0x0E, 0); p8(s, 0x0F, 1);
        p32(s, 0x34, 0); p32(s, 0x38, 0); p32(s, 0x3C, 0x5A0);
        p16(s, 0x10, 0x708);
        p32(s, 0, ST_T5_RUN);
        /* fall through */
    case ST_T5_RUN:                                       /* 0x0601CC46 */
        at_object(c, s, &m);
        if (m_visible(&m, 2.0f)) {
            m_rot_turns(&m, 1, g32(s, 0x24));
            anim(c, s, &m, A_ELEPHANT, 0);
        }
        move_obj(s);
        p16(s, 0x10, gu16(s, 0x10) - 1);
        if (g16(s, 0x10) <= 0) p32(s, 0, ST_FREE);
        break;
    case ST_T9_INIT:                                      /* 0x0601CB14: bird in flight */
        if (w->game_type == 6) { p32(s, 0, ST_FREE); break; }
        p32(s, 0x34, 0); p32(s, 0x38, 0);
        p32(s, 0x3C, 0x8000 + (int)(ev_rand(e) & 0x1FF));
        p8(s, 0x0E, (int)(ev_rand(e) & 0x1F));
        p8(s, 0x0F, 1);
        p16(s, 0x10, 0);
        p32(s, 0, ST_T9_RUN);
        /* fall through */
    case ST_T9_RUN: {                                     /* 0x0601CB52 */
        static const unsigned char enable[16] = { 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
        int ix = gu16(s, 0x12);
        if (ix >= 16 || !enable[ix]) { p32(s, 0, ST_FREE); break; }
        int life = g16(s, 0x10) + 1;
        if (life > 0x258) { p32(s, 0, ST_FREE); break; }
        p16(s, 0x10, life);
        if (w->car_ca != 0) break;
        at_object(c, s, &m);
        if (m_visible(&m, 1.0f)) {
            m_rot_turns(&m, 1, g32(s, 0x24));
            m_scale(&m, 0.29998779296875f, 0.29998779296875f, 0.29998779296875f);
            anim(c, s, &m, A_BIRD2, 0);
        }
        move_obj(s);
        break;
    }
    case ST_T10_INIT:                                     /* 0x0601CCAC: swan */
        p32(s, 0x34, 0); p32(s, 0x38, 0); p32(s, 0x3C, 0x99);
        p32(s, 0x2C, (int)(ev_rand(e) & 0x7FFF));
        p32(s, 0, ST_T10_RUN);
        /* fall through */
    case ST_T10_RUN: {                                    /* 0x0601CCCC: bobs on the water, drifts along its heading */
        int phase = g32(s, 0x2C);
        float sn = (float)(short)0 + sinf((float)((phase >> 4) & 0xFFF) * (6.2831853071795864f / 4096.0f));
        m = *c->view;
        m_translate(&m, fx(g32(s, 0x14)), fx(g32(s, 0x18)) + sn * 0.1f, fx(g32(s, 0x1C)));
        if (m_visible(&m, 1.0f)) draw_people(c, e->parts[P_SWAN], &m, -1);
        move_obj(s);
        p32(s, 0x2C, g32(s, 0x2C) + 91);
        break;
    }
    default: break;
    }
}

/* ---- the helicopter (desert only; FUN_0601DC14 .. 0x0601E2F0): a 512-byte object at 0x060EFE00 called by the master CPU after the slots, plus 32 rotor-dust particles ---- */
enum { HS_OFF = 0, HS_WAIT, HS_FLY };

static inline int bg32(const unsigned char *m, int o) { return (int)(((unsigned)m[o] << 24) | ((unsigned)m[o + 1] << 16) | ((unsigned)m[o + 2] << 8) | m[o + 3]); }
static inline void bp32(unsigned char *m, int o, int v) { m[o] = (unsigned char)((unsigned)v >> 24); m[o + 1] = (unsigned char)((unsigned)v >> 16); m[o + 2] = (unsigned char)((unsigned)v >> 8); m[o + 3] = (unsigned char)v; }
#define H(o) bg32(h->m, o)
#define HS(o, v) bp32(h->m, o, (v))

/* flight tables 0x0601E2F4 (game type 0) / 0x0601E308 (game type 2): start slice, length in path nodes, speed, path offset in the motion block (helidata63 / 64) */
static const struct { int slice, nodes, speed, path; } heli_tab[2] = { { 0xD6, 0x3A, 0x2600, 0xD390 }, { 0xD7, 0x3A, 0x2C08, 0xD750 } };

static void heli_start(Events *e, int course, int game_type)
{
    EvHeli *h = &e->heli;
    memset(h, 0, sizeof *h);
    if (course != 0) return;                                  /* [0x060AA87E] != 0: no helicopter */
    if (game_type == 0) { h->table = 0; HS(0, HS_WAIT); }
    else if (game_type == 2) { h->table = 1; HS(0, HS_WAIT); }
}

/* restore the helicopter as the original's memory holds it (frame files: tools/export_frames.py "heli" line); state functions 0x0601DC6C / 0x0601DCE8 -> HS_WAIT / HS_FLY */
void ev_set_heli(Events *e, const unsigned char *m512, const unsigned char *dust512)
{
    memcpy(e->heli.m, m512, sizeof e->heli.m);
    memcpy(e->heli.dust, dust512, sizeof e->heli.dust);
    unsigned int st = (unsigned int)bg32(e->heli.m, 0);
    bp32(e->heli.m, 0, st == 0x0601DCE8u ? HS_FLY : st == 0x0601DC6Cu ? HS_WAIT : HS_OFF);
}

/* draw_splash_polygon: x as people, y scaled by r6 / 65536 */
static void draw_splash(Ctx *c, int part, const Mat *m, int r6)
{
    Mat b;
    memset(&b, 0, sizeof b);
    b.m[0] = 1.0f; b.m[5] = (float)r6 * (1.0f / 65536.0f); b.m[10] = 1.0f;
    b.m[3] = m->m[3]; b.m[7] = m->m[7]; b.m[11] = m->m[11];
    emit(c, part, &b, 0);
}

/* r_rotate_*: angle in 16.16 radians */
static void rrot(Mat *m, int axis, int rad16) { m_rot_turns(m, axis, g_rad_to_turns(rad16)); }

/* 0x0601DFCC: point on the flight path (cubic Lagrange through four nodes {x, y, z, heading} of 16 bytes, parameter = fraction of +52), heading, bank and pitch */
static void heli_spline(Ctx *c)
{
    Events *e = c->e;
    EvHeli *h = &e->heli;
    const unsigned char *base = e->motion + heli_tab[h->table].path;
    int pos = H(52);
    int n = (short)(pos >> 16);
    int i1 = n * 16, i0 = i1 - 16 < 0 ? 0 : i1 - 16;
    const unsigned char *node[4] = { base + i0, base + i1, base + i1 + 16, base + i1 + 32 };
    for (int k = 0; k < 4; k++) HS(96 + 4 * k, bg32(node[k], 12) - 0x1921F);
    int t = pos & 0xFFFF, one = 0x10000;
    int a = t - one, b = a - one, d = t + one;
    int p = g_mul(a, b);
    int w[4];
    w[0] = (int)(((long long)g_mul(-t, p) * 0x2AAAAAAALL) >> 32);
    w[1] = g_mul(d, p) >> 1;
    int q = g_mul(d, t);
    w[2] = g_mul(-b, q) >> 1;
    w[3] = (int)(((long long)g_mul(a, q) * 0x2AAAAAAALL) >> 32);
    int x, y, z;
    long long s;
    s = 0; for (int k = 0; k < 4; k++) s += (long long)w[k] * bg32(node[k], 0); x = (int)(s >> 16);
    s = 0; for (int k = 0; k < 4; k++) s += (long long)w[k] * bg32(node[k], 8); z = (int)(s >> 16);
    int vx = x - H(24), vz = z - H(32);
    HS(36, vx); HS(40, 0); HS(44, vz);
    /* velocity in the frame of the heading (matrix 0x060D1030: identity, rotate y by -heading, rotation only) */
    Mat r;
    memset(&r, 0, sizeof r);
    r.m[0] = r.m[5] = r.m[10] = 1.0f;
    rrot(&r, 1, -H(16));
    int lx = (int)floor((double)r.m[0] * vx + (double)r.m[2] * vz);
    int lz = (int)floor((double)r.m[8] * vx + (double)r.m[10] * vz);
    s = 0; for (int k = 0; k < 4; k++) s += (long long)w[k] * bg32(node[k], 4); y = (int)(s >> 16);
    HS(28, 0x28000 + y);
    s = 0; for (int k = 0; k < 4; k++) s += (long long)w[k] * bg32(h->m, 96 + 4 * k);
    HS(16, (int)(s >> 16));
    int qd = (int)(0x2CAE0000LL / H(72));
    HS(68, g_mul(-lz, qd));
    HS(64, g_mul(lx, qd));
}

/* 0x0601E146: a dust particle where the downwash hits sand / dirt below 15 m */
static void heli_dust_spawn(Ctx *c)
{
    static const unsigned char dusty[10] = { 0, 0, 0, 1, 1, 0, 0, 1, 1, 0 };           /* 0x0601E1B0, by surface type */
    EvHeli *h = &c->e->heli;
    int type = H(124), alt = H(128);
    if (type < 0 || type > 9 || !dusty[type] || alt > 0xF0000) return;
    for (int i = 0; i < 32; i++) {
        unsigned char *p = h->dust + 16 * i;
        if (bg32(p, 0)) continue;
        bp32(p, 0, (int)(((long long)(0xF0000 - alt) * 0x1851EB85LL) >> 32));
        bp32(p, 4, (int)(ev_rand(c->e) & 0x3FFFF) - 0x20000 + H(24));
        bp32(p, 8, H(28) - alt);
        bp32(p, 12, (int)(ev_rand(c->e) & 0x3FFFF) - 0x20000 + H(32));
        return;
    }
}

/* 0x0601E1BC: draw and age the particles (splash sprite whose height scale is 2 - |size|); size falls by 0.12 per frame and the particle dies at -1.5 */
static void heli_dust_run(Ctx *c)
{
    EvHeli *h = &c->e->heli;
    for (int i = 0; i < 32; i++) {
        unsigned char *p = h->dust + 16 * i;
        int size = bg32(p, 0);
        if (!size) continue;
        Mat m = *c->view;
        m_translate(&m, fx(bg32(p, 4)), fx(bg32(p, 8)), fx(bg32(p, 12)));
        if (m_visible(&m, 1.0f)) draw_splash(c, c->e->parts[P_SAND], &m, 0x20000 - (size < 0 ? -size : size));
        size -= 0x1EB8;
        if (size <= -0x18000) size = 0;
        bp32(p, 0, size);
    }
}

/* 0x0601E228 (called at the end of every pass of the flight): the helicopter's position in the camera's frame (the matrix translated by it: x +0x8C, z +0x94 of the object) sets the
 * pan of its sound: the angle of (-x, z) in 1 / 32 of a half turn (0 .. 30) when it is in front of the camera, else the far left / right; the word 0x02pp goes to the 68000 */
static void heli_sound(Ctx *c)
{
    EvHeli *h = &c->e->heli;
    const int *v = c->w->view_fx;
    int x = H(24), y = H(28), z = H(32), word;
    int xv = (int)((long long)((long long)v[0] * x + (long long)v[1] * y + (long long)v[2] * z) >> 16) + v[3];
    int zv = (int)((long long)((long long)v[8] * x + (long long)v[9] * y + (long long)v[10] * z) >> 16) + v[11];
    if (zv > 0) {
        int pan = (g_atan(-xv, zv) & 0x7FFF) >> 10;
        word = 0x200 | (pan > 30 ? 30 : pan);
    } else {
        word = xv >= 0 ? 0x21E : 0x200;
    }
    ev_snd(c->e, 1, word);
}

static void heli_fly(Ctx *c)
{
    Events *e = c->e;
    EvHeli *h = &e->heli;
    const EvWorld *w = c->w;
    long long dx = w->car_x - H(24), dz = w->car_z - H(32);
    HS(120, g_sqrt((int)((dx * dx + dz * dz) >> 16)));
    if (H(52) >= (heli_tab[h->table].nodes << 16)) { HS(0, HS_OFF); return; }
    heli_spline(c);
    HS(24, H(24) + H(36));
    HS(32, H(32) + H(44));
    HS(12, H(68));
    HS(20, H(64));
    HS(56, H(56) + 0x14000);
    HS(60, H(60) + (int)0x0601DFC8);
    {   /* path speed falls off with the distance to the car */
        int m = (int)(((long long)H(120) * 0x2ECFB9LL) >> 32);
        int sp = heli_tab[h->table].speed;
        HS(52, H(52) + sp - g_mul(m, sp));
    }
    int alt = 0, nrm[3] = { 0, 0, 0 };
    int type = e->coli ? coli_ground(e->coli, H(24), H(28), H(32), &alt, nrm) : 0;
    HS(124, type);
    HS(128, alt);
    if (type != 0 && alt <= 0xF0000) {                       /* shadow on the ground, tilted to the slope, grows with height */
        int a1 = g_atan(nrm[0], nrm[1]) + 0x4000, a2 = g_atan(nrm[2], nrm[1]) + 0x4000;
        Mat s = *c->view;
        m_translate(&s, fx(H(24)), fx(H(28) + 0x7AE - alt), fx(H(32)));
        if (m_visible(&s, 4.0f)) {
            rrot(&s, 1, -H(16));
            m_rot_turns(&s, 2, a1);
            m_rot_turns(&s, 0, a2);
            float sc = fx(0xCCCC + (int)(((long long)alt * 0x028F5C28LL) >> 32));
            m_scale(&s, sc, 1.0f, sc);
            emit(c, e->parts[P_HELI_KAGE], &s, 0);
            rrot(&s, 1, H(56));
            emit(c, e->parts[P_HELI2], &s, 0);
        }
    }
    Mat b = *c->view;
    m_translate(&b, fx(H(24)), fx(H(28)), fx(H(32)));
    if (m_visible(&b, 4.0f)) {
        rrot(&b, 1, H(16));
        rrot(&b, 2, H(20));
        rrot(&b, 0, H(12));
        Mat body = b;
        m_translate(&body, 0.0f, -2.5f, 0.0f);
        emit(c, e->parts[P_HELI1], &body, 0);
        Mat rotor = b;
        rrot(&rotor, 1, H(56));
        emit(c, e->parts[P_HELI2], &rotor, 0);
        m_translate(&b, 0.0f, fx((int)0xFFFED99A), -6.0f);
        rrot(&b, 0, H(60));
        emit(c, e->parts[P_HELI3], &b, 0);
    }
    heli_dust_spawn(c);
    heli_dust_spawn(c);
    heli_dust_run(c);
    heli_sound(c);
}

#define HELI_SLOT 760                                        /* (0x060EFE00 - 0x060E4000) / 0x40: the number the vector recorder gives the helicopter's draws */

static void heli_run(Ctx *c)
{
    EvHeli *h = &c->e->heli;
    c->e->cur_slot = HELI_SLOT;
    if (H(0) == HS_WAIT) {                                   /* 0x0601DC6C: last lap and the car on the start slice */
        const EvWorld *w = c->w;
        if ((short)w->car_ca != (short)(w->nseg_word - 1) || (short)w->car_seg != heli_tab[h->table].slice) return;
        const unsigned char *path = c->e->motion + heli_tab[h->table].path;
        HS(8, 1);
        HS(52, 0); HS(56, 0); HS(60, 0); HS(64, 0); HS(68, 0);
        HS(72, 0xA0000);
        HS(76, 0x79451);
        HS(36, 0); HS(40, 0); HS(44, 0);
        HS(24, bg32(path, 0)); HS(28, bg32(path, 4)); HS(32, bg32(path, 8));
        HS(0, HS_FLY);
        if (ev_sound_mode(c->w)) ev_snd(c->e, 0, 29);        /* 0x0601DCD0: the flyover sound */
    }
    if (H(0) == HS_FLY) heli_fly(c);
}

#undef H
#undef HS

/* one half of the dispatcher (0x0601BCB8 master: even slots; 0x0601BD52 slave: odd slots), visiting the visible blocks in list order */
static void run_half(Ctx *c, const unsigned char *blocks, int nblocks, int flag_value)
{
    Events *e = c->e;
    for (int bi = 0; bi < nblocks; bi++) {
        int b = blocks[bi] & 127;
        int first = e->block_first[b], count = e->block_count[b];
        for (int k = 0; k < count; k++) {
            EvSlot *s = &e->slots[first + k];
            if (gu16(s, 0x0C) == flag_value) { e->cur_slot = (int)(s - e->slots); exec_slot(c, s); }
        }
    }
}

int ev_frame(Events *e, EvWorld *w, const unsigned char *blocks, int nblocks, SceneItem *out, int max)
{
    Ctx c;
    Mat view;
    memcpy(view.m, w->view, sizeof view.m);
    w->side = (w->car_byte_c5 == 0) ? 1 : 0;                 /* FUN_0601BD3E -> [0x060D0114] */
    e->snd.n = 0; w->push_x = 0; w->push_z = 0;
    c.e = e; c.w = w; c.out = out; c.n = 0; c.max = max; c.view = &view;
    run_half(&c, blocks, nblocks, 1);                        /* slave half first (the oracle runs the job synchronously) */
    run_half(&c, blocks, nblocks, 0);
    heli_run(&c);                                            /* the master finally calls the special object at 0x060EFE00 */
    return c.n;
}

/* ---- HUD sprites through the object pipeline ------------------------------------------------------------------------------------ */
int ev_hud(Events *e, int rpm, int cd_idx, int cd_scale, SceneItem *out, int max)
{
    Ctx c;
    c.e = e; c.w = NULL; c.out = out; c.n = 0; c.max = max; c.view = NULL;
    e->cur_slot = 0;
    if (rpm >= 0) {                                         /* dispTachoMeter: needle at 0.5 m, turned by the engine speed, 1/16 size, screen offset (-124, +72) */
        int v = rpm < 0x03200000 ? 0x03200000 : rpm;        /* at least 800 */
        v = g_mul(v, 0xE8BA);
        int turns = v < 0x0FA00000 ? (int)(((long long)v * 0x20C49) >> 32) : (int)(((long long)(v - 0x0FA00000) * 0x83127) >> 32) + 0x2000;
        Mat m;
        memset(&m, 0, sizeof m);
        m.m[0] = m.m[5] = m.m[10] = 1.0f;
        m.m[11] = 0.5f;
        m_rot_turns(&m, 2, turns);
        m_scale(&m, 1.0f / 16.0f, 1.0f / 16.0f, 1.0f / 16.0f);
        m.m[3] += -124.0f * 0.5f / 224.0f;                  /* the offset is added to the projected screen position: folded into the translation (the needle is flat at z = 0.5) */
        m.m[7] += -72.0f * 0.5f / 224.0f;
        emit(&c, e->parts[P_TACHO], &m, 0);
    }
    if (cd_idx >= 0 && cd_idx < 4 && cd_scale != 0) {       /* dispCountDown: table 0x060107B4 {factor, offset} per numeral */
        static const int tab[4][2] = { { 0x3000, 0x1000 }, { 0x2000, 0x2400 }, { 0x2000, 0x2400 }, { 0x2000, 0x2400 } };
        float s = fx(g_mul(tab[cd_idx][0], cd_scale) + tab[cd_idx][1]);
        Mat m;
        memset(&m, 0, sizeof m);
        m.m[0] = m.m[5] = m.m[10] = s;
        m.m[11] = 0.5f;
        emit(&c, e->parts[P_GO + cd_idx], &m, 0);
    }
    return c.n;
}
