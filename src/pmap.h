/* Sega Rally PSP - the list of course parts to draw (car::get_poly_map 0x060081B8, docs/collision_world_ai.md 1.6): from the car's road block, the block-count nibbles and the
 * view sector, a list of up to 16 blocks, nearest first, cut off at a polygon budget.  Data: the `vis` blob and the per-part polygon counts of the course pack (.rcp). */
#ifndef PMAP_H
#define PMAP_H

#define PMAP_BUDGET 1500                /* polygons per frame, single player */

/* vis: the 16 lists (tools/conv_parts.py), npolys: polygon count of each of the 128 parts, block: car byte +0xF0, nibbles: car byte +0xF1 (A low, B high), sector: car byte +0xC7.
 * Returns the number of blocks written to out[16] (the original's 0x060AA002 / 0x060AA140). */
int pmap_build(const unsigned char *vis, const unsigned short *npolys, int block, int nibbles, int sector, int budget, unsigned char *out);

#endif
