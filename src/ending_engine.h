/* Sega Rally PSP - the race engine of the ending (ending_engine.c): menu_engine_hook's native implementation. */
#ifndef ENDING_ENGINE_H
#define ENDING_ENGINE_H
#include "sat.h"
#include "render.h"
#include "car.h"

void ending_engine_set_base(const char *base);                  /* the data folder (course/phys.dat, course/<stage>.rct) */
void ending_real_engine(u32 addr);                              /* the hook: the original's routine addresses the ending calls */
int  ending_engine_ready(void);
int  ending_rival_items(const float *view, SceneItem *out, int max, const CarData *cars, const Course *objects);       /* the rivals that are drawn this pass, in the world of the photo scene's view */

/* what menu_ending.c gives the engine */
void ending_present_car(void);                                  /* the player's car for the host to draw (cer3d) */
void ending_chase_done(void);                                   /* the chase camera's eye / angles are in the camera work: the part list at the eye and the view follow */

#endif
