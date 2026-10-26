/* M9: the SH-2 side of the sound system: the original's sound library (module "sound" / "gamesd" of 0RALLY.DAT, 0x06016B74 .. 0x06017A82), ported function by function onto the memory
 * model: the command table / queues in work RAM (0x060A1268, 0x060AAA14, 0x060AAA24, 0x060AAA34), the command slots of the sound RAM (0x25A00700 ..), the loads of the driver and the
 * banks.  The ported screens call it through snd_call() (sat.c): the tests of the screens leave the hook empty (the oracle stubs the same calls), the PSP host sets snd_call_hook. */
#ifndef SND_LIB_H
#define SND_LIB_H

#include "sat.h"

extern void (*snd_call_hook)(u32 fn, u32 r4, u32 r5, u32 r6);      /* called by snd_call() after it logged the call (sat.c) */

/* the host's hardware for the library: the SMPC's SNDON / SNDOFF (the 68000 runs / is held in reset) and a wait for the driver (the SH-2 loops on the 68000's acknowledge flag) */
extern void (*snd_hw_cpu)(int on);
extern void (*snd_hw_wait)(void);

extern void (*snd_log_hook)(const char *msg);                       /* development */
void snd_lib_boot(void);                    /* FUN_06016B74: what the power-on code does for the sound: load the driver, start the 68000 */
void snd_lib_call(u32 fn, u32 r4, u32 r5, u32 r6);

/* the entry points the other modules (the race) call, by their original names */
void snd_lib_play(u32 id);                  /* FUN_06016FBC */
int  snd_lib_status(int id);                 /* FUN_06016F04 */
void snd_lib_vint(void);                    /* FUN_060170C4: the V-blank handler's share: the voice stream (60 times a second) */
extern unsigned snd_lib_nflush;
void snd_lib_flush(void);                   /* FUN_06017580: the per-frame sender */

#endif
