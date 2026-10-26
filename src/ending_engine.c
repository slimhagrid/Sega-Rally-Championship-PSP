/* Sega Rally PSP - the race engine of the ending: the arcade chain's last stage replayed under the staff roll.
 * menu_ending.c is the original's own code on the console's memory (ending_init / ending_main / the cut-scene camera / the staff roll) and calls the race engine through engine(addr) at the
 * points where the original calls its routines.  This file binds those calls to the native engine: the player's car (src/drive.c, play mode 2, game type 0: the recorded input words of the stage are
 * read from the work RAM at [0x060B0574]), the rival cars (src/opp.c) and, for the chase camera, src/camera.c.  The slots live here as the engine's own byte arrays (the same layout as the console's
 * memory) and are copied into the console's memory after every call, because the original's code around them (the camera, sync_ctrl, the road block of the camera's dummy car) reads them there.
 * Verified: tools/oracle_ending.sh (the replay of a recorded lake race with the rivals: the car slot, the rivals, the cut-scene camera and its part list after every pass, tools/test_drive.sh);
 * the copies and this dispatch are not compared with the oracle. */
#include <stdio.h>
#include <string.h>
#include "ending_engine.h"
#include "menu_ending.h"
#include "drive.h"
#include "opp.h"
#include "camera.h"
#include "car.h"
#include "fx.h"
#include "sat.h"

#define CAR 0x060B0000u
static inline float fx16(int v) { return (float)v * (1.0f / 65536.0f); }

static char base[256];
static Phys phys;
static Coli coli;
static int data_ok, data_tried;
static Drive ed;
static Opp eo;
static Camera ecam;
static int started, rival_pass;

void ending_engine_set_base(const char *b) { snprintf(base, sizeof base, "%s", b); data_tried = data_ok = 0; }

static int load_data(int course)
{
    static const char *const cn[4] = { "desert", "forest", "mount", "lake" };
    char p[300];
    if (data_tried) return data_ok;
    data_tried = 1;
    snprintf(p, sizeof p, "%s/course/phys.dat", base);
    if (phys_load(&phys, p)) return 0;
    snprintf(p, sizeof p, "%s/course/%s.rct", base, cn[course & 3]);
    if (coli_load(&coli, p)) { phys_free(&phys); return 0; }
    data_ok = 1;
    return 1;
}

int ending_engine_ready(void) { return started; }

static void mirror(void)                                /* the engine's slots into the console's memory */
{
    int k;
    memcpy(sat_ptr(CAR), ed.car, SLOT_BYTES);
    for (k = 0; k < OPP_SLOTS; k++) memcpy(sat_ptr(OPP_SLOT_ADDR(k)), eo.car[k], OPP_SLOT_BYTES);
}

static void sync_vis(void)                              /* the part list the last get_poly_map / calc_camera_pmap left ([0x060AA002] blocks at 0x060AA140): get_other_map reads it */
{
    int n = RS16(0x060AA002), k;
    if (n > 16) n = 16;
    if (n < 0) n = 0;
    for (k = 0; k < 16; k++) eo.vis[k] = k < n ? R8(0x060AA140u + (u32)k) : 0;
    eo.nvis = n;
}

static void start(void)                                 /* lap_time_init (0x06014270), called by init_game_work: the player's car of the replay */
{
    DriveCfg cfg;
    int k;
    started = 0;
    if (!load_data(RS16(0x060AA87E) & 3)) return;
    memset(&cfg, 0, sizeof cfg);
    cfg.game_type = RS16(0x060AA860);
    cfg.course = RS16(0x060AA87E) & 3;
    cfg.difficulty = R8(0x060AA808);
    cfg.manual = R8(0x060AA809);
    cfg.car_type = R8(0x060AA80A);
    cfg.mirror = R8(0x060AA82E);
    cfg.setup_mode = R8(0x060AA811) == 1;
    for (k = 0; k < 4; k++) cfg.setup[k] = R8(0x060AA817u + (u32)k);
    cfg.laps = R8(0x060AA80C) ? R8(0x060AA80C) : 3;
    cfg.init_g300 = R8(0x060AA300);
    cfg.init_r = sat_ptr(0x060B0500);                   /* the R block: the record pointers of the stage ([0x060B0574] = the start of the stage's recorded words) */
    cfg.init_rec = sat_ptr(0x060AA400);
    cfg.replay = 1;
    cfg.ending = 1;
    cfg.rec_win.p = sat_ptr(0x00200000); cfg.rec_win.base = 0x00200000u; cfg.rec_win.size = 0x100000u;
    drive_init(&ed, &phys, &coli, &cfg);
    ed.g1b = R8(0x060AA01B);
    opp_init(&eo);
    camera_init(&ecam);
    rival_pass = 0;
    started = 1;
    mirror();
}

