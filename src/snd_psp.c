/* M9: the PSP audio thread of the sound system (snd.h): runs the original sound driver (the 68000 interpreter over sat->sndram) one tick at a time and feeds the mixed 44.1 kHz stereo to
 * sceAudio in blocks of 1024 frames. */
#include <pspkernel.h>
#include <pspaudio.h>
#include <stdio.h>
#include <string.h>
#include "sat.h"
#include "snd.h"
#include "snd_lib.h"
#include "bgm.h"
#include "cdda.h"
#include "sndlog.h"
#include <psppower.h>

#define BLOCK 1024

static SndCore *g_core;
static volatile int g_run;              /* the 68000 runs (SNDON); 0 = held in reset (SNDOFF) */
static volatile int g_reset;            /* SNDON came: reset the 68000 at the next tick */
static volatile int g_busy;             /* the thread is inside a driver tick */
static volatile int g_quit;
static SceUID g_thread = -1;
static int g_ch = -1;
static volatile unsigned g_ticks;
const char *snd_debug_dir;              /* development: data/snddump there -> every mixed block is appended to <dir>/snd_dump.raw (int16 stereo 44.1 kHz) and <dir>/snd_stats.txt gets the driver's state */
static FILE *g_dump, *g_stats;
static unsigned g_prof[0x600];
static int g_prof_on;                   /* development: data/sndprof counts the 68000's instructions per 16 bytes of code (snd_prof.txt) */
static unsigned g_blocks, g_peak;
static unsigned long long g_tick_us, g_active_sum, g_bgm_us, g_bgm_n;
static FILE *g_lib;
static void lib_log(const char *m) { if (g_lib) { fputs(m, g_lib); fputc(10, g_lib); fflush(g_lib); } }

static unsigned long long clock_us(void) { return sceKernelGetSystemTimeWide(); }
static float g_k = 32767.0f;                             /* sample scale: 32767 = full; data/sndvol (a percentage) lowers it */

/* development (data/sndlog, sndlog.h): what the audio thread measures */
static unsigned lg_blocks, lg_late, lg_gap_max, lg_comp_max, lg_tick_max, lg_clip, lg_near, lg_peak, lg_slots_max, lg_flush0;
static unsigned long long lg_comp_sum, lg_last_ret, lg_last_print;
static void lg_sample(int il, int ir)
{
    int a = il < 0 ? -il : il, b = ir < 0 ? -ir : ir;
    if (b > a) a = b;
    if ((unsigned)a > lg_peak) lg_peak = (unsigned)a;
    if (a > 32767) lg_clip++; else if (a >= 29491) lg_near++;
}

