/* M9: the pre-rendered songs of the menu screens, see bgm.h.  Format: tools/bgm_enc.c ("BGM2").  A reader thread keeps a ring of superblocks (1008 frames each) filled from the memory stick;
 * the audio thread decodes them and adds them to the block it hands to sceAudio.  The main thread only posts requests (start / stop / fade) into a queue the audio thread empties at the start
 * of every block.  Without __PSP__ (the PC test, tools/test_bgm.c) the ring is filled by bgm_mix itself. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bgm.h"
#ifdef __PSP__
#include <pspkernel.h>
#endif

#define SUPER 1008
#define NBLK 36
#define SBYTES (4 + NBLK * 15)                  /* one channel of a superblock */
#define RING 32
#define RECMAX (2 * SBYTES)
#define MAXSONG 16
#define HDR 24

typedef struct { unsigned set, value; char name[48]; int ok; uint32_t frames, loop, ch[2]; } Song;
static Song songs[MAXSONG];
static int nsongs;
static char dir[256];
static const volatile unsigned char *stereo_flag;

/* ---- requests: main thread -> audio thread ---- */
typedef struct { int type, a, b; } Req;
enum { R_START = 1, R_STOP, R_FADE };
static Req rq[16];
static volatile unsigned rq_w, rq_r;
static void post(int type, int a, int b)
{
    unsigned w = rq_w;
    if (w - rq_r >= 16) return;
    rq[w & 15].type = type; rq[w & 15].a = a; rq[w & 15].b = b;
    __sync_synchronize();
    rq_w = w + 1;
}

/* ---- the reader: file -> ring ---- */
typedef struct { char path[300]; unsigned sb, recsize, nsb, loop; } Params;
static Params prm[4];
static volatile unsigned rd_gen, rd_ack;        /* generation asked for by the audio thread / taken over by the reader */
static uint8_t ring[RING][RECMAX];
static volatile int ring_sb[RING];
static volatile unsigned ring_w, ring_r;
static volatile int rd_eof;
static volatile int rd_idle = 1;
static volatile int rd_quit;

static int rd_step(void)                        /* one read; 0 = nothing to do */
{
    static FILE *fp;
    static unsigned my_gen, next_sb, recsize, nsb, loop;
    unsigned g = rd_gen;
    if (my_gen != g) {
        const Params *p = &prm[g & 3];
        if (fp) { fclose(fp); fp = NULL; }
        fp = fopen(p->path, "rb");
        next_sb = p->sb; recsize = p->recsize; nsb = p->nsb; loop = p->loop;
        if (fp) fseek(fp, (long)(HDR + (unsigned long)next_sb * recsize), SEEK_SET);
        my_gen = g;
        ring_w = ring_r = 0; rd_eof = fp == NULL;
        __sync_synchronize();
        rd_ack = g;
        return 1;
    }
    if (!fp || rd_eof || rd_gen != g) return 0;
    {
        unsigned freeslots = RING - (ring_w - ring_r), at = ring_w % RING, k = freeslots;
        if (freeslots < 4) return 0;
        if (k > RING - at) k = RING - at;
        if (k > nsb - next_sb) k = nsb - next_sb;
        if (k > 8) k = 8;
        if (k == 0) return 0;
        {
            unsigned i;
            for (i = 0; i < k; i++) {
                if (fread(ring[at + i], 1, recsize, fp) != recsize) { rd_eof = 1; k = i; break; }
                ring_sb[at + i] = (int)(next_sb + i);
            }
        }
        __sync_synchronize();
        ring_w += k; next_sb += k;
        if (next_sb >= nsb) {
            if (loop) { next_sb = 0; fseek(fp, HDR, SEEK_SET); } else rd_eof = 1;
        }
    }
    return 1;
}

#ifdef __PSP__
static SceUID rd_thread = -1;
static int reader_main(SceSize args, void *argp)
{
    (void)args; (void)argp;
    while (!rd_quit) {
        if (rd_idle) { sceKernelDelayThread(20000); continue; }
        if (!rd_step()) sceKernelDelayThread(5000);
    }
    return 0;
}
#endif

/* ---- the decoder ---- */
static const int c1[5] = { 0, 60, 115, 98, 122 };
static const int c2[5] = { 0, 0, -52, -55, -60 };

