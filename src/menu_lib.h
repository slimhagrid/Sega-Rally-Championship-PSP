/* M8: shared helpers of the menu screens (menu_lib.c, menu_sprite.c) */
#ifndef MENU_LIB_H
#define MENU_LIB_H
#include "sat.h"

void copy_b(u32 src, u32 dst, u32 n);
void copy_l(u32 src, u32 dst, u32 n);
void fill16(u32 dst, u32 val, u32 n);
void fill32(u32 dst, u32 val, u32 n);
void box_copy16(u32 src, u32 dst, u32 add);
void box_copy32(u32 src, u32 dst, u32 add);
void box_fill16(u32 val, u32 dst, u32 wh);
void box_fill32(u32 val, u32 dst, u32 wh);

void setcoloffset(s32 en, s32 sel, s32 a, s32 b, s32 c);
void inccoloffset(s32 sel, s32 a, s32 b, s32 c);
void white_in(s32 sel, s32 a, s32 b, s32 c);
void sl_white_out(void);
void fade_inout_init(s32 speed, s32 c0, s32 c1, s32 c2, s32 t0, s32 t1, s32 t2, s32 s0, s32 s1, s32 s2);
void pre_fade_inout(s32 from, s32 to, s32 step);
void fade_inout_work(void);

/* picture tables: the table pointer r14 runs through consecutive tables; the functions return the pointer behind what they consumed */
u32 cd_map_set(u32 tbl, u32 base);
u32 cd_map_set2(u32 tbl, u32 base);
u32 cd_color_set(u32 tbl, u32 base);

void key_repeat_init(void);
u32 key_repeat(s32 player);

void delete_nbg0(void);
void sat_load_name_at(u32 nameaddr);                /* FUN_0600F354: load the scatter file whose name is at the address */

/* VDP1 sprite commands (sprite module, master CPU) */
void draw_sprite(u32 pos, u32 desc, u32 z);
void setup_texture(u32 tbl, u32 base);
void mkpoly_init(void);
void s_mkpoly_init(void);
void m_mkpoly_frame_init(void);
void s_mkpoly_frame_init(void);

/* the action objects (0x0601BB5C / 0x0601BB02): slots of 0x80 bytes, +0 = the action function (an original address registered with menu_reg), run with the slot in act_cur
 * (the original's r14 and gbr) */
extern u32 act_cur;
void actor_init(u32 slot, u32 fn, u32 n);               /* 0x0601BB5C */
void actor_run(u32 slot, u32 n);                        /* 0x0601BB02 */
void menu_pal_anim(u32 blk);                            /* 0x0602EF68: the palette cross fade block (menu_option.c) */

#endif
