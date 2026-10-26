/* M9: the race's sound events, see sndev.h.  Every function is a translation of the SH-2 code (names = the original's addresses); registers that carry a value into a call are named in the comments. */
#include "sndev.h"
#include "fx.h"

#define PLAY(s, id)   se_call(s, SND_PLAY, id, 0, 0)
#define WORD(s, w)    se_call(s, SND_WORD, w, 0, 0)
#define CMD(s, n)     se_call(s, SND_CMD, n, 0, 0)

/* 0x06016918: two commands of the driver */
static void cmd_pair(SndEv *s) { CMD(s, 17); CMD(s, 18); }

/* ---- 0x0601646C: wall and road contact ---- */
static void road_contact(SndEv *s, unsigned char *c, int *msg45)
{
    int orig = (signed char)c[0xC6], wall, idx;
    if (orig & 4) {
        c[0xC6] = (unsigned char)(orig & 0xFB);
        if (s->msg44 == 0) { *msg45 = 25; s->msg_req = 25; }
    }
    if (orig & 2) {                                         /* r1 still holds the byte as it was: the store puts bit 2 back */
        c[0xC6] = (unsigned char)(orig & 0xFD);
        if (se_status(s, 3) == 0) PLAY(s, 3);
    }
    wall = g32(c, 0x100);
    if (!(wall >= 0x1800)) return;
    if (wall >= 0xB000) c[0xC6] |= 4;
    idx = (signed char)c[0xF6];
    switch (idx) {
    case 3: case 12:                                        /* 0x0601656C */
        if (se_status(s, 2) == 0) PLAY(s, 2);
        break;
    case 4: case 5:                                         /* 0x060164EC */
        if (se_status(s, 4) == 0) { PLAY(s, 4); WORD(s, 0); }
        break;
    case 8: case 10:                                        /* 0x0601650C */
        if (se_status(s, 4) == 0) { PLAY(s, 4); WORD(s, 30); }
        break;
    case 1: case 6: case 7: case 9: case 11: case 13:       /* 0x0601652C: r8 = 7, r9 = 0 */
    case 2: {                                               /* 0x06016532: r8 = 23, r9 = 30 */
        int r8 = idx == 2 ? 23 : 7, r9 = idx == 2 ? 30 : 0;
        if (se_status(s, 4) == 0) {
            int v;
            PLAY(s, 4);
            v = g32(c, 0xC0);
            if (v < 0) v = neg(v);
            if (v > 0x1657 && !(v > 0x30DE8)) r9 = r8;
            WORD(s, r9);
        }
        break; }
    default: break;                                         /* 0: nothing */
    }
}

/* ---- 0x06016588: the wall contact counter of the course ---- */
static void wall_count(SndEv *s, unsigned char *c)
{
    if (g32(c, 0x100) >= 0x4000) s->wall[s->course & 3]++;
}

/* ---- 0x060165A4: tyre noise by surface ---- */
static void tyre_stop(SndEv *s)                             /* 0x0601666C */
{
    s->w2 = 0;
    se_call(s, SND_F24, 6, 0, 0);
    se_call(s, SND_F30, 5, 0, 0);
}

static int slip_pan(unsigned char *c)                       /* 0x06016706..: (L[+0xD4] * 16) >> 16 as a signed word, clamped to -15 .. 16 */
{
    int r4 = (short)((u32)g32(c, 0xD4) << 4 >> 16);
    if (!(16 >= r4)) r4 = 16;
    if (!(r4 >= -15)) r4 = -15;
    return r4;
}

static int absd(int d) { return d < 0 ? neg(d) : d; }

static void tyre_volume(SndEv *s, int v)                    /* 0x060166C6 / 0x060166DC */
{
    int r0 = (int)((u32)v >> 1 >> 8);
    if (!(125 >= r0)) r0 = 125;
    se_call(s, SND_F50, -64, r0, 0);
}