static int audio_main(SceSize args, void *argp)
{
    static short outb[2][BLOCK * 2] __attribute__((aligned(64)));
    short *out = outb[0];
    int ob = 0;
    static float tick[SND_TICK_FRAMES * 2];
    int have = 0, pos = 0;              /* frames left in tick[], position */
    (void)args; (void)argp;
    while (!g_quit) {
        int n = 0;
        while (n < BLOCK) {
            if (have == 0) {
                memset(tick, 0, sizeof tick);
                if (g_run) {
                    unsigned long long t0 = sceKernelGetSystemTimeWide();
                    g_busy = 1;
                    if (g_reset) { g_reset = 0; snd_core_reset(g_core); }
                    if (!snd_core_tick(g_core, tick)) { g_run = 0; }
                    g_ticks++;
                    g_active_sum += (unsigned)snd_core_active_slots(g_core);
                    g_busy = 0;
                    g_tick_us += sceKernelGetSystemTimeWide() - t0;
                    if (sndlog_on) { unsigned us = (unsigned)(sceKernelGetSystemTimeWide() - t0), sl = (unsigned)snd_core_active_slots(g_core); if (us > lg_tick_max) lg_tick_max = us; if (sl > lg_slots_max) lg_slots_max = sl; }
                }
                have = SND_TICK_FRAMES; pos = 0;
            }
            {
                int take = have < BLOCK - n ? have : BLOCK - n, i;
                for (i = 0; i < take; i++) {
                    float l = tick[2 * (pos + i)], r = tick[2 * (pos + i) + 1];
                    int il = (int)(l * g_k), ir = (int)(r * g_k);
                    if (sndlog_on) lg_sample(il, ir);
                    out[2 * (n + i)] = (short)(il > 32767 ? 32767 : il < -32768 ? -32768 : il);
                    out[2 * (n + i) + 1] = (short)(ir > 32767 ? 32767 : ir < -32768 ? -32768 : ir);
                }
                n += take; pos += take; have -= take;
            }
        }
        {
            unsigned long long b0 = g_stats ? sceKernelGetSystemTimeWide() : 0;
            bgm_mix(out, BLOCK);                                /* the songs of the menus: pre-rendered streams (bgm.c) */
            if (g_stats) { g_bgm_us += sceKernelGetSystemTimeWide() - b0; g_bgm_n++; }
        }
        if (g_dump) {
            int i;
            fwrite(out, BLOCK * 2 * sizeof(short), 1, g_dump);
            for (i = 0; i < BLOCK * 2; i++) { unsigned a = (unsigned)(out[i] < 0 ? -out[i] : out[i]); if (a > g_peak) g_peak = a; }
            if (++g_blocks % 40 == 0) {                          /* about every second */
                fflush(g_dump);
                if (g_prof_on && g_blocks % 200 == 0) {
                    char pp[300];
                    FILE *q;
                    int k, j;
                    snprintf(pp, sizeof pp, "%s/snd_ram.bin", snd_debug_dir);
                    q = fopen(pp, "wb");
                    if (q) { fwrite(sat->sndram, 1, 0x10000, q); fclose(q); }
                    snprintf(pp, sizeof pp, "%s/snd_prof.txt", snd_debug_dir);
                    q = fopen(pp, "w");
                    if (q) {
                        static unsigned tmp[0x600];
                        memcpy(tmp, g_prof, sizeof tmp);
                        fprintf(q, "ticks run %u\n", g_ticks);
                        for (k = 0; k < 40; k++) {
                            int best = 0;
                            for (j = 1; j < 0x600; j++) if (tmp[j] > tmp[best]) best = j;
                            if (!tmp[best]) break;
                            fprintf(q, "%04x-%04x %.1f per tick\n", best << 4, (best << 4) + 15, g_ticks ? (double)tmp[best] / g_ticks : 0.0);
                            tmp[best] = 0;
                        }
                        fclose(q);
                    }
                }
                if (g_stats) { unsigned long long tcpu, tmix, tsteps; snd_core_times(g_core, &tcpu, &tmix, &tsteps); rewind(g_stats); fprintf(g_stats, "skipped ticks %llu, timer B routine %.1f instructions per tick;  song stream %.1f us per block of 1024 frames;  blocks %u  driver ticks %u  run %d  pc %x  68000 ticks %u  peak %u  avg %.1f us per driver tick, %.1f slots playing on average;  68000 %.1f us (%.0f steps) + mixer %.1f us per tick\n", (unsigned long long)snd_core_skipped(g_core), g_ticks ? (double)snd_core_irq_steps(g_core) / g_ticks : 0.0, g_bgm_n ? (double)g_bgm_us / (double)g_bgm_n : 0.0, g_blocks, g_ticks, g_run, (unsigned)snd_core_pc(g_core), (unsigned)snd_core_ticks(g_core), g_peak, g_ticks ? (double)g_tick_us / g_ticks : 0.0, g_ticks ? (double)g_active_sum / g_ticks : 0.0, g_ticks ? (double)tcpu / g_ticks : 0.0, g_ticks ? (double)tsteps / g_ticks : 0.0, g_ticks ? (double)tmix / g_ticks : 0.0); fflush(g_stats); g_peak = 0; }
            }
        }
        if (sndlog_on) {
            unsigned long long t_ready = clock_us(), t_ret;
            unsigned comp, gap, blocked;
            sceAudioOutputBlocking(g_ch, PSP_AUDIO_VOLUME_MAX, out);
            t_ret = clock_us();
            if (lg_last_ret == 0) { lg_last_ret = t_ret; lg_last_print = t_ret; lg_flush0 = snd_lib_nflush; ob ^= 1; out = outb[ob]; sndlog("start: cpu %d MHz bus %d MHz, block %d frames (%d us)", scePowerGetCpuClockFrequency(), scePowerGetBusClockFrequency(), BLOCK, BLOCK * 1000000 / 44100); continue; }
            comp = (unsigned)(t_ready - lg_last_ret); blocked = (unsigned)(t_ret - t_ready); gap = (unsigned)(t_ret - lg_last_ret);
            lg_last_ret = t_ret;
            lg_blocks++; lg_comp_sum += comp;
            if (comp > lg_comp_max) lg_comp_max = comp;
            if (gap > lg_gap_max) lg_gap_max = gap;
            if (gap > 27000) {                                  /* a block is 23.2 ms: the hardware ran dry before the next one was handed over */
                lg_late++;
                sndlog("LATE block: %u.%u ms since the last (made in %u.%u ms, waited %u.%u ms), driver tick max %u us, %u slots, passes %u", gap / 1000, gap / 100 % 10, comp / 1000, comp / 100 % 10, blocked / 1000, blocked / 100 % 10, lg_tick_max, lg_slots_max, snd_lib_nflush - lg_flush0);
            }
            if (t_ret - lg_last_print >= 1000000) {
                sndlog("blocks %u  gap max %u.%u ms  late %u  make avg %u us max %u us  tick max %u us  slots max %u  clipped %u near %u peak %u%%  passes %u", lg_blocks, lg_gap_max / 1000, lg_gap_max / 100 % 10, lg_late, lg_blocks ? (unsigned)(lg_comp_sum / lg_blocks) : 0, lg_comp_max, lg_tick_max, lg_slots_max, lg_clip, lg_near, lg_peak * 100 / 32767, snd_lib_nflush - lg_flush0);
                { int cok, ctrack, clevel; unsigned cfr; cdda_info(&cok, &ctrack, &cfr, &clevel); sndlog("   music: player %s, track wanted %d, level %d, frames played %u", cok ? "up" : "NOT RUNNING", ctrack, clevel, cfr); }
                lg_blocks = lg_late = lg_gap_max = lg_comp_max = lg_tick_max = lg_clip = lg_near = lg_peak = lg_slots_max = 0; lg_comp_sum = 0;
                lg_last_print = t_ret; lg_flush0 = snd_lib_nflush;
            }
            ob ^= 1; out = outb[ob];
            continue;
        }
        sceAudioOutputBlocking(g_ch, PSP_AUDIO_VOLUME_MAX, out);
        ob ^= 1; out = outb[ob];
    }
    return 0;
}

