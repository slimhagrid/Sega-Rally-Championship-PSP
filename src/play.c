/* Sega Rally PSP - play mode, see play.h. */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspiofilemgr.h>
#include <psprtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "play.h"
#include "drive.h"
#include "camera.h"
#include "pmap.h"
#include "fx.h"
#include "hudgen.h"
#include "spray.h"
#include "opp.h"
#include "gamedir.h"
#include "perf.h"
#include "menu_padcfg.h"
#include "devauto.h"
#include <pspdisplay.h>
#include <pspkernel.h>

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define FB_BYTES (BUF_W * SCR_H * 4)
#define MAX_SCENE 512
#define O_CTRL_BYTE 0xF5

static SceneItem scene[MAX_SCENE];


static char *slurp_text(const char *path)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return NULL;
    int size = (int)sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    char *b = (char *)malloc((size_t)size + 1);
    if (!b) { sceIoClose(fd); return NULL; }
    int got = 0;
    while (got < size) {
        int n = sceIoRead(fd, b + got, (unsigned)(size - got));
        if (n <= 0) break;
        got += n;
    }
    sceIoClose(fd);
    b[got] = 0;
    return b;
}

int play_cfg_load(PlayCfg *pc, const char *path)
{
    char *t = slurp_text(path);
    if (!t) return 0;
    memset(pc, 0, sizeof *pc);
    pc->game_type = 2; pc->view = 2; pc->rect = 1; pc->hold = -1;
    for (char *l = t; *l;) {
        char *e = l;
        while (*e && *e != '\n') e++;
        char save = *e;
        *e = 0;
        int a, b, c, d;
        char name[96];
        if (sscanf(l, "course %d", &a) == 1) pc->course = a & 3;
        else if (sscanf(l, "type %d", &a) == 1) pc->game_type = a;
        else if (sscanf(l, "car %d", &a) == 1) pc->car = a;
        else if (sscanf(l, "mt %d", &a) == 1) pc->mt = a;
        else if (sscanf(l, "diff %d", &a) == 1) pc->diff = a;
        else if (sscanf(l, "mirror %d", &a) == 1) pc->mirror = a;
        else if (sscanf(l, "setup %d %d %d %d", &a, &b, &c, &d) == 4) { pc->setup_mode = 1; pc->setup[0] = a; pc->setup[1] = b; pc->setup[2] = c; pc->setup[3] = d; }
        else if (sscanf(l, "view %d", &a) == 1) pc->view = a;
        else if (sscanf(l, "rect %d", &a) == 1) pc->rect = a;
        else if (sscanf(l, "debug %d", &a) == 1) pc->debug = a;
        else if (sscanf(l, "hold %d", &a) == 1) pc->hold = a;
        else if (sscanf(l, "opponents %d", &a) == 1) pc->nopp = !a;
        else if (sscanf(l, "strip %d", &a) == 1) pc->strip = a;
        else if (sscanf(l, "replay %95s", name) == 1) snprintf(pc->replay, sizeof pc->replay, "%s", name);
        *e = save;
        l = *e ? e + 1 : e;
    }
    free(t);
    return 1;
}

static void fatal(const char *what, const char *path)
{
    pspDebugScreenInit();
    pspDebugScreenPrintf("play mode: cannot load %s (%s)\n", what, path);
    for (;;) sceDisplayWaitVblankStart();
}

static inline float fx16(int v) { return (float)v * (1.0f / 65536.0f); }

/* From the menus the race reads the pad like the original (car::config_keyset 0x0600862C, row table 0x060086C8, the four rows of the digital pad = KEY CONFIG types A .. D): the PSP's buttons
 * make the Saturn pad word, the row names the Saturn buttons that accelerate, brake, shift up / down (a new press) and change the view (a new press).  The KEY CONFIG page of the options
 * (menu_pspfix.c) shows which PSP button each of the Saturn buttons is: A Square, B Cross, C Circle, X Triangle, Y Select, L / R the shoulder buttons, the D-pad. */
/* The PSP's KEY CONFIG types (menu_padcfg.c: A, B, DIY) replace the original's four rows; padcfg_rows() gives the row in the original's layout. */

static unsigned sat_word(unsigned b)
{
    unsigned w = 0;
    if (b & PSP_CTRL_SQUARE) w |= 0x400;
    if (b & PSP_CTRL_CROSS) w |= 0x100;
    if (b & PSP_CTRL_CIRCLE) w |= 0x200;
    if (b & PSP_CTRL_TRIANGLE) w |= 0x040;
    if (b & PSP_CTRL_SELECT) w |= 0x020;
    if (b & PSP_CTRL_LTRIGGER) w |= 0x008;
    if (b & PSP_CTRL_RTRIGGER) w |= 0x080;
    if (b & PSP_CTRL_UP) w |= 0x1000;
    if (b & PSP_CTRL_DOWN) w |= 0x2000;
    return w;
}

