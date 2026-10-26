/* Host test of src/drive.c, src/camera.c and src/pmap.c against the oracle (tools/oracle.py run --slots [--camera | --render]): replays an input file (.inp) from the start
 * of the race and compares the car slot with the oracle's after every pass; with -cam also the camera (work area, view matrix, mode) and with -map the part list.
 *   gcc -O1 -fwrapv -I src tools/test_drive.c src/drive.c src/camera.c src/pmap.c src/coli.c -lm -o build/host/test_drive
 *   build/host/test_drive extracted/psp/course extracted/oracle/desert_drive [-mask lo-hi ...] [-v] [-cam] [-map] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "drive.h"
#include "camera.h"
#include "pmap.h"
#include "hudgen.h"
#include "opp.h"
#include "fx.h"
#include "hude_cmp.h"

typedef struct { int lo, hi; } Range;

/* the sound events (tools/oracle.py run --snd): every call of the sound library is logged with its entry address and arguments, the 68000's status byte is a function of the pass and the class */
typedef struct { unsigned fn, a[3]; } SndLog;
static SndLog snd_log[256];
static int snd_nlog;
static const Phys *snd_phys;
static void snd_cb(void *ud, unsigned fn, int r4, int r5, int r6)
{
    (void)ud;
    if (snd_nlog < 256) { snd_log[snd_nlog].fn = fn; snd_log[snd_nlog].a[0] = (unsigned)r4; snd_log[snd_nlog].a[1] = (unsigned)r5; snd_log[snd_nlog].a[2] = (unsigned)r6; snd_nlog++; }
}
static unsigned snd_pass;
static int snd_st(void *ud, int id)                                 /* saturn_env.snd_status */
{
    (void)ud;
    int b0 = (signed char)(phys_r32(snd_phys, 0x060A1330u + 4u * (unsigned)id) >> 24);
    static const int st[4] = { 0, 1, 0, 0x20 };
    return st[((snd_pass * 7u + ((unsigned)b0 & 255u) * 13u + 3u) >> 3) & 3u];
}
static int snd_argmask(unsigned fn, int k, unsigned *m)             /* which arguments of the entry matter and how many bits */
{
    switch (fn) {
    case SND_PLAY: *m = 0xFFFFFFFFu; return k < 1;
    case SND_WORD: *m = 0xFFFFu; return k < 1;
    case SND_CMD: case SND_F24: case SND_F30: case SND_FA4: *m = 0xFFu; return k < 1;
    case SND_F50: case SND_F60: case SND_CDPLAY: *m = 0xFFu; return k < 2;
    case SND_F3C: *m = 0xFFu; return k < 3;
    default: return 0;
    }
}

#define CAM_REC (0xB4 + 0x30 + 0x30 + 2 + 18)          /* tools/oracle.py cam_record() */

static unsigned char *slurp(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = (unsigned char *)malloc((size_t)*n + 1);
    if (fread(b, 1, (size_t)*n, f) != (size_t)*n) { fclose(f); free(b); return NULL; }
    fclose(f);
    return b;
}

static unsigned rd32(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24; }

static unsigned be16(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }
static unsigned be32(const unsigned char *p) { return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3]; }

/* oracle.py opp_perturb: a synthetic car-car contact - the first two other cars side by side on the guide line just ahead of the player, at LOD 2 */
static void do_perturb(Opp *o, Drive *d, const Phys *ph)
{
    int px = g32(d->car, 0x18), pz = g32(d->car, 0x20), bi = 0;
    long long best = -1;
    for (int k = 0; k < 300; k++) {
        long long dx = (long long)phys_r32(ph, o->guide + 16u * (unsigned)k) - px, dz = (long long)phys_r32(ph, o->guide + 16u * (unsigned)k + 8) - pz;
        long long dd = dx * dx + dz * dz;
        if (best < 0 || dd < best) { best = dd; bi = k; }
    }
    int idx = bi + 1;
    unsigned g = o->guide + 16u * (unsigned)idx;
    int gx = phys_r32(ph, g), gy = phys_r32(ph, g + 4), gz = phys_r32(ph, g + 8);
    for (int k = 0; k < 2; k++) {
        unsigned char *c = o->car[k];
        p32(c, 0x18, gx + (k ? 0x8000 : 0)); p32(c, 0x1C, gy + 0x4CCC); p32(c, 0x20, gz + (k ? 0x5000 : 0));
        p16(c, 0x10A, idx); p16(c, 0x102, 2); p16(c, 0x104, 2);
        p32(c, 0x24, 0); p32(c, 0x28, 0); p32(c, 0x2C, 0); p32(c, 0x128, 0);
        p8(c, 0xF0, (d->car[0xF0] + 1) & 0x7F);
    }
}

