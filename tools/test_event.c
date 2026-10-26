/* Host test of src/event.c against the oracle: replays tools/evvec.py's vector (what the game's own dispatcher drew, pass after pass) through ev_frame().
 *   gcc -O1 -I src tools/test_event.c src/event.c -lm -o build/host/test_event && build/host/test_event extracted/psp/course/events.dat extracted/psp/course/object.names extracted/oracle/ev_0_0.vec
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "event.h"
#include "coli.h"

static Events ev;
typedef struct { int slot; unsigned kind; int part, r6; float a[12]; } ExDraw;

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: test_event events.dat object.names ev.vec [verbose]\n"); return 2; }
    Course dummy;
    memset(&dummy, 0, sizeof dummy);
    int e = ev_load(&ev, argv[1], argv[2], &dummy);
    if (e) { fprintf(stderr, "ev_load: %d\n", e); return 2; }
    FILE *f = fopen(argv[3], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[3]); return 2; }
    char magic[4];
    unsigned int nframes, course, gt;
    if (fread(magic, 1, 4, f) != 4 || (memcmp(magic, "EVEC", 4) && memcmp(magic, "EVE2", 4)) || fread(&nframes, 4, 1, f) != 1 || fread(&course, 4, 1, f) != 1 || fread(&gt, 4, 1, f) != 1) return 2;
    int verbose = argc > 4, v2 = !memcmp(magic, "EVE2", 4), snd_bad = 0, snd_total = 0, push_total = 0;
    static Coli coli;
    if (getenv("EV_RCT") && *getenv("EV_RCT")) {                                  /* ground under the helicopter (desert) */
        if (coli_load(&coli, getenv("EV_RCT"))) { fprintf(stderr, "cannot load %s\n", getenv("EV_RCT")); return 2; }
        ev_set_collision(&ev, &coli);
    }
    ev_start(&ev, (int)course, (int)gt);
    int bad_frames = 0, total = 0, mism = 0;
    static SceneItem out[512];
    for (unsigned fr = 0; fr < nframes; fr++) {
        unsigned int rng, nb, nd;
        EvWorld w;
        memset(&w, 0, sizeof w);
        int ci[9];
        unsigned char blocks[256];
        if (fread(&rng, 4, 1, f) != 1 || fread(w.view, 4, 12, f) != 12 || fread(ci, 4, 9, f) != 9 || fread(&nb, 4, 1, f) != 1) return 2;
        if (nb > 256 || fread(blocks, 1, (nb + 3) & ~3u, f) != ((nb + 3) & ~3u)) return 2;
        if (fread(&nd, 4, 1, f) != 1) return 2;
        ExDraw *ex = (ExDraw *)malloc(sizeof(ExDraw) * (nd + 1));
        for (unsigned i = 0; i < nd; i++) if (fread(&ex[i], 4, 16, f) != 16) return 2;
        unsigned int nsnd = 0, sfn[8], sr4[8];
        int exp_dx = 0, exp_dz = 0;
        if (v2) {                                                                  /* the sound part: the camera matrix as the game holds it, the car's state, the push the dispatch added, the library calls */
            int ext[3], ep[1];
            if (fread(w.view_fx, 4, 12, f) != 12 || fread(ext, 4, 3, f) != 3 || fread(ep, 4, 1, f) != 1 || fread(&nsnd, 4, 1, f) != 1) return 2;
            w.mode810 = ext[0]; w.car_state = ext[1]; exp_dx = ext[2]; exp_dz = ep[0];
            if (nsnd > 8) return 2;
            for (unsigned i = 0; i < nsnd; i++) if (fread(&sfn[i], 4, 1, f) != 1 || fread(&sr4[i], 4, 1, f) != 1) return 2;
        }
        w.game_type = (int)gt; w.car_byte_c5 = ci[0]; w.car_seg = ci[1]; w.car_c8 = ci[2]; w.car_ca = ci[3];
        w.car_x = ci[4]; w.car_y = ci[5]; w.car_z = ci[6]; w.car_yaw = ci[7]; w.nseg_word = ci[8];
        ev.rng = rng;
        int n = ev_frame(&ev, &w, blocks, (int)nb, out, 512);
        if (v2) {
            int sok = (unsigned)ev.snd.n == nsnd && w.push_x == exp_dx && w.push_z == exp_dz;
            for (unsigned i = 0; sok && i < nsnd; i++) if ((ev.snd.kind[i] ? 0x0601704Cu : 0x06016FBCu) != sfn[i] || (unsigned)ev.snd.val[i] != sr4[i]) sok = 0;
            snd_total += (int)nsnd; push_total += (exp_dx != 0 || exp_dz != 0);
            if (!sok) {
                snd_bad++;
                if (snd_bad <= 6 || verbose) {
                    printf("pass %u: sound / push: expected %u calls, push (%d, %d); got %d calls, push (%d, %d)\n", fr, nsnd, exp_dx, exp_dz, ev.snd.n, w.push_x, w.push_z);
                    for (unsigned i = 0; i < nsnd; i++) printf("    expected %08x %x\n", sfn[i], sr4[i]);
                    for (int i = 0; i < ev.snd.n; i++) printf("    got      %08x %x\n", ev.snd.kind[i] ? 0x0601704Cu : 0x06016FBCu, (unsigned)ev.snd.val[i]);
                }
            }
        }
        int ok = (n == (int)nd);
        float worst = 0.0f;
        for (int i = 0; ok && i < n; i++) {
            if (ev.slot_of[i] != ex[i].slot || out[i].part != ex[i].part) { ok = 0; break; }
            if (ex[i].kind == 2 || ex[i].kind == 3) {
                float sc = ex[i].kind == 2 ? (float)ex[i].r6 : 0.0f;
                if (ex[i].kind == 2 && fabsf(out[i].m[0] - sc) > 0.01f) ok = 0;
                for (int k = 0; k < 3; k++) {
                    float er = fabsf(out[i].m[4 * k + 3] - ex[i].a[k]);
                    if (er > worst) worst = er;
                }
            } else {
                if (out[i].clip != (ex[i].kind == 1)) ok = 0;
                for (int k = 0; k < 12; k++) {
                    float er = fabsf(out[i].m[k] - ex[i].a[k]);
                    if (er > worst) worst = er;
                }
            }
        }
        if (worst > 0.02f) ok = 0;
        total += (int)nd;
        if (!ok) {
            bad_frames++;
            if (bad_frames <= 6 || verbose) {
                printf("pass %u: expected %u draws, got %d (worst matrix error %.4f)\n", fr, nd, n, worst);
                for (int i = 0; i < (int)nd || i < n; i++) {
                    if (i < (int)nd && i < n && ev.slot_of[i] == ex[i].slot && out[i].part == ex[i].part) continue;
                    printf("    #%d expected slot %d kind %u part %d | got slot %d part %d\n", i, i < (int)nd ? ex[i].slot : -1, i < (int)nd ? ex[i].kind : 9, i < (int)nd ? ex[i].part : -9,
                           i < n ? ev.slot_of[i] : -1, i < n ? out[i].part : -9);
                    if (++mism > 30) break;
                }
            }
        }
        free(ex);
    }
    printf("%u passes, %d draws expected: %d passes differ\n", nframes, total, bad_frames);
    if (v2) printf("sound: %d library calls expected, %d passes with a push-back: %d passes differ\n", snd_total, push_total, snd_bad);
    return bad_frames || snd_bad ? 1 : 0;
}
