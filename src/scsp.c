/* M9: SCSP model, see scsp.h.  Fixed-point playback (12 bit sample fraction), the envelope and the gains evaluated once per block of 8 samples: the PSP has to mix 20 slots of an organ jingle
 * 44100 times a second next to the game.  The gains come from tables (made with the same expressions as the formulas they replace), the slots are mixed into integer accumulators. */
#include <math.h>
#include <string.h>
#include "scsp.h"

#define RATE 44100.0f
#define BLOCK 8

static const float AR_TIMES[64] = { 100000, 100000, 8100.0f, 6900.0f, 6000.0f, 4800.0f, 4000.0f, 3400.0f, 3000.0f, 2400.0f, 2000.0f, 1700.0f, 1500.0f, 1200.0f, 1000.0f, 860.0f, 760.0f, 600.0f, 500.0f, 430.0f, 380.0f, 300.0f,
    250.0f, 220.0f, 190.0f, 150.0f, 130.0f, 110.0f, 95.0f, 76.0f, 63.0f, 55.0f, 47.0f, 38.0f, 31.0f, 27.0f, 24.0f, 19.0f, 16.0f, 14.0f, 12.0f, 9.4f, 7.9f, 6.8f, 6.0f, 4.7f, 3.9f, 3.4f, 3.0f, 2.4f, 2.0f, 1.7f,
    1.5f, 1.2f, 1.0f, 0.86f, 0.76f, 0.60f, 0.50f, 0.43f, 0.38f, 0.30f, 0.25f, 0.22f };
static const float DR_TIMES[64] = { 100000, 100000, 118200.0f, 101300.0f, 88600.0f, 70900.0f, 59100.0f, 50700.0f, 44300.0f, 35500.0f, 29600.0f, 25300.0f, 22200.0f, 17700.0f, 14800.0f, 12700.0f, 11100.0f, 8900.0f, 7400.0f, 6300.0f,
    5500.0f, 4400.0f, 3700.0f, 3200.0f, 2800.0f, 2200.0f, 1800.0f, 1600.0f, 1400.0f, 1100.0f, 920.0f, 790.0f, 690.0f, 550.0f, 460.0f, 390.0f, 340.0f, 270.0f, 230.0f, 200.0f, 170.0f, 140.0f, 110.0f, 98.0f,
    85.0f, 68.0f, 57.0f, 49.0f, 43.0f, 34.0f, 28.0f, 25.0f, 22.0f, 18.0f, 14.0f, 12.0f, 11.0f, 8.5f, 7.1f, 6.1f, 5.4f, 4.3f, 3.6f, 3.1f };

static float db2lin(float db) { return powf(10.0f, db / 20.0f); }

static unsigned r16(const Scsp *s, unsigned o) { return ((unsigned)s->regs[o] << 8) | s->regs[o + 1]; }

/* gain tables: indexed by the register fields, made once with the formulas the per-slot code used to evaluate (powf on the PSP is slow) */
static float t_tl[256], t_dl[8], t_pan[16], t_target_gain[32];
static float t_ar_inc[32], t_d_db[32], t_d_f[32];
static int t_ready;

static void tables_init(void)
{
    int i;
    if (t_ready) return;
    for (i = 0; i < 256; i++) t_tl[i] = db2lin(-(float)i * 0.375f);
    for (i = 0; i < 8; i++) t_dl[i] = i == 0 ? 0.0f : db2lin(-(float)(7 - i) * 6.0f);
    for (i = 0; i < 16; i++) t_pan[i] = i != 15 ? db2lin(-(float)i * 3.0f) : 0.0f;
    for (i = 0; i < 32; i++) {
        float ar_ms = AR_TIMES[i ? 2 * i : 0], d_ms = DR_TIMES[i ? 2 * i : 0];
        float db = d_ms < 100000.0f ? 96.0f * 1000.0f / RATE / d_ms * BLOCK : 0.0f;
        t_target_gain[i] = db2lin(-(i < 31 ? (float)i * 3.0f : 96.0f));
        t_ar_inc[i] = ar_ms <= 0.0f ? -1.0f : (1000.0f / RATE * BLOCK) / (ar_ms < 100000.0f ? ar_ms : 1e9f);
        t_d_db[i] = db;
        t_d_f[i] = db2lin(-db);
    }
    t_ready = 1;
}

void scsp_init(Scsp *s, const uint8_t *ram)
{
    memset(s, 0, sizeof *s);
    s->ram = ram;
    tables_init();
}

void scsp_write(Scsp *s, uint32_t off, int size, uint32_t v)
{
    int k;
    off &= 0xFFF;
    for (k = 0; k < size; k++) s->regs[(off + (uint32_t)k) & 0xFFF] = (uint8_t)(v >> (8 * (size - 1 - k)));
    if (off < 0x400 && (off & 0x1F) < 2 && (size >= 2 || (off & 0x1F) == 0)) {
        unsigned n = off >> 5;
        if (r16(s, n * 0x20) & 0x1000) {                    /* KYONEX: every slot takes over its KYONB */
            unsigned i;
            for (i = 0; i < 32; i++) {
                ScspSlot *sl = &s->slot[i];
                int kb = (r16(s, i * 0x20) >> 11) & 1;
                if (kb && (sl->state == 0 || sl->state == 4)) {
                    sl->state = 1; sl->pos = 0.0f; sl->dir = 1; sl->amp = 0.0f; sl->level_db = 0.0f; sl->gain = 1.0f;
                    sl->fpos = 0; sl->fdir = 1;
                } else if (!kb && sl->state >= 1 && sl->state <= 3) {
                    sl->state = 4;
                }
            }
            s->regs[n * 0x20] &= 0xEF;
        }
    }
}

