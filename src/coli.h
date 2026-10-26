/* Sega Rally PSP - course collision map (M5 helicopter shadow; the driving core in M6 uses the same lookups).
 * Data: tools/conv_track.py -> <course>.rct.  Ports of get_coli_area (0x06008008 / FUN_060080AA), get_y (0x0600802C) and get_coli3 (0x060083EC), docs/collision_world_ai.md 1.4 / 1.5. */
#ifndef COLI_H
#define COLI_H

typedef struct {
    unsigned char *file;
    unsigned int npoly, off_poly, off_cgrid, off_pool;
    unsigned int off_quad, off_slice, off_bgrid, off_b2s;                 /* block quads, slices, block grid, block -> slice lists (get_block) */
} Coli;

int  coli_load(Coli *c, const char *rct_path);
void coli_free(Coli *c);
int  coli_adopt(Coli *c, unsigned char *file);                 /* an RCT1 image already in memory (a generated course): c owns the buffer */

/* get_coli_area: the collision polygon containing the point, positions in "11.20" (metres * 2^20 = 16.16 << 4); NULL when there is none */
const unsigned char *coli_find(const Coli *c, int x20, int z20);

/* get_y (floor polygons): the ground height at (x, z) in 2^20 units */
int  coli_y20(const unsigned char *poly, int x20, int z20);

/* get_block (0x06007EAC): the position (16.16 metres) -> the block quad containing it (36-byte record: u8 a, b, a2, id, 4 x (x, z)), then the slice (road section, 44-byte record:
 * u8 attr, section, 0, id, 4 x (x, z), direction x, z) of that block; NULL when none is found (the original traps) */
const unsigned char *coli_find_quad(const Coli *c, int x16, int z16);
const unsigned char *coli_find_slice(const Coli *c, int quad_id, int x16, int z16);

/* get_d: how deep the point is behind a wall polygon (surface type 0) */
int  coli_wall_d(const unsigned char *poly, int x20, int z20);

/* get_coli3 for an object at (x, y, z) in 16.16 metres: finds the floor polygon below it.  Returns the polygon's surface type (0 = none or a wall); then *alt = y - ground height
 * (16.16) and normal[3] = the polygon normal (8.24).  Exactly the original's fixed-point steps. */
int  coli_ground(const Coli *c, int x, int y, int z, int *alt, int normal[3]);

/* get_coli2 (0x060083B4): the polygon under / around the point (x, y, z) in 16.16 metres.  Returns its surface type (0 = wall or none); normal[3] = the polygon normal (8.24);
 * *h = for a wall (type 0) how deep the point is behind it (get_d, 2^20 units), for a floor the ground height minus 16 * y (2^20 units) */
int  coli_get2(const Coli *c, int x, int y, int z, int normal[3], int *h);

#endif
