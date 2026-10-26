/* Development aids for unattended test runs in PPSSPP (data/autoplay.txt, src/devauto_menu.c): a pilot that drives the player's car along the rivals' guide line (the oracle's `Pilot`,
 * tools/oracle.py) and a screenshot writer.  Nothing here is used without data/autoplay.txt. */
#ifndef DEVAUTO_H
#define DEVAUTO_H
#include "drive.h"

#define DEVAUTO_NGUIDE 300

typedef struct {
    float x[DEVAUTO_NGUIDE], z[DEVAUTO_NGUIDE];
    int near, cool;
    float vmax, curve_k, steer_k;
} DevPilot;

extern int   devauto_pilot_mode;        /* 0: the pad, 1: drive the guide line, 2: no input at all (the clock runs out) */
extern float devauto_vmax;              /* top speed in metres per pass (2.1 = 227 km/h) */
extern float devauto_steer_k, devauto_look;   /* steering gain (1.6) and a factor on both look-ahead distances (1.0) */
extern float devauto_curve_k;           /* how much the corners slow the pilot down (1.0 = the oracle's pilot) */
extern int   devauto_turbo;             /* 1: the races run without waiting for the V-blank (PPSSPP then runs them as fast as it can) */
extern int   devauto_race_shots;        /* a screenshot every N passes of a race (0 = none); one more whenever the car's control byte changes */

extern int   devauto_cheat_time, devauto_cheat_rank;      /* test cheats: the time limit never runs below 20 s; the rank at the goal is forced (1 = the winner) */
void devauto_cheats(Drive *d);                              /* once per pass of a race, after the opponents' pass */
void devauto_pilot_init(DevPilot *p, const Phys *ph, int course);
void devauto_pilot(DevPilot *p, const unsigned char *car, DriveIn *in);

/* a screenshot of the PPSSPP window, taken by the test runner (the game waits for it): data/shot.req gets the file name NNNN_<name> (NNNN counts up through the run) */
void devauto_shot(const char *base, const char *name);

#endif