static const Camera *part_cam;                                                          /* a replay's cut-scene camera makes the part list from the road block at its eye */
static int part_list(const Drive *d, const unsigned char *vis, const unsigned short *npolys, unsigned char out[16])        /* get_poly_map's block list of the car's position, cut like the goal presentation does */
{
    int cnt = part_cam && part_cam->pm_on ? pmap_build(vis, npolys, part_cam->pm[0], part_cam->pm[1], part_cam->pm[2], PMAP_BUDGET, out) : pmap_build(vis, npolys, d->car[0xF0], d->car[0xF1], d->car[0xC7], PMAP_BUDGET, out);
    if (cnt > drive_part_limit(d)) cnt = drive_part_limit(d);
    return cnt;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: test_drive course_dir oracle_prefix [-mask lo-hi]... [-v] [-cam] [-map]\n"); return 2; }
    Range mask[32];
    int nmask = 0, verbose = 0, use_cam = 0, use_map = 0, use_hud = 0, use_sky = 0, use_hude = 0, use_snd = 0;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "-cam")) use_cam = 1;
        else if (!strcmp(argv[i], "-map")) use_cam = use_map = 1;
        else if (!strcmp(argv[i], "-hud")) use_hud = 1;
        else if (!strcmp(argv[i], "-hude")) use_hude = 1;
        else if (!strcmp(argv[i], "-snd")) use_snd = 1;
        else if (!strcmp(argv[i], "-sky")) use_cam = use_sky = 1;
        else if (!strcmp(argv[i], "-mask") && i + 1 < argc && nmask < 32) {
            int lo, hi;
            if (sscanf(argv[++i], "%x-%x", &lo, &hi) == 2) { mask[nmask].lo = lo; mask[nmask++].hi = hi; }
        }
    }
    char path[512];
    snprintf(path, sizeof path, "%s.inp", argv[2]);
    FILE *fi = fopen(path, "r");
    if (!fi) { fprintf(stderr, "cannot open %s\n", path); return 2; }
    DriveCfg cfg;
    memset(&cfg, 0, sizeof cfg);
    char line[256];
    if (!fgets(line, sizeof line, fi)) return 2;
    int course = 0, type = 2, mt = 0, car = 0, diff = 0, mirror = 0;
    sscanf(line, "# course=%d type=%d mt=%d car=%d diff=%d", &course, &type, &mt, &car, &diff);
    cfg.course = course; cfg.game_type = type; cfg.manual = mt; cfg.car_type = car; cfg.difficulty = diff; cfg.laps = 3;
    const char *sp = strstr(line, "setup=");
    if (sp) {
        int v[4];
        if (sscanf(sp + 6, "%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3]) == 4) { cfg.setup_mode = 1; for (int k = 0; k < 4; k++) cfg.setup[k] = v[k]; }
    }
    sp = strstr(line, "steer=");                                                          /* the steering tuning byte the menus leave behind (oracle.py run --steer N) */
    if (sp) { int st = 0; sscanf(sp + 6, "%d", &st); if (st) cfg.setup[0] = st; }
    sp = strstr(line, "mirror=");
    if (sp) sscanf(sp + 7, "%d", &mirror);
    cfg.mirror = mirror;
    static unsigned char cmp_rec[0x400];                                                  /* <name>.cmp (oracle.py run --cmp N --ref FILE): the compare option and the record buffers the race start found */
    {
        long cn;
        char cp[512];
        snprintf(cp, sizeof cp, "%s.cmp", argv[2]);
        unsigned char *cf = slurp(cp, &cn);
        if (cf && cn == 4 + 0x400) { cfg.compare = (int)rd32(cf); memcpy(cmp_rec, cf + 4, 0x400); cfg.init_rec = cmp_rec; }
    }
    static unsigned char recbuf[0x8000];                                                  /* the input words the player's car records (the oracle's scratch buffer 0x060F0000, <name>.irec: oracle.py run --irec) */
    cfg.rec_win.p = recbuf; cfg.rec_win.base = 0x060F0000u; cfg.rec_win.size = sizeof recbuf;
    static unsigned char rplbuf[0xF800];                                                  /* <name>.rplin (oracle.py run --replay FILE): a replay of recorded input words, play mode 2, the words at 0x002CF800 */
    sp = strstr(line, "replay=");
    if (sp && atoi(sp + 7)) {
        long rn;
        char rp[512];
        snprintf(rp, sizeof rp, "%s.rplin", argv[2]);
        unsigned char *rf = slurp(rp, &rn);
        if (!rf) { fprintf(stderr, "cannot open %s\n", rp); return 2; }
        memcpy(rplbuf, rf, rn < (long)sizeof rplbuf ? (size_t)rn : sizeof rplbuf);
        cfg.replay = 1;
        { const char *ep = strstr(line, "ending="); cfg.ending = ep && atoi(ep + 7); }
        sp = strstr(line, "rtimes=");
        static unsigned char initr[0x100];
        int rt[4], nrt = sp ? sscanf(sp + 7, "%x,%x,%x,%x", &rt[0], &rt[1], &rt[2], &rt[3]) : 0;
        for (int k = 0; k < nrt; k++) { initr[0x8C + 4 * k] = (unsigned char)(rt[k] >> 24); initr[0x8D + 4 * k] = (unsigned char)(rt[k] >> 16); initr[0x8E + 4 * k] = (unsigned char)(rt[k] >> 8); initr[0x8F + 4 * k] = (unsigned char)rt[k]; }
        if (nrt) cfg.init_r = initr;
        cfg.rec_win.p = rplbuf; cfg.rec_win.base = 0x002CF800u; cfg.rec_win.size = sizeof rplbuf; cfg.rec_start = 0x002CF800u;
    }
    sp = strstr(line, "recs=");                                                           /* oracle.py run --recs a,b,c,best: a stored course record (the thresholds of the record box, R+0x24) and the R block the race start found (<name>.initr) */
    if (sp && sp[5] != '-') {
        static unsigned char initr_recs[0x100];
        unsigned rr[4];
        long rn;
        char rp[512];
        snprintf(rp, sizeof rp, "%s.initr", argv[2]);
        unsigned char *rf = slurp(rp, &rn);
        if (sscanf(sp + 5, "%x,%x,%x,%x", &rr[0], &rr[1], &rr[2], &rr[3]) == 4 && rf && rn == 0x100) {
            memcpy(initr_recs, rf, 0x100);
            cfg.rec184 = (int)rr[0]; cfg.rec188 = (int)rr[1]; cfg.rec18c = (int)rr[2];
            cfg.init_r = initr_recs;
        } else { fprintf(stderr, "recs: no <name>.initr\n"); return 2; }
    }
    unsigned char *ghostf = NULL, *slot1 = NULL;                                          /* <name>.ghost / <name>.slot1 (oracle.py run --ghost FILE --slots): the stored run slot 1 races and slot 1 after every pass */
    static unsigned char ghostbuf[0x3C98];
    {
        long gn, sn;
        char gp[512];
        snprintf(gp, sizeof gp, "%s.ghost", argv[2]);
        ghostf = slurp(gp, &gn);
        snprintf(gp, sizeof gp, "%s.slot1", argv[2]);
        slot1 = slurp(gp, &sn);
        if (ghostf && slot1) {
            memcpy(ghostbuf, ghostf, gn < (long)sizeof ghostbuf ? (size_t)gn : sizeof ghostbuf);
            cfg.ghost = 1;
            cfg.ghost_win.p = ghostbuf; cfg.ghost_win.size = sizeof ghostbuf;
            cfg.ghost_win.base = cfg.ghost_start = 0x002C0000u + 0x3D00u * (unsigned)course + 104u;
            cfg.ghost_car = car; cfg.ghost_manual = mt; cfg.ghost_mode = cfg.setup_mode;
            for (int k = 0; k < 4; k++) cfg.ghost_setup[k] = cfg.setup[k];
        } else { free(ghostf); free(slot1); ghostf = slot1 = NULL; }
    }
    int view0 = 0, use_opp = 0, opp_rank = -1, perturb[16], nperturb = 0;
    sp = strstr(line, "view=");
    if (sp) sscanf(sp + 5, "%d", &view0);
    sp = strstr(line, "opp=");
    if (sp) sscanf(sp + 4, "%d", &use_opp);
    sp = strstr(line, "rank=");
    if (sp) sscanf(sp + 5, "%d", &opp_rank);
    if (use_opp) use_cam = use_map = 1;
    sp = strstr(line, "perturb=");
    if (sp) {
        sp += 8;
        while (*sp >= '0' && *sp <= '9' && nperturb < 16) { perturb[nperturb++] = atoi(sp); while (*sp >= '0' && *sp <= '9') sp++; if (*sp == ',') sp++; }
    }
    static const char *const names[4] = { "desert", "forest", "mount", "lake" };
    Coli coli;
    snprintf(path, sizeof path, "%s/%s.rct", argv[1], names[course]);
    if (coli_load(&coli, path)) { fprintf(stderr, "cannot load %s\n", path); return 2; }
    Phys phys;
    snprintf(path, sizeof path, "%s/phys.dat", argv[1]);
    if (phys_load(&phys, path)) { fprintf(stderr, "cannot load %s\n", path); return 2; }
    long n;
    snprintf(path, sizeof path, "%s.slots", argv[2]);
    unsigned char *slots = slurp(path, &n);
    if (!slots) { fprintf(stderr, "cannot open %s\n", path); return 2; }
    unsigned npass = rd32(slots);
    unsigned sb = (unsigned)slots[4] | (unsigned)slots[5] << 8;
    if (sb != SLOT_BYTES) { fprintf(stderr, "slot size %u\n", sb); return 2; }

    unsigned char *cams = NULL;
    if (use_cam) {
        snprintf(path, sizeof path, "%s.cam", argv[2]);
        cams = slurp(path, &n);
        if (!cams) { fprintf(stderr, "cannot open %s\n", path); return 2; }
    }
    unsigned char *rcp = NULL;
    unsigned short npolys[128];
    const unsigned char *vis = NULL;
    if (use_map) {
        snprintf(path, sizeof path, "%s/%s.rcp", argv[1], names[course]);
        rcp = slurp(path, &n);
        if (!rcp || memcmp(rcp, "RCP1", 4) != 0) { fprintf(stderr, "cannot load %s\n", path); return 2; }
        unsigned part_off = rd32(rcp + 20);
        for (int i = 0; i < 128; i++) npolys[i] = (unsigned short)(rcp[part_off + 16 * i + 10] | rcp[part_off + 16 * i + 11] << 8);
        vis = rcp + rd32(rcp + 36);
    }
    static HudGen hud;
    unsigned char *hudf = NULL;
    unsigned short want_w[HUD_NT_WORDS + 4];
    const unsigned char *hudp = NULL;
    unsigned hud_bad = 0, hud_first = 0;
    if (use_hud) {                                                                       /* the name tables the oracle's game_display leaves after every pass (tools/oracle.py run --hud) */
        snprintf(path, sizeof path, "%s.hud", argv[2]);
        hudf = slurp(path, &n);
        if (!hudf) { fprintf(stderr, "cannot open %s\n", path); return 2; }
        snprintf(path, sizeof path, "%s/hud_init.dat", argv[1]);
        if (hudgen_load(&hud, path, type | (cfg.replay ? HUD_REPLAY : 0), course)) { fprintf(stderr, "cannot load %s\n", path); return 2; }
        if (type >= 4) { static Drive tmp; drive_init(&tmp, &phys, &coli, &cfg); if (cfg.replay) hudgen_replay_init(&hud, &tmp); else hudgen_ta_init(&hud, &tmp); }      /* the time attack's display set-up draws the car, the transmission and the times */
        for (int i = 0; i < HUD_NT_WORDS + 4; i++) want_w[i] = (unsigned short)be16(hudf + 4 + 2 * i);
        for (int i = 0; i < HUD_NT_WORDS; i++) if (hud.w[i] != want_w[i]) { fprintf(stderr, "hud_init.dat differs from the trace's initial screen at word %d\n", i); return 2; }
        hudp = hudf + 4 + 2 * (HUD_NT_WORDS + 4);
    }
    unsigned char *hudef = NULL;                                                         /* the end-of-race screen: NBG0 name tables + registers after every pass (oracle.py run --hudend) */
    static unsigned short want_e[HUDE_WORDS];
    const unsigned char *hudep = NULL;
    unsigned hude_bad = 0, hude_first = 0;
    if (use_hude) {
        snprintf(path, sizeof path, "%s.hude", argv[2]);
        hudef = slurp(path, &n);
        if (!hudef) { fprintf(stderr, "cannot open %s\n", path); return 2; }
        if (!use_hud) { snprintf(path, sizeof path, "%s/hud_init.dat", argv[1]); if (hudgen_load(&hud, path, type | (cfg.replay ? HUD_REPLAY : 0), course)) { fprintf(stderr, "cannot load %s\n", path); return 2; } }
        if (!use_hud && type >= 4) { static Drive tmp; drive_init(&tmp, &phys, &coli, &cfg); if (cfg.replay) hudgen_replay_init(&hud, &tmp); else hudgen_ta_init(&hud, &tmp); }
        for (int i = 0; i < HUDE_WORDS; i++) want_e[i] = (unsigned short)be16(hudef + 4 + 2 * i);
        if (hude_check_init(&hud, want_e)) { fprintf(stderr, "hude_init.dat differs from the trace's initial NBG0 pages\n"); return 2; }
        hudep = hudef + 4 + 2 * HUDE_WORDS;
    }
    static Drive d;
    static Camera cam;
    static Opp opp;
    unsigned char *oppf = NULL;
    unsigned opp_bytes = 0, opp_bad = 0, opp_first = 0;
    unsigned char vis_list[16];
    int vis_n = 0;
    static Drive gh;
    drive_init(&d, &phys, &coli, &cfg);
    if (cfg.ghost) drive_ghost_init(&gh, &d);
    unsigned ghost_bad = 0, ghost_first = 0;
    d.g1b = view0;
    unsigned char *sndf = NULL;
    const unsigned char *sndp = NULL;
    unsigned snd_bad = 0, snd_first = 0, snd_events = 0;
    if (use_snd) {
        snprintf(path, sizeof path, "%s.snd", argv[2]);
        sndf = slurp(path, &n);
        if (!sndf) { fprintf(stderr, "cannot open %s\n", path); return 2; }
        sndp = sndf + 4;
        snd_phys = &phys;
        d.se.call = snd_cb; d.se.status = snd_st;
    }
    camera_init(&cam);
    part_cam = &cam;
    static Drive dummy;
    if (cfg.replay) {                                                                      /* the replay's cameras: CAMERA.DAT as the console's memory holds it, the camera's dummy car */
        static CamDat cd;
        long cn;
        snprintf(path, sizeof path, "%s/../../disc/files/CAMERA.DAT", argv[1]);
        cd.p = slurp(path, &cn);
        if (!cd.p) { fprintf(stderr, "cannot open %s\n", path); return 2; }
        cd.base = 0x002A2000u; cd.size = (unsigned)cn;
        drive_dummy_init(&dummy, &d);
        camera_replay_setup(&cam, &cd, &dummy);
    }
    if (use_opp) {                                                                       /* the other cars (oracle.py run --opp): 8 slots x OPP_CMP_BYTES + the globals of otherCtrl after every pass */
        snprintf(path, sizeof path, "%s.opp", argv[2]);
        oppf = slurp(path, &n);
        if (!oppf) { fprintf(stderr, "cannot open %s\n", path); return 2; }
        opp_bytes = rd32(oppf + 4);
        opp_init(&opp);
        DriveIn in0;
        memset(&in0, 0, sizeof in0);
        in0.axis = 127;
        drive_move(&d, &in0);                                                            /* the race start-up routine 0x06010FE0: slot functions, normal_otinit, camera, slot functions, camera, calc_pmap_sub, otherCtrl */
        if (opp_rank >= 0) d.car[0xB6] = (unsigned char)opp_rank;
        opp_spawn(&opp, &d);
        if (cams) camera_pass(&cam, &d);
        drive_move(&d, &in0);
        opp_move(&opp, &d);
        if (cams) camera_pass(&cam, &d);
        drive_calc_dispang(&d);
        part_cam = NULL;                                                                 /* calc_pmap_sub's get_poly_map: the player's own blocks (the camera's list of the passes comes later) */
        vis_n = part_list(&d, vis, npolys, vis_list);
        part_cam = &cam;
        memcpy(opp.vis, vis_list, 16); opp.nvis = vis_n;
        opp_map(&opp, &d);
        opp_other_ctrl(&opp, &d);
    }
    unsigned cam_bad = 0, cam_first = 0, map_bad = 0, map_first = 0, sky_bad = 0, sky_first = 0;
    unsigned char *skyf = NULL;
    if (use_sky) {                                                                       /* the sky's rotation parameter table after every pass (oracle.py run --sky) */
        snprintf(path, sizeof path, "%s.sky", argv[2]);
        skyf = slurp(path, &n);
        if (!skyf) { fprintf(stderr, "cannot open %s\n", path); return 2; }
        camera_sky_init(&cam, course);
    }
    unsigned pass = 0, bad = 0, first_bad = 0;
    while (fgets(line, sizeof line, fi) && pass < npass) {
        DriveIn in;
        int a, b, ax, sh = 0, vw = 0;
        if (sscanf(line, "%d %d %d %d %d", &a, &b, &ax, &sh, &vw) < 3) continue;
        in.accel = a; in.brake = b; in.axis = ax; in.view = vw ? 0x40 : 0; in.shift = sh;
        snd_nlog = 0; snd_pass = pass; d.se.frame = pass;
        drive_move(&d, &in);
        if (cfg.ghost) drive_ghost_move(&gh, &d);
        if (cams) camera_pass(&cam, &d);
        if (skyf) {
            camera_sky_pass(&cam, &d);
            const unsigned char *ws = skyf + 4 + (size_t)pass * 36;
            int got[9] = { cam.sky[0], cam.sky[1], cam.sky[2], cam.sky[3], cam.sky[4], cam.sky_t[0], cam.sky_t[1], cam.sky_t[2], cam.sky_t[3] };
            int nd = 0;
            for (int k = 0; k < 9; k++) if (got[k] != (int)be32(ws + 4 * k)) nd++;
            if (nd) {
                if (!sky_bad) sky_first = pass;
                if (sky_bad++ < 3 || verbose) {
                    printf("sky pass %u:", pass);
                    for (int k = 0; k < 9; k++) if (got[k] != (int)be32(ws + 4 * k)) printf(" [%d] got %08x want %08x;", k, (unsigned)got[k], be32(ws + 4 * k));
                    printf("\n");
                }
            }
        }
        drive_present(&d);
        if (cfg.ghost) {
            drive_ghost_present(&gh);
            drive_p2sync(&d, &gh);
            const unsigned char *w1 = slot1 + 8 + (size_t)pass * SLOT_BYTES;
            int nd = 0, shown1 = 0;
            for (int o = 0; o < SLOT_BYTES; o++) if (gh.car[o] != w1[o]) nd++;
            if (nd) {
                if (!ghost_bad) ghost_first = pass;
                if (ghost_bad++ < 3 || verbose) {
                    printf("ghost pass %u: %d bytes differ:", pass, nd);
                    for (int o = 0; o < SLOT_BYTES && shown1 < 16; o += 4)
                        if (memcmp(gh.car + o, w1 + o, 4)) { printf("\n   +0x%03x  got %02x%02x%02x%02x  want %02x%02x%02x%02x", o, gh.car[o], gh.car[o + 1], gh.car[o + 2], gh.car[o + 3], w1[o], w1[o + 1], w1[o + 2], w1[o + 3]); shown1++; }
                    printf("\n");
                }
            }
        }
        if (use_opp) {
            vis_n = part_list(&d, vis, npolys, vis_list);
            memcpy(opp.vis, vis_list, 16); opp.nvis = vis_n;
            opp_map(&opp, &d);
            opp_pass(&opp, &d);
            for (int k = 0; k < nperturb; k++) if ((int)pass == perturb[k]) do_perturb(&opp, &d, &phys);
            const unsigned char *wo = oppf + 8 + (size_t)pass * opp_bytes;
            int nd = 0, first = -1;
            for (int k = 0; k < OPP_SLOTS; k++)
                for (int o = 0; o < OPP_CMP_BYTES; o++) {
                    int skip = 0;
                    for (int m = 0; m < nmask; m++) if (mask[m].lo >= 0x1000 && o + 0x1000 * (k + 1) >= mask[m].lo && o + 0x1000 * (k + 1) < mask[m].hi) skip = 1;
                    if (!skip && opp.car[k][o] != wo[k * OPP_CMP_BYTES + o]) { if (first < 0) first = k * 0x1000 + o; nd++; }
                }
            unsigned char gl[0x10 + 0x44];
            memset(gl, 0, sizeof gl);
            p32(gl, 0, opp.ncars << 16); gl[2] = (unsigned char)(opp.w2b2 >> 8); gl[3] = (unsigned char)opp.w2b2;
            gl[0] = (unsigned char)(opp.ncars >> 8); gl[1] = (unsigned char)opp.ncars;
            gl[6] = (unsigned char)(opp.progress >> 8); gl[7] = (unsigned char)opp.progress;
            p32(gl, 8, (int)opp.guide);
            for (int k = 0; k < 16; k++) p32(gl, 0x10 + 4 * k, (int)opp.list[k]);
            p32(gl, 0x10 + 0x40, opp.frame);
            const unsigned char *wg = wo + OPP_SLOTS * OPP_CMP_BYTES;
            for (int o = 0; o < (int)sizeof gl; o++) if (o >= 0x0C && o < 0x10) continue; else if (gl[o] != wg[o]) { if (first < 0) first = 0x20000 + o; nd++; }
            if (nd) {
                if (!opp_bad) opp_first = pass;
                if (opp_bad++ < 3 || verbose) {
                    printf("opp pass %u: %d bytes differ, first at car %d +0x%x:", pass, nd, first >= 0x20000 ? -1 : first / 0x1000, first >= 0x20000 ? first - 0x20000 : first & 0xFFF);
                    int shown = 0;
                    for (int k = 0; k < OPP_SLOTS && shown < 16; k++)
                        for (int o = 0; o < OPP_CMP_BYTES && shown < 16; o += 4)
                            if (memcmp(opp.car[k] + o, wo + k * OPP_CMP_BYTES + o, 4)) { printf("\n   car %d +0x%03x  got %08x want %08x", k, o, be32(opp.car[k] + o), be32(wo + k * OPP_CMP_BYTES + o)); shown++; }
                    for (int o = 0; o < (int)sizeof gl && shown < 16; o += 4)
                        if (memcmp(gl + o, wg + o, 4) && !(o == 0x0C)) { printf("\n   globals +0x%02x  got %08x want %08x", o, be32(gl + o), be32(wg + o)); shown++; }
                    printf("\n");
                }
            }
        }
        if (cams) {                                                                      /* the camera state after the pass */
            const unsigned char *wc = cams + 8 + (size_t)pass * CAM_REC;
            unsigned char got[0xB4 + 0x62];
            memcpy(got, cam.w, 0xB4);
            for (int k = 0; k < 12; k++) {
                unsigned v = (unsigned)cam.view[k], u = (unsigned)cam.view_copy[k];
                for (int bb = 0; bb < 4; bb++) { got[0xB4 + 4 * k + bb] = (unsigned char)(v >> (24 - 8 * bb)); got[0xB4 + 0x30 + 4 * k + bb] = (unsigned char)(u >> (24 - 8 * bb)); }
            }
            got[0xB4 + 0x60] = (unsigned char)d.cam_mode;
            got[0xB4 + 0x61] = (unsigned char)d.g1b;
            int nd = 0, first = -1;
            for (int o = 0; o < 0xB4 + 0x62; o++) if (o >= 0x40 && o < 0xB4) continue;            /* the camera owns the first 0x40 bytes of the work area, the rest are other globals */
            else if (got[o] != wc[o]) { if (first < 0) first = o; nd++; }
            if (nd) {
                if (!cam_bad) cam_first = pass;
                if (cam_bad++ < 3 || verbose) {
                    int o = first;
                    printf("camera pass %u: %d bytes differ, first at %s+0x%x: got %02x want %02x\n", pass, nd, o < 0xB4 ? "work" : o < 0xB4 + 0x30 ? "view" : o < 0xB4 + 0x60 ? "copy" : "mode",
                           o < 0xB4 ? o : o < 0xB4 + 0x30 ? o - 0xB4 : o < 0xB4 + 0x60 ? o - 0xB4 - 0x30 : o - 0xB4 - 0x60, got[o], wc[o]);
                }
            }
            if (use_map) {                                                               /* the part list after get_poly_map */
                unsigned char out[16];
                int cnt = cam.pm_on ? pmap_build(vis, npolys, cam.pm[0], cam.pm[1], cam.pm[2], PMAP_BUDGET, out) : pmap_build(vis, npolys, d.car[0xF0], d.car[0xF1], d.car[0xC7], PMAP_BUDGET, out);
                if (cnt > drive_part_limit(&d)) cnt = drive_part_limit(&d);
                const unsigned char *mw = wc + 0xB4 + 0x62;
                int wcnt = (mw[0] << 8) | mw[1];
                int ok = cnt == wcnt;
                for (int k = 0; ok && k < cnt; k++) ok = out[k] == mw[2 + k];
                if (!ok) {
                    if (!map_bad) map_first = pass;
                    if (map_bad++ < 3 || verbose) {
                        printf("parts pass %u: got %d [", pass, cnt);
                        for (int k = 0; k < cnt; k++) printf("%d ", out[k]);
                        printf("] want %d [", wcnt);
                        for (int k = 0; k < wcnt && k < 16; k++) printf("%d ", mw[2 + k]);
                        printf("]\n");
                    }
                }
            }
        }
        if (hudp || hudep) hudgen_pass(&hud, &d, pass);
        if (hudep) {
            char what[96];
            hudep = hude_apply(want_e, hudep);
            if (hude_compare(&hud, &d, want_e, what, sizeof what)) {
                if (!hude_bad) hude_first = pass;
                if (hude_bad++ < 3 || verbose) printf("hude pass %u: %s\n", pass, what);
            }
        }
        if (hudp) {
            unsigned nch = be16(hudp);
            hudp += 2;
            for (unsigned k = 0; k < nch; k++, hudp += 4) want_w[be16(hudp)] = (unsigned short)be16(hudp + 2);
            int nd = 0, first = -1;
            for (int i = 0; i < HUD_NT_WORDS + 4; i++) {
                unsigned short got = i < HUD_NT_WORDS ? hud.w[i] : hud.scroll[i - HUD_NT_WORDS];
                if (got != want_w[i]) { if (first < 0) first = i; nd++; }
            }
            if (nd) {
                if (!hud_bad) hud_first = pass;
                if (hud_bad++ < 3 || verbose) {
                    printf("hud pass %u: %d words differ:", pass, nd);
                    int shown = 0;
                    for (int i = 0; i < HUD_NT_WORDS + 4 && shown < 12; i++) {
                        unsigned short got = i < HUD_NT_WORDS ? hud.w[i] : hud.scroll[i - HUD_NT_WORDS];
                        if (got != want_w[i]) { printf(" [%s %05x] got %04x want %04x;", i < HUD_NT_WORDS ? "nt" : "scroll", i < HUD_NT_WORDS ? 0x3A000 + 2 * i : 2 * (i - HUD_NT_WORDS), got, want_w[i]); shown++; }
                    }
                    printf("\n");
                }
            }
        }
        if (sndp) {                                                                      /* the sound events of the pass and the state they leave */
            unsigned ne = rd32(sndp);
            const unsigned char *we = sndp + 4, *ws = we + 16 * (size_t)ne;
            unsigned char got[25];
            int bad_ev = (int)ne != snd_nlog, k, o;
            snd_events += ne;
            for (k = 0; !bad_ev && k < snd_nlog; k++) {
                if (snd_log[k].fn != rd32(we + 16 * k)) bad_ev = 1;
                for (o = 0; !bad_ev && o < 3; o++) {
                    unsigned m;
                    if (snd_argmask(snd_log[k].fn, o, &m) && ((snd_log[k].a[o] ^ rd32(we + 16 * k + 4 + 4 * o)) & m)) bad_ev = 1;
                }
            }
            p16(got, 0, d.se.w0); p16(got, 2, d.se.w2); p16(got, 4, d.se.w4); got[6] = (unsigned char)d.se.b6; got[7] = (unsigned char)d.se.b7; p16(got, 8, d.se.timer); got[10] = (unsigned char)d.se.b2d;
            p32(got, 11, (int)d.se.eng[0]); p32(got, 15, (int)d.se.eng[1]);
            memcpy(got + 19, d.se.wall, 4); got[23] = (unsigned char)d.se.msg44; got[24] = (unsigned char)d.msg45;
            {
                /* the oracle's longs are big endian, the snapshot is raw memory */
                int bad_st = memcmp(got, ws, 25) != 0;
                if (bad_ev || bad_st) {
                    if (!snd_bad) snd_first = pass;
                    if (snd_bad++ < 4 || verbose) {
                        printf("sound pass %u: %d events (want %u)%s\n", pass, snd_nlog, ne, bad_st ? ", state differs" : "");
                        for (k = 0; k < snd_nlog; k++) printf("   got  %08x %u %u %u\n", snd_log[k].fn, snd_log[k].a[0], snd_log[k].a[1], snd_log[k].a[2]);
                        for (k = 0; k < (int)ne; k++) printf("   want %08x %u %u %u\n", rd32(we + 16 * k), rd32(we + 16 * k + 4), rd32(we + 16 * k + 8), rd32(we + 16 * k + 12));
                        if (bad_st) { printf("   state got "); for (k = 0; k < 25; k++) printf("%02x", got[k]); printf("\n   state want "); for (k = 0; k < 25; k++) printf("%02x", ws[k]); printf("\n"); }
                    }
                }
            }
            sndp = ws + 25;
        }
        const unsigned char *want = slots + 8 + (size_t)pass * SLOT_BYTES;
        int ndiff = 0, shown = 0;
        for (int o = 0; o < SLOT_BYTES; o++) {
            int skip = 0;
            for (int m = 0; m < nmask; m++) if (o >= mask[m].lo && o < mask[m].hi) skip = 1;
            if (skip || d.car[o] == want[o]) continue;
            ndiff++;
        }
        if (ndiff) {
            if (!bad) first_bad = pass;
            bad++;
            if (bad <= 3 || verbose) {
                printf("pass %u: %d bytes differ:", pass, ndiff);
                int last = -100;
                for (int o = 0; o < SLOT_BYTES && shown < 24; o++) {
                    int skip = 0;
                    for (int m = 0; m < nmask; m++) if (o >= mask[m].lo && o < mask[m].hi) skip = 1;
                    if (skip || d.car[o] == want[o]) continue;
                    if (o / 4 != last / 4) { printf("\n   +0x%03x  got %02x%02x%02x%02x  want %02x%02x%02x%02x", o & ~3, d.car[o & ~3], d.car[(o & ~3) + 1], d.car[(o & ~3) + 2], d.car[(o & ~3) + 3],
                                                    want[o & ~3], want[(o & ~3) + 1], want[(o & ~3) + 2], want[(o & ~3) + 3]); shown++; }
                    last = o;
                }
                printf("\n");
            }
        }
        pass++;
    }
    snprintf(path, sizeof path, "%s.rec", argv[2]);                                         /* the record buffers at the end of the run (oracle.py run --rec) */
    unsigned char *rec = slurp(path, &n);
    if (rec && n == (long)sizeof d.rec) {
        int nd = 0;
        for (int o = 0; o < (int)sizeof d.rec; o++) if (rec[o] != d.rec[o]) { if (nd++ < 4) printf("record buffer +0x%03x: got %02x want %02x\n", o, d.rec[o], rec[o]); }
        printf("record buffers: %d bytes differ\n", nd);
        bad += nd != 0;
    }
    snprintf(path, sizeof path, "%s.irec", argv[2]);                                        /* the input words the player's car recorded (oracle.py run --irec) */
    unsigned char *irec = slurp(path, &n);
    if (irec) {
        unsigned wp = (unsigned)g32(d.car, 0x500 + 0x74) - 0x060F0000u;
        int nd = (long)wp != n;
        for (long o = 0; !nd && o < n; o++) nd = irec[o] != recbuf[o];
        printf("input record: %ld bytes, %s\n", n, nd ? "DIFFERS" : "identical");
        bad += nd;
    }
    if (cfg.ghost) printf("ghost: %u passes differ (first at pass %u)\n", ghost_bad, ghost_first);
    bad += ghost_bad != 0;
    printf("%u passes, %u differ (first at pass %u)\n", pass, bad, first_bad);
    if (cams) printf("camera: %u passes differ (first at pass %u)\n", cam_bad, cam_first);
    if (use_opp) printf("other cars: %u passes differ (first at pass %u); contacts: player %u, rival %u, car-car %u, wall %u\n", opp_bad, opp_first, opp_stats[0], opp_stats[1], opp_stats[2], opp_stats[3]);
    if (use_map) printf("parts: %u passes differ (first at pass %u)\n", map_bad, map_first);
    if (use_sky) printf("sky: %u passes differ (first at pass %u)\n", sky_bad, sky_first);
    if (use_snd) printf("sound: %u passes differ (first at pass %u), %u events\n", snd_bad, snd_first, snd_events);
    if (use_hud) printf("hud: %u passes differ (first at pass %u)\n", hud_bad, hud_first);
    if (use_hude) printf("hudend: %u passes differ (first at pass %u)\n", hude_bad, hude_first);
    return bad || cam_bad || map_bad || hud_bad || hude_bad || sky_bad || opp_bad || snd_bad ? 1 : 0;
}
