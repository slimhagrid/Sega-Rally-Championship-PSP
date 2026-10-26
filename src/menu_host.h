/* M8: the PSP host of the ported menu screens (menu_host.c) */
#ifndef MENU_HOST_H
#define MENU_HOST_H
#include "sat.h"

/* runs the menus on the console memory model; does not return (the program is left by the HOME button).  base = the data folder (menu/*.blk below it), start_vector = the screen handler
 * entered first (0 = the main vector the boot leaves, the title) */
int menu_host_run(unsigned int *gulist, const char *base, u32 start_vector);


#include "play.h"
void menu_host_assets(const PlayAssets *pas);     /* the race's models (data/course/cars.dat, object.rcp: also the 3D cars of the menus) and everything play_run needs to run a race for the stage start screens */
#endif
