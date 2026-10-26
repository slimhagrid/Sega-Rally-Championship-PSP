/* M9: the sound system core, see snd.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snd.h"
#include "m68k.h"
#include "scsp.h"
#ifdef SND_PROF
#include <time.h>
static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }
double snd_prof_cpu_ns, snd_prof_render_ns; unsigned long snd_prof_steps;
#endif

#define SCSP_BASE 0x100000u

struct SndCore {
    M68k cpu;
    Scsp scsp;
    uint8_t *ram;
    unsigned scipd;                 /* 0x420: pending interrupt bits (6 = timer A, 7 = timer B) */
    int acked;                      /* the driver has acknowledged timer A (SCIRE bit 6) in this tick */
    int idle_polls;                 /* polls of SCIPD that found nothing pending after the acknowledge */
    int stop;
    int ready;
    uint32_t ticks;
    int dead;
    void (*log_cb)(void *, uint32_t, uint32_t, int, uint32_t);
    void *log_ud;
    int b_call;
    int hle_on;                     /* the driver's loops done in C (hle_slot_scan, hle_entries) */
    uint32_t ent_head, ent_exit, ent_stride;      /* the sequencer loop found by hook_entries */
    int idle_skip;                  /* run no 68000 tick while the driver has nothing to do (see idle()) */
    unsigned long long skipped, n_irq;
    unsigned *prof;                 /* statistics: executions per 16 bytes of the code area */
    unsigned long long (*clock_us)(void);
    unsigned long long t_cpu, t_mix, n_steps;
    void (*trace_cb)(void *, const M68k *);
    void *trace_ud;
};

static uint32_t io_read(void *ud, uint32_t addr, int size)
{
    SndCore *c = (SndCore *)ud;
    uint32_t off = addr - SCSP_BASE, v = 0;
    int k;
    if (addr < SCSP_BASE || off >= 0x1000) return 0;
    if (off >= 0x420 && off < 0x422) {                              /* SCIPD */
        if (c->acked && !(c->scipd & 0xC0)) { if (++c->idle_polls >= 2) c->stop = 1; }
        v = c->scipd;
        return size == 1 ? ((off & 1) ? v & 0xFF : v >> 8) : size == 2 ? v : v << 16;
    }
    if (off >= 0x408 && off < 0x40A) {                              /* MSLC / CA: the slot selected by the driver (bits 15..11) and the upper bits of its sample counter (bits 10..7): the voice stream watches the ring buffer with it */
        unsigned r = ((unsigned)c->scsp.regs[0x408] << 8) | c->scsp.regs[0x409];
        r = (r & 0xF800) | ((c->scsp.slot[(r >> 11) & 31].fpos >> 24) & 0xF) << 7;
        return size == 1 ? ((off & 1) ? r & 0xFF : r >> 8) : size == 2 ? r : r << 16;
    }
    for (k = 0; k < size; k++) v = (v << 8) | c->scsp.regs[(off + (uint32_t)k) & 0xFFF];
    return v;
}

static void io_write(void *ud, uint32_t addr, int size, uint32_t v)
{
    SndCore *c = (SndCore *)ud;
    uint32_t off = addr - SCSP_BASE;
    if (addr < SCSP_BASE || off >= 0x1000) return;
    if (off >= 0x422 && off < 0x424) {                              /* SCIRE: acknowledge */
        unsigned clr = size == 1 ? ((off & 1) ? v & 0xFF : (v & 0xFF) << 8) : v & 0xFFFF;
        c->scipd &= ~clr;
        if (clr & 0x40) c->acked = 1;
    }
    if (c->log_cb) c->log_cb(c->log_ud, c->ticks, off, size, v);
    scsp_write(&c->scsp, off, size, v);
}

/* The driver's tick costs about 500 instructions even when nothing plays (it scans the 8 command slots and the 32 voices).  Nothing it does then is visible: no command is waiting
 * (the slot bytes, the SH-2's flag 0x4E0), no voice is sounding, no sequence is running (the status bytes of the sound classes at 0x780 ..) and the engine words the race writes are 0. */
