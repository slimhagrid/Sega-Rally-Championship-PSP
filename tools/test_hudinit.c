/* Host test of the time attack's display set-up with the race's values (src/hudgen.c hudgen_ta_init) against the original's init_game_disp (tools/hudinit_vec.py -> extracted/oracle/hudinit.vec): the
 * static screen (hud_init.dat) plus hudgen_ta_init must give the name tables of NBG2 / NBG3 and the pages of NBG0 the original leaves.
 *   gcc -O1 -fwrapv -I src tools/test_hudinit.c src/hudgen.c src/drive.c src/camera.c src/pmap.c src/coli.c src/sndev.c -lm -o build/host/test_hudinit
 *   build/host/test_hudinit extracted/psp/course extracted/oracle/hudinit.vec */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hudgen.h"
#include "fx.h"

static unsigned rd32(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24; }
static unsigned be32(const unsigned char *p) { return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3]; }

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: test_hudinit course_dir hudinit.vec\n"); return 2; }
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
    if (fread(buf, 1, (size_t)n, f) != (size_t)n || memcmp(buf, "HIN1", 4) != 0) { fprintf(stderr, "bad file\n"); return 2; }
    fclose(f);
    unsigned ncases = rd32(buf + 4);
    p += 8;
    static HudGen h;
    static Drive d;
    unsigned bad = 0;
    for (unsigned cs = 0; cs < ncases; cs++) {
        unsigned gt = rd32(p), course = rd32(p + 4), car = rd32(p + 8), mt = rd32(p + 12), setup = rd32(p + 16), r24 = rd32(p + 20), r30 = rd32(p + 24), r70 = rd32(p + 28);
        p += 32;
        snprintf(path, sizeof path, "%s/hud_init.dat", argv[1]);
        if (hudgen_load(&h, path, (int)gt, (int)course)) { fprintf(stderr, "cannot load %s\n", path); return 2; }
        memset(&d, 0, sizeof d);
        d.phys = &phys;
        d.cfg.game_type = (int)gt; d.cfg.course = (int)course;
        d.car[0x5C] = (unsigned char)(car == 2 ? 10 : car); d.car[0x5F] = (unsigned char)setup; d.car[0xF4] = (unsigned char)mt;
        p32(d.car, 0x500 + 0x24, (int)r24); p32(d.car, 0x500 + 0x30, (int)r30); p32(d.car, 0x500 + 0x70, (int)r70);
        hudgen_ta_init(&h, &d);
        int diffs = 0;
        for (int i = 0; i < HUD_NT_WORDS; i++) {
            unsigned want = (unsigned)(p[2 * i] << 8 | p[2 * i + 1]);
            if (h.w[i] != want) { if (diffs++ < 3) printf("case %u (type %u course %u car %u mt %u setup %u): [nt %05x] got %04x want %04x\n", cs, gt, course, car, mt, setup, 0x3A000 + 2 * i, h.w[i], want); }
        }
        p += 2 * HUD_NT_WORDS;
        for (int i = 0; i < HUDE_CELLS; i++) {
            unsigned want = be32(p + 4 * i);
            if (h.nt0[i] != want) { if (diffs++ < 3) printf("case %u (type %u course %u car %u mt %u setup %u r70 %08x): [nbg0 %05x] got %08x want %08x\n", cs, gt, course, car, mt, setup, r70, 0x28000 + 4 * i, h.nt0[i], want); }
        }
        p += 4 * HUDE_CELLS;
        if (diffs) bad++;
    }
    printf("%u cases, %u differ\n", ncases, bad);
    return bad ? 1 : 0;
}
