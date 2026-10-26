/* Host test of src/gmath.h and src/coli.c against the oracle (tools/export_math_vec.py -> extracted/oracle/math.txt).
 *   gcc -O1 -I src tools/test_gmath.c src/coli.c -lm -o build/host/test_gmath && build/host/test_gmath extracted/oracle/math.txt extracted/psp/course */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "coli.h"
#include "gmath.h"

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: test_gmath math.txt course_dir\n"); return 2; }
    static const char *const names[4] = { "desert", "forest", "mount", "lake" };
    Coli col[4];
    for (int i = 0; i < 4; i++) {
        char p[512];
        snprintf(p, sizeof p, "%s/%s.rct", argv[2], names[i]);
        if (coli_load(&col[i], p)) { fprintf(stderr, "cannot load %s\n", p); return 2; }
    }
    FILE *f = fopen(argv[1], "r");
    if (!f) return 2;
    char line[256];
    int ns = 0, na = 0, nc = 0, bad[3] = { 0, 0, 0 };
    while (fgets(line, sizeof line, f)) {
        long a, b, c, d, e;
        if (!strncmp(line, "sqrt ", 5)) {
            if (sscanf(line + 5, "%ld %ld", &a, &b) != 2) continue;
            ns++;
            if (g_sqrt((int)a) != (int)b) { if (bad[0]++ < 5) printf("sqrt(%ld): got %d expected %ld\n", a, g_sqrt((int)a), b); }
        } else if (!strncmp(line, "atan ", 5)) {
            if (sscanf(line + 5, "%ld %ld %ld", &a, &b, &c) != 3) continue;
            na++;
            if (g_atan((int)a, (int)b) != (int)c) { if (bad[1]++ < 5) printf("atan(%ld, %ld): got %d expected %ld\n", a, b, g_atan((int)a, (int)b), c); }
        } else if (!strncmp(line, "coli ", 5)) {
            int n = sscanf(line + 5, "%ld %ld %ld %ld %ld", &a, &b, &c, &d, &e);
            if (n < 4) continue;
            nc++;
            const unsigned char *p = coli_find(&col[a], (int)b, (int)c);
            long got = p ? (long)((p - (col[a].file + col[a].off_poly)) / 52) : -1;
            int ok = got == d;
            if (ok && n == 5) ok = coli_y20(p, (int)b, (int)c) == (int)e;
            if (!ok && bad[2]++ < 8) printf("coli %ld (%ld, %ld): got poly %ld expected %ld%s\n", a, b, c, got, d, n == 5 ? " (y differs?)" : "");
        }
    }
    printf("sqrt %d (%d differ), atan %d (%d differ), collision %d (%d differ)\n", ns, bad[0], na, bad[1], nc, bad[2]);
    return bad[0] + bad[1] + bad[2] ? 1 : 0;
}
