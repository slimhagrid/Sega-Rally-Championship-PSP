/* M8: the screens after a race (menu_post.c: dispatcher, ranking, game over; menu_name.c: the name entry; menu_result.c: the result screen) */
#ifndef MENU_POST_H
#define MENU_POST_H
#include "sat.h"

void bup_main_save(void);                       /* 0x0602FC40 (menu_bup.c) */
void bup_replay_save(void);                     /* 0x0602FCB4 */
void tm_set_tm_att(void);                       /* 0x060262A4 (menu_timeattack.c) */

u32 rec_cg_set(u32 t, u32 base);                /* 0x0602D2B8: a table of BPE streams into VDP2 VRAM (menu_option.c); returns the table pointer behind it */
void name_prepare(void);                        /* 0x06030D84: does the time reach the table (-> [0x060A8C62] / [0x060A8C63]) */
void name_entry(void);                          /* 0x060300C0 .. 0x060304A2: the name entry screen (runs until it is over) */
extern int menu_gover_early_key;
extern void (*menu_gover_hook)(void);           /* called when the game over screen starts: the PSP host puts what the original race leaves for it into the VDP2 memory (the GAME OVER picture) */
void menu_name_register(void);
void menu_post_register(void);
void menu_result_register(void);
#endif
