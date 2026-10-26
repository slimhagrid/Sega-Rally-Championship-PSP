/* Sega Rally PSP - the visible part list, see pmap.h. */
#include "pmap.h"

int pmap_build(const unsigned char *vis, const unsigned short *npolys, int block, int nibbles, int sector, int budget, unsigned char *out)
{
    unsigned off = (unsigned)vis[4 * (sector & 15)] | (unsigned)vis[4 * (sector & 15) + 1] << 8 | (unsigned)vis[4 * (sector & 15) + 2] << 16 | (unsigned)vis[4 * (sector & 15) + 3] << 24;
    const unsigned char *list = vis + off;
    int flag = list[0];
    unsigned left = flag == 0 ? (unsigned)(nibbles & 0xF) : (unsigned)((nibbles >> 4) & 0xF);       /* a count of 0 counts down through zero: the whole list (do ... while (--n)) */
    int base = (block & 0xFF) << 2, n = 0, poly = 0;
    const unsigned char *p = list + 1;
    int off_b = (signed char)*p++;
    for (;;) {
        int part = ((off_b + base) & 0x1FC) >> 2;
        poly += npolys[part];
        if (poly > budget) break;
        out[n++] = (unsigned char)part;
        off_b = (signed char)*p++;
        if (off_b == -1) break;
        if (--left == 0) break;
        if (n >= 16) break;
    }
    return n;
}
