/* M9: the sound system.  The original sound driver (SDDRVS.DAT, a 68000 program) runs on an interpreter (m68k.c) over the sound RAM of the emulated console (sat->sndram); its SCSP
 * register writes drive the SCSP model (scsp.c); the result is mixed to 44.1 kHz stereo for the PSP's audio hardware.  The game side (the SH-2's sound library: command table,
 * queues, file loads, docs/sound.md) is snd_lib.c and writes the sound RAM exactly like the original; the driver does the rest (sequences, engine pitch, voices). */
#ifndef SND_H
#define SND_H

#include <stdint.h>

#define SND_TICK_FRAMES 89              /* samples per driver tick (the SCSP's timer A after the driver's reload value 0xA7) */

typedef struct SndCore SndCore;

SndCore *snd_core_new(uint8_t *ram);                                /* ram: 512 KB sound RAM */
void snd_core_free(SndCore *c);
void snd_core_reset(SndCore *c);                                    /* the driver is in the RAM: reset the 68000 (SSP / PC from its vectors), the SCSP and the timers */
int  snd_core_tick(SndCore *c, float *out);                         /* one driver tick: timer B interrupt, timer A work, SND_TICK_FRAMES stereo frames added to out (l, r); 0 = the driver stopped (illegal instruction) */
int  snd_core_ready(const SndCore *c);                              /* 1 once the 68000 has been reset */
uint32_t snd_core_pc(const SndCore *c);
uint32_t snd_core_ticks(const SndCore *c);
int snd_core_active_slots(const SndCore *c);
void snd_core_set_hle(SndCore *c, int on);                          /* the driver's slot-scan loop in C (takes effect at the next snd_core_reset; only for the 1P driver) */
void snd_core_set_prof(SndCore *c, unsigned *hist);                  /* statistics: count the executions per 16 bytes of the code area (0x600 entries) */
void snd_core_set_hle_verify(int on);                                /* tests: every hook also runs the instructions it replaces on a copy of the CPU and compares the registers (abort on a difference) */
void snd_core_set_idle_skip(SndCore *c, int on);                    /* skip the driver's tick while it has nothing to do (silent, no command, no sequence): saves the 68000's housekeeping */
unsigned long long snd_core_skipped(const SndCore *c);
unsigned long long snd_core_irq_steps(const SndCore *c);             /* statistics: instructions of the timer B interrupt routine */
void snd_core_set_clock(SndCore *c, unsigned long long (*us)(void));      /* statistics: a microsecond clock; snd_core_times gives the time spent in the 68000 and in the mixer */
void snd_core_times(const SndCore *c, unsigned long long *cpu_us, unsigned long long *mix_us, unsigned long long *steps);
int snd_core_active_slots_dummy(void);                         /* statistics: slots playing in the last tick */
void snd_core_debug_regs(const SndCore *c, uint8_t *regs_out);      /* the 4 KB SCSP register window (tests) */
void snd_core_set_log(SndCore *c, void (*cb)(void *ud, uint32_t tick, uint32_t off, int size, uint32_t v), void *ud);      /* every write to the SCSP registers (tests) */

#include "m68k.h"
void snd_core_set_test_mode(SndCore *c, int on);                    /* tests (lockstep with unicorn, which cannot take interrupts): timer B runs as a subroutine call from the idle point, the RTE of its routine patched to RTS by the caller */
void snd_core_set_trace(SndCore *c, void (*cb)(void *ud, const M68k *cpu), void *ud);      /* after every 68000 instruction (tests: lockstep with unicorn) */

/* the PSP audio thread (menu_host.c / play.c start it once): produces blocks of 1024 frames for sceAudio */
int  snd_audio_start(void);
void snd_audio_stop(void);
void snd_audio_reset(void);                                         /* a new driver / data set was loaded into the sound RAM */

#endif
