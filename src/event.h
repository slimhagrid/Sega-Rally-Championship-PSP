/* Sega Rally PSP - event objects (M5): spectators, check-point gates, poles, banners, flags, the start object, birds, spray ... what the original drives from
 * EVENTTBL.MAD through the slot scheduler at 0x0601BAF4 / 0x0601BCB8 (NOTES.md M5, docs/render_pipeline.md 9).
 *
 * Every object of a course is a 64-byte slot (the original's layout, so the per-type state machines translate one to one: +0x00 state, +0x0C parity word, +0x0E / +0x0F bytes,
 * +0x10 parameter long (hi word = part index, lo word = flip / variant), +0x14 / +0x18 / +0x1C position x, y, z (16.16 m), +0x24 angle (65536 = one turn), +0x2C part,
 * +0x30.. state).  Each frame the dispatcher visits the slots of the visible map blocks (odd slots first, then even slots, as the slave and master CPU did) and every state function
 * draws its model through the pipeline: ev_frame returns SceneItems for scene_draw().  Data: tools/conv_events.py -> events.dat; parts are looked up by name in object.names.
 */
#ifndef EVENT_H
#define EVENT_H
#include "render.h"
#include "coli.h"

#define EV_MAX_SLOTS 1024

typedef struct {
    unsigned char m[64];
} EvSlot;

/* what the object code reads from the rest of the game (all in game units) */
typedef struct {
    float view[12];                 /* camera matrix (3x4, row-major) */
    int game_type;                  /* [0x060AA860]: 0 arcade 1P, 2 ..., 6 / 10 versus */
    int side;                       /* set by ev_frame: [0x060D0114] = (car byte +0xC5 == 0) */
    int car_byte_c5;                /* [0x060B00C5] */
    int car_seg;                    /* [0x060B0060] word: current track segment */
    int car_c8, car_ca;             /* [0x060B00C8] / [0x060B00CA] words */
    int car_x, car_y, car_z;        /* [0x060B0018..0x20] 16.16 */
    int car_yaw;                    /* [0x060B0010] 16.16 radians */
    int nseg_word;                  /* [0x060AA006] */
    int view_fx[12];                /* the same matrix as the game holds it (16.16 longs, 0x060D1000): the sound code works on it with fixed point */
    int car_state;                  /* [0x060B000A] word: 0 = on the grid */
    int mode810;                    /* [0x060AA810]: 0 race, 2 replay ... (the sounds of the objects are off in a replay) */
    int push_x, push_z;             /* out: what the objects add to the car's push (+0x64 / +0x6C) this pass: the start object keeps the car out of its box */
} EvWorld;

typedef struct {                    /* keyframe animation (FUN_0601CE8C): n frames x nparts x 20 bytes {translation 3 x i32, rotation z, y, x as i16 turns} */
    int n, nparts;
    const unsigned char *data;
    short parts[25];
} EvAnim;

typedef struct {                    /* the helicopter object (0x060EFE00, desert only) and its rotor-dust particles (0x060D2000: 32 x {size, x, y, z}) */
    unsigned char m[512];           /* big-endian, the original's field offsets (docs/events.md section 7) */
    unsigned char dust[32 * 16];
    int table;                      /* 0: game type 0, 1: game type 2 */
} EvHeli;

typedef struct {
    unsigned char *file;
    const unsigned char *blocks;    /* per course: 128 x {first, count} */
    const unsigned char *recs;      /* 28-byte records */
    int nobjects;
    EvSlot slots[EV_MAX_SLOTS];
    int block_first[128], block_count[128];
    unsigned int rng;               /* 0x060D0050 */
    unsigned char flag[3];          /* 0x060D0100..0x060D0102 */
    short flag_anim[2][32];         /* FlagAnimation1 / 2: part numbers in animation order */
    EvAnim anim[7];                 /* bird1 body 21, bird1 body 32, bird1 wings, bird2, elephant, zebra body, zebra tail */
    int parts[120];                  /* object.rcp part numbers by EvPart */
    const Course *objects;
    const unsigned char *motion;    /* ANIMAL.MOT block (animations and the helicopter's flight paths) */
    EvHeli heli;
    const Coli *coli;               /* ground under the helicopter (set by ev_set_collision) */
    int cur_slot;                   /* slot being executed (debug: slot_of[] tells which slot produced each output item) */
    short slot_of[512];
    struct { int n; int kind[4]; int val[4]; } snd;   /* out of ev_frame: the calls of the sound library the objects made, in order (kind 0: FUN_06016FBC play id val, kind 1: FUN_0601704C word val) */
} Events;

int  ev_load(Events *e, const char *events_dat, const char *names_txt, const Course *objects);
void ev_free(Events *e);
int  ev_part_sand(const Events *e);                                   /* OBJECT.MDL part numbers of the spray sprites (sand_poly / water_poly), -1 if the name list lacks them */
int  ev_part_water(const Events *e);
void ev_set_collision(Events *e, const Coli *c);
void ev_set_heli(Events *e, const unsigned char *m512, const unsigned char *dust512);   /* the helicopter object and dust list as recorded from the original */
void ev_start(Events *e, int course, int game_type);                 /* race start: build the slots (FUN_0601BF18 + FUN_0601DC14) */
int  ev_frame(Events *e, EvWorld *w, const unsigned char *blocks, int nblocks, SceneItem *out, int max);

/* the VDP1 sprites of the HUD (dispTachoMeter 0x06010680, dispCountDown 0x06010710): rpm = the car's engine word (+0x8C), or -1 for no tachometer; cd_idx / cd_scale = the
 * count-down numeral (0 GO, 1 .. 3) and its size parameter, cd_idx -1 for none.  Returns the number of items written. */
int  ev_hud(Events *e, int rpm, int cd_idx, int cd_scale, SceneItem *out, int max);

#endif
