/* Sega Rally PSP - car display (M5): what dispcar::dispCar of the original draws for one car (docs/render_pipeline.md 3.5, NOTES.md M5).
 *
 * A car is a group of parts of OBJECT.MDL: body, brake light (off / on), shadow, four tyres.  Depending on the view depth of the car (112 / 128 / 200 m) and its damage
 * state one of four descriptors is used; the tiers differ in what is drawn (near: body + brake light + tyres with steering and wheel spin + shadow that follows the ground
 * under the four wheels; middle: simplified `_s` body + tyres with steering; far: `_s` body + tyres at the mean suspension height; beyond 200 m nothing).
 * Data: tools/conv_cars.py -> cars.dat (descriptors, wheel positions, thresholds).  The output is a list of SceneItems for scene_draw().
 */
#ifndef CAR_H
#define CAR_H
#include "render.h"

typedef struct {
    short body, shadow, br_off, br_on, tyre[4];     /* part numbers in object.rcp, -1 = none */
    float wheel[4][3];                              /* wheel centres in car space (m): front left, front right, rear left, rear right */
} CarDesc;

typedef struct {
    unsigned char *file;
    int ntypes;
    float thr[3];                                   /* tier thresholds (view depth, m) */
    float shadow_dy, k_pitch, k_roll;
    const CarDesc *desc;                            /* [type * 4 + variant]: near normal, near damaged, far normal, far damaged */
} CarData;

/* the fields of the game's car structure that the display reads (units as in the game: metres, radians) */
typedef struct {
    float ang[3];                                   /* +0x0C, +0x10, +0x14: pitch (x), yaw (y), roll (z) */
    float pos[3];                                   /* +0x18..+0x20 world position */
    float steer;                                    /* +0x30 front wheel steering angle */
    float spin;                                     /* +0x38 wheel rotation */
    float susp[4];                                  /* +0x3C..+0x48 suspension offsets */
    float ground[4];                                /* +0x4C..+0x58 ground height under each wheel */
    int type;                                       /* +0x5C car model (0 Celica, 1 Delta, 2 Stratos for the player, 2..9 opponents are enemy bodies on Celica / Delta wheels) */
    int brake;                                      /* +0x5D brake light on */
    int damage;                                     /* +0xB4 damage word (4 nibbles) */
} CarState;

int  cars_load(CarData *d, const char *path);
void cars_free(CarData *d);

/* mode: 0 / 2 = drawn through the slave queue, 1 = direct, 3 = statistics only (nothing drawn); mode 2 uses the damaged models.
 * view: the camera matrix (3x4, row-major).  Appends up to `max` items to out, returns the number written. */
int  car_tier(const CarData *d, const CarState *c, const float *view);       /* detail tier the car display picks: 0 near, 1 middle, 2 far, -1 not drawn (off screen / too far); the spray spawner runs in tiers 0 and 1 */
int  car_display(const CarData *d, const CarState *c, int mode, const float *view, const Course *objects, SceneItem *out, int max);

#endif