static int idle(const SndCore *c)
{
    const uint8_t *r = c->ram;
    int k;
    if (c->scsp.active || (r[0x4E0] & 0x80)) return 0;
    for (k = 0; k < 8; k++) if (r[0x700 + 16 * k]) return 0;
    for (k = 0x780; k < 0x7A0; k++) if (r[k]) return 0;
    for (k = 0x7C0; k < 0x7C8; k++) if (r[k]) return 0;
    for (k = 0x7E0; k < 0x7E8; k++) if (r[k]) return 0;
    return 1;
}

/* ---- the driver's loops in C (m68k_hook).  The driver spends most of its instructions on loops that, in the usual state, only find that there is nothing to do.  Each hook does the work of
 * a run of those instructions on the registers and returns how many instructions it stood for; with `verify` set (tests) the same instructions are also run on a copy of the CPU and every
 * register is compared.  Only the code of the known drivers is hooked (the loops are looked for and checked in snd_core_reset). ---- */
static int hle_verify;

static void verify_hook(const M68k *before, const M68k *after, int count, const char *name)
{
    M68k ref = *before;
    int i;
    ref.hook_off = 1;
    for (i = 0; i < count; i++) m68k_step(&ref);
    if (memcmp(ref.d, after->d, sizeof ref.d) || memcmp(ref.a, after->a, sizeof ref.a) || ref.pc != after->pc || ref.sr != after->sr) {
        fprintf(stderr, "hook %s: registers differ after %d instructions from pc %x (pc %x / %x, sr %x / %x)\n", name, count, (unsigned)before->pc, (unsigned)ref.pc, (unsigned)after->pc, ref.sr, after->sr);
        for (i = 0; i < 8; i++) if (ref.d[i] != after->d[i]) fprintf(stderr, "  d%d %08x / %08x\n", i, (unsigned)ref.d[i], (unsigned)after->d[i]);
        for (i = 0; i < 8; i++) if (ref.a[i] != after->a[i]) fprintf(stderr, "  a%d %08x / %08x\n", i, (unsigned)ref.a[i], (unsigned)after->a[i]);
        abort();
    }
}

/* The main loop looks for a command in the 8 slots at 0x700 .. 0x77F several times per tick (0x1326: 5 instructions per slot).  With all slots empty the loop leaves d0 = ..0780,
 * a0 = 0x770, d7.b = 0, the flags of ADDI.B #$10 on 0x70 (X C Z clear, N V set) and goes to the RTS at 0x13B0: done in one step. */
static int slot_scan(M68k *m, int *count)
{
    const uint8_t *r = m->ram;
    int k;
    for (k = 0; k < 8; k++) if (r[0x700 + 16 * k]) return 0;
    m->d[0] = (m->d[0] & 0xFFFF0000u) | 0x0780u;
    m->a[0] = 0x770;
    m->d[7] &= 0xFFFFFF00u;
    m->sr = (uint16_t)((m->sr & 0xFFE0u) | 0x0Au);
    m->pc = 0x13B0;
    *count = 41;
    return 1;
}

/* The sequencer step (timer B's routine): for each of the 32 entries of a table at (a5), 12 bytes apart, "BSR body; ADDA.L #12,A5 | LEA 12(A5),A5; DBRA D7": an entry without bit 7 in its first
 * byte is left at once (BTST.B #7,(A5) -> Z set, BEQ, RTS).  Runs through the idle entries in C; a busy entry runs as the driver's code. */
static int entries(M68k *m, int *count)
{
    const SndCore *c = (const SndCore *)m->ud;
    uint32_t a5 = m->a[5], d7 = m->d[7] & 0xFFFFu, stride = c->ent_stride;
    int n = 0;
    for (;;) {
        if (a5 + stride > m->ram_size || (m->ram[a5] & 0x80)) break;
        a5 += stride; n++;
        if (d7 == 0) { d7 = 0xFFFF; break; }
        d7--;
    }
    if (!n) return 0;
    m->a[5] = a5;
    m->d[7] = (m->d[7] & 0xFFFF0000u) | d7;
    m->sr |= 4;                                                     /* the last BTST found the bit clear: Z */
    m->pc = d7 == 0xFFFF ? c->ent_exit : c->ent_head;
    *count = 6 * n;
    return 1;
}

