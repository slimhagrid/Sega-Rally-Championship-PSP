/* M9: the CD audio player, see cdda.h.  One thread: waits for a request, opens data/cd/trackNN.mp3, feeds it to the sceMp3 decoder (the Media Engine does the decoding) and hands every decoded
 * frame (1152 samples) to a channel of its own of sceAudio, which the audio hardware mixes with the channel of the sound driver.  The main thread only changes "what should play" (a track
 * and repeat count with a generation number): the thread follows it.
 * The file is read into memory by a second thread (the memory stick can stall for tens of milliseconds, the audio channel has 26 ms of slack): the player only copies from that memory.  The
 * decoded samples go from the decoder straight to the audio hardware, as before. */
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include <pspkernel.h>
#include <pspaudio.h>
#include <pspmp3.h>
#include <psputility.h>
#include <psputility_avmodules.h>
#include <psputility_modules.h>
#include "cdda.h"
#include "sndlog.h"

#define FRAME 1152

static char dir[256];
static SceUID thread = -1;
static volatile int quit;
static volatile unsigned want_gen;                  /* changed by every play / stop */
static volatile int want_track, want_repeat;        /* 0 = nothing */
static volatile int paused;                         /* the pause menu holds the music (FUN_0600F7BE / FUN_0600F7E4) */
static int gain_pc = 100;                            /* data/cdvol: the music's level as a percentage of the game's own */
static volatile int want_level = 6;                 /* 0 .. 7: the level of the CD input */
static unsigned char mp3_buf[16 * 1024 + 1472] __attribute__((aligned(64)));
static unsigned char pcm_buf[2 * 9216] __attribute__((aligned(64)));
static short silence[FRAME * 2] __attribute__((aligned(64)));
static int ok;
static FILE *dbg_log;                                    /* development: data/snddump -> data/cdda.txt */
static void note(const char *fmt, int a, int b) { if (dbg_log) { fprintf(dbg_log, fmt, a, b); fputc(10, dbg_log); fflush(dbg_log); } if (sndlog_on) { char m[100]; snprintf(m, sizeof m, fmt, a, b); sndlog("CD: %s", m); } }

typedef struct {                                    /* the file of the track that plays */
    int fd;
    unsigned char *buf;                             /* the whole file, NULL = no memory: read from the file as the first version did */
    int size;
    volatile int loaded;                            /* bytes of buf that are valid */
    volatile int done, stop;
    SceUID thread;
} Load;
static Load ld;
static volatile unsigned played_frames;
static unsigned slowest_read, waits, dec_max;
static unsigned long long last_out;                /* development: the slowest read of the memory stick (us), feeds that had to wait for it */

void cdda_play(int track, int repeat)
{
    want_track = track; want_repeat = repeat;
    paused = 0;                                      /* a new play starts the music: the title stops the CD (FUN_0600F7BE) before its demo and then plays */
    __sync_synchronize();
    want_gen++;
}

void cdda_stop(void)
{
    if (!want_track) return;
    want_track = 0;
    __sync_synchronize();
    want_gen++;
}

void cdda_info(int *o, int *track, unsigned *frames, int *level) { *o = ok; *track = want_track; *frames = played_frames; *level = want_level; }
void cdda_set_gain(int percent) { if (percent >= 0 && percent <= 100) gain_pc = percent; }
void cdda_volume(int level) { want_level = (level & 0xFF) >> 5; }
void cdda_pause(int on) { paused = on; }

static int volume(void)                             /* the SCSP's direct send level: 6 dB per step below 7, 0 = off */
{
    int l = want_level;
    return l <= 0 ? 0 : (int)(0x8000 * powf(0.5f, (float)(7 - l)) * (float)gain_pc / 100.0f);
}

static int loader(SceSize args, void *argp)         /* reads the file into ld.buf, 16 KB at a time */
{
    (void)args; (void)argp;
    while (!ld.stop && ld.loaded < ld.size) {
        int want = ld.size - ld.loaded < 16384 ? ld.size - ld.loaded : 16384, n;
        unsigned long long t0 = sceKernelGetSystemTimeWide();
        unsigned us;
        n = sceIoRead(ld.fd, ld.buf + ld.loaded, want);
        us = (unsigned)(sceKernelGetSystemTimeWide() - t0);
        if (us > slowest_read) slowest_read = us;
        if (us > 40000) sndlog("CD: the memory stick took %u ms to read %d bytes", us / 1000, want);
        if (n <= 0) break;
        ld.loaded += n;
    }
    ld.done = 1;
    return 0;
}

