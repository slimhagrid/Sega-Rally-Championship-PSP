/* Sega Rally PSP - the other cars (M7): the rally opponents of the arcade and practice modes, ported by hand from the original code
 * (docs/collision_world_ai.md section 4: otherctrl, othercar, othersub, otherset, copro, carcoli) and checked byte for byte against the oracle (tools/test_opp.sh).
 * Every car is the original's 0x400-byte slot as big-endian bytes at the original offsets; the cars point at each other by the console's addresses (0x060B0000 = the player). */
#ifndef OPP_H
#define OPP_H
#include "drive.h"

#define OPP_SLOTS 8
#define OPP_SLOT_BYTES 0x400
#define OPP_PLAYER_ADDR 0x060B0000u
#define OPP_SLOT_ADDR(k) (0x060B0600u + 0x400u * (unsigned)(k))

typedef struct {
    unsigned char car[OPP_SLOTS][OPP_SLOT_BYTES];       /* 0x060B0600 + k * 0x400 */
    int ncars;                                          /* [0x060B02B0] the number of other cars */
    int w2b2;                                           /* [0x060B02B2] */
    int progress;                                       /* [0x060B02B6] the player's progress word (block + lap * 128) */
    unsigned guide;                                     /* [0x060B02B8] the guide table of the course (console address) */
    unsigned list[16];                                  /* [0x060B07A0] the sorted car list (console addresses), + the two wrap-around entries */
    int frame;                                          /* [0x060B07E0] the pass counter of otherCtrl */
    int ghost;                                          /* [0x060AA82D] */
    unsigned char otmap[16];                            /* [0x060AA154] get_other_map: the block ids around the player */
    int notmap;                                         /* [0x060AA01C] */
    unsigned char vis[16];                              /* [0x060AA140] the blocks of the part list (get_poly_map) the draw handler tests */
    int nvis;                                           /* [0x060AA002] */
} Opp;

/* the oracle's per-pass record of the other cars: tools/oracle.py opp_snapshot */
#define OPP_CMP_BYTES 0x200

extern unsigned opp_stats[4];                           /* contacts seen since the start: player vs other car (hitCheck), practice rival (hitCheck2), other car vs other car, wall corners (otrCarColi) - for the tests */

void opp_init(Opp *o);
/* normal_otinit (initOtherGP / initOtherPR): spawn the other cars.  rank: the player's start rank byte [0x060B00B6] */
void opp_spawn(Opp *o, Drive *d);
/* one pass of game_ctrl's loop over the other cars: every slot's handler [+0] and [+4], then sync_ctrl / otherCtrl.  `vis` / `nvis`: get_poly_map's block list of this pass */
void opp_pass(Opp *o, Drive *d);
void opp_move(Opp *o, Drive *d);                        /* the slot handlers [+0] of the other cars (car_move_ctrl / game_ctrl's loop) */
void opp_other_ctrl(Opp *o, Drive *d);                 /* otherCtrl alone (the start-up routine runs it once) */
int  opp_visible(const Opp *o, int k);                 /* the draw handler [+4] of car k is normal_otmt: the car is drawn this pass (it exists and its block is in the part list) */
void opp_map(Opp *o, const Drive *d);                  /* get_other_map: the ten blocks around the player */

#endif