/* The single player driver's voice scan (0x1414, called once per tick): for 8 voices "MOVE.W D4,D5; ADD.W D5,D5; MOVE.B $34(A4,D5.W),D0; BMI skip; MOVE.B (A4,D5.W),D0; BMI skip; ADD.W D4,D4;
 * CLR.W D5; MOVE.B 1(A4,D4.W),D5; LSL.W #5,D5; OR.W 6(A4,D4.W),D5; MOVE.W 8(A6,D5.W),D5; MOVE.L 8(A4,D4.W),D0; BNE work; LSR.W #1,D4; BRA skip" and at skip "ADDI.W #$20,D4;
 * DBRA D7".  A voice that is flagged (bit 7 of either byte) or has no program (the long is 0) only loads some registers; the iterations of those voices are done here, the first voice that has
 * work is left to the driver's code. */
static int voices(M68k *m, int *count)
{
    const uint8_t *r = m->ram;
    uint32_t sz = m->ram_size, d0 = m->d[0], d4 = m->d[4], d5 = m->d[5], d7 = m->d[7], a4 = m->a[4], a6 = m->a[6], flags = m->sr & 0x1F;
    int n = 0, instr = 0, done = 0;
    for (;;) {
        uint32_t t5 = (d4 << 1) & 0xFFFF, o, w, p;
        uint32_t nd0 = d0, nd4 = d4, nd5 = d5, res, x;
        int skip = 0, ni;
        nd5 = (nd5 & 0xFFFF0000u) | t5;                                         /* MOVE.W D4,D5; ADD.W D5,D5 */
        p = a4 + (uint32_t)(int32_t)(int16_t)t5 + 0x34;
        if (p >= sz) break;
        nd0 = (nd0 & 0xFFFFFF00u) | r[p];
        if (r[p] & 0x80) { skip = 1; ni = 4; }
        else {
            p = a4 + (uint32_t)(int32_t)(int16_t)t5;
            if (p >= sz) break;
            nd0 = (nd0 & 0xFFFFFF00u) | r[p];
            if (r[p] & 0x80) { skip = 1; ni = 6; }
            else {
                o = (nd4 << 1) & 0xFFFF;                                        /* ADD.W D4,D4 */
                p = a4 + (uint32_t)(int32_t)(int16_t)o + 1;
                if (p + 12 >= sz) break;
                nd5 = (nd5 & 0xFFFF0000u) | r[p];                               /* CLR.W D5; MOVE.B 1(A4,D4.W),D5 */
                nd5 = (nd5 & 0xFFFF0000u) | (((nd5 & 0xFFFF) << 5) & 0xFFFF);  /* LSL.W #5,D5 */
                p = a4 + (uint32_t)(int32_t)(int16_t)o + 6;
                nd5 |= ((uint32_t)r[p] << 8) | r[p + 1];                        /* OR.W 6(A4,D4.W),D5 */
                p = a6 + 8 + (uint32_t)(int32_t)(int16_t)nd5;
                if (p + 1 >= sz) break;
                nd5 = (nd5 & 0xFFFF0000u) | ((uint32_t)r[p] << 8) | r[p + 1]; /* MOVE.W 8(A6,D5.W),D5 */
                p = a4 + (uint32_t)(int32_t)(int16_t)o + 8;
                w = ((uint32_t)r[p] << 24) | ((uint32_t)r[p + 1] << 16) | ((uint32_t)r[p + 2] << 8) | r[p + 3];
                if (w) break;                                                   /* this voice has work: not ours (nothing of this iteration is kept) */
                nd0 = 0;                                                        /* MOVE.L 8(A4,D4.W),D0 */
                nd4 = (nd4 & 0xFFFF0000u) | (o >> 1);                           /* LSR.W #1,D4: D4 is back to what it was */
                ni = 16;
            }
        }
        (void)skip;
        x = nd4 & 0xFFFF;                                                       /* ADDI.W #$20,D4 */
        res = (x + 0x20) & 0xFFFF;
        flags = ((x + 0x20) > 0xFFFF ? 0x11u : 0u) | (res & 0x8000 ? 8u : 0u) | (res == 0 ? 4u : 0u) | (!(x & 0x8000) && (res & 0x8000) ? 2u : 0u);
        nd4 = (nd4 & 0xFFFF0000u) | res;
        d0 = nd0; d4 = nd4; d5 = nd5;
        n++; instr += ni + 2;                                                   /* + ADDI, DBRA */
        if ((d7 & 0xFFFF) == 0) { d7 = (d7 & 0xFFFF0000u) | 0xFFFF; done = 1; break; }
        d7 = (d7 & 0xFFFF0000u) | ((d7 - 1) & 0xFFFF);
    }
    if (!n) return 0;
    m->d[0] = d0; m->d[4] = d4; m->d[5] = d5; m->d[7] = d7;
    m->sr = (uint16_t)((m->sr & 0xFFE0u) | flags);
    m->pc = done ? 0x1484 : 0x1414;
    *count = instr;
    return 1;
}

