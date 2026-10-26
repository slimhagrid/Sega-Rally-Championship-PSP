/* Sega Rally PSP - the driving core (M6): the player's car physics, input, start sequence and race flow, ported by hand from the original code (docs/physics.md).
 * The car is kept as the original's 0x600-byte slot (big endian bytes, the same offsets) so that every pass can be compared byte for byte with the oracle's traces
 * (tools/test_drive.c, docs/oracle.md); tables are read by their original addresses from phys.dat (tools/conv_phys.py). */
#ifndef DRIVE_H
#define DRIVE_H
#include "coli.h"
#include "sndev.h"

#define SLOT_BYTES 0x600

typedef struct {
    unsigned char *data;                /* regions: pm_r32 etc. find a table by its address in the console's memory */
    int nregions;
    struct { unsigned addr, size; const unsigned char *p; } reg[32];
} Phys;

int  phys_load(Phys *p, const char *path);
int  phys_r32(const Phys *p, unsigned addr);                    /* a long of the console's memory image (one of the tables) */
int  phys_r16s(const Phys *p, unsigned addr);                   /* a word, sign-extended like the SH-2's mov.w */
int  phys_r8(const Phys *p, unsigned addr);                     /* a byte (unsigned) */
void fx_sincos(const Phys *ph, int turns, int *s, int *c);       /* matrix::sincos: sine and cosine (x 4, 16.16) of an angle in 16.16 turns from sincos_tbl */
void phys_free(Phys *p);

/* a window of the console's memory: the bytes of the addresses base .. base + size (the host gives the work RAM; the tests a scratch buffer) */
typedef struct { unsigned char *p; unsigned base, size; } MemWin;

/* what the menus write before a race (docs/physics.md 2.5) */
typedef struct {
    int game_type;                      /* [0x060AA860]: 0 arcade, 2 practice / time attack (single player paths only) */
    int course;                         /* [0x060AA87E]: 0 DESERT 1 FOREST 2 MOUNT 3 LAKE */
    int difficulty;                     /* [0x060AA808] 0..2 */
    int manual;                         /* [0x060AA809]: 0 = automatic, 1 = manual transmission */
    int car_type;                       /* [0x060AA80A]: 0, 1 = Celica / Lancia, 2 = the special car */
    int mirror;                         /* [0x060AA82E] */
    int setup_mode;                     /* [0x060AA811]: 1 = the setup bytes below apply */
    int setup[4];                       /* [0x060AA817..]: steering, tyres, suspension front, suspension rear */
    int laps;                           /* [0x060AA80C]: arcade lap count setting */
    int rec184, rec188, rec18c;         /* [0x060AA184 / 188 / 18C]: the stored course record the time attack's goal compares the total with (record_dataset) */
    int compare;                        /* [0x060AA80E]: the compare option the records set for the race (0 = off, 1 = best run, 2 = best run once there is one) */
    const unsigned char *init_r;        /* the R block (0x060B0500, 0x100 bytes) as the screens before the race left it: rec_dataset's best times, the previous stage's times / time left in the arcade chain.
                                           init_game_work clears the car slots 0x060B0000 .. 0x060B0500 and the other cars' areas, not the R block.  NULL = zero */
    int init_g300;                      /* [0x060AA300]: the player's rank byte of the stage before (the arcade stages after the first start with it; game type 0 course 0 sets 15, game type 2 sets 2) */
    const unsigned char *init_rec;      /* the record buffers (0x060AA400, 0x400 bytes): rec_dataset's lap sums, the lap records of the stages so far */
    MemWin rec_win;                     /* where the player's input words are recorded (R+0x74 starts at rec_start, the original's [0x060AAA5C] = 0x002CF800); p NULL = not stored */
    unsigned rec_start, rec_end;        /* [0x060AAA5C] and the end R+0x78 (0 = rec_start + 0xF7FE; the oracle's own scratch buffer when both are 0) */
    int replay;                         /* [0x060AA810] == 2: play mode 2, the player's car replays the input words recorded in rec_win (from rec_start): the result screen's "replay", a record's replay, the ending */
    int ending;                         /* with replay: the arcade chain's last stage replayed under the staff roll (ending_prepare): the ending's presentation, no sound */
    int ghost;                          /* [0x060AA815] == 4: a stored best run races in slot 1 (game type 4): myline_set */
    MemWin ghost_win;                   /* where its input words are read from (the stored replay of the stage: 0x002C0000 + stage * 0x3D00 + 104 = [0x060AAA60]) */
    unsigned ghost_start;
    int ghost_car, ghost_mode, ghost_manual, ghost_setup[4];     /* its car, set-up mode and transmission ([0x060AA813], [0x060AA816], [0x060AA812]) and tuning bytes ([0x060AA81C ..]) */
    int ghost_solid;                    /* [0x06040062] bit 3: the ghost is drawn every pass (otherwise every other pass) */
} DriveCfg;