static void hw_cpu(int on)                                  /* the SMPC's SNDON / SNDOFF */
{
    if (!on) {
        int guard = 0;
        g_run = 0;
        while (g_busy && guard++ < 200) sceKernelDelayThread(500);
    } else {
        g_reset = 1;
        g_run = 1;
    }
}

static void hw_wait(void) { sceKernelDelayThread(400); }

static void hook(u32 fn, u32 r4, u32 r5, u32 r6) { snd_lib_call(fn, r4, r5, r6); }

int snd_audio_start(void)
{
    if (g_thread >= 0) return 0;
    g_core = snd_core_new(sat->sndram);
    if (!g_core) return -1;
    snd_core_set_clock(g_core, clock_us);
    snd_core_set_hle(g_core, 1);                                /* the driver's slot-scan loop in C (src/snd.c; the output is identical, tools/test_snd.c SND_HLE=1) */
    snd_core_set_idle_skip(g_core, 1);                          /* no 68000 tick while the driver has nothing to do (silent, no command, no sequence): tools/bgm_render.c checks it) */
    g_ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, BLOCK, PSP_AUDIO_FORMAT_STEREO);
    if (g_ch < 0) return -2;
    g_quit = 0; g_run = 0; g_reset = 0;
    snd_hw_cpu = hw_cpu;
    snd_hw_wait = hw_wait;
    snd_call_hook = hook;
    if (snd_debug_dir) {
        char p[300];
        FILE *t;
        sndlog_init(snd_debug_dir);
        snprintf(p, sizeof p, "%s/sndvol", snd_debug_dir);
        t = fopen(p, "r");
        if (t) { int pc = 100; if (fscanf(t, "%d", &pc) == 1 && pc >= 10 && pc <= 100) g_k = 32767.0f * (float)pc / 100.0f; fclose(t); if (sndlog_on) sndlog("data/sndvol: output level %d%%", (int)(g_k * 100.0f / 32767.0f + 0.5f)); }
        snprintf(p, sizeof p, "%s/snd", snd_debug_dir);
        bgm_init(p, &sat->sndram[0x483]);
        snprintf(p, sizeof p, "%s/cd", snd_debug_dir);
        { char lp[300]; FILE *nc; snprintf(lp, sizeof lp, "%s/nocd", snd_debug_dir); nc = fopen(lp, "r");
          if (nc) { fclose(nc); if (sndlog_on) sndlog("data/nocd: the CD music is off"); }
          else { snprintf(lp, sizeof lp, "%s/snddump", snd_debug_dir); { FILE *dd = fopen(lp, "r"); if (dd) { fclose(dd); snprintf(lp, sizeof lp, "%s/cdda.txt", snd_debug_dir); cdda_init(p, lp); } else cdda_init(p, NULL); } } }
        sat_cd_hook = cdda_stop;
        snprintf(p, sizeof p, "%s/cdvol", snd_debug_dir);                      /* data/cdvol: the music's level in percent (a file with the number in it) */
        t = fopen(p, "r");
        if (t) { int pc = 100; if (fscanf(t, "%d", &pc) == 1) cdda_set_gain(pc); fclose(t); if (sndlog_on) sndlog("data/cdvol: music level %d%%", pc); }
        snprintf(p, sizeof p, "%s/snddump", snd_debug_dir);
        t = fopen(p, "r");
        if (t) {
            fclose(t);
            snprintf(p, sizeof p, "%s/snd_dump.raw", snd_debug_dir); g_dump = fopen(p, "wb");
            snprintf(p, sizeof p, "%s/snd_stats.txt", snd_debug_dir); g_stats = fopen(p, "w");
            snprintf(p, sizeof p, "%s/snd_lib.txt", snd_debug_dir); g_lib = fopen(p, "w");
            snprintf(p, sizeof p, "%s/sndprof", snd_debug_dir);
            t = fopen(p, "r");
            if (t) { fclose(t); g_prof_on = 1; snd_core_set_prof(g_core, g_prof); }
            snd_log_hook = lib_log;
        }
    }
    g_thread = sceKernelCreateThread("snd", audio_main, 0x12, 0x4000, PSP_THREAD_ATTR_USER, NULL);
    if (g_thread < 0) return -3;
    sceKernelStartThread(g_thread, 0, NULL);
    return 0;
}

void snd_audio_stop(void)
{
    if (g_thread < 0) return;
    g_quit = 1;
    sceKernelWaitThreadEnd(g_thread, NULL);
    sceKernelDeleteThread(g_thread);
    g_thread = -1;
    sat_cd_hook = NULL;
    cdda_shutdown();
    bgm_shutdown();
    sceAudioChRelease(g_ch);
    snd_call_hook = NULL;
}

void snd_audio_reset(void) { g_reset = 1; }
