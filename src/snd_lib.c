/* M9: the SH-2 side of the sound system, see snd_lib.h.  Every function is a translation of the SH-2 code (tools/sh2dis.py, extracted/analysis/asm); the names are the original's addresses. */
#include "sat.h"
#include "satlib.h"
#include "snd_lib.h"
#include "bgm.h"
#include "cdda.h"
#include "sndlog.h"
#include <stdio.h>
#include <stdarg.h>

void (*snd_log_hook)(const char *msg);
static void slog(const char *fmt, ...)                      /* development: the library's calls (data/snddump) */
{
    char b[160];
    va_list ap;
    if (!snd_log_hook) return;
    va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    snd_log_hook(b);
}

unsigned snd_lib_nflush;                                    /* development (sndlog): passes that sent sound commands */
void (*snd_hw_cpu)(int on);
void (*snd_hw_wait)(void);

#define CMDQ     0x060AAA14u            /* command queue: byte count, then the command numbers (rows of the command table) */
#define IDQ      0x060AAA24u            /* sound id queue: byte count, then the ids */
#define WQ       0x060AAA34u            /* word queue: word count, then the words (volume / pan updates) */
#define CMDTBL   0x060A1268u            /* the driver commands: 8 bytes per row (PRGDATA.DAT) */
#define SETBL    0x060A1330u            /* sound id -> 4 parameter bytes of the driver's play command */
#define SLOT0    0x25A00700u
#define SLOTEND  0x25A00780u
#define FLAG4E0  0x25A004E0u            /* bit 7: the SH-2 has put commands into the slots, the 68000 clears it when it has taken them */
#define FLAG4E1  0x25A004E1u

static void wait_ack(void)                                  /* "loop while [0x25A004E0] bit 7": the 68000 clears it */
{
    int guard = 0;
    while ((R8(FLAG4E0) & 0x80) && guard++ < 2000) { if (snd_hw_wait) snd_hw_wait(); else break; }
    if (guard >= 2000) slog("wait_ack timed out");
}

/* ---- the queues ---- */
static void push_cmd(u32 n)                                 /* FUN_06016F10 */
{
    u32 c = R8(CMDQ);
    if (c == 15) return;
    c++;
    W8(CMDQ, c);
    W8(CMDQ + c, n);
}

static int cur_set = -1;                                    /* the sound set the screens last asked for (select_set's index): -1 = none / the race's */

void snd_lib_play(u32 id)                                   /* FUN_06016FBC */
{
    u32 c;
    if (cur_set >= 0 && bgm_start((unsigned)cur_set, R32(SETBL + 4u * id))) return;      /* a song of the menus is a stream (bgm.c), every other sound goes to the driver */
    c = R8(IDQ);
    if (c == 15) return;
    c++;
    W8(IDQ, c);
    W8(IDQ + c, id);
}

static void push_word(u32 w)                                /* FUN_0601704C */
{
    u32 c = R16(WQ);
    if (c == 4) return;
    c++;
    W16(WQ, c);
    W16(WQ + 2 * c, w);
}

static void f24(u32 r4) { if (r4 == 0) bgm_stop(); W8(CMDTBL + 0x2A, r4); push_cmd(5); }                                         /* FUN_06016F24 */
static void f30(u32 r4) { W8(CMDTBL + 0x42, r4); push_cmd(8); }                                         /* FUN_06016F30 */
static void f3c(u32 r4, u32 r5, u32 r6) { if (r4 == 0) bgm_fade((int)r5, (int)r6); W8(CMDTBL + 0x32, r4); W8(CMDTBL + 0x33, r5); W8(CMDTBL + 0x34, r6); push_cmd(6); }    /* FUN_06016F3C */
static void f50(u32 r4, u32 r5) { W8(CMDTBL + 0x3B, r4); W8(CMDTBL + 0x3D, r5); push_cmd(7); }                                   /* FUN_06016F50 */
static void f60(u32 r4, u32 r5) { W8(CMDTBL + 0x5B, r4); W8(CMDTBL + 0x5D, r5); push_cmd(0xB); }       /* FUN_06016F60 */
static void fa4(u32 r4) { cdda_volume((int)(r4 & 0xFF)); W8(CMDTBL + 0x62, r4); W8(CMDTBL + 0x63, r4); push_cmd(0xC); push_cmd(0xF); }  /* FUN_06016FA4 */

