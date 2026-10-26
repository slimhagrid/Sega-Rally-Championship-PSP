/* M9: runs the sound driver of a scenario on the C core (m68k.c + scsp.c) and writes the SCSP register log and the mixed audio:
 *   gcc -O2 -I src tools/test_snd.c src/snd.c src/m68k.c src/scsp.c -lm -o build/host/test_snd
 *   build/host/test_snd ram.bin cmds.txt nticks out.log out.raw
 * ram.bin = the 512 KB sound RAM after the files were loaded (tools/snd_scenario.py); cmds.txt: lines "tick cmd p0 p1 p2 p3" (decimal): the command is written into the first free slot
 * of the 0x700 area before that tick; out.log: "tick offset size value" of every SCSP register write; out.raw: float32 stereo. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snd.h"

static FILE *logf, *tracef;
static long stop_n = -1, ninst;
static const char *dump_path;
static uint8_t *g_ram;
static void tracecb(void *ud, const M68k *m)
{
    uint32_t r[18];
    int i;
    (void)ud;
    ninst++;
    if (stop_n >= 0 && ninst == stop_n && dump_path) { FILE *d = fopen(dump_path, "wb"); fwrite(g_ram, 1, 0x80000, d); fclose(d); exit(0); }
    r[0] = m->pc;
    for (i = 0; i < 8; i++) { r[1 + i] = m->d[i]; r[9 + i] = m->a[i]; }
    r[17] = m->wh;
    ((M68k *)m)->wh = 0;
    if (tracef) fwrite(r, sizeof r, 1, tracef);
}
static void logcb(void *ud, uint32_t tick, uint32_t off, int size, uint32_t v) { (void)ud; fprintf(logf, "%u %x %d %x\n", tick, off, size, v); }

int main(int argc, char **argv)
{
    static uint8_t ram[0x80000];
    static float buf[2 * SND_TICK_FRAMES];
    FILE *f;
    SndCore *c;
    int nticks, ncmd = 0, ci = 0, t;
    static int ct[512], cc[512][5];
    FILE *raw;
    if (argc < 6) { fprintf(stderr, "usage: test_snd ram.bin cmds.txt nticks out.log out.raw\n"); return 2; }
    f = fopen(argv[1], "rb"); if (!f || fread(ram, 1, sizeof ram, f) != sizeof ram) { fprintf(stderr, "ram\n"); return 2; } fclose(f);
    f = fopen(argv[2], "r");
    while (f && ncmd < 512 && fscanf(f, "%d %d %d %d %d %d", &ct[ncmd], &cc[ncmd][0], &cc[ncmd][1], &cc[ncmd][2], &cc[ncmd][3], &cc[ncmd][4]) == 6) ncmd++;
    if (f) fclose(f);
    nticks = atoi(argv[3]);
    logf = fopen(argv[4], "w"); raw = fopen(argv[5], "wb");
    if (argc > 6) { tracef = fopen(argv[6], "wb"); }
    g_ram = ram;
    if (getenv("STOP_N")) { stop_n = atol(getenv("STOP_N")); dump_path = getenv("DUMP"); }
    ram[0x1580] = 0x4E; ram[0x1581] = 0x75;                  /* RTE -> RTS: the timer B routine runs as a subroutine (lockstep with unicorn) */
    c = snd_core_new(ram);
    snd_core_set_test_mode(c, 1);
    if (getenv("SND_HLE")) snd_core_set_hle(c, 1);                    /* the driver's loops in C (src/snd.c): the output must not change */
    snd_core_set_log(c, logcb, NULL);
    if (tracef || stop_n >= 0) snd_core_set_trace(c, tracecb, NULL);
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
        if (!snd_core_tick(c, buf)) { fprintf(stderr, "driver stopped at tick %d (pc %x)\n", t, snd_core_pc(c)); break; }
        fwrite(buf, sizeof buf, 1, raw);
    }
    fclose(logf); fclose(raw); if (tracef) fclose(tracef);
#ifdef SND_PROF
    { extern double snd_prof_cpu_ns, snd_prof_render_ns; printf("per tick: 68000 + irq %.1f us, SCSP render %.1f us\n", snd_prof_cpu_ns / t / 1000.0, snd_prof_render_ns / t / 1000.0); }
#endif
    printf("%d ticks, pc %x\n", t, snd_core_pc(c));
    return 0;
}
