/* Sega Rally PSP - the dust / spray objects of the car (M6): FUN_0601D0E6 spawns them from the car's wheel contact data, 0x0601D42A draws and ages the list (docs/events.md section 8).
 * The list is the original's (header + 28 object pointers + 28 free pointers + 28 objects of 64 bytes, big endian) so the pool order, the free stack and the stale fields behave
 * like the console's; checked against the oracle (tools/spvec.py, tools/test_spray.c): the whole list after every pass. */
#ifndef SPRAY_H
#define SPRAY_H
#include "drive.h"
#include "render.h"

#define SPRAY_BYTES 0x7F8            /* 0x060D0140 header (0x18: count, free count, enable bytes, 3 pointers), 0x060D3000 active (28 x 4), 0x060D3100 free (28 x 4), 0x060D3200 objects (28 x 64) */

typedef struct {
    unsigned char m[SPRAY_BYTES];
} Spray;

void spray_init(Spray *s, unsigned enable);                               /* 0x0601D074 (race start): enable = one byte per wheel, 0x101 = the rear wheels in the single-player game */
void spray_spawn(Spray *s, Drive *d, unsigned *rng);                      /* FUN_0601D0E6, called by the car display in its two near tiers: up to one object per enabled wheel; writes car +0x3C0 .. +0x3E3 */
void spray_spawn_car(Spray *s, const Phys *ph, unsigned char *car, unsigned *rng);       /* the same for any car slot (the other cars' displays call the spawner too) */
int  spray_frame(Spray *s, const float view[12], const Course *objects, int part_sand, int part_water, SceneItem *out, int max);   /* 0x0601D42A: draw (last object first) and age */

#endif