/* ---- the music's fade-out (FUN_0601734C / FUN_06017360 start it, FUN_06017374 steps it once per pass): the CD input's level goes down one step every `interval` passes through the table
 * 0xA0 .. 0x20 (from index 6 / 7) and ends with 0 (state at 0x060AAAD8: +0 active, +1 index, +2 interval word, +4 counter word) ---- */
#define FADE 0x060AAAD8u

static void fade_start(u32 index, u32 interval)
{
    W8(FADE, 1);
    W8(FADE + 1, index);
    W16(FADE + 2, interval);
    W16(FADE + 4, 1);
}

static void fade_step(void)
{
    static const u8 tab[8] = { 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0 };
    u32 left, idx;
    if (!R8(FADE)) return;
    left = (u32)((s32)(s16)R16(FADE + 4) - 1);
    W16(FADE + 4, left);                                    /* (the delay slot of the branch: stored whether or not it is zero) */
    if ((left & 0xFFFF) != 0) return;
    idx = R8(FADE + 1) - 1;
    W8(FADE + 1, idx);
    if (!tab[idx & 7]) { W8(FADE, 0); fa4(0); return; }
    W16(FADE + 4, R16(FADE + 2));
    fa4((u32)(s32)(s8)tab[idx & 7]);
}

static void f70(void)                                       /* FUN_06016F70: the queued commands into the slots */
{
    u32 n = R8(CMDQ), r13 = CMDQ + 1, r1 = SLOT0;
    if (!n) return;
    for (;;) {
        u32 r0;
        if (r1 == SLOTEND) return;
        r0 = (u32)R8(r13++) << 3;
        W32(r1, R32(CMDTBL + r0));
        W32(r1 + 4, R32(CMDTBL + r0 + 4));
        if (--n == 0) return;
        r1 += 0x10;
    }
}

static void fd0(void)                                       /* FUN_06016FD0: the sound ids and the words into the slots after the commands, then the queues are empty */
{
    u32 ids = R8(IDQ), r13, r1 = SLOT0 + 16u * R8(CMDQ), r7, w;
    if (ids) {
        r13 = IDQ + 1;
        r7 = ids;
        for (;;) {
            u32 r5;
            if (r1 == SLOTEND) goto clear;
            r5 = R32(SETBL + 4u * R8(r13++));
            slog("  slot %02x: play %08x", (unsigned)((r1 - SLOT0) >> 4), (unsigned)r5);
            W8(r1, 1); W16(r1 + 2, r5 >> 16); W16(r1 + 4, r5);
            r1 += 0x10;
            if (--r7 == 0) break;
        }
    }
    w = R16(WQ);
    if (!w) goto clear;
    r13 = WQ + 2;
    r7 = w;
    for (;;) {
        if (r1 == SLOTEND) goto clear;
        W8(r1, 0x12); W16(r1 + 2, R16(r13)); r13 += 2;
        r1 += 0x10;
        if (--r7 == 0) break;
    }
    W16(WQ, 0);
clear:
    W8(CMDQ, 0); W8(IDQ, 0);
}

/* ---- the co-driver voice: a phrase number in [0x060AAA45] is streamed from the work RAM (PCMDATA.DAT) through two 4 KB buffers of the sound RAM; the 68000 stream voice (commands 0x85 / 0x86) plays
 * them and flips the buffer byte [0x25A007A0].  The state is the work area at 0x060AAA44 (+0 playing, +1 phrase, +2 list index, +3 buffer, +4 state function, +8 source, +12 longs left, +16 destination,
 * +20 the phrase list of (source, length) blocks).  FUN_0601708C starts a phrase (from the flush), FUN_060170C4 runs the state function (from the V-blank handler). ---- */
#define VS 0x060AAA44u
enum { VF_IDLE = 0x060170CC, VF_START = 0x060170D0, VF_STREAM = 0x06017164, VF_END1 = 0x060171F4, VF_END2 = 0x06017238, VF_END3 = 0x0601726C, VF_SHORT = 0x0601727C, VF_SEND = 0x060172AC, VF_FINISH = 0x060172EC };
#define VBUF_E 0x25A7E000u
#define VBUF_F 0x25A7F000u
#define VBUF_D 0x25A7D000u
#define VSTAT 0x25A007A0u