static void dec_super(const uint8_t *rec, int16_t *out)
{
    int h1 = (int16_t)(rec[0] | (rec[1] << 8)), h2 = (int16_t)(rec[2] | (rec[3] << 8)), k;
    const uint8_t *b = rec + 4;
    for (k = 0; k < NBLK; k++, b += 15) {
        int sh = b[0] & 15, f = b[0] >> 4, i;
        int a1, a2;
        if (sh > 12) sh = 12;
        if (f > 4) f = 0;
        a1 = c1[f]; a2 = c2[f];
        sh = 12 - sh;                                           /* the step is 1 << (12 - shift): exact, no shift of a negative number */
        for (i = 0; i < 14; i++) {
            int byte = b[1 + i], s;
            s = ((h1 * a1 + h2 * a2) >> 6) + ((((int)((unsigned)byte << 28)) >> 28) << sh);              /* low nibble, sign extended */
            if ((unsigned)(s + 32768) > 65535u) s = s < 0 ? -32768 : 32767;
            h2 = h1; h1 = s; *out++ = (int16_t)s;
            s = ((h1 * a1 + h2 * a2) >> 6) + ((((int)((unsigned)(byte >> 4) << 28)) >> 28) << sh);
            if ((unsigned)(s + 32768) > 65535u) s = s < 0 ? -32768 : 32767;
            h2 = h1; h1 = s; *out++ = (int16_t)s;
        }
    }
}

/* ---- the player (audio thread) ---- */
static int cur = -1, variant, cur_ch, cur_nsb;
static int16_t dec[2][SUPER];
static int dec_len, dec_pos, dec_sb = -1, skip_pending;
static float g_db, tgt_db, rate_db;             /* gain in dB, the fade's target, dB per frame */
static int fade_delay;
static unsigned my_gen;

static int variant_now(void) { return stereo_flag ? (*stereo_flag >> 7) & 1 : 0; }

static void open_stream(int song, int v, int sb)
{
    Song *s = &songs[song];
    Params *p;
    my_gen = rd_gen + 1;
    p = &prm[my_gen & 3];
    snprintf(p->path, sizeof p->path, "%s/%s_%d.bgm", dir, s->name, v);
    p->nsb = (s->frames + SUPER - 1) / SUPER;
    p->sb = (unsigned)sb < p->nsb ? (unsigned)sb : 0;
    p->recsize = s->ch[v] * SBYTES;
    p->loop = s->loop;
    cur_ch = (int)s->ch[v]; cur_nsb = (int)p->nsb;
    variant = v;
    dec_len = dec_pos = 0; dec_sb = -1;
    __sync_synchronize();
    rd_idle = 0;
    rd_gen = my_gen;
}

static void do_start(int song)
{
    cur = song;
    g_db = tgt_db = 0.0f; rate_db = 0.0f; fade_delay = 0;
    skip_pending = 0;
    open_stream(song, variant_now(), 0);
}

static void do_stop(void)
{
    cur = -1;
    rd_idle = 1;
    dec_len = dec_pos = 0;
}

static void do_fade(int vol, int time)
{
    tgt_db = vol >= 127 ? 0.0f : -(float)(127 - (vol < 0 ? 0 : vol)) * 0.756f;
    if (time <= 0) { g_db = tgt_db; fade_delay = 0; rate_db = 0.0f; }
    else { rate_db = (400.0f / (float)time) / 44100.0f; fade_delay = 4410; }                    /* measured on the driver: about 400 / time dB per second after a short delay */
}

static int next_super(void)                     /* 1 = decoded, 0 = nothing yet, -1 = the song is over */
{
    int slot, sb, valid, ch;
    uint32_t frames;
    if (rd_ack != my_gen) return 0;
    if (ring_r == ring_w) return rd_eof ? -1 : 0;
    slot = (int)(ring_r % RING);
    sb = ring_sb[slot];
    frames = songs[cur].frames;
    valid = (int)(frames - (uint32_t)sb * SUPER);
    if (valid > SUPER) valid = SUPER;
    for (ch = 0; ch < cur_ch; ch++) dec_super(ring[slot] + ch * SBYTES, dec[ch]);
    __sync_synchronize();
    ring_r++;
    dec_len = valid; dec_sb = sb;
    dec_pos = skip_pending < valid ? skip_pending : 0;
    skip_pending = 0;
    return 1;
}

static void lin_ramp(float db0, float db1, int *g0, int *g1)
{
    *g0 = (int)(32768.0f * expf(db0 * 0.11512925f));
    *g1 = (int)(32768.0f * expf(db1 * 0.11512925f));
}

