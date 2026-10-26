/* M8: the original program's library routines (etc / vdp / scroll / switch / init modules) on the Saturn memory model.  Names are the original's. */
#ifndef SATLIB_H
#define SATLIB_H
#include "sat.h"

/* int module: the sync calls (sat.c) */
void sync_vin(void);
void sync_vout(void);
void sync_dsp(void);
void sync_n(u32 n);
void intm_sync(u32 n);
void ints_sync(void);

/* etc: block copy / fill (the original passes src in r8, dst in r9; fills end at r8 and run downwards) */
void rcopy(u32 src, u32 dst, u32 nlongs);
void rcopyw(u32 src, u32 dst, u32 nwords);
void rfill(u32 dst, u32 val, u32 nlongs);
void rfillff(u32 end, u32 val, u32 nlongs);          /* fills [end - 4 * nlongs, end) */

/* scroll */
void set_mov_scr(void);
void set_mov_scri(void);
void init_mov_scri(void);
void set_window_scr(void);
void set_window_scri(void);
void init_window_scri(void);
void set_scr_int(void);
void init_scr_int(void);
void set_grpara_int(void);
void set_grpara_int2p(void);
void set_rpara_int(void);
void init_rpara_int(void);

/* vdp */
void tv_off_boot(void);
void tv_off(void);
void tv_on(void);
void init_vdp1sys(void);
void init_vdp1sys2(void);
void init_vdp2sys(void);
void init_vram(void);
void vram_allclr(void);

void vram_scrmap_clr2(void);
void vram_scrmap_a_clr(void);
void init_colorwk(void);
void init_nscr_vol(void);
void init_gspr(void);
void init_gspr2(void);
void init_oscroll(void);

/* switch / vdp */
void pad_read(void);
void pad_kind_init(void);
void swclear(void);

void sat_boot(void);

#endif