static void v_copy(u32 *dst, u32 *src, u32 n) { while (n--) { W32(*dst, R32(*src)); *dst += 4; *src += 4; } }
static void v_zero(u32 *dst, u32 n) { while (n--) { W32(*dst, 0); *dst += 4; } }

static void voice_reset(void) { W32(VS, 0); W32(VS + 4, VF_IDLE); }                    /* FUN_06017080 */

static void v_finish_step(void)                                                        /* 0x060172EC: stop the stream voice once the 68000 has taken the last command */
{
    if (R8(FLAG4E0) & 0x80) return;
    W8(VS, 0); W32(VS + 4, VF_IDLE);
    slog("  voice: stop");
    sndlog("voice: stop");
    W32(SLOT0, 0x86000000u);
    W8(FLAG4E0, 0x80);
}
static void v_finish(void) { W32(VS + 4, VF_FINISH); v_finish_step(); }                /* 0x060172E8 */

static void v_send(u32 next)                                                           /* 0x060172AC (next = the stream state) / 0x0601727C (the phrase fits in the first block) */
{
    if (R8(FLAG4E0) & 0x80) return;
    W8(VS, 1); W32(VS + 4, next);
    slog("  voice: play command (state %08x)", (unsigned)next);
    W32(SLOT0, 0x850010C0u); W32(SLOT0 + 4, 0x7D002000u); W32(SLOT0 + 8, 0x70000000u);
    W8(FLAG4E0, 0x80);
}

/* the end of a block of `blk` longs: the rest of the buffer comes from the start of the next block of the phrase, or is silence once the list is over (then `after` is the next state) */
static void v_refill(u32 blk, u32 len, u32 *dst, u32 after)
{
    u32 pad = blk - len, idx = R8(VS + 2) + 4u, e, src, l2;
    W8(VS + 2, idx);
    e = R32(R32(VS + 20) + (idx & 0xFF));
    if (e) {
        src = R32(e); l2 = R32(e + 4);
        W32(VS + 12, l2 - pad);
        if (pad) v_copy(dst, &src, pad);
        W32(VS + 8, src);
    } else {
        W32(VS + 4, after);
        if (pad) v_zero(dst, pad);
    }
}

static void v_start(void)                                                              /* 0x060170D0: the first 0x400 longs */
{
    u32 src = R32(VS + 8), len = R32(VS + 12), dst = R32(VS + 16);
    W32(VS + 4, VF_SEND);
    if (len > 0x400) {
        W32(VS + 12, len - 0x400);
        v_copy(&dst, &src, 0x400);
        W32(VS + 8, src);
    } else {
        v_copy(&dst, &src, len);
        v_refill(0x400, len, &dst, VF_SHORT);
    }
    W8(VS + 3, 1);
    W32(VS + 16, dst);
}

static int v_buffer(u32 *dst, int tog)                                                 /* the top of the stream states: 0 = wait for the 68000, 1 = go on in the buffer, 2 = a new buffer is free: flip */
{
    u32 base = tog ? VBUF_F : VBUF_E;
    *dst = R32(VS + 16);
    if (*dst == base) {
        int st = RS8(VSTAT);
        *dst = tog ? VBUF_D : base;
        if (st != tog) return 0;
        return 2;
    }
    return 1;
}

static void v_stream(void)                                                             /* 0x06017164 */
{
    int tog = RS8(VS + 3), r;
    u32 dst, len, src;
    r = v_buffer(&dst, tog);
    if (!r) return;
    if (r == 2) { W8(VS + 3, (u32)(tog ^ 1)); slog("  voice: buffer flip, %u longs left", (unsigned)R32(VS + 12)); sndlog("voice: buffer flip, %u longs left", (unsigned)R32(VS + 12)); }
    len = R32(VS + 12); src = R32(VS + 8);
    if (len > 64) {
        W32(VS + 12, len - 64);
        v_copy(&dst, &src, 64);
        W32(VS + 8, src);
    } else {
        v_copy(&dst, &src, len);
        v_refill(64, len, &dst, VF_END1);
    }
    W32(VS + 16, dst);
}

static void v_end1(void)                                                               /* 0x060171F4: the list is over: silence until the last buffer has been taken */
{
    int tog = RS8(VS + 3), r;
    u32 dst;
    r = v_buffer(&dst, tog);
    if (!r) return;
    if (r == 2) { W32(VS + 4, VF_END2); W8(VS + 3, (u32)(tog ^ 1)); }
    else v_zero(&dst, 64);
    W32(VS + 16, dst);
}