static void tyre_noise(SndEv *s, unsigned char *c)
{
    int v, idx, kind;                                       /* kind: 0 = tables A (0, 1, 8, 9), 1 = B (2, 3, 6, 7), 2 = C (4, 5) */
    if ((signed char)c[0xB2] == 0) { tyre_stop(s); return; }
    if ((s->frame & 3) != 0) return;
    idx = g16(c, 0xFA);
    if (s->w0 != idx) {
        s->w0 = idx;
        s->w2 = 0;
        se_call(s, SND_F24, 6, 0, 0);
    }
    v = g32(c, 0x98);                                       /* r0 on entry of the state handlers */
    switch (idx) {
    case 0: case 1: case 8: case 9: kind = 0; break;
    case 2: case 3: case 6: case 7: kind = 1; break;
    case 4: case 5: kind = 2; break;
    default: return;
    }
    if (kind == 0) {
        switch (s->w2) {
        case 0:                                             /* 0x06016680 */
            if (v == 0) { tyre_stop(s); return; }
            se_call(s, SND_F50, -64, 64, 0);
            s->w2 = 2;
            s->w4 = (short)v;
            PLAY(s, 25);
            break;
        case 2:                                             /* 0x0601669E */
            if (v == 0) { tyre_stop(s); return; }
            if (absd((int)(unsigned short)s->w4 - v) > 0x1333) {
                s->w2 = 4;
                s->w4 = (short)v;
                PLAY(s, 26);
            } else {
                tyre_volume(s, v);
            }
            break;
        case 4:                                             /* 0x060166DC */
            if (v == 0) { tyre_stop(s); return; }
            tyre_volume(s, v);
            break;
        default: break;
        }
    } else if (kind == 1) {
        switch (s->w2) {
        case 0:                                             /* 0x060166F6 */
            if (v == 0) return;
            WORD(s, 0x10F + slip_pan(c));
            s->w2 = 2;
            s->w4 = (short)v;
            PLAY(s, 7);
            break;
        case 2:                                             /* 0x06016732 */
            if (v == 0) { tyre_stop(s); return; }
            if (se_status(s, 7) == 1) WORD(s, 0x10F + slip_pan(c));
            if (absd((int)(unsigned short)s->w4 - v) > 0x1333) {
                static const signed char ids[4] = { 5, 6, 8, 5 };
                s->w4 = (short)v;
                PLAY(s, ids[v & 3]);
            }
            break;
        default: break;
        }
    } else {
        switch (s->w2) {
        case 0:                                             /* 0x06016798 */
            s->w2 = 2;
            PLAY(s, 30);
            break;
        case 2:                                             /* 0x060167A4 */
            if (se_status(s, 30) == 1) return;
            if (g32(c, 0x94) > 0x5ED0) { s->w2 = 2; PLAY(s, 30); }
            break;
        default: break;
        }
    }
}

/* ---- 0x060167C0: skid noise ---- */
static void skid_noise(SndEv *s, unsigned char *c)
{
    static const signed char tbl[20] = { 10, 30, 10, 24, 10, 18, 10, 12, 10, 6, 13, 5, 13, 4, 13, 3, 13, 3, 13, 3 };      /* 0x06016844: (class base, delay) per speed step; the original's table has these 10 pairs and then runs into code */
    int speed, idx, low, id;
    if (((s->frame + 2) & 3) != 0) return;
    if ((g16(c, 0xF8) & 0xCC) == 0) return;
    if (s->b6 != 0) { s->b6 = (signed char)(s->b6 - 1); return; }
    speed = g32(c, 0x94);
    if (!(speed > 0x17B4)) return;
    idx = (int)(((u32)speed << 2) >> 16) & 0xFF;
    if (idx > 9) idx = 9;                                   /* no car gets there (checked in the traces) */
    low = speed & 3;
    s->b6 = (signed char)(low + tbl[2 * idx + 1]);
    id = tbl[2 * idx] + (low == 3 ? 0 : low);
    PLAY(s, id);
}