static inline int pcm_be16(const uint8_t *p) { return (int)(int16_t)(((unsigned)p[0] << 8) | p[1]); }

void scsp_render(Scsp *s, float *out, int n)
{
    int32_t acc[2 * 128];
    int si, k, any = 0;
    s->active = 0;
    if (n > 128) n = 128;
    for (si = 0; si < 32; si++) {
        ScspSlot *sl = &s->slot[si];
        unsigned b = (unsigned)si * 0x20, c, v;
        int pcm8, lpctl, sa, lsa, lea, ar, d1r, d2r, rr, dl, tl, oct, fns, disdl, dipan, i, blk;
        float tlg, dlg, pl, pr, target_gain, target, d1_db, d2_db, rr_db, d1_f, d2_f, rr_f, ar_inc;
        uint32_t step, endf, lsaf, pos;
        int dir, safe;
        const uint8_t *ram = s->ram, *pcm;
        if (sl->state == 0) continue;
        if (!any) { memset(acc, 0, sizeof(int32_t) * 2 * (size_t)n); any = 1; }
        s->active++;
        c = r16(s, b);
        pcm8 = (c >> 4) & 1; lpctl = (c >> 5) & 3;
        sa = (int)(((c & 0xF) << 16) | r16(s, b + 2));
        lsa = (int)r16(s, b + 4); lea = (int)r16(s, b + 6);
        v = r16(s, b + 8); ar = (int)(v & 31); d1r = (int)((v >> 6) & 31); d2r = (int)((v >> 11) & 31);
        v = r16(s, b + 0xA); rr = (int)(v & 31); dl = (int)((v >> 5) & 31);
        tl = (int)(r16(s, b + 0xC) & 255);
        v = r16(s, b + 0x10); oct = (int)((v >> 11) & 15); if (oct >= 8) oct -= 16; fns = (int)(v & 1023);
        v = r16(s, b + 0x16); disdl = (int)((v >> 13) & 7); dipan = (int)((v >> 8) & 31);
        {
            uint32_t base = 4096u + 4u * (uint32_t)fns;                 /* (1 + fns / 1024) * 4096, exact; the octave shifts it (rounded like the float version) */
            step = oct >= 0 ? base << oct : (base + (1u << (-oct - 1))) >> -oct;
        }
        tlg = t_tl[tl];
        dlg = t_dl[disdl];
        {
            float att = t_pan[dipan & 15];
            pl = (dipan & 0x10) == 0 ? 1.0f : att;
            pr = (dipan & 0x10) == 0 ? att : 1.0f;
        }
        ar_inc = t_ar_inc[ar]; d1_db = t_d_db[d1r]; d1_f = t_d_f[d1r]; d2_db = t_d_db[d2r]; d2_f = t_d_f[d2r]; rr_db = t_d_db[rr]; rr_f = t_d_f[rr];
        target = dl < 31 ? (float)dl * 3.0f : 96.0f;
        target_gain = t_target_gain[dl];
        endf = (uint32_t)lea << 12; lsaf = (uint32_t)lsa << 12;
        pos = sl->fpos; dir = sl->fdir;
        pcm = ram + (sa & 0x7FFFF);
        safe = lpctl != 3 && dir > 0 && (uint32_t)sa + (pcm8 ? 1u : 2u) * ((uint32_t)(lea > lsa ? lea : lsa) + 2u) <= 0x80000u;     /* the fast loops read straight from the RAM: forward only, nothing wraps around the end of it */
        for (blk = 0; blk < n; blk += BLOCK) {
            int cnt = n - blk < BLOCK ? n - blk : BLOCK;
            float lvl, g;
            int gl, gr;
            int32_t *o = acc + 2 * blk;
            if (sl->state == 1) {
                if (ar_inc < 0.0f) sl->amp = 1.0f; else sl->amp += ar_inc;
                if (sl->amp >= 1.0f) { sl->amp = 1.0f; sl->state = 2; sl->level_db = 0.0f; sl->gain = 1.0f; }
                lvl = sl->amp;
            } else if (sl->state == 2) {
                sl->level_db += d1_db; sl->gain *= d1_f;
                if (sl->level_db >= target) { sl->level_db = target; sl->gain = target_gain; sl->state = 3; }
                lvl = sl->gain;
            } else if (sl->state == 3) {
                sl->level_db += d2_db; sl->gain *= d2_f;
                if (sl->level_db >= 96.0f) { sl->state = 0; break; }
                lvl = sl->gain;
            } else {
                sl->level_db += rr_db; sl->gain *= rr_f;
                if (sl->level_db >= 96.0f) { sl->state = 0; break; }
                lvl = sl->gain;
            }
            g = lvl * tlg * dlg * 16384.0f;
            gl = (int)(g * pl); gr = (int)(g * pr);
            if (gl == 0 && gr == 0) {                               /* inaudible: the position still runs on */
                if (lpctl == 0) {
                    pos += step * (uint32_t)cnt;
                    if (pos >= endf) { sl->state = 0; break; }
                } else if (lpctl == 3) {
                    pos += step * (uint32_t)cnt * (uint32_t)dir;
                } else {
                    uint32_t span = endf > lsaf ? endf - lsaf : 4096;
                    pos += step * (uint32_t)cnt;
                    while (pos >= endf) pos -= span;
                }
                continue;
            }
            i = 0;
            if (safe) {
                while (i < cnt) {
                    if (pos >= endf) {
                        if (lpctl == 0) { sl->state = 0; break; }
                        {
                            uint32_t span = endf > lsaf ? endf - lsaf : 4096;
                            pos = lsaf + (pos - lsaf) % span;
                        }
                    }
                    if (pcm8) {
                        if (gl == gr) {                                         /* centred: one product for both sides */
                            do {
                                const int8_t *q = (const int8_t *)pcm + (pos >> 12);
                                int s0 = q[0], v = ((s0 << 8) + (((q[1] - s0) * (int)(pos & 4095)) >> 4)) * gl >> 14;       /* (q1 * 256 - q0 * 256) * f >> 12 = (q1 - q0) * f >> 4 */
                                o[2 * i] += v;
                                o[2 * i + 1] += v;
                                pos += step; i++;
                            } while (i < cnt && pos < endf);
                        } else {
                            do {
                                const int8_t *q = (const int8_t *)pcm + (pos >> 12);
                                int s0 = q[0], smp = (s0 << 8) + (((q[1] - s0) * (int)(pos & 4095)) >> 4);
                                o[2 * i] += (smp * gl) >> 14;
                                o[2 * i + 1] += (smp * gr) >> 14;
                                pos += step; i++;
                            } while (i < cnt && pos < endf);
                        }
                    } else {
                        if (gl == gr) {
                            do {
                                const uint8_t *q = pcm + 2 * (pos >> 12);
                                int s0 = pcm_be16(q), v = (s0 + (((pcm_be16(q + 2) - s0) * (int)(pos & 4095)) >> 12)) * gl >> 14;
                                o[2 * i] += v;
                                o[2 * i + 1] += v;
                                pos += step; i++;
                            } while (i < cnt && pos < endf);
                        } else {
                            do {
                                const uint8_t *q = pcm + 2 * (pos >> 12);
                                int s0 = pcm_be16(q), smp = s0 + (((pcm_be16(q + 2) - s0) * (int)(pos & 4095)) >> 12);
                                o[2 * i] += (smp * gl) >> 14;
                                o[2 * i + 1] += (smp * gr) >> 14;
                                pos += step; i++;
                            } while (i < cnt && pos < endf);
                        }
                    }
                }
                if (sl->state == 0) break;
                continue;
            }
            for (; i < cnt; i++) {                                  /* the general loop: alternating loops, backwards, samples at the end of the RAM */
                int s0, s1, smp;
                uint32_t idx, fr;
                if (lpctl == 0) {
                    if (pos >= endf) { sl->state = 0; break; }
                } else if (lpctl == 3) {
                    if (dir > 0 && pos >= endf) { pos = endf - (pos - endf); dir = -1; }
                    else if (dir < 0 && (int32_t)pos <= (int32_t)lsaf) { pos = lsaf + (lsaf - pos); dir = 1; }
                } else if (pos >= endf) {
                    uint32_t span = endf > lsaf ? endf - lsaf : 4096;
                    pos = lsaf + (pos - lsaf) % span;
                }
                idx = pos >> 12; fr = pos & 4095;
                if (pcm8) {
                    uint32_t a = ((uint32_t)sa + idx) & 0x7FFFF;
                    s0 = (int)(int8_t)ram[a] * 256; s1 = (int)(int8_t)ram[(a + 1) & 0x7FFFF] * 256;
                } else {
                    uint32_t a = ((uint32_t)sa + 2 * idx) & 0x7FFFF;
                    s0 = (int)(int16_t)(((unsigned)ram[a] << 8) | ram[(a + 1) & 0x7FFFF]);
                    s1 = (int)(int16_t)(((unsigned)ram[(a + 2) & 0x7FFFF] << 8) | ram[(a + 3) & 0x7FFFF]);
                }
                smp = s0 + (((s1 - s0) * (int)fr) >> 12);
                o[2 * i] += (smp * gl) >> 14;
                o[2 * i + 1] += (smp * gr) >> 14;
                pos += step * (uint32_t)dir;
            }
            if (sl->state == 0) break;
        }
        sl->fpos = pos; sl->fdir = dir;
    }
    if (any) for (k = 0; k < 2 * n; k++) out[k] += (float)acc[k] * (1.0f / 32768.0f);
}