static int feed(int h, unsigned gen)                /* 1 = data added, 0 = the end of the file */
{
    SceUChar8 *dst;
    SceInt32 want, pos;
    int n;
    if (sceMp3GetInfoToAddStreamData(h, &dst, &want, &pos) < 0) return 0;
    if (ld.buf) {
        int need = pos + want < ld.size ? pos + want : ld.size;
        if (ld.loaded < need && !ld.done) { waits++; sndlog("CD: the player waits for the memory stick (%d of %d bytes loaded)", ld.loaded, need); }
        while (ld.loaded < need && !ld.done && !quit && want_gen == gen) sceKernelDelayThread(2000);
        n = ld.loaded - pos;
        if (n > want) n = want;
        if (n < 0) n = 0;
        if (n > 0) {
            memcpy(dst, ld.buf + pos, (size_t)n);
            sceKernelDcacheWritebackRange(dst, (unsigned)n);        /* the decoder (another processor) reads it from memory */
        }
    } else {
        if (sceIoLseek32(ld.fd, pos, PSP_SEEK_SET) < 0) return 0;
        n = sceIoRead(ld.fd, dst, want);
        if (n < 0) n = 0;
    }
    sceMp3NotifyAddStreamData(h, n);
    return n > 0;
}

static void load_start(int fd, long long size)
{
    memset(&ld, 0, sizeof ld);
    ld.fd = fd; ld.thread = -1; ld.size = (int)size;
    sceIoLseek32(fd, 0, PSP_SEEK_SET);
    ld.buf = size > 0 && size < 12 * 1024 * 1024 ? (unsigned char *)memalign(64, (size_t)size) : NULL;
    if (!ld.buf) { note("no memory for the file (%d bytes)%d", (int)size, 0); return; }
    ld.thread = sceKernelCreateThread("cdload", loader, 0x1A, 0x4000, PSP_THREAD_ATTR_USER, NULL);
    if (ld.thread < 0) { free(ld.buf); ld.buf = NULL; return; }
    sceKernelStartThread(ld.thread, 0, NULL);
}

static void load_end(void)
{
    if (ld.thread >= 0) {
        ld.stop = 1;
        sceKernelWaitThreadEnd(ld.thread, NULL);
        sceKernelDeleteThread(ld.thread);
        ld.thread = -1;
    }
    free(ld.buf); ld.buf = NULL;
    if (ld.fd >= 0) sceIoClose(ld.fd);
    ld.fd = -1;
}

