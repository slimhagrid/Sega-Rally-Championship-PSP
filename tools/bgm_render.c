/* M9: renders what the original's sound driver plays for one call of the sound library (a song of a menu screen) on the PC: the library (snd_lib.c) on the console memory loads the
 * driver and the sound set, the driver runs on the 68000 core, the mixed audio from the tick the play command was taken is written as float32 stereo (44.1 kHz).
 *   build/host/bgm_render datadir index id seconds out.raw [mono]
 * index / id = the arguments of select_set (FUN_060173AC): the sound set and the sound id of the call; "mono": the stereo option off. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sat.h"
#include "satlib.h"
#include "snd.h"
#include "snd_lib.h"

static SndCore *core;
static float *pcm;
static long ntick, maxtick, play_tick = -1;

static void tick(void)
{
    float buf[2 * SND_TICK_FRAMES];
    memset(buf, 0, sizeof buf);
    if (ntick >= maxtick) return;
    if (getenv("BGM_DUMPAT") && play_tick >= 0 && ntick == play_tick + atol(getenv("BGM_DUMPAT"))) {        /* development: the sound RAM before that tick (relative to the play command) */
        FILE *d = fopen(getenv("BGM_DUMPFILE") ? getenv("BGM_DUMPFILE") : "build/host/ramdump.bin", "wb");
        fwrite(sat->sndram, 1, 0x10000, d);
        fclose(d);
        exit(0);
    }
    {
        unsigned long long s0 = snd_core_skipped(core);
        if (snd_core_tick(core, buf)) memcpy(pcm + 2 * SND_TICK_FRAMES * ntick, buf, sizeof buf);
        if (getenv("BGM_SKIPLOG") && play_tick >= 0 && snd_core_skipped(core) != s0) { static int shown; if (shown++ < 12) fprintf(stderr, "skipped tick %ld (play + %ld)\n", ntick, ntick - play_tick); }
    }
    ntick++;
}

static void tracepc(void *ud, const M68k *m) { (void)ud; printf("%06x\n", (unsigned)m->pc); }
static void hw_cpu(int on) { if (on) snd_core_reset(core); }
static void hw_wait(void) { tick(); }
static void logcb(const char *m)
{
    if (getenv("BGM_LOG")) fprintf(stderr, "[%ld] %s\n", ntick, m);
    if (!strncmp(m, "  slot", 6) && strstr(m, "play") && play_tick < 0) play_tick = ntick;
}

