/* M9: Motorola 68000 interpreter, see m68k.h */
#include <stdlib.h>
#include <string.h>
#include "m68k.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

#define F_C 0x01
#define F_V 0x02
#define F_Z 0x04
#define F_N 0x08
#define F_X 0x10

/* ---------------------------------------------------------------------------------------------------------------------------------- bus */
#ifdef M68K_NO_WH
#define WH(m, a, size, v) ((void)0)
#else
#define WH(m, a, size, v) ((m)->wh = ((((m)->wh ^ (a)) * 16777619u ^ (size)) * 16777619u ^ (v)) * 16777619u)
#endif
#define CODE_WRITE(m, a) do { if ((m)->cache && (a) - (m)->cache_lo < (m)->cache_hi - (m)->cache_lo) invalidate_write((m), (a)); } while (0)
#define AI static inline __attribute__((always_inline))
static void invalidate_write(M68k *m, u32 a);
AI u32 rd8(M68k *m, u32 a)
{
    a &= 0xFFFFFF;
    if (a < m->ram_size) return m->ram[a];
    return m->io_read ? m->io_read(m->ud, a, 1) & 0xFF : 0;
}

AI u32 rd16(M68k *m, u32 a)
{
    a &= 0xFFFFFF;
    if (a + 1 < m->ram_size) return ((u32)m->ram[a] << 8) | m->ram[a + 1];
    return m->io_read ? m->io_read(m->ud, a, 2) & 0xFFFF : 0;
}

AI u32 rd32(M68k *m, u32 a)
{
    a &= 0xFFFFFF;
    if (a + 3 < m->ram_size) return ((u32)m->ram[a] << 24) | ((u32)m->ram[a + 1] << 16) | ((u32)m->ram[a + 2] << 8) | m->ram[a + 3];
    return m->io_read ? m->io_read(m->ud, a, 4) : 0;
}

AI void wr8(M68k *m, u32 a, u32 v)
{
    a &= 0xFFFFFF;
    if (a < m->ram_size) { m->ram[a] = (u8)v; WH(m, a, 1u, v & 0xFF); CODE_WRITE(m, a); return; }
    if (m->io_write) m->io_write(m->ud, a, 1, v & 0xFF);
}

AI void wr16(M68k *m, u32 a, u32 v)
{
    a &= 0xFFFFFF;
    if (a + 1 < m->ram_size) { m->ram[a] = (u8)(v >> 8); m->ram[a + 1] = (u8)v; WH(m, a, 2u, v & 0xFFFF); CODE_WRITE(m, a); return; }
    if (m->io_write) m->io_write(m->ud, a, 2, v & 0xFFFF);
}

AI void wr32(M68k *m, u32 a, u32 v)
{
    a &= 0xFFFFFF;
    if (a + 3 < m->ram_size) { m->ram[a] = (u8)(v >> 24); m->ram[a + 1] = (u8)(v >> 16); m->ram[a + 2] = (u8)(v >> 8); m->ram[a + 3] = (u8)v; WH(m, a, 4u, v); CODE_WRITE(m, a); return; }
    if (m->io_write) m->io_write(m->ud, a, 4, v);
}

static u32 rd(M68k *m, u32 a, int sz) { return sz == 0 ? rd8(m, a) : sz == 1 ? rd16(m, a) : rd32(m, a); }
static void wr(M68k *m, u32 a, int sz, u32 v) { if (sz == 0) wr8(m, a, v); else if (sz == 1) wr16(m, a, v); else wr32(m, a, v); }

static u32 fetch16(M68k *m) { u32 v = rd16(m, m->pc); m->pc += 2; return v; }
static u32 fetch32(M68k *m) { u32 v = rd32(m, m->pc); m->pc += 4; return v; }

/* ---------------------------------------------------------------------------------------------------------------------------------- helpers */
static const u32 SMASK[3] = { 0xFF, 0xFFFF, 0xFFFFFFFFu };
static const u32 SMSB[3] = { 0x80, 0x8000, 0x80000000u };
static const int SBYTES[3] = { 1, 2, 4 };

static u32 sext(u32 v, int sz) { return sz == 0 ? (u32)(s32)(s8)v : sz == 1 ? (u32)(s32)(s16)v : v; }

static void set_sr(M68k *m, u32 v)
{
    u32 old = m->sr;
    v &= 0xA71F;
    if ((old & 0x2000) != (v & 0x2000)) {               /* the supervisor bit changes: swap the stack pointers */
        if (old & 0x2000) m->ssp = m->a[7]; else m->usp = m->a[7];
        m->a[7] = (v & 0x2000) ? m->ssp : m->usp;
    }
    m->sr = (u16)v;
}

static const u8 MSBSHIFT[3] = { 7, 15, 31 };

AI void sflags(M68k *m, u32 r, int sz)                 /* N, Z from the result; V and C cleared; X untouched */
{
    u32 sr = m->sr & ~(F_N | F_Z | F_V | F_C);
    r &= SMASK[sz];
    m->sr = (u16)(sr | ((r == 0) << 2) | (((r >> MSBSHIFT[sz]) & 1) << 3));
}

AI u32 do_add(M68k *m, u32 s, u32 d, int sz, int x)
{
    u32 mask = SMASK[sz], msb = SMSB[sz], res, carry, sr;
    u32 a = s & mask, b = d & mask;
    if (sz < 2) { u32 t = a + b + (u32)x; res = t & mask; carry = t > mask; }
    else { u32 t = a + b; carry = t < a; res = t + (u32)x; carry |= res < t; }
    sr = m->sr & ~(F_N | F_Z | F_V | F_C | F_X);
    if (carry) sr |= F_C | F_X;
    if (((s ^ res) & (d ^ res)) & msb) sr |= F_V;
    if (res & msb) sr |= F_N;
    if (res == 0) sr |= (x ? (m->sr & F_Z) : F_Z);
    m->sr = (u16)sr;
    return res;
}

AI u32 do_sub(M68k *m, u32 s, u32 d, int sz, int x, int keepx)        /* d - s - x */
{
    u32 mask = SMASK[sz], msb = SMSB[sz], res;
    res = ((d & mask) - (s & mask) - (unsigned)x) & mask;
    {
        u32 sr = m->sr;
        u32 xz = sr & F_Z;
        sr &= ~(F_N | F_Z | F_V | F_C);
        if (!keepx) sr &= ~F_X;
        if ((s & mask) + (unsigned)x > (d & mask)) { sr |= F_C; if (!keepx) sr |= F_X; }
        if (((s ^ d) & (d ^ res)) & msb) sr |= F_V;
        if (res & msb) sr |= F_N;
        if (res == 0) sr |= (x ? xz : F_Z);
        m->sr = (u16)sr;
    }
    return res;
}

static u8 cond_tbl[32 * 16];
static int cond_ready;

static int cond_calc(u32 sr, int c)
{
    int C = !!(sr & F_C), V = !!(sr & F_V), Z = !!(sr & F_Z), N = !!(sr & F_N);
    switch (c) {
    case 0: return 1;
    case 1: return 0;
    case 2: return !C && !Z;
    case 3: return C || Z;
    case 4: return !C;
    case 5: return C;
    case 6: return !Z;
    case 7: return Z;
    case 8: return !V;
    case 9: return V;
    case 10: return !N;
    case 11: return N;
    case 12: return N == V;
    case 13: return N != V;
    case 14: return !Z && N == V;
    default: return Z || N != V;
    }
}

static void cond_init(void)
{
    int i, c;
    for (i = 0; i < 32; i++) for (c = 0; c < 16; c++) cond_tbl[i * 16 + c] = (u8)cond_calc((u32)i, c);
    cond_ready = 1;
}

AI int cond(M68k *m, int c)
{
    if (!cond_ready) cond_init();
    return cond_tbl[((m->sr & 0x1F) << 4) | (u32)c];
}

static void exception(M68k *m, int vec)
{
    u32 sr = m->sr;
    set_sr(m, (sr | 0x2000) & ~0x8000u);
    m->a[7] -= 4; wr32(m, m->a[7], m->pc);
    m->a[7] -= 2; wr16(m, m->a[7], sr);
    m->pc = rd32(m, (u32)vec * 4);
}

/* effective address: kind 0 = Dn, 1 = An, 2 = memory (addr), 3 = immediate (value) */
typedef struct { int kind; u32 v; } Loc;

static Loc ea_loc(M68k *m, int mode, int reg, int sz)
{
    Loc l;
    u32 a, ext;
    switch (mode) {
    case 0: l.kind = 0; l.v = (u32)reg; return l;
    case 1: l.kind = 1; l.v = (u32)reg; return l;
    case 2: l.kind = 2; l.v = m->a[reg]; return l;
    case 3:
        l.kind = 2; l.v = m->a[reg];
        m->a[reg] += (reg == 7 && sz == 0) ? 2 : (u32)SBYTES[sz];
        return l;
    case 4:
        m->a[reg] -= (reg == 7 && sz == 0) ? 2 : (u32)SBYTES[sz];
        l.kind = 2; l.v = m->a[reg]; return l;
    case 5: l.kind = 2; l.v = m->a[reg] + (u32)(s32)(s16)fetch16(m); return l;
    case 6:
        a = m->a[reg];
    index:
        ext = fetch16(m);
        {
            u32 xr = (ext & 0x8000) ? m->a[(ext >> 12) & 7] : m->d[(ext >> 12) & 7];
            if (!(ext & 0x0800)) xr = (u32)(s32)(s16)xr;
            l.kind = 2; l.v = a + xr + (u32)(s32)(s8)ext;
        }
        return l;
    default:
        switch (reg) {
        case 0: l.kind = 2; l.v = (u32)(s32)(s16)fetch16(m); return l;
        case 1: l.kind = 2; l.v = fetch32(m); return l;
        case 2: l.kind = 2; l.v = m->pc; l.v += (u32)(s32)(s16)fetch16(m); return l;
        case 3: a = m->pc; goto index;
        default:
            l.kind = 3;
            if (sz == 2) l.v = fetch32(m); else { l.v = fetch16(m); if (sz == 0) l.v &= 0xFF; }
            return l;
        }
    }
}

