/* M8: time the software renderer on an oracle trace tick: bench_vdp trace.mtr tick iterations */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>
#include "../src/sat.h"
#include "../src/vdpr.h"

static unsigned char *tr;
static size_t trpos;
static u32 rd32(void) { u32 v = tr[trpos] | (tr[trpos + 1] << 8) | (tr[trpos + 2] << 16) | ((u32)tr[trpos + 3] << 24); trpos += 4; return v; }

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    unsigned char *raw;
    long rawlen;
    z_stream z;
    u32 k, nreg, ntick, tick, want = (u32)atoi(argv[2]), np, i;
    u32 raddr[16], rsize[16];
    static VdpFrame fr;
    int it, iters = atoi(argv[3]);
    clock_t t0;
    fseek(f, 0, SEEK_END); rawlen = ftell(f); fseek(f, 0, SEEK_SET);
    raw = (unsigned char *)malloc(rawlen);
    if (fread(raw, 1, rawlen, f) != (size_t)rawlen) return 1;
    memset(&z, 0, sizeof(z));
    inflateInit(&z);
    tr = (unsigned char *)malloc(256u << 20);
    z.next_in = raw + 4; z.avail_in = (uInt)(rawlen - 4); z.next_out = tr; z.avail_out = 256u << 20;
    inflate(&z, Z_FINISH);
    sat_init("extracted/psp");
    rd32();
    np = rd32();
    for (i = 0; i < np; i++) { u32 a = rd32(), n = rd32(); u32 j; for (j = 0; j < n; j++) W8(a + j, tr[trpos + j]); trpos += n; }
    nreg = rd32();
    for (k = 0; k < nreg; k++) { raddr[k] = rd32(); rsize[k] = rd32(); }
    for (k = 0; k < nreg; k++) { memcpy(sat_ptr(raddr[k]), tr + trpos, rsize[k]); trpos += rsize[k]; }
    ntick = rd32();
    for (tick = 0; tick < ntick && tick <= want; tick++) {
        u32 nch, c;
        trpos += 8;
        nch = rd32();
        for (c = 0; c < nch; c++) { u32 a = rd32(), n = rd32(); memcpy(sat_ptr(a), tr + trpos, n); trpos += n; }
    }
    t0 = clock();
    for (it = 0; it < iters; it++) vdp_render_frame(&fr);
    printf("%.2f ms per frame\n", 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC / iters);
    return 0;
}