static void v_end3(void) { if (RS8(VS + 3) == RS8(VSTAT)) v_finish(); }                /* 0x0601726C */

static void v_end2(void)                                                               /* 0x06017238 */
{
    int tog = RS8(VS + 3);
    u32 base = tog ? VBUF_F : VBUF_E, dst;
    if (tog == RS8(VSTAT)) { v_finish(); return; }
    dst = R32(VS + 16);
    if (base != dst) { v_zero(&dst, 64); W32(VS + 16, dst); return; }
    W32(VS + 4, VF_END3);
    v_end3();
}

void snd_lib_vint(void)                                                                /* FUN_060170C4, once per V-blank */
{
    if (sndlog_on) {                                                                   /* development: the gaps between the calls while a voice streams (a call is due every 16 ms) */
        static unsigned long long last;
        unsigned long long now = sndlog_us();
        if (R32(VS + 4) == VF_IDLE) last = 0;
        else { if (last && now - last > 40000) sndlog("voice: %u ms between two V-blank services", (unsigned)((now - last) / 1000)); last = now; }
    }
    switch (R32(VS + 4)) {
    case VF_START: v_start(); break;
    case VF_SEND: v_send(VF_STREAM); break;
    case VF_SHORT: v_send(VF_END2); break;
    case VF_STREAM: v_stream(); break;
    case VF_END1: v_end1(); break;
    case VF_END2: v_end2(); break;
    case VF_END3: v_end3(); break;
    case VF_FINISH: v_finish_step(); break;
    default: break;                                                                    /* VF_IDLE */
    }
}

static void voice_start(void)                                                          /* FUN_0601708C: a new phrase (called by the flush) */
{
    u32 w = R16(VS), msg = w & 0xFF, list, desc;
    if (!msg) return;
    if (w >> 8) { v_finish(); return; }                                                /* one is playing: it is stopped first */
    list = R32(0x00299BE8u + 4u * msg);
    desc = list ? R32(list) : 0;
    if (!desc) { W32(VS, 0); return; }                                                 /* no voice for this message */
    slog("voice %u", (unsigned)msg);
    sndlog("voice: phrase %u starts", (unsigned)msg);
    W32(VS + 20, list);
    W32(VS + 8, R32(desc)); W32(VS + 12, R32(desc + 4));
    W32(VS + 4, VF_START); W32(VS + 16, VBUF_D);
    W32(VS, 0);
}

void snd_lib_flush(void)                                    /* FUN_06017580 */
{
    snd_lib_nflush++;
    if (R8(FLAG4E0) & 0x80) return;
    voice_start();
    f70();
    fd0();
    W8(FLAG4E0, 0x80);
}

static void flush_wait(void)                                /* FUN_060175A4: the same after waiting for the 68000 (at most 65535 polls) */
{
    int guard = 0;
    while ((R8(FLAG4E0) & 0x80) && guard++ < 2000) { if (snd_hw_wait) snd_hw_wait(); else break; }
    if (R8(FLAG4E0) & 0x80) return;
    f70();
    fd0();
    W8(FLAG4E0, 0x80);
}

/* ---- sending the commands of the table (FUN_06016D70 / FUN_060176E8): row n (8 bytes) into the first slot, then wait for the acknowledge ---- */
static void send_row(u32 n)                                 /* FUN_06016D70 + _res_ei */
{
    if (!n) return;
    if (n == 0x17) bgm_stop();                                  /* the first command of a new sound set: everything stops */
    slog("  row %u: %08x %08x  (68000 status %02x %02x)", (unsigned)n, (unsigned)R32(CMDTBL + 8 * n), (unsigned)R32(CMDTBL + 8 * n + 4), (unsigned)R8(0x25A00422), (unsigned)R8(0x25A00423));
    W32(SLOT0, R32(CMDTBL + 8 * n));
    W32(SLOT0 + 4, R32(CMDTBL + 8 * n + 4));
    W8(FLAG4E0, 0x80);
    wait_ack();
}

static void mark_loaded(void)                               /* FUN_06016D90: byte +4 of every entry of the bank directory (0x25A00500, 8 bytes each, ended by 0xFF) */
{
    u32 a = 0x25A00500u;
    do { W8(a + 4, 0x80); a += 8; } while (R8(a) != 0xFF);
}