static u32 loc_read(M68k *m, Loc l, int sz)
{
    switch (l.kind) {
    case 0: return m->d[l.v] & SMASK[sz];
    case 1: return m->a[l.v] & SMASK[sz];
    case 2: return rd(m, l.v, sz);
    default: return l.v & SMASK[sz];
    }
}

static void loc_write(M68k *m, Loc l, int sz, u32 v)
{
    switch (l.kind) {
    case 0: m->d[l.v] = sz == 2 ? v : (m->d[l.v] & ~SMASK[sz]) | (v & SMASK[sz]); break;
    case 1: m->a[l.v] = sext(v, sz); break;
    case 2: wr(m, l.v, sz, v); break;
    default: break;
    }
}

/* ---------------------------------------------------------------------------------------------------------------------------------- shifts */
static u32 do_shift(M68k *m, int type, int left, u32 v, int cnt, int sz)
{
    u32 mask = SMASK[sz], msb = SMSB[sz];
    int bits = SBYTES[sz] * 8, i;
    u32 sr = m->sr & ~(F_N | F_Z | F_V | F_C);
    int c = 0, vflag = 0, x = !!(m->sr & F_X);
    v &= mask;
    if (cnt == 0) {
        if (type == 2) sr = (sr & ~F_C) | (x ? F_C : 0);        /* ROXd: C = X */
        sr &= ~F_V;
        if (v == 0) sr |= F_Z;
        if (v & msb) sr |= F_N;
        m->sr = (u16)sr;
        return v;
    }
    for (i = 0; i < cnt; i++) {
        u32 old = v;
        switch (type) {
        case 0:                                                  /* ASd */
            if (left) { c = !!(v & msb); v = (v << 1) & mask; if ((old ^ v) & msb) vflag = 1; }
            else { c = (int)(v & 1); v = (v >> 1) | (v & msb); }
            x = c;
            break;
        case 1:                                                  /* LSd */
            if (left) { c = !!(v & msb); v = (v << 1) & mask; }
            else { c = (int)(v & 1); v >>= 1; }
            x = c;
            break;
        case 2:                                                  /* ROXd */
            if (left) { c = !!(v & msb); v = ((v << 1) & mask) | (u32)x; }
            else { c = (int)(v & 1); v = (v >> 1) | (x ? msb : 0); }
            x = c;
            break;
        default:                                                 /* ROd */
            if (left) { c = !!(v & msb); v = ((v << 1) & mask) | (u32)c; }
            else { c = (int)(v & 1); v = (v >> 1) | (c ? msb : 0); }
            break;
        }
    }
    (void)bits;
    if (c) sr |= F_C;
    if (type != 3) sr = (sr & ~F_X) | (x ? F_X : 0);
    if (vflag) sr |= F_V;
    if (v == 0) sr |= F_Z;
    if (v & msb) sr |= F_N;
    m->sr = (u16)sr;
    return v;
}

/* ---------------------------------------------------------------------------------------------------------------------------------- BCD */
static u32 do_abcd(M68k *m, u32 s, u32 d)
{
    u32 lo = (s & 0xF) + (d & 0xF) + ((m->sr & F_X) ? 1 : 0), hi = (s >> 4) + (d >> 4), res, sr = m->sr & ~(F_C | F_X);
    if (lo > 9) { lo += 6; }
    hi += lo >> 4;
    lo &= 0xF;
    if (hi > 9) { hi += 6; sr |= F_C | F_X; }
    res = ((hi & 0xF) << 4) | lo;
    if (res) sr &= ~F_Z;
    m->sr = (u16)sr;
    return res & 0xFF;
}

static u32 do_sbcd(M68k *m, u32 s, u32 d)
{
    int lo = (int)(d & 0xF) - (int)(s & 0xF) - ((m->sr & F_X) ? 1 : 0), hi = (int)(d >> 4) - (int)(s >> 4);
    u32 sr = m->sr & ~(F_C | F_X), res;
    if (lo < 0) { lo += 10; hi -= 1; }
    if (hi < 0) { hi += 10; sr |= F_C | F_X; }
    res = (u32)((hi << 4) | lo) & 0xFF;
    if (res) sr &= ~F_Z;
    m->sr = (u16)sr;
    return res;
}

/* ---------------------------------------------------------------------------------------------------------------------------------- the instruction */
void m68k_reset(M68k *m)
{
    memset(m->d, 0, sizeof m->d);
    memset(m->a, 0, sizeof m->a);
    m->sr = 0x2700;
    m->usp = 0;
    m->a[7] = m->ssp = rd32(m, 0);
    m->pc = rd32(m, 4);
    m->stopped = m->halted = 0;
}

int m68k_irq(M68k *m, int level)
{
    u32 mask = (m->sr >> 8) & 7;
    if (level != 7 && (int)mask >= level) return 0;
    m->stopped = 0;
    {
        u32 sr = m->sr;
        set_sr(m, (sr | 0x2000) & ~0x8000u);
        m->a[7] -= 4; wr32(m, m->a[7], m->pc);
        m->a[7] -= 2; wr16(m, m->a[7], sr);
        m->sr = (u16)((m->sr & ~0x700) | ((u32)level << 8));
        m->pc = rd32(m, (u32)(24 + level) * 4);
    }
    return 1;
}

static void illegal(M68k *m, u32 op)
{
    m->last_illegal = op;
    m->halted = 1;
}

