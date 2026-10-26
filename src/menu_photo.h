/* M8: the pieces of the photo module (menu_photo.c) the title / attract module (menu_adv.c) shares: the scene it draws is the same (the course seen by a script camera over the sky) */
#ifndef MENU_PHOTO_H
#define MENU_PHOTO_H
#include "sat.h"

void get_block(u32 blk);                        /* 0x06007EAC: the road block under the car block `blk` (the original's gbr) */
void calc_dispang2(u32 blk);                    /* 0x06007D68 / calc_dispang 0x06007D2C */
void get_poly_map(u32 blk);                     /* 0x060081B8: the blocks drawn around it ([0x060AA104 ..], count [0x060AA000]) */
void crs_adv_dataset(void);                     /* 0x060047C4: AD<course>.DAT */
void set_game_texture(void);                    /* 0x0601AB6C */
void event_init(u32 course);                    /* 0x0601BC94 */
void normal_endmove(void);                      /* 0x06006BD4: the scroll control */
void photo_stats(void);                         /* 0x0601D756 */
void fade_color_init(u32 n);                    /* 0x0600585C */
void photo_scene_vram(void);                    /* 0x0600EC38 = init_adv_vram 0x0600D490 (the same set-up) */

#endif