/* ---- the sound CPU and the driver ---- */
static void cpu_off(void) { bgm_stop(); if (snd_hw_cpu) snd_hw_cpu(0); }                 /* FUN_06017A82: SNDOFF */
static void cpu_on(void) { if (snd_hw_cpu) snd_hw_cpu(1); }                  /* FUN_06017A5C: SNDON */

static void stereo_flag(void)                               /* the end of FUN_06016C3C / FUN_06016B74: bit 7 of 0x25A00483 = the stereo / mono option bit of the unlock byte */
{
    W8(0x25A00483, R8(0x25A00483) & 0x7F);
    if (R8(0x06040062) & 0x20) W8(0x25A00483, R8(0x25A00483) | 0x80);
}

static void load_driver(u32 type)                           /* the middle of FUN_06016C3C */
{
    slog("load driver %u", (unsigned)type);
    cur_set = -1;
    const char *name = type == 2 ? "P2SDDRVS.DAT" : type == 0 ? "SDDRVS.DAT" : "ESDDRVS.DAT";
    u32 i;
    cpu_off();
    W8(0x25B00400, 2);
    for (i = 0; i < 0x2C00u; i++) W32(0x25A0B000u + 4 * i, 0);              /* rfillff(0, 0x2C00 longs, 0x25A0B000) */
    sat_load_file(name);
    cpu_on();
}

static void driver(u32 type)                                /* FUN_06016C3C */
{
    if (R8(0x060AA80B) != (u8)type) {
        W8(0x060AA80B, type);
        W8(0x060AA803, R8(0x060AA803) & 0xFE);
        load_driver(type);
        W8(0x060AA803, R8(0x060AA803) | 1);
        W8(FLAG4E1, R8(FLAG4E1) | 0x80);
    }
    stereo_flag();
}

void snd_lib_boot(void)                                     /* FUN_06016B74 */
{
    load_driver(0);
    W8(0x060AA82C, 1);
    W8(FLAG4E1, R8(FLAG4E1) | 0x80);
    stereo_flag();
    W8(0x060AA80B, 0);
    W8(0x060AA80D, 1);
    W8(0x060AAAD8, 0);
}

static void load_name(u32 p)                                /* FUN_0600F354: the file whose name string is at p */
{
    char name[40];
    int i = 0;
    while (R8(p) && i < 39) name[i++] = (char)R8(p++);
    name[i] = 0;
    slog("  load %s", name);
    if (name[0]) sat_load_file(name);
}

static void load_lists(u32 list)                            /* the loop of FUN_06016CF4 / FUN_06016DA4: the CD requests of a null-terminated list of request pointers */
{
    for (;;) {
        u32 req = R32(list);
        list += 4;
        if (!req) break;
        load_name(req);
    }
}

static void sound_set(u32 map, u32 row_a, u32 row_b, u32 list)      /* FUN_06016CF4 (driver 0) */
{
    driver(0);
    send_row(0x17);
    send_row(1);
    W8(CMDTBL + 0x22, map);
    send_row(4);
    load_lists(list);
    mark_loaded();
    send_row(row_a);
    send_row(row_b);
    send_row(0x18);
    W8(CMDQ, 0); W8(IDQ, 0);
    fa4(0xC0);
    flush_wait();
}

static void race_set(u32 map, u32 row_a, u32 row_b, u32 list)       /* FUN_06016DA4 (the single player path) */
{
    u32 type = (u32)RS16(0x060AA860), stage = (u32)RS16(0x060AA87E), car = R8(0x060AA80A), v, ecar;
    bgm_stop();                                             /* the race has no stream: the music is the CD's */
    cur_set = -1;
    driver(type == 10 ? 2 : 1);
    if (type != 0 || stage == 0) send_row(0x17);
    send_row(1);
    W8(CMDTBL + 0x22, map);
    send_row(4);
    load_lists(list);
    if (type == 10) {                                       /* two player battle: not connected yet */
    } else {
        ecar = R32(0x00299200u + 4 * car);
        v = R8(0x060AA811) ? R8(0x060AA81B) : 2;
        load_name(R32(ecar + 4 * v));
    }
    send_row(4);
    send_row(row_a);
    send_row(row_b);
    send_row(0x18);
    W8(CMDQ, 0); W8(IDQ, 0);
    flush_wait();
}