static int step_generic(M68k *m)
{
    u32 op, pc0;
    int sz, mode, reg, rn, i;
    Loc l;
    if (m->halted) return 0;
    if (m->stopped) return 1;
    pc0 = m->pc;
    op = fetch16(m);
    mode = (int)((op >> 3) & 7);
    reg = (int)(op & 7);
    rn = (int)((op >> 9) & 7);
    switch (op >> 12) {
    case 0x0: {
        if ((op & 0x0138) == 0x0108) {                              /* MOVEP */
            u32 a = m->a[reg] + (u32)(s32)(s16)fetch16(m);
            int lng = !!(op & 0x40);
            if (op & 0x80) {                                        /* register -> memory */
                u32 v = m->d[rn];
                if (lng) { wr8(m, a, v >> 24); wr8(m, a + 2, v >> 16); wr8(m, a + 4, v >> 8); wr8(m, a + 6, v); }
                else { wr8(m, a, v >> 8); wr8(m, a + 2, v); }
            } else if (lng) m->d[rn] = (rd8(m, a) << 24) | (rd8(m, a + 2) << 16) | (rd8(m, a + 4) << 8) | rd8(m, a + 6);
            else m->d[rn] = (m->d[rn] & 0xFFFF0000u) | (rd8(m, a) << 8) | rd8(m, a + 2);
            break;
        }
        if (op & 0x0100) {                                          /* BTST / BCHG / BCLR / BSET with a register bit number */
            int bt = (int)((op >> 6) & 3);
            Loc d = ea_loc(m, mode, reg, 0);
            u32 bit, v;
            if (mode == 1) { illegal(m, op); break; }
            if (d.kind == 0) { bit = m->d[rn] & 31; v = m->d[d.v]; }
            else { bit = m->d[rn] & 7; v = loc_read(m, d, 0); }
            m->sr = (u16)((m->sr & ~F_Z) | ((v & (1u << bit)) ? 0 : F_Z));
            if (bt == 1) v ^= 1u << bit; else if (bt == 2) v &= ~(1u << bit); else if (bt == 3) v |= 1u << bit;
            if (bt) { if (d.kind == 0) m->d[d.v] = v; else loc_write(m, d, 0, v); }
            break;
        }
        {
            int g = (int)((op >> 9) & 7);
            sz = (int)((op >> 6) & 3);
            if (g == 4) {                                           /* static bit number */
                int bt = (int)((op >> 6) & 3);
                u32 nb = fetch16(m) & 0xFF, bit, v;
                Loc d = ea_loc(m, mode, reg, 0);
                if (mode == 1 || (mode == 7 && reg == 4)) { illegal(m, op); break; }
                if (d.kind == 0) { bit = nb & 31; v = m->d[d.v]; }
                else { bit = nb & 7; v = loc_read(m, d, 0); }
                m->sr = (u16)((m->sr & ~F_Z) | ((v & (1u << bit)) ? 0 : F_Z));
                if (bt == 1) v ^= 1u << bit; else if (bt == 2) v &= ~(1u << bit); else if (bt == 3) v |= 1u << bit;
                if (bt) { if (d.kind == 0) m->d[d.v] = v; else loc_write(m, d, 0, v); }
                break;
            }
            if (sz == 3 || g == 7) { illegal(m, op); break; }
            {
                u32 imm, s, r;
                if (sz == 2) imm = fetch32(m); else { imm = fetch16(m); if (sz == 0) imm &= 0xFF; }
                if (mode == 7 && reg == 4) {                        /* ORI / ANDI / EORI to CCR / SR */
                    if (sz == 0 && (g == 0 || g == 1 || g == 5)) {
                        u32 ccr = m->sr & 0xFF;
                        ccr = g == 0 ? ccr | imm : g == 1 ? ccr & imm : ccr ^ imm;
                        m->sr = (u16)((m->sr & 0xFF00) | (ccr & 0x1F));
                    } else if (sz == 1 && (g == 0 || g == 1 || g == 5)) {
                        if (!(m->sr & 0x2000)) { m->pc = pc0; exception(m, 8); break; }
                        set_sr(m, g == 0 ? m->sr | imm : g == 1 ? m->sr & imm : m->sr ^ imm);
                    } else illegal(m, op);
                    break;
                }
                if (mode == 1) { illegal(m, op); break; }
                l = ea_loc(m, mode, reg, sz);
                s = loc_read(m, l, sz);
                switch (g) {
                case 0: r = s | imm; sflags(m, r, sz); loc_write(m, l, sz, r); break;
                case 1: r = s & imm; sflags(m, r, sz); loc_write(m, l, sz, r); break;
                case 2: r = do_sub(m, imm, s, sz, 0, 0); loc_write(m, l, sz, r); break;
                case 3: r = do_add(m, imm, s, sz, 0); loc_write(m, l, sz, r); break;
                case 5: r = s ^ imm; sflags(m, r, sz); loc_write(m, l, sz, r); break;
                default: { u32 save = m->sr & F_X; do_sub(m, imm, s, sz, 0, 1); m->sr = (u16)((m->sr & ~F_X) | save); break; }      /* CMPI */
                }
            }
        }
        break;
    }
    case 0x1: case 0x2: case 0x3: {                                 /* MOVE / MOVEA */
        u32 v;
        int dm = (int)((op >> 6) & 7);
        sz = (op >> 12) == 1 ? 0 : (op >> 12) == 3 ? 1 : 2;
        l = ea_loc(m, mode, reg, sz);
        v = loc_read(m, l, sz);
        if (dm == 1) { if (sz == 0) { illegal(m, op); break; } m->a[rn] = sext(v, sz); break; }
        sflags(m, v, sz);
        {
            Loc d = ea_loc(m, dm, rn, sz);
            if (dm == 7 && rn > 1) { illegal(m, op); break; }
            loc_write(m, d, sz, v);
        }
        break;
    }
    case 0x4: {
        if ((op & 0x01C0) == 0x01C0) {                              /* LEA */
            if (mode < 2 || mode == 3 || mode == 4 || (mode == 7 && reg > 3)) { illegal(m, op); break; }
            l = ea_loc(m, mode, reg, 2);
            m->a[rn] = l.v;
            break;
        }
        if ((op & 0x01C0) == 0x0180) {                              /* CHK */
            u32 b, v;
            l = ea_loc(m, mode, reg, 1);
            b = loc_read(m, l, 1); v = m->d[rn] & 0xFFFF;
            if ((s16)v < 0) { m->sr |= F_N; exception(m, 6); }
            else if ((s16)v > (s16)b) { m->sr &= ~F_N; exception(m, 6); }
            break;
        }
        switch ((op >> 8) & 0xF) {
        case 0x0: case 0x2: case 0x4: case 0x6: {                   /* NEGX / CLR / NEG / NOT, and MOVE from SR */
            int k = (int)((op >> 9) & 3);
            sz = (int)((op >> 6) & 3);
            if (sz == 3) {
                if (k == 0) {                                       /* MOVE from SR */
                    l = ea_loc(m, mode, reg, 1);
                    loc_write(m, l, 1, m->sr);
                } else if (k == 1) { illegal(m, op); }              /* MOVE from CCR is a 68010 instruction */
                else {                                              /* MOVE to CCR (k == 2) / to SR (k == 3) */
                    u32 v;
                    l = ea_loc(m, mode, reg, 1);
                    v = loc_read(m, l, 1);
                    if (k == 2) m->sr = (u16)((m->sr & 0xFF00) | (v & 0x1F));
                    else if (!(m->sr & 0x2000)) { m->pc = pc0; exception(m, 8); }
                    else set_sr(m, v);
                }
                break;
            }
            if (mode == 1) { illegal(m, op); break; }
            l = ea_loc(m, mode, reg, sz);
            {
                u32 s = loc_read(m, l, sz), r;
                if (k == 0) { r = do_sub(m, s, 0, sz, !!(m->sr & F_X), 0); }
                else if (k == 1) { r = 0; m->sr = (u16)((m->sr & ~(F_N | F_V | F_C)) | F_Z); }
                else if (k == 2) { r = do_sub(m, s, 0, sz, 0, 0); }
                else { r = ~s & SMASK[sz]; sflags(m, r, sz); }
                loc_write(m, l, sz, r);
            }
            break;
        }
        case 0x8:
            if ((op & 0x01B8) == 0x0080 && mode == 0) {             /* EXT.W / EXT.L */
                if (op & 0x40) { m->d[reg] = (u32)(s32)(s16)m->d[reg]; sflags(m, m->d[reg], 2); }
                else { m->d[reg] = (m->d[reg] & 0xFFFF0000u) | ((u32)(s32)(s8)m->d[reg] & 0xFFFF); sflags(m, m->d[reg], 1); }
                break;
            }
            if ((op & 0x01F8) == 0x0040 && mode == 0) {             /* SWAP */
                m->d[reg] = (m->d[reg] << 16) | (m->d[reg] >> 16); sflags(m, m->d[reg], 2); break;
            }
            if ((op & 0x01F8) == 0x0048) {                          /* PEA is below: mode 1 here is BKPT on later CPUs */
                illegal(m, op); break;
            }
            if ((op & 0x01C0) == 0x0000) {                          /* NBCD */
                l = ea_loc(m, mode, reg, 0);
                { u32 r = do_sbcd(m, loc_read(m, l, 0), 0); loc_write(m, l, 0, r); }
                break;
            }
            if ((op & 0x01C0) == 0x0040) {                          /* PEA */
                l = ea_loc(m, mode, reg, 2);
                m->a[7] -= 4; wr32(m, m->a[7], l.v);
                break;
            }
            /* MOVEM registers -> memory */
            {
                u32 mask = fetch16(m);
                int lng = !!(op & 0x40), sizeb = lng ? 4 : 2;
                if (mode == 4) {
                    u32 a = m->a[reg];
                    for (i = 0; i < 16; i++) if (mask & (1u << i)) {
                        u32 v = i < 8 ? m->a[7 - i] : m->d[15 - i];     /* the mask of the predecrement form: bit 0 = A7 .. bit 15 = D0 */
                        a -= (u32)sizeb;
                        if (lng) wr32(m, a, v); else wr16(m, a, v);
                    }
                    m->a[reg] = a;
                } else {
                    u32 a;
                    if (mode < 2 || mode == 3 || (mode == 7 && reg > 1)) { illegal(m, op); break; }
                    l = ea_loc(m, mode, reg, 2);
                    a = l.v;
                    for (i = 0; i < 16; i++) if (mask & (1u << i)) {
                        u32 v = i < 8 ? m->d[i] : m->a[i - 8];
                        if (lng) wr32(m, a, v); else wr16(m, a, v);
                        a += (u32)sizeb;
                    }
                }
            }
            break;
        case 0xA:
            if ((op & 0x00C0) == 0x00C0) {                          /* TAS (and ILLEGAL 0x4AFC) */
                u32 v;
                if (op == 0x4AFC) { illegal(m, op); break; }
                l = ea_loc(m, mode, reg, 0);
                v = loc_read(m, l, 0);
                sflags(m, v, 0);
                loc_write(m, l, 0, v | 0x80);
                break;
            }
            /* fallthrough: TST */
            sz = (int)((op >> 6) & 3);
            l = ea_loc(m, mode, reg, sz);
            sflags(m, loc_read(m, l, sz), sz);
            break;
        case 0xC:                                                   /* MOVEM memory -> registers */
            if ((op & 0x0380) != 0x0080) { illegal(m, op); break; }
            {
                u32 mask = fetch16(m), a;
                int lng = !!(op & 0x40), sizeb = lng ? 4 : 2;
                if (mode == 0 || mode == 1 || mode == 4 || (mode == 7 && reg == 4)) { illegal(m, op); break; }
                if (mode == 3) a = m->a[reg]; else { l = ea_loc(m, mode, reg, 2); a = l.v; }
                for (i = 0; i < 16; i++) if (mask & (1u << i)) {
                    u32 v = lng ? rd32(m, a) : (u32)(s32)(s16)rd16(m, a);
                    if (i < 8) m->d[i] = v; else m->a[i - 8] = v;
                    a += (u32)sizeb;
                }
                if (mode == 3) m->a[reg] = a;
            }
            break;
        case 0xE:
            switch ((op >> 4) & 0xF) {
            case 0x4: exception(m, 32 + (int)(op & 15)); break;     /* TRAP */
            case 0x5:
                if (!(op & 8)) {                                    /* LINK */
                    s32 d = (s32)(s16)fetch16(m);
                    m->a[7] -= 4; wr32(m, m->a[7], m->a[reg]);
                    m->a[reg] = m->a[7];
                    m->a[7] += (u32)d;
                } else {                                            /* UNLK */
                    m->a[7] = m->a[reg];
                    m->a[reg] = rd32(m, m->a[7]);
                    m->a[7] += 4;
                }
                break;
            case 0x6:                                               /* MOVE USP */
                if (!(m->sr & 0x2000)) { m->pc = pc0; exception(m, 8); break; }
                if (op & 8) m->a[reg] = m->usp; else m->usp = m->a[reg];
                break;
            case 0x7:
                switch (op & 15) {
                case 0: break;                                      /* RESET */
                case 1: break;                                      /* NOP */
                case 2:                                             /* STOP */
                    if (!(m->sr & 0x2000)) { m->pc = pc0; exception(m, 8); break; }
                    set_sr(m, fetch16(m)); m->stopped = 1; break;
                case 3:                                             /* RTE */
                    if (!(m->sr & 0x2000)) { m->pc = pc0; exception(m, 8); break; }
                    { u32 sr = rd16(m, m->a[7]); m->a[7] += 2; m->pc = rd32(m, m->a[7]); m->a[7] += 4; set_sr(m, sr); }
                    break;
                case 5: m->pc = rd32(m, m->a[7]); m->a[7] += 4; break;      /* RTS */
                case 6: if (m->sr & F_V) exception(m, 7); break;    /* TRAPV */
                case 7: {                                           /* RTR */
                    u32 ccr = rd16(m, m->a[7]); m->a[7] += 2;
                    m->sr = (u16)((m->sr & 0xFF00) | (ccr & 0x1F));
                    m->pc = rd32(m, m->a[7]); m->a[7] += 4;
                    break;
                }
                default: illegal(m, op); break;
                }
                break;
            default:
                if ((op & 0x00C0) == 0x0080 || (op & 0x00C0) == 0x00C0) {   /* JSR / JMP (0x4E80 / 0x4EC0) */
                    l = ea_loc(m, mode, reg, 2);
                    if (mode < 2 || mode == 3 || mode == 4 || (mode == 7 && reg > 3)) { illegal(m, op); break; }
                    if ((op & 0x00C0) == 0x0080) { m->a[7] -= 4; wr32(m, m->a[7], m->pc); }
                    m->pc = l.v;
                } else illegal(m, op);
                break;
            }
            break;
        default: illegal(m, op); break;
        }
        break;
    }
    case 0x5: {
        sz = (int)((op >> 6) & 3);
        if (sz == 3) {
            if (mode == 1) {                                        /* DBcc */
                s32 d = (s32)(s16)fetch16(m);
                if (!cond(m, (int)((op >> 8) & 15))) {
                    u32 v = (m->d[reg] - 1) & 0xFFFF;
                    m->d[reg] = (m->d[reg] & 0xFFFF0000u) | v;
                    if (v != 0xFFFF) m->pc = pc0 + 2 + (u32)d;
                }
            } else {                                                /* Scc */
                l = ea_loc(m, mode, reg, 0);
                loc_write(m, l, 0, cond(m, (int)((op >> 8) & 15)) ? 0xFF : 0);
            }
            break;
        }
        {
            u32 q = (op >> 9) & 7, r, s;
            if (!q) q = 8;
            l = ea_loc(m, mode, reg, sz);
            if (mode == 1) { m->a[reg] = (op & 0x100) ? m->a[reg] - q : m->a[reg] + q; break; }
            s = loc_read(m, l, sz);
            r = (op & 0x100) ? do_sub(m, q, s, sz, 0, 0) : do_add(m, q, s, sz, 0);
            loc_write(m, l, sz, r);
        }
        break;
    }
    case 0x6: {                                                     /* Bcc / BRA / BSR */
        s32 d = (s32)(s8)op;
        int c = (int)((op >> 8) & 15);
        if ((op & 0xFF) == 0) d = (s32)(s16)fetch16(m);
        if (c == 1) { m->a[7] -= 4; wr32(m, m->a[7], m->pc); m->pc = pc0 + 2 + (u32)d; }
        else if (cond(m, c)) m->pc = pc0 + 2 + (u32)d;
        break;
    }
    case 0x7:                                                       /* MOVEQ */
        if (op & 0x100) { illegal(m, op); break; }
        m->d[rn] = (u32)(s32)(s8)op;
        sflags(m, m->d[rn], 2);
        break;
    case 0x8: case 0xC: {                                           /* OR / DIVU / DIVS / SBCD  -  AND / MULU / MULS / ABCD / EXG */
        int is_and = (op >> 12) == 0xC;
        sz = (int)((op >> 6) & 3);
        if (sz == 3) {                                              /* DIVU / DIVS / MULU / MULS */
            u32 s;
            l = ea_loc(m, mode, reg, 1);
            if (mode == 1) { illegal(m, op); break; }
            s = loc_read(m, l, 1);
            if (!is_and) {
                int sgn = !!(op & 0x100);
                if (s == 0) { m->pc = pc0 + 2; exception(m, 5); break; }
                if (sgn) {
                    s32 dv = (s32)m->d[rn], sv = (s32)(s16)s;
                    long long q = (long long)dv / sv, rem = (long long)dv % sv;
                    if (q > 32767 || q < -32768) m->sr = (u16)((m->sr & ~F_C) | F_V);
                    else {
                        m->d[rn] = ((u32)rem << 16) | ((u32)q & 0xFFFF);
                        m->sr = (u16)(m->sr & ~(F_N | F_Z | F_V | F_C));
                        if ((q & 0xFFFF) == 0) m->sr |= F_Z;
                        if (q & 0x8000) m->sr |= F_N;
                    }
                } else {
                    u32 dv = m->d[rn], q = dv / s, rem = dv % s;
                    if (q > 0xFFFF) m->sr = (u16)((m->sr & ~F_C) | F_V);
                    else {
                        m->d[rn] = (rem << 16) | q;
                        m->sr = (u16)(m->sr & ~(F_N | F_Z | F_V | F_C));
                        if (q == 0) m->sr |= F_Z;
                        if (q & 0x8000) m->sr |= F_N;
                    }
                }
            } else {
                u32 r;
                if (op & 0x100) r = (u32)((s32)(s16)(m->d[rn] & 0xFFFF) * (s32)(s16)s);
                else r = (m->d[rn] & 0xFFFF) * s;
                m->d[rn] = r;
                sflags(m, r, 2);
            }
            break;
        }
        if ((op & 0x01F0) == 0x0100) {                              /* SBCD / ABCD */
            if (op & 8) {                                           /* -(Ay), -(Ax) */
                u32 sa, da, s, d, r;
                m->a[reg] -= 1; sa = m->a[reg];
                m->a[rn] -= 1; da = m->a[rn];
                s = rd8(m, sa); d = rd8(m, da);
                r = is_and ? do_abcd(m, s, d) : do_sbcd(m, s, d);
                wr8(m, da, r);
            } else {
                u32 r = is_and ? do_abcd(m, m->d[reg] & 0xFF, m->d[rn] & 0xFF) : do_sbcd(m, m->d[reg] & 0xFF, m->d[rn] & 0xFF);
                m->d[rn] = (m->d[rn] & ~0xFFu) | r;
            }
            break;
        }
        if (is_and && (op & 0x0130) == 0x0100) {                    /* EXG */
            u32 t;
            int xm = (int)((op >> 3) & 0x1F);
            if (xm == 0x08) { t = m->d[rn]; m->d[rn] = m->d[reg]; m->d[reg] = t; }
            else if (xm == 0x09) { t = m->a[rn]; m->a[rn] = m->a[reg]; m->a[reg] = t; }
            else if (xm == 0x11) { t = m->d[rn]; m->d[rn] = m->a[reg]; m->a[reg] = t; }
            else illegal(m, op);
            break;
        }
        {
            u32 s, d, r;
            if (op & 0x100) {                                       /* Dn op <ea> -> <ea> */
                if (mode < 2 || (mode == 7 && reg > 1)) { illegal(m, op); break; }
                l = ea_loc(m, mode, reg, sz);
                d = loc_read(m, l, sz);
                s = m->d[rn];
                r = is_and ? (s & d) : (s | d);
                sflags(m, r, sz);
                loc_write(m, l, sz, r);
            } else {                                                /* <ea> op Dn -> Dn */
                if (mode == 1) { illegal(m, op); break; }
                l = ea_loc(m, mode, reg, sz);
                s = loc_read(m, l, sz);
                d = m->d[rn];
                r = is_and ? (s & d) : (s | d);
                sflags(m, r, sz);
                loc_write(m, (Loc){ 0, (u32)rn }, sz, r);
            }
        }
        break;
    }
    case 0x9: case 0xD: {                                           /* SUB / SUBA / SUBX  -  ADD / ADDA / ADDX */
        int is_add = (op >> 12) == 0xD;
        sz = (int)((op >> 6) & 3);
        if (sz == 3) {                                              /* ADDA / SUBA */
            u32 s;
            int asz = (op & 0x100) ? 2 : 1;
            l = ea_loc(m, mode, reg, asz);
            s = sext(loc_read(m, l, asz), asz);
            m->a[rn] = is_add ? m->a[rn] + s : m->a[rn] - s;
            break;
        }
        if ((op & 0x0130) == 0x0100) {                              /* ADDX / SUBX */
            u32 s, d, r;
            int x = !!(m->sr & F_X);
            if (op & 8) {
                m->a[reg] -= (u32)SBYTES[sz]; m->a[rn] -= (u32)SBYTES[sz];
                s = rd(m, m->a[reg], sz); d = rd(m, m->a[rn], sz);
                r = is_add ? do_add(m, s, d, sz, x) : do_sub(m, s, d, sz, x, 0);
                wr(m, m->a[rn], sz, r);
            } else {
                s = m->d[reg] & SMASK[sz]; d = m->d[rn] & SMASK[sz];
                r = is_add ? do_add(m, s, d, sz, x) : do_sub(m, s, d, sz, x, 0);
                loc_write(m, (Loc){ 0, (u32)rn }, sz, r);
            }
            break;
        }
        {
            u32 s, d, r;
            if (op & 0x100) {
                if (mode < 2 || (mode == 7 && reg > 1)) { illegal(m, op); break; }
                l = ea_loc(m, mode, reg, sz);
                d = loc_read(m, l, sz);
                s = m->d[rn] & SMASK[sz];
                r = is_add ? do_add(m, s, d, sz, 0) : do_sub(m, s, d, sz, 0, 0);
                loc_write(m, l, sz, r);
            } else {
                if (mode == 1 && sz == 0) { illegal(m, op); break; }
                l = ea_loc(m, mode, reg, sz);
                s = loc_read(m, l, sz);
                d = m->d[rn] & SMASK[sz];
                r = is_add ? do_add(m, s, d, sz, 0) : do_sub(m, s, d, sz, 0, 0);
                loc_write(m, (Loc){ 0, (u32)rn }, sz, r);
            }
        }
        break;
    }
    case 0xB: {                                                     /* CMP / CMPA / CMPM / EOR */
        sz = (int)((op >> 6) & 3);
        if (sz == 3) {                                              /* CMPA */
            int asz = (op & 0x100) ? 2 : 1;
            u32 s, save = m->sr & F_X;
            l = ea_loc(m, mode, reg, asz);
            s = sext(loc_read(m, l, asz), asz);
            do_sub(m, s, m->a[rn], 2, 0, 1);
            m->sr = (u16)((m->sr & ~F_X) | save);
            break;
        }
        if (op & 0x100) {
            if (mode == 1) {                                        /* CMPM (Ay)+, (Ax)+ */
                u32 s, d, save = m->sr & F_X;
                s = rd(m, m->a[reg], sz); m->a[reg] += (reg == 7 && sz == 0) ? 2 : (u32)SBYTES[sz];
                d = rd(m, m->a[rn], sz); m->a[rn] += (rn == 7 && sz == 0) ? 2 : (u32)SBYTES[sz];
                do_sub(m, s, d, sz, 0, 1);
                m->sr = (u16)((m->sr & ~F_X) | save);
            } else {                                                /* EOR Dn, <ea> */
                u32 s, d, r;
                if (mode == 7 && reg > 1) { illegal(m, op); break; }
                l = ea_loc(m, mode, reg, sz);
                d = loc_read(m, l, sz);
                s = m->d[rn] & SMASK[sz];
                r = s ^ d;
                sflags(m, r, sz);
                loc_write(m, l, sz, r);
            }
        } else {                                                    /* CMP <ea>, Dn */
            u32 s, save = m->sr & F_X;
            l = ea_loc(m, mode, reg, sz);
            s = loc_read(m, l, sz);
            do_sub(m, s, m->d[rn] & SMASK[sz], sz, 0, 1);
            m->sr = (u16)((m->sr & ~F_X) | save);
        }
        break;
    }
    case 0xE: {                                                     /* shifts and rotates */
        int left = !!(op & 0x100);
        sz = (int)((op >> 6) & 3);
        if (sz == 3) {                                              /* memory, by one */
            u32 v;
            if (mode < 2 || (mode == 7 && reg > 1)) { illegal(m, op); break; }
            l = ea_loc(m, mode, reg, 1);
            v = loc_read(m, l, 1);
            v = do_shift(m, (int)((op >> 9) & 3), left, v, 1, 1);
            loc_write(m, l, 1, v);
        } else {
            int cnt = (op & 0x20) ? (int)(m->d[rn] & 63) : (rn ? rn : 8);
            u32 v = do_shift(m, (int)((op >> 3) & 3), left, m->d[reg], cnt, sz);
            loc_write(m, (Loc){ 0, (u32)reg }, sz, v);
        }
        break;
    }
    case 0xA: m->pc = pc0; exception(m, 10); break;                 /* line A */
    default: m->pc = pc0; exception(m, 11); break;                  /* line F */
    }
    return !m->halted;
}