void bgm_mix(short *out, int frames)
{
    while (rq_r != rq_w) {
        Req r = rq[rq_r & 15];
        rq_r++;
        if (r.type == R_START) do_start(r.a);
        else if (r.type == R_STOP) do_stop();
        else if (r.type == R_FADE && cur >= 0) do_fade(r.a, r.b);
    }
    if (cur < 0) return;
    if (variant_now() != variant) {             /* the stereo option was changed: the other rendering of the song, from the same position */
        if (dec_sb >= 0) { skip_pending = dec_pos; open_stream(cur, variant_now(), dec_sb); }
        else open_stream(cur, variant_now(), 0);
    }
#ifndef __PSP__
    while (rd_step()) { }
#endif
    while (frames > 0) {
        int n, g0, g1, i;
        float d0 = g_db, eff;
        if (dec_pos >= dec_len) {
            int r = next_super();
            if (r < 0) { do_stop(); return; }
            if (r == 0) return;
            continue;
        }
        n = dec_len - dec_pos;
        if (n > frames) n = frames;
        if (n > 128) n = 128;
        eff = (float)n;
        if (fade_delay > 0) { int d = fade_delay < n ? fade_delay : n; fade_delay -= d; eff = (float)(n - d); }
        if (g_db > tgt_db) { g_db -= rate_db * eff; if (g_db < tgt_db) g_db = tgt_db; }
        else if (g_db < tgt_db) { g_db += rate_db * eff; if (g_db > tgt_db) g_db = tgt_db; }
        if (g_db <= -90.0f && tgt_db <= -90.0f) { do_stop(); return; }
        if (d0 == 0.0f && g_db == 0.0f) g0 = g1 = 32768;
        else lin_ramp(d0, g_db, &g0, &g1);
        {
            const int16_t *l = dec[0] + dec_pos, *r = dec[cur_ch > 1 ? 1 : 0] + dec_pos;
            if (g0 == 32768 && g1 == 32768) {                       /* the usual case: no fade */
                for (i = 0; i < n; i++) {
                    int a = out[0] + l[i], b = out[1] + r[i];
                    out[0] = (short)(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
                    out[1] = (short)(b > 32767 ? 32767 : b < -32768 ? -32768 : b);
                    out += 2;
                }
            } else {
                int g = g0 << 8, gs = ((g1 - g0) << 8) / n;
                for (i = 0; i < n; i++, g += gs) {
                    int a = out[0] + ((l[i] * (g >> 8)) >> 15), b = out[1] + ((r[i] * (g >> 8)) >> 15);
                    out[0] = (short)(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
                    out[1] = (short)(b > 32767 ? 32767 : b < -32768 ? -32768 : b);
                    out += 2;
                }
            }
        }
        dec_pos += n; frames -= n;
    }
}

/* ---- the sound library's side ---- */
void bgm_init(const char *d, const volatile unsigned char *stereo_byte)
{
    char path[320];
    FILE *f;
    snprintf(dir, sizeof dir, "%s", d);
    stereo_flag = stereo_byte;
    nsongs = 0;
    snprintf(path, sizeof path, "%s/bgm.idx", dir);
    f = fopen(path, "r");
    if (!f) return;
    {
        char line[128];
        while (nsongs < MAXSONG && fgets(line, sizeof line, f)) {
            Song *s = &songs[nsongs];
            unsigned set, value;
            char name[48];
            int v, good = 1;
            if (sscanf(line, "%u %x %47s", &set, &value, name) != 3) continue;
            memset(s, 0, sizeof *s);
            s->set = set; s->value = value; snprintf(s->name, sizeof s->name, "%s", name);
            for (v = 0; v < 2; v++) {
                FILE *g;
                unsigned char h[HDR];
                snprintf(path, sizeof path, "%s/%s_%d.bgm", dir, name, v);
                g = fopen(path, "rb");
                if (!g) { good = 0; break; }
                if (fread(h, 1, HDR, g) != HDR || memcmp(h, "BGM2", 4)) { good = 0; fclose(g); break; }
                fclose(g);
                { uint32_t fr = h[4] | (h[5] << 8) | (h[6] << 16) | ((uint32_t)h[7] << 24); if (v && fr != s->frames) good = 0; s->frames = fr; }
                s->loop = h[8]; s->ch[v] = h[12];
                if (h[20] != (SUPER & 255) || h[21] != (SUPER >> 8) || s->ch[v] < 1 || s->ch[v] > 2) good = 0;
            }
            s->ok = good;
            nsongs++;
        }
    }
    fclose(f);
#ifdef __PSP__
    rd_quit = 0;
    rd_thread = sceKernelCreateThread("bgmrd", reader_main, 0x19, 0x4000, PSP_THREAD_ATTR_USER, NULL);
    if (rd_thread >= 0) sceKernelStartThread(rd_thread, 0, NULL);
#endif
}

int bgm_start(unsigned set, unsigned value)
{
    int i;
    for (i = 0; i < nsongs; i++) {
        if (songs[i].ok && songs[i].set == set && songs[i].value == value) { post(R_START, i, 0); return 1; }
    }
    return 0;
}

void bgm_stop(void) { post(R_STOP, 0, 0); }
void bgm_fade(int vol, int time) { post(R_FADE, vol, time); }
int bgm_playing(void) { return cur >= 0; }

void bgm_shutdown(void)
{
#ifdef __PSP__
    rd_quit = 1;
    if (rd_thread >= 0) { sceKernelWaitThreadEnd(rd_thread, NULL); sceKernelDeleteThread(rd_thread); rd_thread = -1; }
#endif
}
