/* Development: data/autoplay.txt, a pad script for unattended runs of the menus in PPSSPP that waits for the game's own state (see devauto_menu.c for the commands). */
#ifndef DEVAUTO_MENU_H
#define DEVAUTO_MENU_H

int      devauto_menu_load(const char *base);                         /* 1 when data/autoplay.txt exists and has commands */
extern int devauto_realpad;                                          /* "realpad 1": the script only waits and shoots, the real pad is the input (keys sent by the test runner) */
unsigned devauto_menu_pad(unsigned tick);                             /* the Saturn pad word of this V-blank tick (the script replaces the pad) */

#endif