/* ================================================================================================================================================================
 * The predecoded interpreter: the instructions of the code area (the sound driver) are decoded once into a table (extension words resolved, the branch targets computed, the handler picked)
 * and executed from there.  Every handler is the same code as the generic step above with the decoding taken out; anything the decoder does not handle (rare or illegal forms) runs through
 * step_generic, which stays the reference (tools/test_m68k.c checks both paths against unicorn: every opcode word, random states).
 * ================================================================================================================================================================ */
enum { K_DN, K_AN, K_IND, K_POST, K_PRE, K_DISP, K_IDX, K_ABS, K_IMM };

typedef struct { uint8_t k, r; uint16_t ext; u32 v; } EAD;                       /* an effective address: kind, register, index extension word, displacement / address / immediate */
typedef struct Dec Dec;
typedef void (*Fn)(M68k *, const Dec *, u32);
struct Dec { Fn fn; uint16_t op; uint8_t len, sz; EAD s, d; u32 x; };

void m68k_cache_enable(M68k *m, uint32_t lo, uint32_t hi)
{
    free(m->cache);
    m->cache_lo = lo; m->cache_hi = hi;
    m->cache = (Dec *)calloc((hi - lo) / 2, sizeof(Dec));
}

void m68k_invalidate(M68k *m)
{
    if (m->cache) memset(m->cache, 0, (m->cache_hi - m->cache_lo) / 2 * sizeof(Dec));
}

