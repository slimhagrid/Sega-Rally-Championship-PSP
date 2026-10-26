/* M8: the PSP's own button names in the menu pictures (src/menu_pspfix.c) */
#ifndef MENU_PSPFIX_H
#define MENU_PSPFIX_H
int pspfix_load(const char *dir);   /* reads the PSP art of the pictures (data/menu/psp_pad.bin); 1 = loaded */
int pspfix_apply(void);         /* redraws the Saturn button texts that are loaded into VDP2 VRAM now; returns the number of fixes applied */
void pspfix_learn(void);        /* development: prints the hash of every fix box in the current memory */
void pspfix_scan_tick(unsigned tick);   /* development: finds the pictures of the fixes at other places of the layers */
#endif