static int player(SceSize args, void *argp)
{
    int ch;
    unsigned seen = 0;
    (void)args; (void)argp;
    {
        int r1 = sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC), r2 = sceUtilityLoadModule(PSP_MODULE_AV_MP3), r3;
        note("the player thread starts: avcodec %x, mp3 module %x", r1, r2);
        if (r1 < 0) r1 = sceUtilityLoadAvModule(PSP_AV_MODULE_AVCODEC);          /* (the other way to load them, for firmwares that refuse the first) */
        if (r2 < 0) r2 = sceUtilityLoadAvModule(PSP_AV_MODULE_MP3);
        r3 = sceMp3InitResource();
        note("  second try: avcodec %x, mp3 module %x", r1, r2);
        note("  mp3 resource %x", r3, 0);
        if (r3 < 0) { note("the mp3 decoder is not available %d%d", 0, 0); ok = 0; return 0; }
    }
    ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, FRAME, PSP_AUDIO_FORMAT_STEREO);
    note("audio channel %d", ch, 0);
    if (ch < 0) { note("no audio channel %d%d", ch, 0); ok = 0; return 0; }
    ok = 1;
    ld.fd = -1;
    while (!quit) {
        int track, repeat, fd, h, loops, frames;
        SceMp3InitArg arg;
        char path[300];
        long long size;
        unsigned gen = want_gen;
        if (gen == seen || !want_track) { seen = gen; sceKernelDelayThread(10000); continue; }
        seen = gen;
        track = want_track; repeat = want_repeat;
        snprintf(path, sizeof path, "%s/track%02d.mp3", dir, track);
        fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
        note("track %d requested, open %x", track, fd);
        if (fd < 0) { note("track %d: no file (repeat %d)", track, repeat); continue; }
        size = sceIoLseek(fd, 0, PSP_SEEK_END);
        load_start(fd, size);
        note("  file of %d bytes, %d in memory", (int)size, ld.buf != NULL);
        memset(&arg, 0, sizeof arg);
        arg.mp3StreamStart = 0; arg.mp3StreamEnd = size;
        arg.mp3Buf = mp3_buf; arg.mp3BufSize = 16 * 1024;
        arg.pcmBuf = pcm_buf; arg.pcmBufSize = sizeof pcm_buf;
        h = sceMp3ReserveMp3Handle(&arg);
        note("  mp3 handle %d", h, 0);
        if (h < 0) { note("track %d: no handle (%d)", track, h); load_end(); continue; }
        while (sceMp3CheckStreamDataNeeded(h) > 0) if (!feed(h, gen)) break;
        { int ri = sceMp3Init(h); note("  mp3 init %x, rate %d", ri, sceMp3GetSamplingRate(h)); if (ri < 0) { sceMp3ReleaseMp3Handle(h); load_end(); continue; } }
        if (sceMp3GetSamplingRate(h) != 44100) { note("track %d: not a 44.1 kHz mp3 (%d)", track, sceMp3GetSamplingRate(h)); sceMp3ReleaseMp3Handle(h); load_end(); continue; }
        note("track %d: playing, repeat %d", track, repeat);
        sndlog("CD: track %d starts, repeat %d, level %d", track, repeat, want_level);
        last_out = 0;
        loops = repeat >= 15 ? -1 : repeat;
        frames = 0;
        while (!quit && want_gen == gen) {
            short *pcm = NULL;
            int n;
            if (paused) { sceKernelDelayThread(10000); continue; }
            if (sceMp3CheckStreamDataNeeded(h) > 0) feed(h, gen);
            { unsigned long long d0 = sndlog_on ? sceKernelGetSystemTimeWide() : 0; n = sceMp3Decode(h, &pcm); if (sndlog_on) { unsigned us = (unsigned)(sceKernelGetSystemTimeWide() - d0); if (us > dec_max) dec_max = us; } }
            if (n > 0) {
                int v = volume();
                if (n < FRAME * 4) { memset(silence, 0, sizeof silence); memcpy(silence, pcm, (size_t)n); pcm = silence; }
                if (sndlog_on) { unsigned long long now = sceKernelGetSystemTimeWide(); if (last_out && now - last_out > 33000) sndlog("CD: LATE music block, %u ms since the last (decode max %u us)", (unsigned)((now - last_out) / 1000), dec_max); }
                sceAudioOutputPannedBlocking(ch, v, v, pcm);
                if (sndlog_on) { last_out = sceKernelGetSystemTimeWide(); if (frames % 400 == 0) { sndlog("CD: track %d level %d, decode max %u us, stick read max %u us, waits %u", track, want_level, dec_max, slowest_read, waits); dec_max = 0; } }
                played_frames++;
                if (++frames == 100 || frames == 2000) note("track %d: %d frames played", track, frames);
                if (dbg_log && frames % 1000 == 0) note("  slowest read of the stick %d us, feeds that waited for it %d", (int)slowest_read, (int)waits);
            } else if (n == 0) {                    /* the end of the file */
                note("track %d: end of the file, loops left %d", track, loops);
                if (loops == 0) break;
                if (loops > 0) loops--;
                sceMp3ResetPlayPosition(h);
            } else break;
        }
        sceMp3ReleaseMp3Handle(h);
        load_end();
    }
    sceAudioChRelease(ch);
    return 0;
}

void cdda_init(const char *d, const char *logpath)
{
    if (logpath) dbg_log = fopen(logpath, "w");
    snprintf(dir, sizeof dir, "%s", d);
    quit = 0;
    thread = sceKernelCreateThread("cdda", player, 0x14, 0x8000, PSP_THREAD_ATTR_USER, NULL);
    if (thread >= 0) sceKernelStartThread(thread, 0, NULL);
}

void cdda_shutdown(void)
{
    if (thread < 0) return;
    quit = 1;
    sceKernelWaitThreadEnd(thread, NULL);
    sceKernelDeleteThread(thread);
    thread = -1;
}
