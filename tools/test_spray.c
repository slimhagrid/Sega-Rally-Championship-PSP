/* Host test of src/spray.c against the oracle (tools/spvec.py): replays the vector through spray_spawn() / spray_frame() and compares, after every pass, the car's work fields
 * +0x3C0 .. +0x3E3, the whole list (header, pointers, 28 objects) after the spawner and after the list run, and the splash draws of the run.
 *   gcc -O1 -fwrapv -I src tools/test_spray.c src/spray.c src/drive.c src/coli.c -lm -o build/host/test_spray && build/host/test_spray extracted/psp/course extracted/oracle/sp_0_0.vec [-v] */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spray.h"
#include "drive.h"

static unsigned char *slurp(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = (unsigned char *)malloc((size_t)*n + 16);
    if (fread(b, 1, (size_t)*n, f) != (size_t)*n) return NULL;
    fclose(f);
    return b;
}
static unsigned le32(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24; }

static void first_diff(const char *what, unsigned pass, const unsigned char *got, const unsigned char *want, int n)
{
    int shown = 0;
    printf("pass %u: %s differs:", pass, what);
    for (int i = 0; i < n && shown < 8; i++) if (got[i] != want[i]) { printf(" +0x%03x got %02x want %02x;", i, got[i], want[i]); shown++; }
    printf("\n");
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: test_spray course_dir sp.vec [-v]\n"); return 2; }
    int verbose = argc > 3;
    static Phys phys;
    char path[512];
    snprintf(path, sizeof path, "%s/phys.dat", argv[1]);
    if (phys_load(&phys, path)) { fprintf(stderr, "cannot load %s\n", path); return 2; }
    long n = 0;
    unsigned char *v = slurp(argv[2], &n);
    if (!v || memcmp(v, "SVEC", 4)) { fprintf(stderr, "cannot read %s\n", argv[2]); return 2; }
    unsigned nframes = le32(v + 4);
    const unsigned char *p = v + 16;
    static Drive d;
    static Spray sp;
    d.phys = &phys;
    spray_init(&sp, 0x101);
    unsigned rng = 0;
    unsigned bad_spawn = 0, bad_list = 0, bad_draw = 0, bad_run = 0, first = 0, spawns = 0, draws = 0;
    static SceneItem out[64];
    for (unsigned fr = 0; fr < nframes; fr++) {
        unsigned spawned = le32(p), rng_in = le32(p + 4);
        p += 8;
        if (spawned) {
            const unsigned char *car = p, *want_out = p + 0x400, *want_pool = p + 0x400 + 0x24;
            p += 0x400 + 0x24 + SPRAY_BYTES;
            memcpy(d.car, car, 0x400);
            rng = rng_in;
            spray_spawn(&sp, &d, &rng);
            spawns++;
            if (memcmp(d.car + 0x3C0, want_out, 0x24)) { if (!bad_spawn++) first = fr; if (bad_spawn <= 4 || verbose) first_diff("car work fields (+0x3C0)", fr, d.car + 0x3C0, want_out, 0x24); }
            else if (memcmp(sp.m, want_pool, SPRAY_BYTES)) { if (!bad_list++ && !bad_spawn) first = fr; if (bad_list <= 4 || verbose) first_diff("list after the spawner", fr, sp.m, want_pool, SPRAY_BYTES); }
            else if (memcmp(sp.m, want_pool, SPRAY_BYTES)) bad_list++;
            memcpy(sp.m, want_pool, SPRAY_BYTES);                                    /* resynchronise so one slip does not hide the rest */
        }
        float view[12];
        for (int k = 0; k < 12; k++) view[k] = (float)(int)le32(p + 4 * k) * (1.0f / 65536.0f);
        p += 48;
        unsigned ran = le32(p), nd = le32(p + 4);
        p += 8;
        const unsigned char *dr = p;
        p += 20 * nd;
        if (ran) {
            int cnt = spray_frame(&sp, view, NULL, 148, 149, out, 64);
            const unsigned char *want_pool = p;
            p += SPRAY_BYTES;
            int ok = cnt == (int)nd;
            for (int i = 0; ok && i < cnt; i++) {
                int part = (int)le32(dr + 20 * i), r6 = (int)le32(dr + 20 * i + 4);
                if (part != out[i].part) ok = 0;
                if (fabsf(out[i].m[5] * 65536.0f - (float)r6) > 1.0f) ok = 0;
                for (int k = 0; k < 3; k++) if (fabsf(out[i].m[4 * k + 3] - (float)(int)le32(dr + 20 * i + 8 + 4 * k) * (1.0f / 65536.0f)) > 0.02f) ok = 0;
            }
            draws += nd;
            if (!ok) { if (!bad_draw++) first = fr; if (bad_draw <= 4 || verbose) printf("pass %u: expected %u splash draws, got %d\n", fr, nd, cnt); }
            if (memcmp(sp.m, want_pool, SPRAY_BYTES)) { if (!bad_run++ && !bad_spawn && !bad_list && !bad_draw) first = fr; if (bad_run <= 4 || verbose) first_diff("list after the run", fr, sp.m, want_pool, SPRAY_BYTES); }
            memcpy(sp.m, want_pool, SPRAY_BYTES);
        }
    }
    printf("%u passes, %u spawner runs, %u splash draws: spawner fields %u, list after spawn %u, draws %u, list after run %u differ (first at pass %u)\n", nframes, spawns, draws,
           bad_spawn, bad_list, bad_draw, bad_run, first);
    return bad_spawn || bad_list || bad_draw || bad_run ? 1 : 0;
}