/* ---- the entry points the screens call ---- */
static void select_set(u32 index, u32 id)                   /* FUN_060173AC (the byte table at 0x06017410) */
{
    static const u8 maps[8] = { 0, 3, 4, 5, 6, 7, 0, 9 };
    u32 flags = R8(0x060AA80D);
    if (flags & 3) {
        if (!(flags & 2)) sound_set(maps[index & 7], 3, 9, R32(0x060173F8u + 4 * index));      /* bit 0: the set has to be loaded (bit 1: it is there) */
        cur_set = (int)index;
        if (id) snd_lib_play(id);
        W8(0x060AA80D, 0);
        f3c(0, 0x7F, 0);
        flush_wait();
    }
    W8(0x060AAAD8, 0);
}

static void race_sound_set(void)                            /* FUN_06017418: the arcade / practice race */
{
    static const u8 maps[4] = { 1, 2, 2, 1 };
    u32 stage = (u32)RS16(0x060AA87E);
    W8(0x060AAAD8, 0);                                      /* FUN_06017454: the queues are empty */
    W32(CMDQ, 0); W32(IDQ, 0); W32(WQ, 0);
    if (R8(0x060AA810) == 2) return;                        /* a replay (the ending): FUN_06017628, not ported yet */
    race_set(maps[stage & 3], 3, 9, R32((R8(0x06040062) & 0x20 ? 0x00298F88u : 0x00298F78u) + 4 * stage));
    voice_reset();                                          /* FUN_06017080 */
}

int snd_lib_status(int id)                                  /* FUN_06016F04: the 68000's status byte of the class of sound `id` (the byte at 0x25A00780 + 2 * class) */
{
    return (int)(signed char)R8(0x25A00780u + 2u * (u32)(int)(signed char)R8(SETBL + 4u * (u32)id));
}

void snd_lib_call(u32 fn, u32 r4, u32 r5, u32 r6)
{
    slog("call %08x %u %u %u", (unsigned)fn, (unsigned)r4, (unsigned)r5, (unsigned)r6);
    if (sndlog_on && fn != 0x06017580u && fn != 0x0601704Cu && fn != 0x060170C4u && fn != 0x060175A4u) sndlog("lib %08x %u %u %u", (unsigned)fn, (unsigned)r4, (unsigned)r5, (unsigned)r6);
    switch (fn) {
    case 0x06016FBC: snd_lib_play(r4); break;
    case 0x0601704C: push_word(r4); break;
    case 0x06016F50: f50(r4, r5); break;
    case 0x06016F10: push_cmd(r4); break;
    case 0x06016F24: f24(r4); break;
    case 0x06016F30: f30(r4); break;
    case 0x06016F3C: f3c(r4, r5, r6); break;
    case 0x06016F60: f60(r4, r5); break;
    case 0x06016FA4: fa4(r4); break;
    case 0x06017580: snd_lib_flush(); break;
    case 0x060175A4: flush_wait(); break;
    case 0x060173AC: select_set(r4, r5); break;
    case 0x06017418: race_sound_set(); break;
    case 0x0601753C: W32(0x25A007C0u, 0); W32(0x25A007E0u, 0); break;
    case 0x06017548: W32(0x25A007C0u, 0x45); W32(0x25A007C4u, 0); W32(0x25A007E0u, 0x45); W32(0x25A007E4u, 0); break;
    case 0x060170C4: snd_lib_vint(); break;
    case 0x0601734C: fade_start(6, r4); break;
    case 0x06017360: fade_start(7, r4); break;
    case 0x06017374: fade_step(); break;
    case 0x0600F756: cdda_play((int)r4, (int)r5); break;                  /* FUN_0600F756: the CD track (the drive plays the audio; the PSP plays data/cd/trackNN.mp3) */
    case 0x0600F7BE: cdda_pause(1); break;                                  /* FUN_0600F7BE / FUN_0600F7E4: the race's pause menu stops and goes on with the CD music */
    case 0x0600F7E4: cdda_pause(0); break;
    case 0x06017454: W8(0x060AAAD8, 0); W32(CMDQ, 0); W32(IDQ, 0); W32(WQ, 0); break;
    case 0x06017080: voice_reset(); break;
    default: break;                                         /* the CD play (0x0600F756) and the rest: not part of the library's SH-2 side */
    }
}