int main(int argc, char **argv)
{
    int index, id, mono;
    double secs;
    long i, from;
    FILE *o;
    if (argc < 6) { fprintf(stderr, "usage: bgm_render datadir index id seconds out.raw [mono]\n"); return 2; }
    index = atoi(argv[2]); id = atoi(argv[3]); secs = atof(argv[4]); mono = argc > 6;
    maxtick = (long)(secs * 44100.0 / SND_TICK_FRAMES) + 4000;
    pcm = (float *)calloc((size_t)maxtick * 2 * SND_TICK_FRAMES, sizeof(float));
    if (sat_init(argv[1]) != 0) { fprintf(stderr, "no data in %s\n", argv[1]); return 1; }
    sat_boot();
    if (getenv("BGM_ROWS")) {
        int r;
        for (r = 0; r < 0x200; r++) { if (r % 16 == 0) printf("\n%08x:", 0x06033B38u + (unsigned)r); printf(" %02x", (unsigned)R8(0x06033B38u + (unsigned)r)); }
        for (r = 0; r < 8; r++) printf("\nset %d list %08x", r, (unsigned)R32(0x060173F8u + 4 * (unsigned)r));
        printf("\n");
        for (r = 0; r < 32; r++) printf("row %2d: %08x %08x\n", r, (unsigned)R32(0x060A1268u + 8 * (unsigned)r), (unsigned)R32(0x060A1268u + 8 * (unsigned)r + 4));
        for (r = 0; r < 96; r++) printf("id %2d: %08x\n", r, (unsigned)R32(0x060A1330u + 4 * (unsigned)r));
    }
    W8(0x06040062, mono ? R8(0x06040062) & ~0x20 : R8(0x06040062) | 0x20);
    core = snd_core_new(sat->sndram);
    if (getenv("BGM_SKIP")) snd_core_set_idle_skip(core, 1);
    if (getenv("BGM_HLE")) snd_core_set_hle(core, 1);
    if (getenv("BGM_HLE_VERIFY")) snd_core_set_hle_verify(1);
    snd_hw_cpu = hw_cpu; snd_hw_wait = hw_wait; snd_log_hook = logcb;
    snd_lib_boot();
    for (i = 0; i < 400; i++) tick();                           /* the driver's start-up */
    if (getenv("BGM_RACE")) {                                   /* development: the sound of a race (the single player driver, an engine note that follows the revs) and the work of the 68000 in 50 ticks of it */
        int p;
        W16(0x060AA860, 0); W16(0x060AA87E, 0); W8(0x060AA80A, 0); W8(0x060AA810, 0); W8(0x060AA811, 0);
        snd_lib_call(0x06017418, 0, 0, 0);
        for (p = 0; p < 300; p++) {
            W32(0x25A007C0u, 69u + (u32)(p / 3 % 30)); W32(0x25A007C4u, 12);
            if (getenv("BGM_RACE_RANDOM")) {                    /* sound ids of the race at random: the more voices and sequences are busy, the more of the hooks' fall-backs run */
                static unsigned rs = 12345;
                int k, nid;
                rs = rs * 1103515245u + 12345u; nid = (int)((rs >> 16) % 4);
                for (k = 0; k < nid; k++) { rs = rs * 1103515245u + 12345u; snd_lib_play(1u + (rs >> 16) % 32u); }
                if (p % 50 == 7) snd_lib_call(0x06016F3C, 0, 0x7F, 0);
            }
            snd_lib_call(0x06017580, 0, 0, 0);
            for (i = 0; i < 22; i++) tick();
        }
        if (getenv("BGM_RACE_OUT")) {                           /* the audio of the passes, to compare runs (the driver's loops in C must not change it) */
            FILE *q = fopen(getenv("BGM_RACE_OUT"), "wb");
            fwrite(pcm + 2 * SND_TICK_FRAMES * 400, sizeof(float), (size_t)(2 * SND_TICK_FRAMES * (ntick - 400)), q);
            fclose(q);
            printf("%ld ticks, %llu skipped\n", ntick, snd_core_skipped(core));
            return 0;
        }
        snd_core_set_trace(core, tracepc, NULL);
        for (i = 0; i < 50; i++) { tick(); printf("--\n"); }
        snd_core_set_trace(core, NULL, NULL);
        return 0;
    }
    snd_lib_call(0x060173AC, (u32)index, (u32)id, 0);
    if (getenv("BGM_IDLE")) {                                   /* development: the 68000's work in one tick when nothing plays (the song id must be 0) */
        for (i = 0; i < 2000; i++) tick();
        for (i = 0; i < 3; i++) {
            snd_core_set_trace(core, tracepc, NULL);
            tick();
            snd_core_set_trace(core, NULL, NULL);
            printf("--\n");
        }
        return 0;
    }
    if (play_tick < 0) { fprintf(stderr, "no play command was sent\n"); return 1; }
    if (getenv("BGM_FADE")) {                                   /* development: a fade command (FUN_06016F3C: class, volume, time) after that many ticks */
        long at = atol(getenv("BGM_FADE"));
        int vol = getenv("BGM_FADE_VOL") ? atoi(getenv("BGM_FADE_VOL")) : 0, tm = getenv("BGM_FADE_TIME") ? atoi(getenv("BGM_FADE_TIME")) : 48;
        while (ntick < play_tick + at) tick();
        fprintf(stderr, "fade command at tick %ld\n", ntick);
        snd_lib_call(0x06016F3C, 0, (u32)vol, (u32)tm);
        snd_lib_call(0x060175A4, 0, 0, 0);
    }
    while (ntick < maxtick) tick();
    from = play_tick;
    o = fopen(argv[5], "wb");
    fwrite(pcm + 2 * SND_TICK_FRAMES * from, sizeof(float), (size_t)(2 * SND_TICK_FRAMES * (maxtick - from)), o);
    fclose(o);
    printf("play at tick %ld, %ld ticks written, %llu driver ticks skipped\n", play_tick, maxtick - from, snd_core_skipped(core));
    return 0;
}