/* hooks: the decoded instruction at the address is replaced by h_hook, which asks the function and falls back to the instruction it replaced (kept in hook_orig) */
static void h_hook(M68k *m, const Dec *d, u32 pc0)
{
    const Dec *o = (const Dec *)m->hook_orig + d->x;
    m->pc = pc0;                                                            /* (the step has set it to the next instruction) */
    if (!m->hook_off && m->hook_fn[d->x](m)) return;
    m->pc = pc0 + o->len;
    o->fn(m, o, pc0);
}

void m68k_hook(M68k *m, uint32_t pc, int (*fn)(M68k *m))
{
    if (m->nhook >= 4 || !m->cache) return;
    if (!m->hook_orig) m->hook_orig = calloc(4, sizeof(Dec));
    m->hook_pc[m->nhook] = pc; m->hook_fn[m->nhook] = fn; m->nhook++;
    m68k_invalidate(m);
}

void m68k_hooks_clear(M68k *m)
{
    m->nhook = 0;
    m68k_invalidate(m);
}

static void invalidate_write(M68k *m, u32 a)                                      /* a write into the code area: drop the instructions that cover the address */
{
    u32 first = a >= m->cache_lo + 8 ? a - 8 : m->cache_lo, i;
    for (i = first & ~1u; i <= a; i += 2) m->cache[(i - m->cache_lo) >> 1].fn = 0;
}

static inline Loc ea_res(M68k *m, const EAD *e, int sz)
{
    Loc l;
    u32 r = e->r;
    switch (e->k) {
    case K_DN: l.kind = 0; l.v = r; return l;
    case K_AN: l.kind = 1; l.v = r; return l;
    case K_IND: l.kind = 2; l.v = m->a[r]; return l;
    case K_POST: l.kind = 2; l.v = m->a[r]; m->a[r] += (r == 7 && sz == 0) ? 2u : (u32)SBYTES[sz]; return l;
    case K_PRE: m->a[r] -= (r == 7 && sz == 0) ? 2u : (u32)SBYTES[sz]; l.kind = 2; l.v = m->a[r]; return l;
    case K_DISP: l.kind = 2; l.v = m->a[r] + e->v; return l;
    case K_IDX: {
        u32 ext = e->ext, xr = (ext & 0x8000) ? m->a[(ext >> 12) & 7] : m->d[(ext >> 12) & 7];
        if (!(ext & 0x0800)) xr = (u32)(s32)(s16)xr;
        l.kind = 2; l.v = (r < 8 ? m->a[r] : e->v) + xr + (u32)(s32)(s8)ext;
        return l; }
    case K_ABS: l.kind = 2; l.v = e->v; return l;
    default: l.kind = 3; l.v = e->v; return l;
    }
}

static int ea_dec(M68k *m, u32 *pp, int mode, int reg, int sz, EAD *e)           /* 0 = not a valid mode */
{
    u32 p = *pp;
    e->r = (uint8_t)reg; e->ext = 0; e->v = 0;
    switch (mode) {
    case 0: e->k = K_DN; break;
    case 1: e->k = K_AN; break;
    case 2: e->k = K_IND; break;
    case 3: e->k = K_POST; break;
    case 4: e->k = K_PRE; break;
    case 5: e->k = K_DISP; e->v = (u32)(s32)(s16)rd16(m, p); p += 2; break;
    case 6: e->k = K_IDX; e->ext = (uint16_t)rd16(m, p); p += 2; break;
    default:
        switch (reg) {
        case 0: e->k = K_ABS; e->v = (u32)(s32)(s16)rd16(m, p); p += 2; break;
        case 1: e->k = K_ABS; e->v = rd32(m, p); p += 4; break;
        case 2: e->k = K_ABS; e->v = p + (u32)(s32)(s16)rd16(m, p); p += 2; break;
        case 3: e->k = K_IDX; e->r = 8; e->ext = (uint16_t)rd16(m, p); e->v = p; p += 2; break;
        case 4:
            e->k = K_IMM;
            if (sz == 2) { e->v = rd32(m, p); p += 4; } else { e->v = rd16(m, p); p += 2; if (sz == 0) e->v &= 0xFF; }
            break;
        default: return 0;
        }
    }
    *pp = p;
    return 1;
}

/* ---- operand access: the value of an effective address with its side effects, written / resolved the same way ---- */
AI u32 rdN(M68k *m, u32 a, const int sz) { return sz == 0 ? rd8(m, a) : sz == 1 ? rd16(m, a) : rd32(m, a); }
AI void wrN(M68k *m, u32 a, const int sz, u32 v) { if (sz == 0) wr8(m, a, v); else if (sz == 1) wr16(m, a, v); else wr32(m, a, v); }
AI u32 pinc(u32 r, const int sz) { return (r == 7 && sz == 0) ? 2u : (u32)(1 << sz); }

AI u32 idx_addr(M68k *m, const EAD *e)
{
    u32 ext = e->ext, xr = (ext & 0x8000) ? m->a[(ext >> 12) & 7] : m->d[(ext >> 12) & 7];
    if (!(ext & 0x0800)) xr = (u32)(s32)(s16)xr;
    return (e->r < 8 ? m->a[e->r] : e->v) + xr + (u32)(s32)(s8)ext;
}

AI u32 ea_addr(M68k *m, const EAD *e, const int sz)                               /* the address of a memory operand (post-increment / pre-decrement done) */
{
    u32 r = e->r, a;
    switch (e->k) {
    case K_IND: return m->a[r];
    case K_POST: a = m->a[r]; m->a[r] = a + pinc(r, sz); return a;
    case K_PRE: a = m->a[r] - pinc(r, sz); m->a[r] = a; return a;
    case K_DISP: return m->a[r] + e->v;
    case K_IDX: return idx_addr(m, e);
    default: return e->v;                                                           /* K_ABS */
    }
}

AI u32 ea_read(M68k *m, const EAD *e, const int sz)
{
    switch (e->k) {
    case K_DN: return m->d[e->r] & SMASK[sz];
    case K_AN: return m->a[e->r] & SMASK[sz];
    case K_IMM: return e->v;
    default: return rdN(m, ea_addr(m, e, sz), sz);
    }
}

AI void ea_write(M68k *m, const EAD *e, const int sz, u32 v)
{
    switch (e->k) {
    case K_DN: m->d[e->r] = sz == 2 ? v : (m->d[e->r] & ~SMASK[sz]) | (v & SMASK[sz]); break;
    case K_AN: m->a[e->r] = sext(v, sz); break;
    case K_IMM: break;
    default: wrN(m, ea_addr(m, e, sz), sz, v); break;
    }
}

/* read-modify-write of an operand: reg = a register operand (value in *pv), else the address in *pa */
#define RMW_BEGIN(m, e, sz, pa, val, isreg) do { \
        if ((e)->k <= K_AN) { (isreg) = 1; (val) = (e)->k == K_DN ? (m)->d[(e)->r] & SMASK[sz] : (m)->a[(e)->r] & SMASK[sz]; } \
        else if ((e)->k == K_IMM) { (isreg) = 2; (val) = (e)->v; } \
        else { (isreg) = 0; (pa) = ea_addr((m), (e), (sz)); (val) = rdN((m), (pa), (sz)); } \
    } while (0)
