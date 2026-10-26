/* Host test of the pause menu's picture (src/hudgen.c hudgen_pause_*) against the original's routines (tools/pause_vec.py -> extracted/oracle/pause.vec): after every call of pause_display /
 * pause_cursor / pause_delete the three HUD name table pages must be identical.
 *   gcc -O1 -fwrapv -I src tools/test_pause.c src/hudgen.c src/drive.c src/camera.c src/pmap.c src/coli.c src/sndev.c -lm -o build/host/test_pause
 *   build/host/test_pause extracted/psp/course extracted/oracle/pause.vec */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hudgen.h"

static unsigned rd32(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24; }

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: test_pause course_dir pause.vec\n"); return 2; }
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
    if (fread(buf, 1, (size_t)n, f) != (size_t)n || memcmp(buf, "PSV1", 4) != 0) { fprintf(stderr, "bad file\n"); return 2; }
    fclose(f);
    unsigned ncases = rd32(buf + 4);
    p += 8;
    static HudGen h;
    static Drive d;
    unsigned badcases = 0, steps_total = 0;
    for (unsigned cs = 0; cs < ncases; cs++) {
        unsigned gt = rd32(p), course = rd32(p + 4);
        p += 8;
        memset(&h, 0, sizeof h);
        memset(&d, 0, sizeof d);
        d.phys = &phys;
        d.cfg.course = (int)course; d.cfg.game_type = (int)gt;
        for (int i = 0; i < HUD_NT_WORDS; i++) h.w[i] = (unsigned short)(p[2 * i] << 8 | p[2 * i + 1]);
        p += 2 * HUD_NT_WORDS;
        unsigned nsteps = rd32(p);
        p += 4;
        int bad = 0;
        for (unsigned s = 0; s < nsteps; s++) {
            int op = p[0], item = p[1];
            const unsigned char *snap = p + 4;
            if (op == 0) hudgen_pause_open(&h, &d);
            else if (op == 1) { d.pause[0] = (unsigned char)item; hudgen_pause_cursor(&h, &d); }
            else hudgen_pause_close(&h);
            for (int i = 0; i < HUD_NT_WORDS && !bad; i++) {
                unsigned want = (unsigned)(snap[2 * i] << 8 | snap[2 * i + 1]);
                if (h.w[i] != want) { bad = (int)s + 1; printf("case %u (type %u course %u) step %u (op %d item %d): [nt %05x] got %04x want %04x\n", cs, gt, course, s, op, item, 0x3A000 + 2 * i, h.w[i], want); }
            }
            p += 4 + 2 * HUD_NT_WORDS;
            steps_total++;
        }
        if (bad) badcases++;
    }
    printf("%u cases, %u steps, %u cases differ\n", ncases, steps_total, badcases);
    return badcases ? 1 : 0;
}