/* ---- 0x06016858: road noise (one of two loops by surface class) ---- */
static void road_noise(SndEv *s, unsigned char *c)
{
    int idx, base;
    if (((s->frame + 1) & 3) != 0) return;
    idx = g16(c, 0xFA);
    switch (idx) {
    case 0: case 1: base = 72; break;
    case 2: case 3: case 6: case 7: base = 92; break;
    case 4: case 5: se_call(s, SND_F60, 64, 0, 0); return;
    case 8: case 9:                                         /* 0x0601689C */
        if (s->b7 != 0) return;
        s->b7 = 1;
        if (!s->stereo) PLAY(s, 24);
        CMD(s, 19);
        CMD(s, 20);
        return;
    default: return;
    }
    if (s->b7 != 0) {                                       /* 0x060168CA */
        s->timer = 0x1C20;
        s->b7 = 0;
        PLAY(s, 23);
        cmd_pair(s);
    }
    s->timer = (short)(s->timer - 1) & 0xFFFF;
    if ((short)s->timer == 0) {
        s->timer = 0x1C20;
        PLAY(s, 23);
    }
    {
        int r0 = (int)((u32)g32(c, 0x94) >> 12) + base;
        if (!(125 >= r0)) r0 = 125;
        se_call(s, SND_F60, 64, r0, 0);
    }
}

static void replay_sdctrl(SndEv *s, int *msg45)             /* 0x06016AD4: the sounds of the replay (the car's mode word is 2): a timeline on [0x060AFE00] (state) / [0x060AFE02] (timer) */
{
    static const unsigned short frames[4] = { 0x51, 0x51, 0x81, 0x87 };      /* 0x06016B04: by stage */
    switch (s->rp_state) {
    case 2:                                                                  /* 0x06016AF4 */
        s->rp_state = 4;
        s->rp_timer = frames[s->course & 3];
        break;
    case 4:                                                                  /* 0x06016B0C: the stage's phrase when the timer runs out */
        if (--s->rp_timer & 0xFFFF) break;
        s->rp_state = 6;
        *msg45 = s->msg_req = 38 + s->course;
        se_call(s, SND_FA4, -64, 0, 0);
        break;
    case 6: s->rp_state = 8; break;                                          /* 0x06016B30 */
    case 8:                                                                  /* 0x06016B38 */
        if (s->msg44 != 0) break;
        s->rp_state = 0;
        se_call(s, SND_F24, 0, 0, 0);
        se_call(s, SND_FA4, -32, 0, 0);
        break;
    default: break;
    }
}

void sndev_game_se(SndEv *s, unsigned char *c, int *msg45)
{
    int w8 = g16(c, 8), w10 = g16(c, 10);
    if (w8 == 2) { replay_sdctrl(s, msg45); return; }
    if (w10 == 2) { s->eng[0] = 69; s->eng[1] = 0; return; }
    if (w10 != 0) {
        int r0 = g16(c, 0xB2);
        if ((r0 & 0xFF) == 0 && r0 != 0) c[0xC6] |= 1;
        if (c[0xC6] & 1) {
            int sum = (int)((u32)g32(c, 60) + (u32)g32(c, 64) + (u32)g32(c, 68) + (u32)g32(c, 72));
            if (sum >= 0x66555) { c[0xC6] &= 0xFE; PLAY(s, 2); }
        }
        road_contact(s, c, msg45);
        wall_count(s, c);
        tyre_noise(s, c);
        if ((signed char)c[0xB2] == 0) {
            se_call(s, SND_F60, 64, 0, 0);
        } else {
            skid_noise(s, c);
            road_noise(s, c);
        }
    }
    {
        u32 x;
        int vol;
        s->eng[0] = ((u32)g32(c, 0x8C) >> 16) + 69u;
        x = (u32)g32(c, 0xA0) << 4;
        x = (x << 16 | x >> 16) << 1;
        vol = (int)x;
        if (!(16 > vol)) vol = 15;
        s->eng[1] = (unsigned)vol & 15u;
    }
}

void sndev_init(SndEv *s)
{
    se_call(s, SND_F3C, 0, 127, 0);
    if (s->mode810 != 2) {
        s->timer = 0x1C20;
        PLAY(s, 23);
        if (s->game_type == 10) { s->eng[2] = 69; s->eng[3] = 0; }
        s->eng[0] = 69; s->eng[1] = 0;
    }
    se_call(s, SND_FLUSH_WAIT, 0, 0, 0);
    s->b7 = 1; s->b2d = 1;
    se_call(s, SND_F60, 64, 0, 0);
    se_call(s, SND_FLUSH_WAIT, 0, 0, 0);
}