#define RMW_END(m, e, sz, pa, res, isreg) do { \
        if ((isreg) == 1) { if ((e)->k == K_DN) (m)->d[(e)->r] = (sz) == 2 ? (res) : ((m)->d[(e)->r] & ~SMASK[sz]) | ((res) & SMASK[sz]); else (m)->a[(e)->r] = sext((res), (sz)); } \
        else if ((isreg) == 0) wrN((m), (pa), (sz), (res)); \
    } while (0)

/* ---- handlers ---- */
static void h_slow(M68k *m, const Dec *d, u32 pc0) { (void)d; m->pc = pc0; step_generic(m); }

#define HSZ(X) X(0, b) X(1, w) X(2, l)

#define DEF_MOVE(SZ, N) \
static void h_move_##N(M68k *m, const Dec *d, u32 pc0) { u32 v = ea_read(m, &d->s, SZ); (void)pc0; sflags(m, v, SZ); ea_write(m, &d->d, SZ, v); }
HSZ(DEF_MOVE)

#define DEF_MOVEA(SZ, N) \
static void h_movea_##N(M68k *m, const Dec *d, u32 pc0) { u32 v = ea_read(m, &d->s, SZ); (void)pc0; m->a[d->d.r] = sext(v, SZ); }
DEF_MOVEA(1, w) DEF_MOVEA(2, l)

static void h_moveq(M68k *m, const Dec *d, u32 pc0)
{
    (void)pc0;
    m->d[d->d.r] = (u32)(s32)(s8)d->op;
    sflags(m, m->d[d->d.r], 2);
}

static void h_bra(M68k *m, const Dec *d, u32 pc0) { (void)pc0; m->pc = d->x; }
static void h_bsr(M68k *m, const Dec *d, u32 pc0) { (void)pc0; m->a[7] -= 4; wr32(m, m->a[7], m->pc); m->pc = d->x; }
static void h_bcc(M68k *m, const Dec *d, u32 pc0) { (void)pc0; if (cond(m, (d->op >> 8) & 15)) m->pc = d->x; }
static void h_beq(M68k *m, const Dec *d, u32 pc0) { (void)pc0; if (m->sr & F_Z) m->pc = d->x; }
static void h_bne(M68k *m, const Dec *d, u32 pc0) { (void)pc0; if (!(m->sr & F_Z)) m->pc = d->x; }

static void h_dbcc(M68k *m, const Dec *d, u32 pc0)
{
    (void)pc0;
    if (!cond(m, (d->op >> 8) & 15)) {
        u32 reg = d->op & 7u, v = (m->d[reg] - 1) & 0xFFFF;
        m->d[reg] = (m->d[reg] & 0xFFFF0000u) | v;
        if (v != 0xFFFF) m->pc = d->x;
    }
}

static void h_scc(M68k *m, const Dec *d, u32 pc0)
{
    (void)pc0;
    ea_write(m, &d->s, 0, cond(m, (d->op >> 8) & 15) ? 0xFF : 0);
}

#define DEF_ADDQ(SZ, N) \
static void h_addq_##N(M68k *m, const Dec *d, u32 pc0) \
{ \
    u32 q = (d->op >> 9) & 7u, s, r, pa = 0; \
    int isreg; \
    (void)pc0; \
    if (!q) q = 8; \
    if (d->s.k == K_AN) { u32 *ar = &m->a[d->s.r]; *ar = (d->op & 0x100) ? *ar - q : *ar + q; return; } \
    RMW_BEGIN(m, &d->s, SZ, pa, s, isreg); \
    r = (d->op & 0x100) ? do_sub(m, q, s, SZ, 0, 0) : do_add(m, q, s, SZ, 0); \
    RMW_END(m, &d->s, SZ, pa, r, isreg); \
}
HSZ(DEF_ADDQ)

static void h_lea(M68k *m, const Dec *d, u32 pc0) { (void)pc0; m->a[d->d.r] = ea_addr(m, &d->s, 2); }
static void h_pea(M68k *m, const Dec *d, u32 pc0) { u32 a = ea_addr(m, &d->s, 2); (void)pc0; m->a[7] -= 4; wr32(m, m->a[7], a); }

#define DEF_TST(SZ, N) \
static void h_tst_##N(M68k *m, const Dec *d, u32 pc0) { (void)pc0; sflags(m, ea_read(m, &d->s, SZ), SZ); }
HSZ(DEF_TST)

#define DEF_UNARY(SZ, N) \
static void h_unary_##N(M68k *m, const Dec *d, u32 pc0) \
{ \
    int k = (d->op >> 9) & 3, isreg; \
    u32 s, r, pa = 0; \
    (void)pc0; \
    RMW_BEGIN(m, &d->s, SZ, pa, s, isreg); \
    if (k == 0) r = do_sub(m, s, 0, SZ, !!(m->sr & F_X), 0); \
    else if (k == 1) { r = 0; m->sr = (u16)((m->sr & ~(F_N | F_V | F_C)) | F_Z); } \
    else if (k == 2) r = do_sub(m, s, 0, SZ, 0, 0); \
    else { r = ~s & SMASK[SZ]; sflags(m, r, SZ); } \
    RMW_END(m, &d->s, SZ, pa, r, isreg); \
}
HSZ(DEF_UNARY)

static void h_ext(M68k *m, const Dec *d, u32 pc0)
{
    u32 reg = d->op & 7u;
    (void)pc0;
    if (d->op & 0x40) { m->d[reg] = (u32)(s32)(s16)m->d[reg]; sflags(m, m->d[reg], 2); }
    else { m->d[reg] = (m->d[reg] & 0xFFFF0000u) | ((u32)(s32)(s8)m->d[reg] & 0xFFFF); sflags(m, m->d[reg], 1); }
}

static void h_swap(M68k *m, const Dec *d, u32 pc0)
{
    u32 reg = d->op & 7u;
    (void)pc0;
    m->d[reg] = (m->d[reg] << 16) | (m->d[reg] >> 16);
    sflags(m, m->d[reg], 2);
}

static void h_jsr(M68k *m, const Dec *d, u32 pc0) { u32 a = ea_addr(m, &d->s, 2); (void)pc0; m->a[7] -= 4; wr32(m, m->a[7], m->pc); m->pc = a; }
static void h_jmp(M68k *m, const Dec *d, u32 pc0) { (void)pc0; m->pc = ea_addr(m, &d->s, 2); }
static void h_rts(M68k *m, const Dec *d, u32 pc0) { (void)d; (void)pc0; m->pc = rd32(m, m->a[7]); m->a[7] += 4; }
static void h_nop(M68k *m, const Dec *d, u32 pc0) { (void)m; (void)d; (void)pc0; }

static void h_link(M68k *m, const Dec *d, u32 pc0)
{
    u32 reg = d->op & 7u;
    (void)pc0;
    m->a[7] -= 4; wr32(m, m->a[7], m->a[reg]);
    m->a[reg] = m->a[7];
    m->a[7] += d->x;
}

static void h_unlk(M68k *m, const Dec *d, u32 pc0)
{
    u32 reg = d->op & 7u;
    (void)pc0;
    m->a[7] = m->a[reg];
    m->a[reg] = rd32(m, m->a[7]);
    m->a[7] += 4;
}

static void h_movem_to_mem(M68k *m, const Dec *d, u32 pc0)
{
    u32 mask = d->x;
    int lng = !!(d->op & 0x40), sizeb = lng ? 4 : 2, i;
    (void)pc0;
    if (d->s.k == K_PRE) {
        u32 a = m->a[d->s.r];
        for (i = 0; i < 16; i++) if (mask & (1u << i)) {
            u32 v = i < 8 ? m->a[7 - i] : m->d[15 - i];
            a -= (u32)sizeb;
            if (lng) wr32(m, a, v); else wr16(m, a, v);
        }
        m->a[d->s.r] = a;
    } else {
        u32 a = ea_addr(m, &d->s, 2);
        for (i = 0; i < 16; i++) if (mask & (1u << i)) {
            u32 v = i < 8 ? m->d[i] : m->a[i - 8];
            if (lng) wr32(m, a, v); else wr16(m, a, v);
            a += (u32)sizeb;
        }
    }
}

static void h_movem_from_mem(M68k *m, const Dec *d, u32 pc0)
{
    u32 mask = d->x, a;
    int lng = !!(d->op & 0x40), sizeb = lng ? 4 : 2, i;
    (void)pc0;
    a = d->s.k == K_POST ? m->a[d->s.r] : ea_addr(m, &d->s, 2);
    for (i = 0; i < 16; i++) if (mask & (1u << i)) {
        u32 v = lng ? rd32(m, a) : (u32)(s32)(s16)rd16(m, a);
        if (i < 8) m->d[i] = v; else m->a[i - 8] = v;
        a += (u32)sizeb;
    }
    if (d->s.k == K_POST) m->a[d->s.r] = a;
}

/* ADD / SUB / AND / OR / EOR / CMP: <ea> op Dn -> Dn, Dn op <ea> -> <ea> */
#define ALU_ADD(SZ, s, dd) do_add(m, s, dd, SZ, 0)
#define ALU_SUB(SZ, s, dd) do_sub(m, s, dd, SZ, 0, 0)
#define ALU_AND(SZ, s, dd) (sflags(m, (s) & (dd), SZ), (s) & (dd))
#define ALU_OR(SZ, s, dd)  (sflags(m, (s) | (dd), SZ), (s) | (dd))
#define ALU_EOR(SZ, s, dd) (sflags(m, (s) ^ (dd), SZ), (s) ^ (dd))

#define DEF_ALU(OPN, SZ, N) \
static void h_##OPN##_dn_##N(M68k *m, const Dec *d, u32 pc0) \
{ \
    u32 rn = d->d.r, s = ea_read(m, &d->s, SZ), dd = m->d[rn] & SMASK[SZ], r = ALU_##OPN(SZ, s, dd); \
    (void)pc0; \
    m->d[rn] = SZ == 2 ? r : (m->d[rn] & ~SMASK[SZ]) | (r & SMASK[SZ]); \
} \
static void h_##OPN##_ea_##N(M68k *m, const Dec *d, u32 pc0) \
{ \
    u32 dd, s = m->d[d->d.r] & SMASK[SZ], r, pa = 0; \
    int isreg; \
    (void)pc0; \
    RMW_BEGIN(m, &d->s, SZ, pa, dd, isreg); \
    r = ALU_##OPN(SZ, s, dd); \
    RMW_END(m, &d->s, SZ, pa, r, isreg); \
}
#define DEF_ALU3(OPN) DEF_ALU(OPN, 0, b) DEF_ALU(OPN, 1, w) DEF_ALU(OPN, 2, l)
DEF_ALU3(ADD) DEF_ALU3(SUB) DEF_ALU3(AND) DEF_ALU3(OR) DEF_ALU3(EOR)