/* The single player driver's track step (0x4CD6 .. 0x4D12, eight times "BSR $50D4; LEA $60(A6),A6", the last without the LEA): a track whose first byte has bit 7 clear is left at once (MOVE.B
 * (A6),D0; BPL; RTS).  The idle tracks are done here, the first one that is busy is left to the driver's code. */
static int tracks(M68k *m, int *count)
{
    const uint8_t *r = m->ram;
    uint32_t a6 = m->a[6], d0 = m->d[0], flags = 0;
    int n = 0, instr = 0;
    while (n < 8) {
        uint32_t b;
        if (a6 >= m->ram_size) break;
        b = r[a6];
        if (b & 0x80) break;
        d0 = (d0 & 0xFFFFFF00u) | b;
        flags = b == 0 ? 4u : 0u;                                               /* MOVE.B: N Z, V and C cleared */
        if (n < 7) { a6 += 0x60; instr += 5; } else instr += 4;
        n++;
    }
    if (!n) return 0;
    m->d[0] = d0; m->a[6] = a6;
    m->sr = (uint16_t)((m->sr & 0xFFF0u) | flags);
    m->pc = n == 8 ? 0x4D12u : 0x4CD6u + 8u * (uint32_t)n;
    *count = instr;
    return 1;
}

#define HOOK_FN(name) static int hle_##name(M68k *m) { int cnt = 0; M68k before; int ok; if (hle_verify) before = *m; ok = name(m, &cnt); if (ok && hle_verify) verify_hook(&before, m, cnt, #name); return ok; }
HOOK_FN(slot_scan)
HOOK_FN(entries)
HOOK_FN(voices)
HOOK_FN(tracks)

/* hook_entries: the loop "BSR body; ADDA.L #12,A5 | LEA 12(A5),A5; DBRA D7" at `head`, whose body begins BTST.B #7,(A5); BEQ.W rts: checks the code and hooks it (the single player driver and the
 * menus' driver have it at different addresses, the code is the same). */
static const uint8_t PAT_BODY[6] = { 0x08, 0x15, 0x00, 0x07, 0x67, 0x00 }, PAT_ADDA[6] = { 0xDB, 0xFC, 0x00, 0x00, 0x00, 0x0C }, PAT_LEA[4] = { 0x4B, 0xED, 0x00, 0x0C };
static void hook_entries(SndCore *c, uint32_t head)
{
    const uint8_t *r = c->ram;
    uint32_t body, tgt, next;
    int16_t disp;
    if (head + 16 > 0x6000 || r[head] != 0x61 || r[head + 1] != 0) return;
    disp = (int16_t)((r[head + 2] << 8) | r[head + 3]);
    body = head + 2 + (uint32_t)(int32_t)disp;
    if (body + 8 > 0x6000 || memcmp(r + body, PAT_BODY, 6)) return;                  /* BTST.B #7,(A5); BEQ.W */
    tgt = body + 6 + (uint32_t)(int32_t)(int16_t)((r[body + 6] << 8) | r[body + 7]);
    if (tgt + 2 > 0x6000 || r[tgt] != 0x4E || r[tgt + 1] != 0x75) return;                                  /* ... to an RTS */
    if (!memcmp(r + head + 4, PAT_ADDA, 6)) next = head + 10;                    /* ADDA.L #12,A5 */
    else if (!memcmp(r + head + 4, PAT_LEA, 4)) next = head + 8;                         /* LEA 12(A5),A5 */
    else return;
    if (r[next] != 0x51 || r[next + 1] != 0xCF || (int16_t)((r[next + 2] << 8) | r[next + 3]) != (int16_t)((int32_t)head - (int32_t)(next + 2))) return;       /* DBRA D7,head */
    c->ent_head = head; c->ent_exit = next + 4; c->ent_stride = 12;
    m68k_hook(&c->cpu, head, hle_entries);
}