static void player_move(void)                           /* slot 0's state function (normal_plinit .. goal_plmove) */
{
    DriveIn in0;
    memset(&in0, 0, sizeof in0);
    in0.axis = 127;
    drive_move(&ed, &in0);
    rival_pass = 0;
    mirror();
}

static void rivals_move(void)                           /* the other cars' state functions, once per pass whichever slot asks first */
{
    if (rival_pass) return;
    rival_pass = 1;
    sync_vis();
    opp_move(&eo, &ed);
    mirror();
}

static void chase_camera(void)                          /* normal_ca / replay_ca: the chase view's eye and angle words go to the camera work (CAMW 0x060AA9B0); the original code goes on from there */
{
    unsigned char *w = ecam.w;
    int k;
    for (k = 0; k < CAM_WORK_BYTES; k++) w[k] = R8(0x060AA9B0u + (u32)k);
    camera_chase_pose(&ecam, &ed);
    for (k = 0; k < 0x40; k++) W8(0x060AA9B0u + (u32)k, w[k]);
    ending_chase_done();
}

void ending_real_engine(u32 addr)
{
    if (addr == 0xFFFF0001u) {                                                       /* the ending is over: the physics tables and the collision map go */
        if (data_ok) { coli_free(&coli); phys_free(&phys); }
        data_ok = data_tried = started = 0;
        return;
    }
    if (!started && addr != 0x06014270u) { ending_ghost_engine(addr); return; }        /* no course data: the stand-in */
    switch (addr) {
    case 0x06014270: start(); if (!started) ending_ghost_engine(addr); break;
    case 0x06009938: break;                                                         /* carconfig: drive_init did it */
    case 0x06005E84: player_move(); rivals_move(); break;                           /* car_move_ctrl: every slot's state function */
    case 0x06006214: opp_spawn(&eo, &ed); mirror(); break;                           /* normal_otinit */
    case 0x06006668:                                                                 /* calc_pmap_sub: the player's road blocks, the blocks around them */
        drive_calc_dispang(&ed);
        sync_vis();
        opp_map(&eo, &ed);
        mirror();
        break;
    case 0x0600CAD0: sync_vis(); opp_other_ctrl(&eo, &ed); mirror(); break;         /* otherCtrl */
    case 0x0601BCB8: case 0x0601D410: break;                                         /* the scenery objects, the tyre marks: the host draws the objects with the photo scene */
    case 0x0600FAF0: case 0x0600FC5C: chase_camera(); break;
    case 0x06006030: case 0x06006228: case 0x060062F4: case 0x0600640C: case 0x06006470: case 0x060063F0:
        player_move();
        break;
    case 0x060066F8: case 0x06006848: case 0x060068E0:                               /* the presentation: the count-down, dispCar, get_other_map */
        drive_present(&ed);
        sync_vis();
        opp_map(&eo, &ed);
        mirror();
        ending_present_car();
        break;
    case 0x0600C958: rivals_move(); break;                                           /* a rival's first frame / exec_other_car */
    default:
        if ((addr & ~1u) == 0x06009E8C) rivals_move();
        break;                                                                       /* normal_otmt / normal_otmtn (drawn by the host) and the rest */
    }
}

int ending_rival_items(const float *view, SceneItem *out, int max, const CarData *cars, const Course *objects)
{
    int n = 0, k, j;
    if (!started || !cars || !cars->file || !objects) return 0;
    for (k = 0; k < OPP_SLOTS && n < max - 32; k++) {
        CarState cs;
        const unsigned char *o = eo.car[k];
        if (!opp_visible(&eo, k)) continue;
        for (j = 0; j < 3; j++) { cs.ang[j] = fx16(g32(o, 0x0C + 4 * j)); cs.pos[j] = fx16(g32(o, 0x18 + 4 * j)); }
        cs.steer = fx16(g32(o, 0x30)); cs.spin = fx16(g32(o, 0x38));
        for (j = 0; j < 4; j++) { cs.susp[j] = fx16(g32(o, 0x3C + 4 * j)); cs.ground[j] = fx16(g32(o, 0x4C + 4 * j)); }
        cs.type = o[0x5C]; cs.brake = o[0x5D]; cs.damage = g16u(o, 0xB4);
        n += car_display(cars, &cs, 0, view, objects, out + n, 32);
    }
    return n;
}