#define DEF_CMP(SZ, N) \
static void h_cmp_##N(M68k *m, const Dec *d, u32 pc0) \
{ \
    u32 save = m->sr & F_X, s = ea_read(m, &d->s, SZ); \
    (void)pc0; \
    do_sub(m, s, m->d[d->d.r] & SMASK[SZ], SZ, 0, 1); \
    m->sr = (u16)((m->sr & ~F_X) | save); \
}
HSZ(DEF_CMP)

static void h_adda(M68k *m, const Dec *d, u32 pc0)                                /* ADDA / SUBA / CMPA: x = 0 add, 1 sub, 5 cmp */
{
    u32 s, rn = d->d.r;
    (void)pc0;
    s = d->sz == 1 ? (u32)(s32)(s16)ea_read(m, &d->s, 1) : ea_read(m, &d->s, 2);
    if (d->x == 0) m->a[rn] += s;
    else if (d->x == 1) m->a[rn] -= s;
    else { u32 save = m->sr & F_X; do_sub(m, s, m->a[rn], 2, 0, 1); m->sr = (u16)((m->sr & ~F_X) | save); }
}

/* immediate operations: ORI / ANDI / SUBI / ADDI / EORI / CMPI (the g field of the opcode picks the operation) */
#define DEF_IMM(SZ, N) \
static void h_imm_##N(M68k *m, const Dec *d, u32 pc0) \
{ \
    u32 imm = d->x, s, r, pa = 0; \
    int isreg; \
    (void)pc0; \
    RMW_BEGIN(m, &d->s, SZ, pa, s, isreg); \
    switch ((d->op >> 9) & 7) { \
    case 0: r = s | imm; sflags(m, r, SZ); break; \
    case 1: r = s & imm; sflags(m, r, SZ); break; \
    case 2: r = do_sub(m, imm, s, SZ, 0, 0); break; \
    case 3: r = do_add(m, imm, s, SZ, 0); break; \
    case 5: r = s ^ imm; sflags(m, r, SZ); break; \
    default: { u32 save = m->sr & F_X; do_sub(m, imm, s, SZ, 0, 1); m->sr = (u16)((m->sr & ~F_X) | save); return; } \
    } \
    RMW_END(m, &d->s, SZ, pa, r, isreg); \
}
HSZ(DEF_IMM)

static void h_bit(M68k *m, const Dec *d, u32 pc0)                                 /* BTST / BCHG / BCLR / BSET: x = 0x100 | bit number for the static form, 0 for a bit number in a register */
{
    int bt = (d->op >> 6) & 3, isreg;
    u32 bit, v, nb = d->x ? (d->x & 0xFF) : m->d[(d->op >> 9) & 7], pa = 0;
    (void)pc0;
    RMW_BEGIN(m, &d->s, 0, pa, v, isreg);
    if (d->s.k == K_DN) { bit = nb & 31; v = m->d[d->s.r]; }
    else bit = nb & 7;
    m->sr = (u16)((m->sr & ~F_Z) | ((v & (1u << bit)) ? 0 : F_Z));
    if (bt == 1) v ^= 1u << bit; else if (bt == 2) v &= ~(1u << bit); else if (bt == 3) v |= 1u << bit;
    if (bt) {
        if (d->s.k == K_DN) m->d[d->s.r] = v;
        else if (isreg == 0) wr8(m, pa, v);
    }
}

#define DEF_SHIFT(SZ, N) \
static void h_shift_reg_##N(M68k *m, const Dec *d, u32 pc0) \
{ \
    int left = !!(d->op & 0x100), rn = (d->op >> 9) & 7; \
    int cnt = (d->op & 0x20) ? (int)(m->d[rn] & 63) : (rn ? rn : 8); \
    u32 reg = d->op & 7u, v = do_shift(m, (d->op >> 3) & 3, left, m->d[reg], cnt, SZ); \
    (void)pc0; \
    m->d[reg] = SZ == 2 ? v : (m->d[reg] & ~SMASK[SZ]) | (v & SMASK[SZ]); \
}
HSZ(DEF_SHIFT)

static void h_shift_mem(M68k *m, const Dec *d, u32 pc0)
{
    u32 pa = ea_addr(m, &d->s, 1), v = rd16(m, pa);
    (void)pc0;
    v = do_shift(m, (d->op >> 9) & 3, !!(d->op & 0x100), v, 1, 1);
    wr16(m, pa, v);
}

static void h_exg(M68k *m, const Dec *d, u32 pc0)
{
    u32 t, rn = (d->op >> 9) & 7u, reg = d->op & 7u;
    int xm = (d->op >> 3) & 0x1F;
    (void)pc0;
    if (xm == 0x08) { t = m->d[rn]; m->d[rn] = m->d[reg]; m->d[reg] = t; }
    else if (xm == 0x09) { t = m->a[rn]; m->a[rn] = m->a[reg]; m->a[reg] = t; }
    else { t = m->d[rn]; m->d[rn] = m->a[reg]; m->a[reg] = t; }
}

static const Fn T_MOVE[3] = { h_move_b, h_move_w, h_move_l }, T_MOVEA[3] = { 0, h_movea_w, h_movea_l }, T_ADDQ[3] = { h_addq_b, h_addq_w, h_addq_l };
static const Fn T_TST[3] = { h_tst_b, h_tst_w, h_tst_l }, T_UNARY[3] = { h_unary_b, h_unary_w, h_unary_l }, T_CMP[3] = { h_cmp_b, h_cmp_w, h_cmp_l }, T_IMM[3] = { h_imm_b, h_imm_w, h_imm_l };
static const Fn T_SHIFT[3] = { h_shift_reg_b, h_shift_reg_w, h_shift_reg_l };
static const Fn T_ALU_DN[5][3] = { { h_ADD_dn_b, h_ADD_dn_w, h_ADD_dn_l }, { h_SUB_dn_b, h_SUB_dn_w, h_SUB_dn_l }, { h_AND_dn_b, h_AND_dn_w, h_AND_dn_l }, { h_OR_dn_b, h_OR_dn_w, h_OR_dn_l }, { h_EOR_dn_b, h_EOR_dn_w, h_EOR_dn_l } };
static const Fn T_ALU_EA[5][3] = { { h_ADD_ea_b, h_ADD_ea_w, h_ADD_ea_l }, { h_SUB_ea_b, h_SUB_ea_w, h_SUB_ea_l }, { h_AND_ea_b, h_AND_ea_w, h_AND_ea_l }, { h_OR_ea_b, h_OR_ea_w, h_OR_ea_l }, { h_EOR_ea_b, h_EOR_ea_w, h_EOR_ea_l } };