static uint32_t fnv(const uint8_t *p, int n)
{
    uint32_t h = 0x811C9DC5u;
    while (n--) { h ^= *p++; h *= 0x01000193u; }
    return h;
}

SndCore *snd_core_new(uint8_t *ram)
{
    SndCore *c = (SndCore *)calloc(1, sizeof *c);
    if (!c) return NULL;
    c->ram = ram;
    scsp_init(&c->scsp, ram);
    c->cpu.ram = ram; c->cpu.ram_size = 0x80000;
    c->cpu.io_read = io_read; c->cpu.io_write = io_write; c->cpu.ud = c;
    m68k_cache_enable(&c->cpu, 0x1000, 0x6000);               /* the driver's code: decoded once */
    return c;
}

void snd_core_free(SndCore *c) { free(c->cpu.cache); free(c); }

void snd_core_reset(SndCore *c)
{
    scsp_init(&c->scsp, c->ram);
    c->scipd = 0; c->acked = 0; c->idle_polls = 0; c->stop = 0; c->ticks = 0; c->dead = 0;
    m68k_invalidate(&c->cpu);                                     /* new code may have been loaded */
    m68k_hooks_clear(&c->cpu);
    c->ent_head = 0;
    if (c->hle_on) {                                                /* the loops are only known in the code of the menus' driver (SDDRVS.DAT) and the single player driver (ESDDRVS.DAT) */
        if (fnv(c->ram + 0x1326, 0x13B2 - 0x1326) == 0xF1276E15u) m68k_hook(&c->cpu, 0x1326, hle_slot_scan);
        hook_entries(c, 0x4344);
        if (!c->ent_head) hook_entries(c, 0x4CC6);
        if (fnv(c->ram + 0x1414, 0x1486 - 0x1414) == 0x367EAC42u) m68k_hook(&c->cpu, 0x1414, hle_voices);
        if (fnv(c->ram + 0x4CD6, 0x4D16 - 0x4CD6) == 0x2F257DE7u && fnv(c->ram + 0x50D4, 6) == 0x64422ED9u && c->ram[0x52FA] == 0x4E && c->ram[0x52FB] == 0x75) m68k_hook(&c->cpu, 0x4CD6, hle_tracks);
    }
    m68k_reset(&c->cpu);
    c->ready = 1;
}

void snd_core_set_test_mode(SndCore *c, int on) { c->b_call = on; }
void snd_core_set_idle_skip(SndCore *c, int on) { c->idle_skip = on; }
void snd_core_set_hle(SndCore *c, int on) { c->hle_on = on; }
void snd_core_set_prof(SndCore *c, unsigned *hist) { c->prof = hist; }
void snd_core_set_hle_verify(int on) { hle_verify = on; }
unsigned long long snd_core_skipped(const SndCore *c) { return c->skipped; }
unsigned long long snd_core_irq_steps(const SndCore *c) { return c->n_irq; }
void snd_core_set_trace(SndCore *c, void (*cb)(void *, const M68k *), void *ud) { c->trace_cb = cb; c->trace_ud = ud; }
void snd_core_set_log(SndCore *c, void (*cb)(void *, uint32_t, uint32_t, int, uint32_t), void *ud) { c->log_cb = cb; c->log_ud = ud; }
int snd_core_ready(const SndCore *c) { return c->ready; }
uint32_t snd_core_pc(const SndCore *c) { return c->cpu.pc; }
uint32_t snd_core_ticks(const SndCore *c) { return c->ticks; }
int snd_core_active_slots(const SndCore *c) { return c->scsp.active; }
void snd_core_set_clock(SndCore *c, unsigned long long (*us)(void)) { c->clock_us = us; }
void snd_core_times(const SndCore *c, unsigned long long *cpu_us, unsigned long long *mix_us, unsigned long long *steps) { *cpu_us = c->t_cpu; *mix_us = c->t_mix; *steps = c->n_steps; }
int snd_core_active_slots_dummy(void) { return 0; }
void snd_core_debug_regs(const SndCore *c, uint8_t *regs_out) { memcpy(regs_out, c->scsp.regs, 0x1000); }

