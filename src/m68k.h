/* M9: a Motorola 68000 interpreter, just exact enough to run the Saturn sound driver (SDDRVS.DAT, docs/sound.md) on the PSP.
 * 512 KB of sound RAM at 0, the SCSP's registers (0x100000 .. 0x100FFF) through callbacks.  Instruction counts, no cycle timing.
 * Checked against unicorn's M68000 by tools/m68k_fuzz.py + tools/test_m68k.c (every opcode word, random states) and in lockstep on the driver (tools/test_m68k_drv.py). */
#ifndef M68K_H
#define M68K_H

#include <stdint.h>

typedef struct M68k M68k;
struct M68k {
    uint32_t d[8], a[8];            /* a[7] = the active stack pointer */
    uint32_t pc, usp, ssp;
    uint16_t sr;
    uint8_t *ram;                   /* ram_size bytes (a power of two) at address 0 */
    uint32_t ram_size;
    uint32_t (*io_read)(void *ud, uint32_t addr, int size);          /* every access outside the RAM: size 1 / 2 / 4 */
    void (*io_write)(void *ud, uint32_t addr, int size, uint32_t v);
    void *ud;
    int stopped;                    /* STOP executed: waits for an interrupt */
    int halted;                     /* illegal instruction / address error: the driver is broken */
    uint32_t last_illegal;
    uint32_t wh;                    /* tests: a hash of the RAM writes since it was last cleared (address, size, value); not kept when M68K_NO_WH is defined */
    struct Dec *cache;              /* predecoded instructions of the code area [cache_lo, cache_hi) (m68k_cache_enable); NULL = decode every instruction again */
    uint32_t cache_lo, cache_hi;
    int nhook;                      /* m68k_hook: instructions replaced by C functions (a loop of the driver done in one step) */
    uint32_t hook_pc[4];
    int (*hook_fn[4])(M68k *m);
    void *hook_orig;                /* the decoded instructions the hooks replaced */
    int hook_off;                   /* tests: run the replaced instructions instead of asking the hooks */
};

void m68k_reset(M68k *m);                           /* SSP / PC from the vectors at 0, supervisor mode, interrupts masked */
int  m68k_step(M68k *m);                            /* one instruction; returns 0 when halted */
void m68k_cache_enable(M68k *m, uint32_t lo, uint32_t hi);     /* instructions at addresses lo .. hi are decoded once; writes of the 68000 into that area drop them, m68k_invalidate drops all (new code was loaded) */
void m68k_invalidate(M68k *m);
int  m68k_run(M68k *m, int max, const volatile int *stop);     /* steps until *stop, a halt or `max` instructions; returns the instructions run */
void m68k_hook(M68k *m, uint32_t pc, int (*fn)(M68k *m));      /* from now on the instruction at pc (in the cached code area) is replaced by fn: it returns 1 when it did the work of the instruction (and
                                                                  and what follows, setting m->pc itself), 0 when it does not apply (the instruction runs).  At most 4; m68k_hooks_clear drops them. */
void m68k_hooks_clear(M68k *m);
int  m68k_irq(M68k *m, int level);                  /* autovector interrupt: 1 = taken (the mask allows it), 0 = masked */

#endif