/* development: data/race_pad.txt "pass hexmask" lines (PSP_CTRL_ bits, from that call on the pad reads as the mask; the sticks stay centred): scripted inputs for a race run from the menus */
static unsigned dev_at[64], dev_mask[64];
static int dev_n, dev_calls;

void play_dev_pad_load(const char *base)
{
    char path[256], line[96];
    snprintf(path, sizeof path, "%s/race_pad.txt", base);
    FILE *f = fopen(path, "r");
    dev_n = 0; dev_calls = 0;
    if (!f) return;
    while (dev_n < 64 && fgets(line, sizeof line, f)) if (sscanf(line, "%u %x", &dev_at[dev_n], &dev_mask[dev_n]) == 2) dev_n++;
    fclose(f);
}

/* the pad as the game's input: the analog nub is the steering axis (127 = centre), X the accelerator, SQUARE the brake */
void play_read_pad(const PlayCfg *pc, SceCtrlData *pad, unsigned int *prev, DriveIn *in)
{
    sceCtrlPeekBufferPositive(pad, 1);
    if (dev_n) {
        for (int k = 0; k < dev_n; k++) if (dev_at[k] <= (unsigned)dev_calls) { pad->Buttons = dev_mask[k]; pad->Lx = 128; pad->Ly = 128; }
        dev_calls++;
    }
    unsigned int pressed = pad->Buttons & ~*prev;
    *prev = pad->Buttons;
    int lx = pad->Lx;
    int axis = 127;
    if (lx < 128 - 14) axis = 127 - (128 - 14 - lx) * 127 / (128 - 14);
    else if (lx > 128 + 14) axis = 127 + (lx - 128 - 14) * 128 / (127 - 14);
    if (pad->Buttons & PSP_CTRL_LEFT) axis = 0;
    if (pad->Buttons & PSP_CTRL_RIGHT) axis = 255;
    if (axis < 0) axis = 0;
    if (axis > 255) axis = 255;
    in->axis = axis;
    in->accel = (pad->Buttons & PSP_CTRL_CROSS) != 0;
    in->brake = (pad->Buttons & PSP_CTRL_SQUARE) != 0;
    in->view = (pressed & PSP_CTRL_LTRIGGER) ? 0x10 : 0;
    in->shift = (pressed & PSP_CTRL_TRIANGLE) ? 1 : (pressed & PSP_CTRL_CIRCLE) ? -1 : 0;
    if (pc->from_menu) {
        unsigned short r[7];
        unsigned held = sat_word(pad->Buttons), now = sat_word(pressed);
        padcfg_rows(pc->padrow & 15, r);
        in->accel = (r[0] & held) != 0;
        in->brake = (r[1] & held) != 0;
        in->shift = (now & r[2]) ? 1 : (now & r[3]) ? -1 : 0;
        in->view = (now & (r[4] | r[5] | r[6])) ? 0x10 : 0;
    }
}


/* the VDP2 colour-offset fade (clroffset::fade_inout_init 0x06022564 / fade_inout_work 0x060225DC, run once per pass by the stage loop 0x06015510): the offset of all layers steps from
 * `cur` to `tgt` by `step` every `reload` passes; positive = towards white.  Modes of etc::fade_color_init (0x0600585C): 3 = white-out at the end of the fly-by, 0 = white-in at the race start. */
typedef struct { int active, cnt, reload, cur, tgt, step; } Fade;

static void fade_init(Fade *f, int interval, int start, int tgt, int step)
{
    f->active = 1; f->cnt = f->reload = interval; f->cur = start; f->tgt = tgt; f->step = step;
}
static void fade_mode(Fade *f, int mode)
{
    if (mode == 0) fade_init(f, 1, 255, 0, -16);
    else if (mode == 1) fade_init(f, 1, 0, 255, 51);
    else if (mode == 2) fade_init(f, 1, 0, 255, 8);
    else if (mode == 3) fade_init(f, 1, 0, 255, 32);
    else if (mode == 4) fade_init(f, 1, 255, 0, -48);
}
static void fade_work(Fade *f)
{
    if (!f->active) return;
    if (--f->cnt != 0) return;
    f->cnt = f->reload;
    if (f->cur == f->tgt) { f->active = 0; return; }
    int v = f->cur + f->step;
    if (f->step > 0) { if (v >= f->tgt) v = f->tgt; }
    else if (f->tgt >= v) v = f->tgt;
    f->cur = v;
}
static int fade_offset(const Fade *f) { int v = f->cur & 0x1FF; return v >= 256 ? v - 512 : v; }       /* the register holds 9 bits, two's complement */

