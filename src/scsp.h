/* M9: a small model of the Saturn's sound chip (SCSP) for the PSP: the 32 slots of the original (PCM8 / PCM16 playback with loop modes, pitch, the envelope generator, total level, direct
 * send level and pan) mixed to 44.1 kHz stereo.  Not modelled: the LFO, FM, the DSP effects (reverb), the effect sends.  tools/scsp_render.py is the Python twin (same tables). */
#ifndef SCSP_H
#define SCSP_H

#include <stdint.h>

typedef struct {
    int state;                      /* 0 off, 1 attack, 2 decay 1, 3 decay 2, 4 release */
    float pos;                      /* position inside the sample in samples */
    int dir;                        /* 1 forward, -1 backward (alternating loops) */
    float amp;                      /* attack: linear amplitude 0 .. 1 */
    float level_db;                 /* decay / release: attenuation in dB */
    float gain;                     /* the same as a factor (kept by multiplying: no powf per sample) */
    uint32_t fpos;                  /* position in samples << 12 */
    int fdir;
} ScspSlot;

typedef struct {
    uint8_t regs[0x1000];           /* the register window the 68000 sees at 0x100000 */
    ScspSlot slot[32];
    const uint8_t *ram;             /* sound RAM (512 KB) */
    int active;                     /* slots that were playing in the last scsp_render (statistics) */
} Scsp;

void scsp_init(Scsp *s, const uint8_t *ram);
void scsp_write(Scsp *s, uint32_t off, int size, uint32_t v);         /* a 68000 write to the registers: KYONEX starts / stops the slots */
void scsp_render(Scsp *s, float *out, int n);                         /* adds n stereo frames (l, r interleaved) of the slots to out */

#endif