typedef struct {
    const Phys *phys;
    const Coli *coli;
    DriveCfg cfg;
    unsigned char car[SLOT_BYTES];      /* slot 0 at 0x060B0000 */
    unsigned char dyn[0x1A0];           /* [0x060A9400 ..] the surface dynamics blocks of player 1 (0xD0 bytes; carconfig copies and tunes them) and of player 2 = the ghost (+0xD0) */
    int rp_chase;                       /* [0x060AFE7C]: the replay's chase camera instead of the cut-scene cameras (the player's part list is then used) */
    int slot1;                          /* the ghost's Drive (slot 1 at 0x060B0600): its surface dynamics pointers are +0xD0 (carControl adds it for every slot but slot 0), it plays inputs back */
    int ghost_cnt;                      /* [0x060AA182]: the passes the ghost was presented (it is drawn on every other one unless ghost_solid) */
    /* globals of the game the driving code reads and writes */
    int ret_state;                      /* [0x060AA178] countdown state, [0x060AA17C] timer */
    int cd_timer;
    int g180;                           /* [0x060AA180] */
    unsigned char g803;                 /* [0x060AA803] flag byte (bit 6 set at the countdown "ready") */
    int force_scale_base;               /* [0x060AA100] */
    int cam_mode, g1b;                  /* [0x060AA01A] camera mode (4 fly-by, 6 pause, 0 race, 2 goal orbit; also selects the length of the grid wait), [0x060AA01B] camera view (0 = A, 2 = B) */
    int g300;                           /* [0x060AA300] */
    int total_laps;                     /* [0x060AA006] */
    int g9e8;                           /* [0x060AA9E8] */
    int cd_idx, cd_size;                /* the last call of the count-down driver to dispCountDown: numeral (3, 2, 1, 0 = GO) and size (0 = nothing drawn) */
    int msg45;                          /* [0x060AAA45]: the HUD / sound message byte the race flow writes */
                                        /* (se.msg_req: the last value written since the sound system took the message: a phrase to speak) */
    int cam_ang1, cam_ang2;             /* [0x060AA9C6], [0x060AA9CC]: camera angle words calc_dispang2 reads */
    int g301, g302, g303;               /* [0x060AA301 ..]: the time attack HUD's state, flags and frame counter (re_1x1time_ft: a new best lap) */
    int hud_lapnum, hud_area;           /* free run: lap_rewrite (0x06012304) / area_rewrite (0x06012334) wanted by the new-lap / section code; the HUD draws them with these times (BCD bytes) */
    unsigned char hud_laptime[4], hud_areatime[4];
    unsigned char pause[4];             /* [0x060AA821 ..]: the pause menu: item, the item drawn last, blink counter, flags */
    int hud_arearow;                    /* the lap row index area_rewrite draws in (R+124 - 2) */
    int lap_redraw;                     /* lap + 1 of a lap time the HUD must redraw (the original calls 0x060121BE from the new-lap code when the time was corrected), 0 = none */
    unsigned char rec[0x400];           /* [0x060AA400 ..]: the split / lap record buffers the race clock fills (pointers in the R block) */
    unsigned *rng;                      /* [0x060D0050]: the game's random number state (the event objects share it); NULL = the oracle's stub that returns its own address */
    SndEv se;                           /* the sound events of the car (sndev.c); the host connects .call / .status and counts .frame */
} Drive;

#define SET_MSG(d, v) ((d)->msg45 = (d)->se.msg_req = (v))         /* a write of [0x060AAA45]: the HUD sees the value, the voice stream takes it at the next flush */

/* the input of one pass as the pad leaves it: accelerator / brake buttons, steering axis byte (127 = centre), the three view buttons */
typedef struct { int accel, brake, axis, view, shift; } DriveIn;           /* shift: +1 / -1 = shift up / down request (manual transmission) */

void drive_init(Drive *d, const Phys *phys, const Coli *coli, const DriveCfg *cfg);     /* init_game_work for the player's car: grid pose, tuning, countdown at its start */
void drive_move(Drive *d, const DriveIn *in);                                          /* slot function 0: the car's move (start grid, race, goal, game over) */
void drive_present(Drive *d);                                                          /* slot function 4: countdown driver and per-pass upkeep (the camera runs between the two) */
void drive_pass(Drive *d, const DriveIn *in);
void drive_get_block(Drive *d);                                                        /* car::get_block 0x06007EAC: the road block under the car (the replay camera's dummy car uses it at the eye) */
void drive_dummy_init(Drive *dst, const Drive *src);                                    /* the camera's dummy car: a car slot that only has a position */
void drive_calc_dispang(Drive *d);                                                     /* car::calc_dispang2 (the race start-up routine calls it with the part list builders) */
int  drive_part_limit(const Drive *d);
/* the ghost ("myline", game type 4 with a stored best run): a second Drive for slot 1.  Per pass, after the player's: drive_ghost_move (slot function 0, the move), after drive_present: drive_ghost_present
 * (slot function 4: -1 = not presented yet, else the mode dispCar is called with: 0 = drawn, 3 = this pass not) and drive_p2sync (0x060067A4: starts the ghost when the count-down ends) */
void drive_ghost_init(Drive *g, const Drive *player);                                  /* game_init's myline_set + carconfig's second player */
void drive_ghost_move(Drive *g, const Drive *player);
int  drive_ghost_present(Drive *g);
void drive_p2sync(Drive *player, Drive *g);                                                 /* the goal presentation (0x060068E0) cuts the part list to 5 blocks; 16 otherwise */                                          /* one 30 Hz pass: slot function 0 (move) then slot function 4 (countdown / presentation) */

#endif