int snd_core_tick(SndCore *c, float *out)
{
    int guard;
    unsigned long long tc0 = c->clock_us ? c->clock_us() : 0, tc1 = 0;
#ifdef SND_PROF
    double prof_t0 = now_ns();
#endif
    if (!c->ready || c->dead) return 0;
    if (c->idle_skip && c->ticks > 100 && !c->trace_cb && idle(c)) { c->skipped++; c->ticks++; return 1; }
    /* timer B: the interrupt routine (level 2) steps the sequencer */
    if (c->b_call && (((c->scsp.regs[0x41E] << 8) | c->scsp.regs[0x41F]) & 0x80) && (((c->cpu.sr >> 8) & 7) < 2)) {
        uint32_t ret = c->cpu.pc, h = ((uint32_t)c->ram[26 * 4] << 24) | ((uint32_t)c->ram[26 * 4 + 1] << 16) | ((uint32_t)c->ram[26 * 4 + 2] << 8) | c->ram[26 * 4 + 3];
        c->cpu.a[7] -= 4;
        c->ram[c->cpu.a[7]] = (uint8_t)(ret >> 24); c->ram[c->cpu.a[7] + 1] = (uint8_t)(ret >> 16); c->ram[c->cpu.a[7] + 2] = (uint8_t)(ret >> 8); c->ram[c->cpu.a[7] + 3] = (uint8_t)ret;
        c->cpu.pc = h;
        c->scipd |= 0x80;
        for (guard = 0; guard < 20000 && c->cpu.pc != ret && !c->cpu.halted; guard++) { m68k_step(&c->cpu); if (c->trace_cb) c->trace_cb(c->trace_ud, &c->cpu); }
        c->scipd &= ~0x80u;
    } else if (!c->b_call && (((c->scsp.regs[0x41E] << 8) | c->scsp.regs[0x41F]) & 0x80)) {
        if (m68k_irq(&c->cpu, 2)) {
            c->cpu.wh = 0;                                          /* (tests: the interrupt frame is not part of the write hash) */
            c->scipd |= 0x80;
            for (guard = 0; guard < 20000 && (((c->cpu.sr >> 8) & 7) >= 2) && !c->cpu.halted; guard++) { if (c->prof && c->cpu.pc < 0x6000) c->prof[c->cpu.pc >> 4]++; m68k_step(&c->cpu); if (c->trace_cb) c->trace_cb(c->trace_ud, &c->cpu); }
            c->scipd &= ~0x80u;
            c->n_irq += (unsigned)guard;
        }
    }
    /* timer A: pending until the driver's main loop acknowledges it; the work of the tick runs until the loop polls again */
    c->scipd |= 0x40; c->acked = 0; c->idle_polls = 0; c->stop = 0;
    if (c->trace_cb) { for (guard = 0; guard < 60000 && !c->stop && !c->cpu.halted; guard++) { m68k_step(&c->cpu); c->trace_cb(c->trace_ud, &c->cpu); } }
    else if (c->prof) { for (guard = 0; guard < 60000 && !c->stop && !c->cpu.halted; guard++) { if (c->cpu.pc < 0x6000) c->prof[c->cpu.pc >> 4]++; m68k_step(&c->cpu); } }
    else guard = m68k_run(&c->cpu, 60000, &c->stop);
    c->n_steps += (unsigned)guard;
    if (c->clock_us) tc1 = c->clock_us();
    if (c->cpu.halted) { c->dead = 1; return 0; }
#ifdef SND_PROF
    { double t1 = now_ns(); snd_prof_cpu_ns += t1 - prof_t0; scsp_render(&c->scsp, out, SND_TICK_FRAMES); snd_prof_render_ns += now_ns() - t1; }
#else
    scsp_render(&c->scsp, out, SND_TICK_FRAMES);
#endif
    if (c->clock_us) { unsigned long long t2 = c->clock_us(); c->t_cpu += tc1 - tc0; c->t_mix += t2 - tc1; }
    c->ticks++;
    return 1;
}
