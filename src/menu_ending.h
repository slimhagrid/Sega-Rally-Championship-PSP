/* M8: the ceremony and the ending after the goal of the last arcade stage (menu_ending.c).  What the original's pipeline would draw is recorded here every tick for the host (menu_host.c). */
#ifndef MENU_ENDING_H
#define MENU_ENDING_H
#include "sat.h"

void ceremony_init(void);                       /* 0x0601E4E8 */
void ceremony_main(void);                       /* 0x0601E664 */
void ending_init(void);                         /* 0x0601EF34 */
void ending_main(void);                         /* 0x0601F0A0 */
extern void (*menu_engine_hook)(u32 addr);      /* the race engine's routines the ending calls (addresses of the original's routines); the host binds it, the test leaves it empty */
void init_game_over(void);                      /* 0x06011EA4 */
void get_camera_angle(const s32 a[3], const s32 b[3], s32 *r0, s32 *r1);      /* 0x0601E970 */
void ending_ghost_engine(u32 addr);             /* the PSP's stand-in for the race engine of the ending (menu_host.c binds it to menu_engine_hook) */
void init_gscroll(void);                        /* 0x06015B80: the scroll / window set-up of the game screens */
extern int menu_anim_log;                       /* the tests: every part's matrix and model address are also written to the console memory (0x06070000 + 52 * part), where the oracle's hook of draw_polygon2 puts them */
void menu_ending_register(void);

enum { CER3D_CONF = 70 };
typedef struct { s32 pos[3], ang[3]; u32 model; } CerConf;
typedef struct {
    int have;                                   /* the camera actor ran this tick */
    int ending;                                 /* the ghost car of the ending was recorded this tick (car_id / car) */
    s32 cam_pos[3], cam_ang[3];                 /* the camera: position (16.16) and pitch / yaw / roll (16 bit turns: angle as the original passes it to rotate_x / y / z, negated) */
    int mirror;
    int car_id;                                 /* byte 92 of the car block: 0 Celica, 1 Delta, 10 Stratos */
    u8 car[0x100];                              /* the first 0x100 bytes of the car block 0x060B0000 as the actor left them: +12 / +16 / +20 angles, +24 / +28 / +32 position */
    int anim;                                   /* the animated model of the car (FUN_0601CF44) is on screen */
    u32 anim_tbl;                               /* its table: u16 frames, u16 parts, u32 key frames, u32 model of every part */
    s32 anim_frame;                             /* the animation frame (16.16) */
    int anim_np;                                /* the parts drawn this tick: the matrix FUN_0601CF44 passes to draw_polygon2 (view * the model's place * the part's key: 16.16, 3 x 4) and the model's address */
    s32 anim_m[48][12];
    u32 anim_model[48];
    int nconf;
    CerConf conf[CER3D_CONF];                   /* the confetti that is on screen */
} Cer3D;
extern Cer3D cer3d;
extern unsigned cer3d_tick;

#endif
