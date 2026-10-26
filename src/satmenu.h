/* M8: the menu screens' entry points, addressed by the original's code addresses (the main vector [0x060A001C] and the screen vectors hold these words). */
#ifndef SATMENU_H
#define SATMENU_H
#include "sat.h"

typedef void (*MenuFn)(void);

void menu_reg(u32 addr, MenuFn fn);     /* register the port of the original function at `addr` */
void menu_call(u32 addr);               /* run it; an address without a port aborts (the screens that are not ported yet) */
void menu_unported(u32 addr);           /* a path into code that has no port yet: abort with the address */
void menu_init(void);                   /* registers every ported screen */
extern void (*menu_call_hook)(u32 addr);        /* called by menu_call() first with the address (the test uses it to end a scenario at a given dispatch) */
extern void (*menu_unported_hook)(u32 addr);    /* called by menu_unported() first (the test uses it to end a scenario at the hand-over to an unported screen) */
extern int menu_option_ntypes;                  /* KEY CONFIG types the options page offers (the original: 4; the PSP host: 3, A / B / DIY) */
extern int menu_no_battle;                       /* the PSP host: there is no second pad, so the mode select skips "2 Player Battle" like the original does without one (the tests leave it 0: the harness has a pad id in 0x060AA9A6 / 0x060AA9A9 that decides) */
extern int (*menu_option_diy_hook)(void);       /* the PSP host: the confirm key on the last type (DIY) opens its editor (returns 1); the tests leave it empty */
extern int (*menu_stub_hook)(u32 addr);        /* called before that: 1 = the host did the routine's work itself and menu_unported() returns (the PSP host, for the race's data set-up 0x060041F0) */
void record_dataset(void);                      /* rec_dataset 0x0602C6C0 (menu_rec_data.c) */
void race_exit(void);                           /* plctrl::game_exit 0x06006A18 (menu_race.c): the next screen after a race of game type 0 / 2 */
void race_history(void);                        /* game_exit's tail 0x06006B58: the result of the stage goes to the history (menu_race.c; also after leaving a race through the pause menu) */


/* The 3D parts of the screens (the turntable cars, the course models) are not drawn by the port: every pass the screens say what the original's pipeline would draw, and the PSP host draws
 * it (menu_host.c).  The request is a snapshot of the model block (0x060E4000 .. / 0x060E5100 / 0x060E5180) and the screen offset word (x << 16 | y, 352 x 224 picture dots).  The list
 * belongs to the pass that filled it: the first request of a new pass (new tick) starts a new list. */
typedef struct { u32 kind, block, offset; int model, still, car2, noshadow; s32 ang[3]; s32 pos[3]; } Menu3D;       /* kind 0: a car (dispSelectCar); angles in 16 bit turns, positions 16.16 */
enum { MENU3D_MAX = 16 };
extern Menu3D menu3d_req[MENU3D_MAX];
extern int menu3d_n;
extern void (*sat_frame_init_hook)(void);                /* menu_sprite.c: the host's hook at the start of every screen pass (mkpoly_frame_init) */
extern unsigned menu3d_tick;
void menu3d_car(u32 block, u32 offset, int car2);       /* car2: dispSelectCar2 (time attack, set-up) instead of dispSelectCar (car select) */
void menu3d_model(u32 model_ptr, u32 offset);        /* kind 1: a model of SELECT.MDL (the course / object of the block 0x060E5100: position +12 / +16 / +20, pitch +24, yaw +28) */

/* The matrix calls of the screens that run their own polygon models (the name entry's letters, the result screen's objects): the port records the original's calls (initial_matrix,
 * translate, rotate_x / y / z, scale, then the draw of a polygon model: FUN_0601DB20 with r5 = the model, r6 = a shading parameter) and the host replays them on its own matrix. Angles are
 * the original's 16 bit turns (the argument as passed: only the low 16 bits count), translations / scales 16.16.  The list belongs to the pass that filled it, like the 3D cars. */
enum { M3_INIT, M3_TRANSLATE, M3_ROT_X, M3_ROT_Y, M3_ROT_Z, M3_SCALE, M3_DRAW, M3_OFFSET };
typedef struct { int op; s32 a, b, c; } M3Op;
enum { M3_MAX = 1024 };
extern M3Op m3_ops[M3_MAX];
extern int m3_n;
extern unsigned m3_tick;
void m3(int op, s32 a, s32 b, s32 c);
void m3_translate_v(u32 vec);                           /* translate(r5 = pointer to three longs) */

/* The photo module's scene (menu_photo.c, the staff roll after a won practice race): the course seen by the camera of the shot.  The port runs the shot walk and the road block search; the
 * course polygons and the scenery objects are the host's (menu_host.c draws them with the race renderer between the sky (VDP2 RBG0) and the text (NBG3)).  Filled every pass. */
typedef struct {
    unsigned tick;                  /* sat_ticks of the pass that filled it (0 = never) */
    int course;                     /* 0 desert .. 3 lake */
    s32 pos[3];                     /* the camera's eye (16.16 m) */
    s32 pitch, yaw;                 /* rotate_x(-pitch), rotate_y(-yaw) of the view matrix (16 bit turns), then translate(-pos) */
    int nparts;
    u8 parts[16];                   /* the map blocks to draw, nearest first (get_poly_map: [0x060AA140 ..]) */
    int stretch;                    /* 1: the race's chase camera made the view (FUN_060100D8): scale(1.0625, 1, 1) in front of the rotations */
    int have_mat;                   /* 1: the view matrix is the original's own (the title demo's script cameras turn it with c_turnv): mat = its 3 x 4 rows (16.16), pos / pitch / yaw are not used */
    s32 mat[12];
} PhotoView;
extern PhotoView photo_view;

/* The title demo's two cars (menu_adv.c): the record of the pass (the recorded run of the demo: car block 0x060B0000 / 0x060B0600 with the angles at +12, the position at +24, the model at +92)
 * and whether the script shows the car (the car display functions 0x0600D5D4 / 0x0600D5E8 call dispCar).  Filled every pass of the demo. */
typedef struct { int shown; int model; s32 ang[3]; s32 pos[3]; } AdvCar;
typedef struct { unsigned tick; AdvCar car[2]; } AdvCars;
extern AdvCars adv_cars;

#endif
