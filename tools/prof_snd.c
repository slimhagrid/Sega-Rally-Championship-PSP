/* M9: where the 68000 spends its instructions: runs a scenario of tools/test_snd.c (ram.bin, cmds.txt) and counts the executions of every address.
 *   gcc -O2 -I src tools/prof_snd.c src/snd.c src/m68k.c src/scsp.c -lm -o build/host/prof_snd
 *   build/host/prof_snd ram.bin cmds.txt nticks [top] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snd.h"

static unsigned long long cnt[0x10000];
static unsigned long long total;

static void tracecb(void *ud, const M68k *m)
{
    (void)ud;
    cnt[m->pc & 0xFFFF]++;
    total++;
}

int main(int argc, char **argv)
{
    static uint8_t ram[0x80000];
    static float buf[2 * SND_TICK_FRAMES];
    FILE *f;
    SndCore *c;
    int nticks, ncmd = 0, ci = 0, t, top = 50, i;
    static int ct[512], cc[512][5];
    if (argc < 4) { fprintf(stderr, "usage: prof_snd ram.bin cmds.txt nticks [top]\n"); return 2; }
    f = fopen(argv[1], "rb"); if (!f || fread(ram, 1, sizeof ram, f) != sizeof ram) { fprintf(stderr, "ram\n"); return 2; } fclose(f);
    f = fopen(argv[2], "r");
    while (f && ncmd < 512 && fscanf(f, "%d %d %d %d %d %d", &ct[ncmd], &cc[ncmd][0], &cc[ncmd][1], &cc[ncmd][2], &cc[ncmd][3], &cc[ncmd][4]) == 6) ncmd++;
    if (f) fclose(f);
    nticks = atoi(argv[3]);
    if (argc > 4) top = atoi(argv[4]);
    ram[0x1580] = 0x4E; ram[0x1581] = 0x75;
    c = snd_core_new(ram);
    snd_core_set_test_mode(c, 1);
    snd_core_set_trace(c, tracecb, NULL);
    snd_core_reset(c);
    for (t = 0; t < nticks; t++) {
        while (ci < ncmd && ct[ci] <= t) {
            int k;
            for (k = 0; k < 8; k++) {
                uint8_t *s = ram + 0x700 + 16 * k;
                if (s[0] == 0) { s[2] = (uint8_t)cc[ci][1]; s[3] = (uint8_t)cc[ci][2]; s[4] = (uint8_t)cc[ci][3]; s[5] = (uint8_t)cc[ci][4]; s[0] = (uint8_t)cc[ci][0]; break; }
            }
            ci++;
        }
        memset(buf, 0, sizeof buf);
        if (!snd_core_tick(c, buf)) { fprintf(stderr, "driver stopped at tick %d\n", t); break; }
    }
    printf("%d ticks, %llu instructions = %.1f per tick\n", t, total, (double)total / t);
    /* the hottest addresses, then by 64-byte region */
    {
        static unsigned long long reg[0x400];
        int k;
        for (i = 0; i < 0x10000; i++) reg[i >> 6] += cnt[i];
        for (k = 0; k < top; k++) {
            int best = -1;
            for (i = 0; i < 0x400; i++) if (reg[i] && (best < 0 || reg[i] > reg[best])) best = i;
            if (best < 0) break;
            printf("%04x-%04x: %.1f per tick (%.1f%%)\n", best << 6, (best << 6) + 63, (double)reg[best] / t, 100.0 * (double)reg[best] / (double)total);
            reg[best] = 0;
        }
    }
    return 0;
}
