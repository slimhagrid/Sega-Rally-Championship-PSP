/* Sega Rally PSP - play mode (M6): the driving core (src/drive.c), camera (src/camera.c) and part list (src/pmap.c) run at the game's 30 Hz, the renderers of M5 draw what they make.
 * Started by data/play.txt (the frame viewer of M5 runs without it).  The pad: analog nub steers, X accelerates, SQUARE brakes, TRIANGLE / CIRCLE shift up / down (manual
 * transmission), L toggles the camera view, START exits.  With "replay" the recorded inputs of an oracle trace are played instead of the pad (the verification of the whole chain:
 * the picture at pass N is compared with the reference frame the original code makes at pass N). */
#ifndef PLAY_H
#define PLAY_H
#include "render.h"
#include "car.h"
#include "event.h"
#include "hud.h"
#include "sndev.h"
#include "drive.h"
#include "camera.h"

typedef struct {
    int course;                     /* 0 DESERT 1 FOREST 2 MOUNT 3 LAKE */
    int game_type;                  /* 0 arcade, 2 practice */
    int car, mt, diff, mirror;      /* car model 0..2, manual transmission, difficulty 0..2, mirror mode */
    int setup_mode, setup[4];       /* tuned setup bytes (steering, tyres, suspension front / rear) */
    int view;                       /* camera view byte: 0 = A (in the car), 2 = B (chase) */
    int rect;                       /* picture placement: 0 = 352x224 at the top left, 1 = 4:3 fit, 2 = stretched */
    char replay[96];                /* oracle input file under data/replay/ ("accel brake axis shift" per pass), empty = the pad */
    int debug;                      /* 1 = the text line of the old debug overlay (speed, gear, rpm ...) */
    int hold;                       /* freeze the simulation after this pass (screenshots), -1 = never */
    int nopp;                       /* 1 = no opponents ("opponents 0"; replays of oracle traces made without them) */
    int from_menu;                  /* started by the menus (menu_host.c): START does nothing, the race ends after the goal / game over screens and play_run returns */
    int init_g300;                              /* [0x060AA300] before the race (DriveCfg.init_g300) */
    const unsigned char *init_r, *init_rec;     /* the console memory's R block / record buffers before the race (DriveCfg.init_r / init_rec) */
    int strip;                      /* development: "strip 1" logs the passes in which parts were left out of the picture (data/strip.txt) */
    int laps;                       /* arcade lap count (0 = 3); development: "laps N" in data/menu_race.txt */
    const SndHost *snd;             /* the sound system (NULL = silent) */
    int snd_stereo, snd_opt_vol, snd_bgm_sel;   /* [0x06040062] bit 5, [0x0604000C], [0x060AA828] */
    unsigned char init_wall[4];     /* [0x060AFE68]: the wall contact counters before the race */
    int compare;                    /* [0x060AA80E]: the compare option (DriveCfg.compare); [0x060AA184 / 188 / 18C]: the stored course record (DriveCfg.rec184 ..) */
    int rec184, rec188, rec18c;
    MemWin rec_win;                 /* DriveCfg.rec_win / rec_start: where the player's input words are recorded (the work RAM window of the menus; [0x060AAA5C]) */
    unsigned rec_start;
    int mode2;                      /* [0x060AA810] == 2: a replay of recorded inputs (DriveCfg.replay; the words are read from rec_win at rec_start): the cut-scene cameras, the replay HUD */
    CamDat camdat;                  /* CAMERA.DAT as the console's memory holds it (the replay's cameras) */
    int ghost;                      /* DriveCfg.ghost ..: the stored best run of a time attack (game type 4) raced in slot 1 */
    MemWin ghost_win;
    unsigned ghost_start;
    int ghost_car, ghost_mode, ghost_manual, ghost_setup[4], ghost_solid;
    int padrow;                     /* the key config row (types A, B, DIY = 0 .. 2 in the low nibble of [0x060AA80F]; menu_padcfg.c): from the menus the PSP pad is turned into a Saturn pad word and read through it like config_keyset does */
} PlayCfg;

typedef struct {
    Course *objects;
    CarData *cars;
    Events *events;
    Hud *hud;
    int cars_ok, events_ok, hud_ok;
    unsigned int *gu_list;          /* GE list buffer of the caller */
} PlayAssets;

/* data/play.txt: lines "course N", "type N", "car N", "mt N", "diff N", "mirror N", "setup a b c d", "view N", "rect N", "replay name", "hold N"; 0 when the file does not exist */
int  play_cfg_load(PlayCfg *pc, const char *path);

/* what a race leaves behind for the screens that follow (the menus read it as the console's memory: the car slot at 0x060B0000 with the R block at +0x500) */
typedef struct { const unsigned char *car; unsigned car_bytes; const unsigned char *rec; unsigned rec_bytes; int view; int g300; unsigned char wall[4]; int retire; } PlayOut;     /* view = the camera view byte [0x060AA01B] at the end (the next stage starts with it); retire = the pause menu's item that left the race (1 / 2), 0 = the race ended by itself */

/* runs until the user exits (START); from the menus (pc->from_menu): until the end-of-race screens are done, then returns 0 with `out` filled in.  *draw_off = the frame buffer offset of the
 * next picture (in / out: the menus draw into the same two buffers) */
int  play_run(const PlayCfg *pc, const PlayAssets *as, const char *base, PlayOut *out, int *draw_off);

#endif
