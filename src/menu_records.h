/* M8: the record screens and the record data code (menu_records.c, menu_rec_data.c, menu_rec_disp.c, menu_bup.c) */
#ifndef MENU_RECORDS_H
#define MENU_RECORDS_H
#include "sat.h"

typedef struct { u32 r0, r1; } RecPair;

/* ---- record data (menu_rec_data.c): shared with the result screen / name entry ---- */
void record_dataset(void);                      /* 0x0602C6C0 rec_dataset */
RecPair record_connect(u32 r4);                 /* 0x0602C9E8 rec_connect: r0 = the record list of the current mode / course / car, r1 = its comparison value */
/* the merges return the value the original leaves in r5 (the screens store it) */
u32 record_sort_a(u32 r4);                      /* 0x0602EA84 */
u32 record_sort_b(u32 r4, u32 r5);              /* 0x0602EA62 */
u32 record_sort_c(u32 r4);                      /* 0x0602EAAC */
u32 record_merge16(u32 r4);                     /* 0x0602E8AC rec_menu2::record_menu */
u32 record_merge3(u32 r4, u32 r5);              /* 0x0602E968: r4 -> three list pointers (r4, r4+16, r4+32); the 6 best -> r5 (+48: where each came from) */
u32 record_merge2(u32 r4, u32 r5, u32 r6);      /* 0x0602EA0E: r4, r5 -> pointers to two merged lists, the 6 best -> r6 */

/* ---- record displays (menu_rec_disp.c): the digit / name writers of rec_connect.  r4 = the VDP2 name table position (returned advanced); r2 = the digit */
typedef u32 (*RecPutNum)(u32 r4, u32 r2);
void rec_disp_ctx(s32 r11, s32 r14);            /* the registers the original's writers read: r11 > 0 = 32-bit cells, r14 >= 0 = highlighted row */
u32 time_disp(u32 r4, u32 r5, RecPutNum put);   /* 0x0602CAF8: a BCD time 0x00MMSSCC as M'SS"CC through the digit writer `put` */
u32 putnum_1x2(u32 r4, u32 r2);                 /* 0x0602CB48 */
u32 putnum_rec(u32 r4, u32 r2);                 /* 0x0602CB62 */
u32 putnum_name(u32 r4, u32 r2);                /* 0x0602CBA4 */
u32 putnum_name2(u32 r4, u32 r2);               /* 0x0602CBBA */
u32 putnum_rank(u32 r4, u32 r2);                /* 0x0602CBD8 */
u32 putnum_rank2(u32 r4, u32 r2);               /* 0x0602CC0A */
u32 putnum_Grn(u32 r4, u32 r2);                 /* 0x0602CC44 */
u32 putnum_Grn2(u32 r4, u32 r2);                /* 0x0602CC5C */

u32 record_name_disp(u32 r4, u32 r5);           /* 0x0602EE68: the three letters of a name word */
void record_page_list(u32 r4, u32 r5);          /* 0x0602EB34: draws a ranking page */

/* the other record users (name entry, results, options) */
u32 record_insert(u32 r4, u32 r5, u32 r6, s32 r8);   /* 0x0602E8EA: insert the new time at r4 into the list r5 (r6 entries) -> 0x060A8CF0 + r8 * 5; returns the number of entries before the new one */
void adv_recordact_init(void);                  /* 0x0602E5F0: the title demo's record header slide */
void adv_recordact(void);                       /* 0x0602E638 */
void rec_vdp2_set_w(u32 tbl);                   /* 0x0602D370 (r8 = table: n, then n x (u16 offset, u16 value) into the VDP2 registers) */
void rec_pal_set(u32 tbl, u32 base);            /* 0x0602D330 (r8 = table, r11 = base): n x (u32 source + base, s16 offset into 0x060B8000, s16 long count) */
void rec_color_set(u32 tbl);                    /* 0x0602D358 (r8 = table): n x (s16 offset, u16 value) into 0x060B8000 */
void rec_tile16(u32 r4, u32 r5, u32 r6, u32 r7);   /* 0x0602EF2E: r7 rows of r6 16-bit cells numbered from r4 at r5 (row = 0x80) */
void rec_tile32(u32 r4, u32 r5, u32 r6, u32 r7);   /* 0x0602EF4A: the same with 32-bit cells (row = 0x100) */
void menu_records_register(void);
#endif
