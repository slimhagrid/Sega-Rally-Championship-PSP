/* Host test of the end-of-race HUD (src/hudgen.c hudend_pass) against synthetic oracle cases (tools/oracle.py hudsynth -> extracted/oracle/hudsynth.bin): every game type / stage / control byte
 * (goal, game over) / placing / lap with random times, 330 passes each, NBG0 cells + registers + the state machine's counter R+0 compared after every pass.
 *   gcc -O1 -fwrapv -I src -I tools tools/test_hudend.c src/hudgen.c src/drive.c src/camera.c src/pmap.c src/coli.c -lm -o build/host/test_hudend
 *   build/host/test_hudend extracted/psp/course extracted/oracle/hudsynth.bin */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hudgen.h"
#include "fx.h"
#include "hude_cmp.h"

#define SYNTH_PASSES 330

static unsigned rd32(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24; }

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: test_hudend course_dir hudsynth.bin\n"); return 2; }
    char path[512];
    static Phys phys;
    snprintf(path, sizeof path, "%s/phys.dat", argv[1]);
    if (phys_load(&phys, path)) { fprintf(stderr, "cannot load %s\n", path); return 2; }
    FILE *f = fopen(argv[2], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[2]); return 2; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = (unsigned char *)malloc((size_t)n), *p = buf;
    if (fread(buf, 1, (size_t)n, f) != (size_t)n || memcmp(buf, "HSY1", 4) != 0) { fprintf(stderr, "bad file\n"); return 2; }
    fclose(f);
    unsigned ncases = rd32(buf + 4);
    p += 8;
    static HudGen h;
    static Drive d;
    static unsigned short want[HUDE_WORDS];
    unsigned badcases = 0;
    for (unsigned cs = 0; cs < ncases; cs++) {
        unsigned course = rd32(p), gt = rd32(p + 4), ctrl = rd32(p + 8), rank = rd32(p + 12), lap = rd32(p + 16), laps = rd32(p + 20);
        p += 24;
        snprintf(path, sizeof path, "%s/hud_init.dat", argv[1]);
        if (hudgen_load(&h, path, (int)gt, (int)course)) { fprintf(stderr, "cannot load %s\n", path); return 2; }
        memset(&d, 0, sizeof d);
        d.phys = &phys;
        d.cfg.course = (int)course; d.cfg.game_type = (int)gt; d.total_laps = (int)laps;
        d.car[0xF5] = (unsigned char)ctrl; d.car[0xB6] = (unsigned char)rank; p16(d.car, 0xC8, (int)lap);
        memcpy(d.car + 0x500, p, 0x50);
        p += 0x50;
        d.g803 = 0x64;
        for (int i = 0; i < HUDE_WORDS; i++) want[i] = (unsigned short)hude_be16(p + 2 * i);
        p += 2 * HUDE_WORDS;
        if (hude_check_init(&h, want)) { fprintf(stderr, "case %u: hude_init.dat differs from the case's initial NBG0 pages\n", cs); return 2; }
        int bad = 0;
        char what[96] = "";
        for (int pass = 0; pass < SYNTH_PASSES; pass++) {
            hudgen_pass(&h, &d, (unsigned)pass);
            p = (unsigned char *)hude_apply(want, p);
            int r0 = *p++;
            if (!bad && d.car[0x500] != r0) { bad = pass + 1; snprintf(what, sizeof what, "R+0 got %02x want %02x", d.car[0x500], r0); }
            if (!bad && hude_compare(&h, &d, want, what, sizeof what)) bad = pass + 1;
        }
        if (bad) {
            badcases++;
            printf("case %u (stage %u, type %u, ctrl %u, rank %u, lap %u, laps %u): pass %d: %s\n", cs, course, gt, ctrl, rank, lap, laps, bad - 1, what);
        }
    }
    printf("%u cases, %u differ\n", ncases, badcases);
    return badcases ? 1 : 0;
}
