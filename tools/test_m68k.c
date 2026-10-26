/* M9: checks src/m68k.c against the results unicorn gave (tools/m68k_fuzz.py):  gcc -O1 -I src tools/test_m68k.c src/m68k.c -o build/host/test_m68k && build/host/test_m68k fuzz.bin [-v] [-flags]
 * Every case is rebuilt from its seed (same generator as the Python script); the registers, the status register (the bits that are defined), the PC and a hash of the RAM window must match. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "m68k.h"

static uint32_t lcg_x;
static uint32_t rnd(void) { lcg_x = lcg_x * 1664525u + 1013904223u; return lcg_x; }

static uint32_t fnv(const uint8_t *b, int n)
{
    uint32_t h = 0x811C9DC5u;
    for (int i = 0; i < n; i++) h = (h ^ b[i]) * 0x01000193u;
    return h;
}

static uint32_t rd32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static uint32_t io_r(void *ud, uint32_t a, int sz) { (void)ud; (void)a; (void)sz; return 0; }
static void io_w(void *ud, uint32_t a, int sz, uint32_t v) { (void)ud; (void)a; (void)sz; (void)v; }

int main(int argc, char **argv)
{
    FILE *f;
    static uint8_t ram[0x4000];
    static M68k probe;
    int verbose = 0, strict = 0, shown = 0;
    long total = 0, checked = 0, bad = 0, badsr = 0;
    static long badop[16];
    uint8_t rec[12 + 4 * 19];
    int use_cache = 1;
    for (int i = 2; i < argc; i++) { if (!strcmp(argv[i], "-nocache")) use_cache = 0; if (!strcmp(argv[i], "-v")) verbose = 1; if (!strcmp(argv[i], "-flags")) strict = 1; }
    m68k_cache_enable(&probe, 0x400, 0x420);
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    while (fread(rec, 1, 12, f) == 12) {
        uint32_t op = rd32(rec), seed = rd32(rec + 4), has = rd32(rec + 8);
        M68k m;
        uint32_t d[8], a[8], sr, e[4];
        uint8_t exp[4 * 19];
        total++;
        if (has) { if (fread(exp, 1, sizeof exp, f) != sizeof exp) break; } else continue;
        lcg_x = seed;
        for (int i = 0; i < 8; i++) d[i] = rnd();
        for (int i = 0; i < 7; i++) a[i] = 0x2000 + ((rnd() % 0x1000) & ~1u);
        a[7] = 0x3000 + ((rnd() % 0x400) & ~1u);
        { uint32_t r1 = rnd(), r2 = rnd(); sr = 0x2000 | (r1 & 0x1F) | ((r2 & 7) << 8); }       /* the order of the two draws must match the Python generator */
        memset(ram, 0, sizeof ram);
        for (int i = 0; i < 0x2000; i++) ram[0x2000 + i] = (uint8_t)(rnd() >> 24);
        for (int i = 0; i < 4; i++) {
            uint32_t r = rnd(), k = (r >> 28) & 3;
            e[i] = (k == 0 ? 0x2000 + (r & 0x7FE) : k == 1 ? (r & 0xFF) : k == 2 ? (0xFF00 | (r & 0xFE)) : 0) & ~0x100u;
        }
        ram[0x400] = (uint8_t)(op >> 8); ram[0x401] = (uint8_t)op;
        for (int i = 0; i < 4; i++) { ram[0x402 + 2 * i] = (uint8_t)(e[i] >> 8); ram[0x403 + 2 * i] = (uint8_t)e[i]; }
        memset(&m, 0, sizeof m);
        m.ram = ram; m.ram_size = 0x4000; m.io_read = io_r; m.io_write = io_w;
        if (use_cache) { m.cache = probe.cache; m.cache_lo = 0x400; m.cache_hi = 0x420; m68k_invalidate(&m); }       /* the predecoded path (the default); -nocache runs the generic step */
        memcpy(m.d, d, sizeof d); memcpy(m.a, a, sizeof a);
        m.sr = (uint16_t)sr; m.ssp = a[7]; m.pc = 0x400;
        checked++;
        m68k_step(&m);
        {
            int ok = 1;
            uint32_t xd[8], xa[8], xpc = rd32(exp + 64), xsr = rd32(exp + 68), xh = rd32(exp + 72);
            for (int i = 0; i < 8; i++) { xd[i] = rd32(exp + 4 * i); xa[i] = rd32(exp + 32 + 4 * i); }
            if (m.halted) ok = 0;
            for (int i = 0; i < 8; i++) if (m.d[i] != xd[i] || m.a[i] != xa[i]) ok = 0;
            if (m.pc != xpc || fnv(ram + 0x2000, 0x2000) != xh) ok = 0;
            if (ok && (strict ? (m.sr != xsr) : ((m.sr & 0xFF1F) != (xsr & 0xFF1F)))) { ok = 0; badsr++; }
            if (!ok) {
                bad++; badop[op >> 12]++;
                if (verbose && (shown < 40 || (getenv("ALL") && !m.halted))) {          /* ALL=1: every difference of an instruction the core does run (halted = an opcode the driver never uses) */
                    shown++;
                    printf("op %04x seed %08x: got sr %04x pc %x d0 %x a0 %x a7 %x, want sr %04x pc %x d0 %x a0 %x a7 %x%s\n", op, seed, m.sr, m.pc, m.d[0], m.a[0], m.a[7], xsr, xpc, xd[0], xa[0], xa[7], m.halted ? " (halted)" : "");
                }
            }
        }
    }
    printf("%ld cases, %ld with a result, %ld differ (%ld only in the flags)\n", total, checked, bad, badsr);
    for (int i = 0; i < 16; i++) if (badop[i]) printf("  opcode group %X: %ld\n", i, badop[i]);
    return bad != 0;
}
