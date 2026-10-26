/* The PSP's race controls (KEY CONFIG of the options): three types A, B and DIY (the original's type D is gone).  Each type says, for the eight buttons that can have a function, which
 * function it has; the digital pad's left / right and the analog nub always steer.  menu_pspfix.c draws the page from this table, play.c reads the race pad through it, menu_host.c edits
 * the DIY type and keeps it in data/keycfg.txt. */
#ifndef MENU_PADCFG_H
#define MENU_PADCFG_H

enum { PC_NONE, PC_BRAKE, PC_ACCEL, PC_SHIFT_UP, PC_SHIFT_DOWN, PC_VIEW, PC_NACT };
enum { PCB_L, PCB_UP, PCB_DOWN, PCB_TRIANGLE, PCB_R, PCB_CIRCLE, PCB_CROSS, PCB_SQUARE, PC_NBTN };      /* the label slots of the page, in the order of the Saturn pad picture's slots */
enum { PC_TYPES = 3 };                                                                                   /* A, B, DIY */

extern unsigned char padcfg_act[PC_TYPES][PC_NBTN];             /* [type][button] = function */
extern const char *const padcfg_act_name[PC_NACT];
extern const char *const padcfg_btn_name[PC_NBTN];

void padcfg_defaults(void);                                     /* A and B as designed, DIY = A */
void padcfg_load(const char *base);                             /* data/keycfg.txt: the DIY type */
void padcfg_save(const char *base);
/* the race's view of a type: Saturn pad word masks (the original's row table 0x060086C8 layout: accelerator, brake, shift up, shift down, view, 0, 0) */
void padcfg_rows(int type, unsigned short row[7]);
unsigned padcfg_sat_mask(int button);                           /* the Saturn pad bit of a button slot */

#endif
