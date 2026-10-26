/* Shared by tools/test_drive.c (-hude) and tools/test_hudend.c: the end-of-race screen after one pass against the oracle's words (tools/oracle.py hude_snapshot(): the NBG0 pages as big endian words,
 * then 11 register words: NBG0 scroll x / y, BGON shadow / register, WCTLA, state (2 words), timer (2 words), flag byte [0x060AA803], message byte [0x060AAA45]). */
#ifndef HUDE_CMP_H
#define HUDE_CMP_H
#include <stdio.h>
#include "hudgen.h"

#define HUDE_WORDS (HUDE_CELLS * 2 + 11)

static unsigned hude_be16(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }

/* applies one pass record (u16 changes, (u16 word index, u16 value) each) to `want`; returns the pointer after it */
static const unsigned char *hude_apply(unsigned short *want, const unsigned char *rec)
{
    unsigned nch = hude_be16(rec);
    rec += 2;
    for (unsigned k = 0; k < nch; k++, rec += 4) want[hude_be16(rec)] = (unsigned short)hude_be16(rec + 2);
    return rec;
}

/* the NBG0 pages hudgen_load makes (hude_init.dat) against the trace's first snapshot; 0 = equal */
static int hude_check_init(const HudGen *h, const unsigned short *want)
{
    for (int i = 0; i < HUDE_CELLS; i++) if (h->nt0[i] != ((unsigned)want[2 * i] << 16 | want[2 * i + 1])) return i + 1;
    return 0;
}

/* 0 = equal, else a message in `what` */
static int hude_compare(const HudGen *h, const Drive *d, const unsigned short *want, char *what, size_t n)
{
    for (int i = 0; i < HUDE_CELLS; i++) {
        unsigned w = h->nt0[i];
        if ((w >> 16) != want[2 * i] || (w & 0xFFFF) != want[2 * i + 1]) { snprintf(what, n, "nt0[%05x] got %08x want %04x%04x", 0x28000 + 4 * i, w, want[2 * i], want[2 * i + 1]); return 1; }
    }
    const unsigned short got[11] = { h->nscroll[0], h->nscroll[1], h->bgon, h->bgon, h->wctla, (unsigned short)((unsigned)d->ret_state >> 16), (unsigned short)d->ret_state,
                                     (unsigned short)((unsigned)d->cd_timer >> 16), (unsigned short)d->cd_timer, (unsigned short)(d->g803 & 0x40), (unsigned short)d->msg45 };
    static const char *const rn[11] = { "scroll x", "scroll y", "bgon", "bgon reg", "wctla", "state hi", "state", "timer hi", "timer", "flags&40", "msg" };
    for (int i = 0; i < 11; i++) {
        unsigned short w = want[HUDE_CELLS * 2 + i];
        if (i == 9) w &= 0x40;                                                          /* (bit 5, the fade, is not modelled; bit 1 depends on it) */
        if (i == 10 && want[HUDE_CELLS * 2 + 2] == 0x1C) continue;                      /* (the message byte is the event code's while racing) */
        if (got[i] != w) { snprintf(what, n, "%s got %04x want %04x", rn[i], got[i], w); return 1; }
    }
    return 0;
}
#endif