/* ---- FUN_06008BCC: the engine's throttle-lift noises; state in the car: +0x368 counter, +0x36C state ---- */
void sndev_torque(SndEv *s, unsigned char *c, int t, unsigned *rng)
{
    static const unsigned char modes[8] = { 1, 0, 0, 1, 1, 9, 0, 9 };       /* 0x06008C9C, indexed by the car's mode word / 2 */
    static const unsigned char ids[8] = { 20, 21, 20, 21, 20, 21, 20, 21 };  /* 0x06008C94 */
    int state = g32(c, 0x36C), mi = g16u(c, 8) >> 1;
    if (mi > 7) mi = 0;
    switch (state) {
    case 0:                                                                  /* 0x06008BE2: the torque stays high */
        if (t > 0x2666) {
            int n = g32(c, 0x368) + 1;
            p32(c, 0x368, n);
            if (n > 30) p32(c, 0x36C, 1);
        } else {
            int n = g32(c, 0x368) - 1;
            p32(c, 0x368, n >= 0 ? n : 0);
        }
        break;
    case 1:                                                                  /* 0x06008C0C: it drops: the car backfires once */
        if (!(0x28F > t)) break;
        if (g16(c, 10) != 0 && modes[mi] != 0 && c[0x5C] != 10 && c[0x5C] != 13) PLAY(s, 22);
        p32(c, 0x368, 0);
        p32(c, 0x36C, 2);
        break;
    case 2:                                                                  /* 0x06008C4A: and pops */
        if (g16(c, 10) != 0 && modes[mi] != 0) {
            unsigned r;
            if (rng) { *rng = *rng * 5u + 0x3537u; r = *rng; } else r = 0x0601D6CCu;
            PLAY(s, ids[r & 7]);
        }
        p32(c, 0x36C, 0);
        break;
    default: break;
    }
}

void sndev_kansei(SndEv *s)
{
    static const signed char ids[4] = { 16, 18, 17, 16 };                    /* 0x06016A1C */
    PLAY(s, ids[s->course & 3]);
}

void sndev_replay_start(SndEv *s)                                                    /* count_down_start_replay 0x06013F50: the first stage starts its replay music (track 14, the message 38, the sound timeline [0x060AFE00] = 6) */
{
    if ((s->course & 3) != 0) return;                                                /* (the other stages call 0x0600F804 with a table byte: the voice sample, not ported) */
    se_call(s, SND_CDPLAY, 14, 15, 0);
    se_call(s, SND_FA4, -64, 0, 0);
    s->rp_state = 6;
}

void sndev_replay_go(SndEv *s)                                                       /* 0x0601406C: the end of the replay's count-down on the other stages: the music, the jingle, the sound timeline [0x060AFE00] = 2 */
{
    static const unsigned char tracks[4] = { 4, 2, 3, 5 };
    int stage = s->course & 3;
    if (stage == 0) return;
    se_call(s, SND_CDPLAY, tracks[stage], 15, 0);
    se_call(s, SND_FA4, -64, 0, 0);
    PLAY(s, 86);
    s->rp_state = 2;
}

void sndev_go(SndEv *s)
{
    static const unsigned char tracks[6] = { 4, 2, 3, 5, 0xB, 9 };           /* 0x06013274: the CD tracks of the stages / the practice music choices */
    int sel = s->game_type == 0 ? s->course : s->bgm_sel;
    se_call(s, SND_CDPLAY, tracks[sel < 6 && sel >= 0 ? sel : 0], 15, 0);
    se_call(s, SND_FA4, s->opt_vol, 0, 0);
    se_call(s, SND_F24, 0, 0, 0);
}

void sndev_eng_idle(SndEv *s)
{
    s->eng[0] = 0x45; s->eng[1] = 0; s->eng[2] = 0x45; s->eng[3] = 0;
}