int play_run(const PlayCfg *pc, const PlayAssets *as, const char *base, PlayOut *out, int *draw_off_io)
{
    static Phys phys;
    static Coli coli;
    static Drive drive;
    static Camera cam;
    static Course course;
    static Sky sky;
    static HudGen hg;
    static Spray spray;
    static Opp opp;
    static const char *const cn[4] = { "desert", "forest", "mount", "lake" };
    char path[200];

    snprintf(path, sizeof path, "%s/course/phys.dat", base);
    if (phys_load(&phys, path)) fatal("physics tables", path);
    snprintf(path, sizeof path, "%s/course/%s.rct", base, cn[pc->course]);
    if (coli_load(&coli, path)) fatal("collision map", path);
    snprintf(path, sizeof path, "%s/course/%s.rcp", base, cn[pc->course]);
    if (rcp_load(&course, path)) fatal("course model", path);
    snprintf(path, sizeof path, "%s/course/%s.rbg", base, cn[pc->course]);
    int skyerr = sky_load(&sky, path);

    DriveCfg cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.game_type = pc->game_type; cfg.course = pc->course; cfg.difficulty = pc->diff; cfg.manual = pc->mt; cfg.car_type = pc->car; cfg.mirror = pc->mirror;
    cfg.compare = pc->compare; cfg.rec184 = pc->rec184; cfg.rec188 = pc->rec188; cfg.rec18c = pc->rec18c;
    cfg.setup_mode = pc->setup_mode; memcpy(cfg.setup, pc->setup, sizeof cfg.setup); cfg.laps = pc->laps > 0 ? pc->laps : 3; cfg.init_r = pc->init_r; cfg.init_rec = pc->init_rec; cfg.init_g300 = pc->init_g300;
    cfg.rec_win = pc->rec_win; cfg.rec_start = pc->rec_start;
    cfg.replay = pc->mode2 && pc->game_type == 4;
    cfg.ghost = pc->ghost && pc->game_type == 4 && !pc->mirror; cfg.ghost_win = pc->ghost_win; cfg.ghost_start = pc->ghost_start;
    cfg.ghost_car = pc->ghost_car; cfg.ghost_mode = pc->ghost_mode; cfg.ghost_manual = pc->ghost_manual; memcpy(cfg.ghost_setup, pc->ghost_setup, sizeof cfg.ghost_setup); cfg.ghost_solid = pc->ghost_solid;
    drive_init(&drive, &phys, &coli, &cfg);
    static Drive ghost;
    int ghost_on = cfg.ghost;
    if (ghost_on) drive_ghost_init(&ghost, &drive);                                                       /* game_init's myline_set: slot 1 races the stored run */
    if (pc->snd) {                                                                                        /* M9: the race's sound events go to the sound library */
        drive.se.call = pc->snd->call; drive.se.status = pc->snd->status; drive.se.ud = pc->snd->ud;
        drive.se.stereo = pc->snd_stereo; drive.se.opt_vol = pc->snd_opt_vol; drive.se.bgm_sel = pc->snd_bgm_sel;
        memcpy(drive.se.wall, pc->init_wall, 4);
        se_call(&drive.se, 0x06017418, 0, 0, 0);                                                           /* game_init: the sound set of the stage and the car (loads the banks), then init_game_se */
        sndev_init(&drive.se);
    }
    if (as->events_ok) drive.rng = &as->events->rng;
    snprintf(path, sizeof path, "%s/course/hud_init.dat", base);
    int hudgen_ok = hudgen_load(&hg, path, pc->game_type | (cfg.replay ? HUD_REPLAY : 0), pc->course) == 0;                 /* the screen after init_game_disp; game_display then rewrites it every pass */
    if (hudgen_ok) { if (cfg.replay) hudgen_replay_init(&hg, &drive); else hudgen_ta_init(&hg, &drive); }                     /* time attack: the panel's times, the car / transmission boxes, the record box (a replay: the times of the run) */
    static HudEnd hend;                                                                              /* the cells of the goal / game-over screens (NBG0): the result files of this stage and game type */
    if (pc->game_type >= 4) snprintf(path, sizeof path, "%s/course/hude_f.dat", base);                                          /* time attack: CGDATA_F.DAT (0x06004514) */
    else snprintf(path, sizeof path, "%s/course/hude_%s_%s.dat", base, pc->game_type == 0 ? "cr" : "pr", cn[pc->course]);
    int hend_ok = hud_end_load(&hend, path) == 0;
    camera_init(&cam);
    static Drive cam_dummy;
    if (cfg.replay) { drive_dummy_init(&cam_dummy, &drive); camera_replay_setup(&cam, &pc->camdat, &cam_dummy); }       /* play mode 2: the cut-scene cameras of CAMERA.DAT */
    spray_init(&spray, 0x101);                                                                         /* race start: dust from the rear wheels */
    camera_sky_init(&cam, pc->course);
    drive.g1b = pc->view;
    ev_set_collision(as->events, &coli);
    if (as->events_ok) ev_start(as->events, pc->course, pc->game_type);

    unsigned short npolys[128];
    for (unsigned i = 0; i < 128; i++) npolys[i] = i < course.nparts ? course.part[i].npolys : 0;

    /* the other cars (arcade and practice): the race start-up routine 0x06010FE0 - slot functions, normal_otinit, camera, slot functions, camera, calc_pmap_sub, otherCtrl */
    int use_opp = !pc->nopp && (pc->game_type == 0 || pc->game_type == 2);
    if (use_opp) {
        opp_init(&opp);
        DriveIn in0;
        memset(&in0, 0, sizeof in0);
        in0.axis = 127;
        unsigned char vis0[16];
        drive_move(&drive, &in0);
        opp_spawn(&opp, &drive);
        camera_pass(&cam, &drive);
        drive_move(&drive, &in0);
        opp_move(&opp, &drive);
        camera_pass(&cam, &drive);
        drive_calc_dispang(&drive);
        int n0 = pmap_build(course.vis, npolys, drive.car[0xF0], drive.car[0xF1], drive.car[0xC7], PMAP_BUDGET, vis0);
        if (n0 > drive_part_limit(&drive)) n0 = drive_part_limit(&drive);
        memcpy(opp.vis, vis0, sizeof vis0); opp.nvis = n0;
        opp_map(&opp, &drive);
        opp_other_ctrl(&opp, &drive);
    }

    /* recorded inputs: "accel brake axis shift" per pass */
    int (*rp)[4] = NULL;
    int nrp = 0;
    if (pc->replay[0]) {
        snprintf(path, sizeof path, "%s/replay/%s", base, pc->replay);
        char *t = slurp_text(path);
        if (!t) fatal("replay", path);
        int cap = 1024;
        rp = (int (*)[4])malloc(sizeof(int[4]) * (size_t)cap);
        for (char *l = t; *l;) {
            char *e = l;
            while (*e && *e != '\n') e++;
            char save = *e;
            *e = 0;
            int a, b, ax, sh = 0;
            if (l[0] != '#' && sscanf(l, "%d %d %d %d", &a, &b, &ax, &sh) >= 3) {
                if (nrp == cap) { cap *= 2; rp = (int (*)[4])realloc(rp, sizeof(int[4]) * (size_t)cap); }
                rp[nrp][0] = a; rp[nrp][1] = b; rp[nrp][2] = ax; rp[nrp][3] = sh; nrp++;
            }
            *e = save;
            l = *e ? e + 1 : e;
        }
        free(t);
    }

    const RenderRect rect = VIEW_RECT;                                                      /* the picture is drawn at 352 x 224 and copied to the screen (render.c view_end) */
    int rect_mode = pc->rect;                                                               /* 0: 1:1 in the middle, 1: scaled to the screen height, 2: the same, smoothed (SELECT cycles) */
    if (rect_mode < 0 || rect_mode > 2) rect_mode = 1;
    perf_open(rect_mode);
    if (pc->from_menu) play_dev_pad_load(base);
    FILE *trace = NULL;                                                                         /* development: data/race_trace exists -> data/race_trace.txt gets "pass x y z speed" of the car (16.16) every 15 passes (compare with the oracle's .csv) */
    {
        char tp[256];
        snprintf(tp, sizeof tp, "%s/race_trace", base);
        FILE *tf = fopen(tp, "r");
        if (tf) { fclose(tf); snprintf(tp, sizeof tp, "%s/race_trace.txt", base); trace = fopen(tp, "w"); }
    }

    SceCtrlData pad;
    unsigned int prev = 0;
    int prev_sel = 0;
    static DevPilot dpil;                                                                       /* development (data/autoplay.txt): the pilot that drives the guide line */
    int dev_pilot = pc->from_menu && devauto_pilot_mode, dev_ctrl = -1;
    if (dev_pilot) devauto_pilot_init(&dpil, &phys, pc->course);
    int nscene = 0, draw_off = draw_off_io ? *draw_off_io : 0, have_view = 0, end_fade = 0, end_pass = 0;
    int paused = 0, retire = 0;                                                                  /* the pause menu (the race loop 0x06011170, states 2 / 4 / 6 / 8 / 10) */
    unsigned int prev_pb = 0, prev_stick = 0, prev_rp = 0;
    RenderStats st = { 0, 0, 0, 0, 0, 0 };
    unsigned char parts[16];
    int nparts = 0;
    Fade fade;
    memset(&fade, 0, sizeof fade);
    fade_init(&fade, 1, 0x2FF, 0x1FF, -54);                                                    /* start_init: clroffset::pre_fade_inout(0x2FF, 0x1FF, -54): white at the start of the stage, gone in five passes */
    unsigned last_vcount = (unsigned)sceDisplayGetVcount(), deadline = last_vcount + 2;
    for (int pass = 0;; pass++) {
        DriveIn in;
        unsigned long long tp0 = sceKernelGetSystemTimeWide(), tp1 = tp0, tp2 = tp0, tp3 = tp0;
        play_read_pad(pc, &pad, &prev, &in);
        if (!pc->from_menu && (pad.Buttons & PSP_CTRL_START)) { perf_close(); sceKernelExitGame(); }
        if (!pc->from_menu && (pad.Buttons & PSP_CTRL_SELECT) && !(prev_sel)) rect_mode = (rect_mode + 1) % 3;
        prev_sel = (pad.Buttons & PSP_CTRL_SELECT) != 0;
        if (dev_pilot) devauto_pilot(&dpil, drive.car, &in);
        if (nrp) {
            int k = pass < nrp ? pass : nrp - 1;
            in.accel = rp[k][0]; in.brake = rp[k][1]; in.axis = rp[k][2]; in.shift = rp[k][3]; in.view = 0;
            if (pass >= nrp) { in.accel = 0; in.brake = 0; in.axis = 127; in.shift = 0; }
        }
        if (pc->from_menu && hudgen_ok && !nrp && !end_fade) {                                       /* START in the running race opens the pause menu; UP / DOWN choose, START confirms: 0 continue, 1 / 2 leave the race */
            unsigned pe = pad.Buttons & ~prev_pb, stick = (pad.Ly < 64 ? 1u : 0u) | (pad.Ly > 192 ? 2u : 0u), se = stick & ~prev_stick;
            if (!paused) {
                if ((pe & PSP_CTRL_START) && (drive.g803 & 0x40)) {                                  /* (bit 6 is set from the countdown on and cleared when the goal / game over screens start) */
                    paused = 1;
                    hudgen_pause_open(&hg, &drive);
                    if (pc->snd) {
                        static const unsigned eng0[4] = { 0, 0, 0, 0 };
                        pc->snd->engine(pc->snd->ud, eng0); pc->snd->call(pc->snd->ud, SND_CDPAUSE, 0, 0, 0); pc->snd->flush(pc->snd->ud);        /* FUN_06017474: the engine and the CD music stop */
                    }
                }
            } else {
                if ((pe & PSP_CTRL_DOWN) || (se & 2)) { if (drive.pause[0] < 2) { drive.pause[0]++; drive.pause[2] = 0; } }
                if ((pe & PSP_CTRL_UP) || (se & 1)) { if (drive.pause[0] > 0) { drive.pause[0]--; drive.pause[2] = 0; } }
                if (pe & PSP_CTRL_START) {
                    int item = drive.pause[0];
                    hudgen_pause_close(&hg);
                    if (item == 0) {
                        paused = 0;
                        if (pc->snd) { pc->snd->call(pc->snd->ud, SND_CDRESUME, 0, 0, 0); pc->snd->flush(pc->snd->ud); }                            /* FUN_060174EC */
                    } else { retire = item; end_fade = 1; fade_mode(&fade, 1); }                          /* 0x060114B4: the box goes, the screen fades out, the race is over */
                } else hudgen_pause_cursor(&hg, &drive);
            }
            prev_stick = stick;
        }
        prev_pb = pad.Buttons;
        int run = !(pc->hold >= 0 && pass > pc->hold) && !paused && !retire;
        if (run) {
            unsigned char *c = drive.car;
            drive.se.frame = (unsigned)pass;
            if (pc->snd) { pc->snd->vint(pc->snd->ud); drive.se.msg44 = pc->snd->msg_busy(pc->snd->ud); }          /* the V-blanks of the 33 ms between two flushes: the voice stream needs the 68000 to have taken the flush's commands */
            drive_move(&drive, &in);
            if (ghost_on) drive_ghost_move(&ghost, &drive);
            if (trace && pass % 15 == 0) { fprintf(trace, "%d %d %d %d %d\n", pass, g32(c, 0x18), g32(c, 0x1C), g32(c, 0x20), g32(c, 0x94)); fflush(trace); }
            if (trace && pass >= 203 && pass <= 212) { char nm[300]; snprintf(nm, sizeof nm, "%s/race_slot_%d.bin", base, pass); FILE *sf = fopen(nm, "wb"); if (sf) { fwrite(c, 1, SLOT_BYTES, sf); fclose(sf); } }
            if (cfg.replay) {                                                                       /* replay_cactl: the buttons that were pressed since the last pass */
                unsigned pe2 = pad.Buttons & ~prev_rp;
                prev_rp = pad.Buttons;
                camera_replay_keys(&cam, &drive, (pe2 & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE | PSP_CTRL_SQUARE)) != 0, (pe2 & (PSP_CTRL_TRIANGLE | PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) != 0);
            }
            camera_pass(&cam, &drive);
            if (cam.fade >= 0) fade_mode(&fade, cam.fade);
            fade_work(&fade);
            camera_sky_pass(&cam, &drive);
            drive_present(&drive);
            int gmode = -1;
            if (ghost_on) { gmode = drive_ghost_present(&ghost); drive_p2sync(&drive, &ghost); }                   /* slot 1: the move was above, the presentation (0 = drawn, 3 = this pass not), then the count-down's sync */
            if (hudgen_ok) hudgen_pass(&hg, &drive, (unsigned)pass);
            have_view = cam.view[0] != 0 || cam.view[5] != 0;
            nparts = cam.pm_on ? pmap_build(course.vis, npolys, cam.pm[0], cam.pm[1], cam.pm[2], PMAP_BUDGET, parts) : pmap_build(course.vis, npolys, c[0xF0], c[0xF1], c[0xC7], PMAP_BUDGET, parts);       /* (a replay's cut-scene camera: the blocks at its eye) */
            if (nparts > drive_part_limit(&drive)) nparts = drive_part_limit(&drive);                    /* the goal presentation draws 5 blocks */
            if (use_opp) {
                memcpy(opp.vis, parts, sizeof opp.vis); opp.nvis = nparts;
                opp_map(&opp, &drive);
                opp_pass(&opp, &drive);
            }
            if (pc->from_menu) devauto_cheats(&drive);
            tp1 = sceKernelGetSystemTimeWide();

            /* what this pass draws, in the order the game enters it: the car, the event objects, the HUD sprites, the course parts */
            float view[12];
            for (int k = 0; k < 12; k++) view[k] = fx16(cam.view[k]);
            nscene = 0;
            scene_set_pers(cfg.replay && cam.cut ? cam.pers : 224);                                           /* (the cut-scene camera's perspective 0x200; the goal's orbit is the pipeline's own) */
            if (as->cars_ok && as->objects->file) {
                CarState cs;
                for (int k = 0; k < 3; k++) { cs.ang[k] = fx16(g32(c, 0x0C + 4 * k)); cs.pos[k] = fx16(g32(c, 0x18 + 4 * k)); }
                cs.steer = fx16(g32(c, 0x30)); cs.spin = fx16(g32(c, 0x38));
                for (int k = 0; k < 4; k++) { cs.susp[k] = fx16(g32(c, 0x3C + 4 * k)); cs.ground[k] = fx16(g32(c, 0x4C + 4 * k)); }
                cs.type = c[0x5C]; cs.brake = c[0x5D]; cs.damage = g16u(c, 0xB4);
                if (drive.cam_mode != 0 || drive.g1b != 0 || (cfg.replay && !drive.rp_chase)) {                                                 /* normal_plmt 0x06006754: the word [0x060AA01A] is 0 in the in-car view A: the player's car is not drawn (nor its spray spawned) */
                    nscene += car_display(as->cars, &cs, 0, view, as->objects, scene + nscene, 32);
                    if (as->events_ok && car_tier(as->cars, &cs, view) >= 0 && car_tier(as->cars, &cs, view) < 2) spray_spawn(&spray, &drive, &as->events->rng);       /* dispGhost's near and middle tiers */
                }
                if (gmode >= 0) {                                                                             /* the ghost (slot 1): dispCar with the mode its presentation chose; the spray is made in either mode */
                    const unsigned char *o = ghost.car;
                    CarState gs;
                    for (int j = 0; j < 3; j++) { gs.ang[j] = fx16(g32(o, 0x0C + 4 * j)); gs.pos[j] = fx16(g32(o, 0x18 + 4 * j)); }
                    gs.steer = fx16(g32(o, 0x30)); gs.spin = fx16(g32(o, 0x38));
                    for (int j = 0; j < 4; j++) { gs.susp[j] = fx16(g32(o, 0x3C + 4 * j)); gs.ground[j] = fx16(g32(o, 0x4C + 4 * j)); }
                    gs.type = o[0x5C]; gs.brake = o[0x5D]; gs.damage = g16u(o, 0xB4);
                    if (nscene < MAX_SCENE - 96) nscene += car_display(as->cars, &gs, gmode, view, as->objects, scene + nscene, 32);
                    if (as->events_ok && car_tier(as->cars, &gs, view) >= 0 && car_tier(as->cars, &gs, view) < 2) spray_spawn_car(&spray, &phys, ghost.car, &as->events->rng);
                }
                for (int k = 0; use_opp && k < OPP_SLOTS; k++) {                                              /* the other cars: slot order, each drawn by the same car display */
                    if (!opp_visible(&opp, k)) continue;
                    const unsigned char *o = opp.car[k];
                    for (int j = 0; j < 3; j++) { cs.ang[j] = fx16(g32(o, 0x0C + 4 * j)); cs.pos[j] = fx16(g32(o, 0x18 + 4 * j)); }
                    cs.steer = fx16(g32(o, 0x30)); cs.spin = fx16(g32(o, 0x38));
                    for (int j = 0; j < 4; j++) { cs.susp[j] = fx16(g32(o, 0x3C + 4 * j)); cs.ground[j] = fx16(g32(o, 0x4C + 4 * j)); }
                    cs.type = o[0x5C]; cs.brake = o[0x5D]; cs.damage = g16u(o, 0xB4);
                    if (nscene < MAX_SCENE - 96) nscene += car_display(as->cars, &cs, 0, view, as->objects, scene + nscene, 32);
                    if (as->events_ok && car_tier(as->cars, &cs, view) >= 0 && car_tier(as->cars, &cs, view) < 2) spray_spawn_car(&spray, &phys, opp.car[k], &as->events->rng);
                }
            }
            if (as->events_ok && as->objects->file && have_view) {
                EvWorld w;
                memcpy(w.view, view, sizeof w.view);
                w.game_type = pc->game_type; w.car_byte_c5 = c[0xC5]; w.car_seg = g16u(c, 0x60); w.car_c8 = g16u(c, 0xC8); w.car_ca = g16u(c, 0xCA);
                w.car_x = g32(c, 0x18); w.car_y = g32(c, 0x1C); w.car_z = g32(c, 0x20); w.car_yaw = g32(c, 0x10); w.nseg_word = drive.total_laps;
                memcpy(w.view_fx, cam.view, sizeof w.view_fx); w.car_state = g16u(c, 0x0A); w.mode810 = drive.se.mode810;
                nscene += ev_frame(as->events, &w, parts, nparts, scene + nscene, MAX_SCENE - 16 - nscene);
                p32(c, 0x64, g32(c, 0x64) + w.push_x);                                                 /* the start object keeps the car out of its box (the push is used by the next pass's move) */
                p32(c, 0x6C, g32(c, 0x6C) + w.push_z);
                for (int k = 0; k < as->events->snd.n; k++)                                           /* what the objects asked of the sound library (the helicopter, the start object) */
                    se_call(&drive.se, as->events->snd.kind[k] ? SND_WORD : SND_PLAY, as->events->snd.val[k], 0, 0);
                if ((signed char)c[0xF5] < 8)                                                          /* game_ctrl step 7 */
                    nscene += spray_frame(&spray, view, as->objects, ev_part_sand(as->events), ev_part_water(as->events), scene + nscene, MAX_SCENE - 8 - nscene);
                nscene += ev_hud(as->events, (signed char)c[0xF5] < 8 ? g32(c, 0x8C) : -1, drive.cd_idx, drive.cd_size, scene + nscene, 4);       /* 0x06006754: no needle once the control byte is >= 8 (goal, game over) */
            }
            if (pc->snd) {                                                                          /* after the objects (the helicopter / the start object call the sound library in the pass) */
                pc->snd->vint(pc->snd->ud);
                if (drive.se.msg_req) { pc->snd->msg_put(pc->snd->ud, drive.se.msg_req); drive.se.msg_req = 0; }
                pc->snd->engine(pc->snd->ud, drive.se.eng); pc->snd->flush(pc->snd->ud); }          /* game_se_set's engine longs (the goal screen may idle them); start_main's FUN_06017580 after the pass */
            for (int i = 0; i < nparts && nscene < MAX_SCENE - 4 && have_view; i++) {
                scene[nscene].c = &course; scene[nscene].part = parts[i] & 127; scene[nscene].clip = 0;
                memcpy(scene[nscene].m, view, sizeof scene[nscene].m);
                nscene++;
            }
        } else if (end_fade) fade_work(&fade);                                                     /* (leaving through the pause menu: the fade runs on the frozen picture) */

        tp2 = sceKernelGetSystemTimeWide();
        sceGuStart(GU_DIRECT, as->gu_list);
        view_begin();
        if (have_view) {
            if (skyerr == 0) sky_draw_table(&sky, cam.sky[4], cam.sky[0], cam.sky[1], &rect);
            scene_draw(scene, nscene, &rect, &st);
        }
        if (hudgen_ok && as->hud_ok) {                                                     /* NBG3 / NBG2 text layers over the picture */
            int scr[4];
            for (int k = 0; k < 4; k++) scr[k] = hg.scroll[k] & 0x3FF;
            hud_draw(as->hud, (hg.bgon & 4) ? hg.w : NULL, (hg.bgon & 8) ? hg.w + 0x1000 : NULL, hg.w + 0x2000, scr, &rect);       /* BGON: the goal / game-over screens switch NBG2 / NBG3 off and NBG0 on */
            if ((hg.bgon & 1) && hend_ok) hud_draw_end(as->hud, &hend, hg.nt0, hg.nscroll[0] & 0x3FF, hg.nscroll[1] & 0x3FF, &rect);
        }
        {
            int fo = fade_offset(&fade);
            view_fade(fo > -2 && fo < 2 ? 0 : fo);                                              /* (the stage's own white-in ends at -1, which nobody sees) */
        }
        view_end(draw_off, rect_mode);
        sceGuFinish();
        tp3 = sceKernelGetSystemTimeWide();
        sceGuSync(0, 0);
        unsigned long long tp4 = sceKernelGetSystemTimeWide();
        if (pc->strip && run && (st.skipped || st.polys_in >= 4090)) {
            FILE *sf = fopen(SEGA_BASE "/strip.txt", "a");
            if (sf) { fprintf(sf, "pass %d SKIP parts %d vused %d polys %d items %d nparts %d\n", pass, st.skipped, st.vused, st.polys_in, nscene, nparts); fclose(sf); }
        }
        PerfFrame pf;
        {
            const unsigned char *c = drive.car;
            pf.pass = pass;
            pf.logic_ms = (float)(tp1 - tp0) / 1000.0f; pf.build_ms = (float)(tp2 - tp1) / 1000.0f; pf.submit_ms = (float)(tp3 - tp2) / 1000.0f; pf.ge_ms = (float)(tp4 - tp3) / 1000.0f;
            pf.items = nscene; pf.polys = st.polys_in; pf.tris = st.tris; pf.draws = st.draws; pf.nparts = nparts; pf.spray = g32(spray.m, 0);
            pf.seg = g16u(c, 0x60); pf.mph = (int)(((long long)g32(c, 0x94) * 67) >> 16); pf.laps = g16u(c, 0xCA);
        }
        /* pace at 30 Hz: the picture of this pass is shown at the vblank two frames after the previous one (not "two vblanks after the work": 22 ms of work would then cost three) */
        while (!devauto_turbo && (int)((unsigned)sceDisplayGetVcount() - deadline) < 0) sceDisplayWaitVblankStart();
        {
            unsigned now = (unsigned)sceDisplayGetVcount();
            deadline = (int)(now - deadline) >= 2 ? now + 2 : deadline + 2;                     /* far behind (a long hitch): start counting again from now */
            pf.vblanks = (int)(now - last_vcount);
            last_vcount = now;
        }
        sceGuSwapBuffers();
        draw_off ^= FB_BYTES;
        if (draw_off_io) *draw_off_io = draw_off;
        if (pc->from_menu && devauto_race_shots) {                                              /* development: pictures of the race every N passes and whenever the car's control byte changes */
            int ctl = drive.car[O_CTRL_BYTE];
            char nm[40];
            if (ctl != dev_ctrl) { dev_ctrl = ctl; snprintf(nm, sizeof nm, "race%d_ctl%02x_p%d", pc->course, ctl, pass); devauto_shot(base, nm); }
            else if (pass % (ctl >= 8 ? 40 : devauto_race_shots) == 0) { snprintf(nm, sizeof nm, "race%d_p%04d", pc->course, pass); devauto_shot(base, nm); }
        }
        if (pass > 0) perf_frame(&pf);
        if (pc->from_menu) {                                                                    /* the end-of-race screens are done (state 26 of the HUD's state machine, docs/hud.md 7): white-out, then the next screen */
            if (!end_fade && ((hudgen_ok && hg.done) || (!hudgen_ok && drive.car[O_CTRL_BYTE] >= 8 && ++end_pass > 600))) { end_fade = 1; fade_mode(&fade, 1); }
            if (end_fade && !fade.active) break;
        }
    }
    if (pc->snd) pc->snd->call(pc->snd->ud, 0x06017548, 0, 0, 0);                                        /* the race is left: the engine idles (0x06017548) */
    dev_n = 0;
    if (trace) fclose(trace);
    perf_close();
    rcp_free(&course);
    sky_free(&sky);
    coli_free(&coli);
    phys_free(&phys);
    hud_end_free(&hend);
    free(rp);
    if (out) { out->car = drive.car; out->car_bytes = SLOT_BYTES; out->rec = drive.rec; out->rec_bytes = sizeof drive.rec; out->view = drive.g1b; out->g300 = drive.g300; memcpy(out->wall, drive.se.wall, 4); out->retire = retire; }
    return 0;
}
