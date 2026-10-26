/* Host test of src/car.c against the oracle: replays tools/cartest.py's vector (what the game's own dispCar drew) through car_display().
 *   gcc -O1 -I src tools/test_car.c src/car.c -lm -o build/host/test_car && build/host/test_car extracted/psp/course/cars.dat extracted/oracle/cardisp.vec
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "car.h"

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: test_car cars.dat cardisp.vec\n"); return 2; }
    CarData d;
    int e = cars_load(&d, argv[1]);
    if (e) { fprintf(stderr, "cars_load: %d\n", e); return 2; }
    FILE *f = fopen(argv[2], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[2]); return 2; }
    char magic[4];
    unsigned int ncases;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "CVEC", 4) || fread(&ncases, 4, 1, f) != 1) return 2;
    Course dummy;
    memset(&dummy, 0, sizeof dummy);
    int bad = 0, total_inst = 0, empty = 0;
    float worst = 0.0f;
    for (unsigned c = 0; c < ncases; c++) {
        unsigned int mode, ninst;
        float view[12], st[16];
        int type, brake, damage;
        if (fread(&mode, 4, 1, f) != 1 || fread(view, 4, 12, f) != 12 || fread(st, 4, 16, f) != 16 ||
            fread(&type, 4, 1, f) != 1 || fread(&brake, 4, 1, f) != 1 || fread(&damage, 4, 1, f) != 1 || fread(&ninst, 4, 1, f) != 1) return 2;
        struct { int part; float m[12]; } exp[16];
        if (ninst > 16) { fprintf(stderr, "case %u: %u instances\n", c, ninst); return 2; }
        for (unsigned i = 0; i < ninst; i++)
            if (fread(&exp[i].part, 4, 1, f) != 1 || fread(exp[i].m, 4, 12, f) != 12) return 2;
        CarState cs;
        cs.ang[0] = st[0]; cs.ang[1] = st[1]; cs.ang[2] = st[2];
        cs.pos[0] = st[3]; cs.pos[1] = st[4]; cs.pos[2] = st[5];
        cs.steer = st[6]; cs.spin = st[7];
        for (int k = 0; k < 4; k++) { cs.susp[k] = st[8 + k]; cs.ground[k] = st[12 + k]; }
        cs.type = type; cs.brake = brake; cs.damage = damage;
        SceneItem out[16];
        int n = car_display(&d, &cs, (int)mode, view, &dummy, out, 16);
        int ok = (n == (int)ninst);
        float maxerr = 0.0f;
        for (int i = 0; ok && i < n; i++) {
            if (out[i].part != exp[i].part) ok = 0;
            for (int k = 0; k < 12; k++) {
                float er = fabsf(out[i].m[k] - exp[i].m[k]);
                if (er > maxerr) maxerr = er;
            }
        }
        if (maxerr > worst) worst = maxerr;
        if (maxerr > 0.01f) ok = 0;
        total_inst += (int)ninst;
        if (ninst == 0) empty++;
        if (!ok) {
            bad++;
            if (bad <= 12) {
                printf("case %u mode %u type %d dmg %04x: expected %u items, got %d, max matrix error %.4f\n", c, mode, type, damage & 0xFFFF, ninst, n, maxerr);
                for (unsigned i = 0; i < ninst || (int)i < n; i++) {
                    float ie = 0.0f;
                    if (i < ninst && (int)i < n) for (int k = 0; k < 12; k++) ie = fmaxf(ie, fabsf(out[i].m[k] - exp[i].m[k]));
                    printf("    exp part %4d  got part %4d  matrix error %.4f\n", i < ninst ? exp[i].part : -9, (int)i < n ? out[i].part : -9, ie);
                }
                if (bad == 1) {
                    printf("    car: ang %.4f %.4f %.4f pos %.3f %.3f %.3f steer %.3f spin %.3f susp %.3f %.3f %.3f %.3f ground %.3f %.3f %.3f %.3f\n",
                           st[0], st[1], st[2], st[3], st[4], st[5], st[6], st[7], st[8], st[9], st[10], st[11], st[12], st[13], st[14], st[15]);
                    for (unsigned i = 0; i < ninst && (int)i < n; i++) {
                        printf("    item %u exp:", i);
                        for (int k = 0; k < 12; k++) printf(" %.4f", exp[i].m[k]);
                        printf("\n    item %u got:", i);
                        for (int k = 0; k < 12; k++) printf(" %.4f", out[i].m[k]);
                        printf("\n");
                    }
                }
            }
        }
    }
    printf("%u cases (%d empty = off screen, beyond 200 m or mode 3), %d instances: %d mismatches, worst matrix error %.5f\n", ncases, empty, total_inst, bad, worst);
    return bad ? 1 : 0;
}