/* ---- the decoder ---- */
static void decode(M68k *m, u32 pc0, Dec *d)
{
    u32 op = rd16(m, pc0), p = pc0 + 2;
    int mode = (int)((op >> 3) & 7), reg = (int)(op & 7), rn = (int)((op >> 9) & 7), sz;
    memset(d, 0, sizeof *d);
    d->op = (uint16_t)op;
    d->fn = h_slow;
    d->len = 2;
    switch (op >> 12) {
    case 0x0: {
        int g = (int)((op >> 9) & 7);
        if ((op & 0x0138) == 0x0108) break;                                     /* MOVEP */
        if (op & 0x0100) {                                                      /* bit operation, number in a register */
            if (mode == 1) break;
            if (!ea_dec(m, &p, mode, reg, 0, &d->s)) break;
            d->fn = h_bit; d->len = (uint8_t)(p - pc0); d->x = 0;
            break;
        }
        sz = (int)((op >> 6) & 3);
        if (g == 4) {                                                           /* static bit number */
            u32 nb = rd16(m, p) & 0xFF;
            p += 2;
            if (mode == 1 || (mode == 7 && reg == 4)) break;
            if (!ea_dec(m, &p, mode, reg, 0, &d->s)) break;
            d->fn = h_bit; d->len = (uint8_t)(p - pc0); d->x = 0x100 | nb;
            break;
        }
        if (sz == 3 || g == 7) break;
        if (mode == 7 && reg == 4) break;                                       /* ... to CCR / SR */
        if (mode == 1) break;
        if (sz == 2) { d->x = rd32(m, p); p += 4; } else { d->x = rd16(m, p); p += 2; if (sz == 0) d->x &= 0xFF; }
        if (!ea_dec(m, &p, mode, reg, sz, &d->s)) break;
        d->sz = (uint8_t)sz; d->fn = T_IMM[sz]; d->len = (uint8_t)(p - pc0);
        break; }
    case 0x1: case 0x2: case 0x3: {
        int dm = (int)((op >> 6) & 7);
        sz = (op >> 12) == 1 ? 0 : (op >> 12) == 3 ? 1 : 2;
        if (!ea_dec(m, &p, mode, reg, sz, &d->s)) break;
        if (dm == 1) {
            if (sz == 0) break;
            d->d.r = (uint8_t)rn; d->sz = (uint8_t)sz; d->fn = T_MOVEA[sz]; d->len = (uint8_t)(p - pc0);
            break;
        }
        if (dm == 7 && rn > 1) break;
        if (!ea_dec(m, &p, dm, rn, sz, &d->d)) break;
        d->sz = (uint8_t)sz; d->fn = T_MOVE[sz]; d->len = (uint8_t)(p - pc0);
        break; }
    case 0x4: {
        if ((op & 0x01C0) == 0x01C0) {                                          /* LEA */
            if (mode < 2 || mode == 3 || mode == 4 || (mode == 7 && reg > 3)) break;
            if (!ea_dec(m, &p, mode, reg, 2, &d->s)) break;
            d->d.r = (uint8_t)rn; d->fn = h_lea; d->len = (uint8_t)(p - pc0);
            break;
        }
        if ((op & 0x01C0) == 0x0180) break;                                     /* CHK */
        switch ((op >> 8) & 0xF) {
        case 0x0: case 0x2: case 0x4: case 0x6:
            sz = (int)((op >> 6) & 3);
            if (sz == 3 || mode == 1) break;
            if (!ea_dec(m, &p, mode, reg, sz, &d->s)) break;
            d->sz = (uint8_t)sz; d->fn = T_UNARY[sz]; d->len = (uint8_t)(p - pc0);
            break;
        case 0x8:
            if ((op & 0x01B8) == 0x0080 && mode == 0) { d->fn = h_ext; break; }
            if ((op & 0x01F8) == 0x0040 && mode == 0) { d->fn = h_swap; break; }
            if ((op & 0x01F8) == 0x0048) break;
            if ((op & 0x01C0) == 0x0000) break;                                 /* NBCD */
            if ((op & 0x01C0) == 0x0040) {                                      /* PEA */
                if (!ea_dec(m, &p, mode, reg, 2, &d->s)) break;
                d->fn = h_pea; d->len = (uint8_t)(p - pc0);
                break;
            }
            {                                                                   /* MOVEM registers -> memory */
                u32 mask = rd16(m, p);
                p += 2;
                if (mode != 4 && (mode < 2 || mode == 3 || (mode == 7 && reg > 1))) break;
                if (!ea_dec(m, &p, mode, reg, 2, &d->s)) break;
                d->x = mask; d->fn = h_movem_to_mem; d->len = (uint8_t)(p - pc0);
            }
            break;
        case 0xA:
            if ((op & 0x00C0) == 0x00C0) break;                                 /* TAS */
            sz = (int)((op >> 6) & 3);
            if (!ea_dec(m, &p, mode, reg, sz, &d->s)) break;
            d->sz = (uint8_t)sz; d->fn = T_TST[sz]; d->len = (uint8_t)(p - pc0);
            break;
        case 0xC:                                                               /* MOVEM memory -> registers */
            if ((op & 0x0380) != 0x0080) break;
            {
                u32 mask = rd16(m, p);
                p += 2;
                if (mode == 0 || mode == 1 || mode == 4 || (mode == 7 && reg == 4)) break;
                if (!ea_dec(m, &p, mode, reg, 2, &d->s)) break;
                d->x = mask; d->fn = h_movem_from_mem; d->len = (uint8_t)(p - pc0);
            }
            break;
        case 0xE:
            switch ((op >> 4) & 0xF) {
            case 0x5:
                if (!(op & 8)) { d->x = (u32)(s32)(s16)rd16(m, p); d->fn = h_link; d->len = 4; }
                else d->fn = h_unlk;
                break;
            case 0x7:
                if ((op & 15) == 1) d->fn = h_nop;
                else if ((op & 15) == 5) d->fn = h_rts;
                break;
            case 0x4: case 0x6: break;
            default:
                if ((op & 0x00C0) == 0x0080 || (op & 0x00C0) == 0x00C0) {       /* JSR / JMP */
                    if (mode < 2 || mode == 3 || mode == 4 || (mode == 7 && reg > 3)) break;
                    if (!ea_dec(m, &p, mode, reg, 2, &d->s)) break;
                    d->fn = (op & 0x00C0) == 0x0080 ? h_jsr : h_jmp; d->len = (uint8_t)(p - pc0);
                }
                break;
            }
            break;
        default: break;
        }
        break; }
    case 0x5: {
        sz = (int)((op >> 6) & 3);
        if (sz == 3) {
            if (mode == 1) { d->x = pc0 + 2 + (u32)(s32)(s16)rd16(m, p); d->fn = h_dbcc; d->len = 4; }
            else if (ea_dec(m, &p, mode, reg, 0, &d->s)) { d->fn = h_scc; d->len = (uint8_t)(p - pc0); }
            break;
        }
        if (!ea_dec(m, &p, mode, reg, sz, &d->s)) break;
        d->sz = (uint8_t)sz; d->fn = T_ADDQ[sz]; d->len = (uint8_t)(p - pc0);
        break; }
    case 0x6: {
        s32 disp = (s32)(s8)op;
        int c = (int)((op >> 8) & 15);
        if ((op & 0xFF) == 0) { disp = (s32)(s16)rd16(m, p); d->len = 4; }
        d->x = pc0 + 2 + (u32)disp;
        d->fn = c == 0 ? h_bra : c == 1 ? h_bsr : c == 7 ? h_beq : c == 6 ? h_bne : h_bcc;
        break; }
    case 0x7:
        if (op & 0x100) break;
        d->d.r = (uint8_t)rn; d->fn = h_moveq;
        break;
    case 0x8: case 0xC: {
        int is_and = (op >> 12) == 0xC;
        sz = (int)((op >> 6) & 3);
        if (sz == 3 || (op & 0x01F0) == 0x0100) break;                          /* DIV / MUL, SBCD / ABCD */
        if (is_and && (op & 0x0130) == 0x0100) {                                /* EXG */
            int xm = (int)((op >> 3) & 0x1F);
            if (xm == 0x08 || xm == 0x09 || xm == 0x11) d->fn = h_exg;
            break;
        }
        if (op & 0x100) {
            if (mode < 2 || (mode == 7 && reg > 1)) break;
            if (!ea_dec(m, &p, mode, reg, sz, &d->s)) break;
            d->d.r = (uint8_t)rn; d->sz = (uint8_t)sz; d->fn = T_ALU_EA[is_and ? 2 : 3][sz]; d->len = (uint8_t)(p - pc0);
        } else {
            if (mode == 1) break;
            if (!ea_dec(m, &p, mode, reg, sz, &d->s)) break;
            d->d.r = (uint8_t)rn; d->sz = (uint8_t)sz; d->fn = T_ALU_DN[is_and ? 2 : 3][sz]; d->len = (uint8_t)(p - pc0);
        }
        break; }
    case 0x9: case 0xD: {
        int is_add = (op >> 12) == 0xD;
        sz = (int)((op >> 6) & 3);
        if (sz == 3) {                                                          /* ADDA / SUBA */
            int asz = (op & 0x100) ? 2 : 1;
            if (!ea_dec(m, &p, mode, reg, asz, &d->s)) break;
            d->d.r = (uint8_t)rn; d->sz = (uint8_t)asz; d->x = is_add ? 0 : 1; d->fn = h_adda; d->len = (uint8_t)(p - pc0);
            break;
        }
        if ((op & 0x0130) == 0x0100) break;                                     /* ADDX / SUBX */
        if (op & 0x100) {
            if (mode < 2 || (mode == 7 && reg > 1)) break;
            if (!ea_dec(m, &p, mode, reg, sz, &d->s)) break;
            d->d.r = (uint8_t)rn; d->sz = (uint8_t)sz; d->fn = T_ALU_EA[is_add ? 0 : 1][sz]; d->len = (uint8_t)(p - pc0);
        } else {
            if (mode == 1 && sz == 0) break;
            if (!ea_dec(m, &p, mode, reg, sz, &d->s)) break;
            d->d.r = (uint8_t)rn; d->sz = (uint8_t)sz; d->fn = T_ALU_DN[is_add ? 0 : 1][sz]; d->len = (uint8_t)(p - pc0);
        }
        break; }
    case 0xB: {
        sz = (int)((op >> 6) & 3);
        if (sz == 3) {                                                          /* CMPA */
            int asz = (op & 0x100) ? 2 : 1;
            if (!ea_dec(m, &p, mode, reg, asz, &d->s)) break;
            d->d.r = (uint8_t)rn; d->sz = (uint8_t)asz; d->x = 5; d->fn = h_adda; d->len = (uint8_t)(p - pc0);
            break;
        }
        if (op & 0x100) {
            if (mode == 1) break;                                               /* CMPM */
            if (mode == 7 && reg > 1) break;
            if (!ea_dec(m, &p, mode, reg, sz, &d->s)) break;
            d->d.r = (uint8_t)rn; d->sz = (uint8_t)sz; d->fn = T_ALU_EA[4][sz]; d->len = (uint8_t)(p - pc0);       /* EOR Dn, <ea> */
        } else {
            if (!ea_dec(m, &p, mode, reg, sz, &d->s)) break;
            d->d.r = (uint8_t)rn; d->sz = (uint8_t)sz; d->fn = T_CMP[sz]; d->len = (uint8_t)(p - pc0);
        }
        break; }
    case 0xE:
        sz = (int)((op >> 6) & 3);
        if (sz == 3) {
            if (mode < 2 || (mode == 7 && reg > 1)) break;
            if (!ea_dec(m, &p, mode, reg, 1, &d->s)) break;
            d->sz = 1; d->fn = h_shift_mem; d->len = (uint8_t)(p - pc0);
        } else {
            d->sz = (uint8_t)sz; d->fn = T_SHIFT[sz];
        }
        break;
    default: break;
    }
    if (d->fn == h_slow) d->len = 2;
}

int m68k_step(M68k *m)
{
    u32 pc0;
    if (m->halted) return 0;
    if (m->stopped) return 1;
    pc0 = m->pc;
    if (m->cache && pc0 - m->cache_lo < m->cache_hi - m->cache_lo && !(pc0 & 1)) {
        Dec *d = &m->cache[(pc0 - m->cache_lo) >> 1];
        if (!d->fn) {
            decode(m, pc0, d);
            if (m->nhook) {
                int i;
                for (i = 0; i < m->nhook; i++) {
                    if (m->hook_pc[i] == pc0 && d->fn != h_slow) { ((Dec *)m->hook_orig)[i] = *d; d->fn = h_hook; d->x = (u32)i; }
                }
            }
        }
        m->pc = pc0 + d->len;
        d->fn(m, d, pc0);
        return !m->halted;
    }
    return step_generic(m);
}

int m68k_run(M68k *m, int max, const volatile int *stop)
{
    int n = 0;
    while (n < max && !*stop && !m->halted) { m68k_step(m); n++; }
    return n;
}
