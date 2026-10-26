/* Sega Rally PSP - the race camera (M6), ported from the original's camera module (0x0600FA5C .. 0x060101B8, docs/collision_world_ai.md section 3): the start fly-by on a
 * B-spline, the chase view (B) and the car-fixed view (A), the goal orbit.  camera_pass() = gctrl::camera_ctrl; the result is the game's own 3 x 4 view matrix (16.16). */
#ifndef CAMERA_H
#define CAMERA_H
#include "drive.h"

#define CAM_WORK_BYTES 0xB4

typedef struct { const unsigned char *p; unsigned base, size; } CamDat;        /* CAMERA.DAT as the console's memory holds it (0x002A2000 ..: pointers inside are absolute) */

typedef struct {
    unsigned char w[CAM_WORK_BYTES];    /* the work area at 0x060AA9B0 (big endian): +0 handler, +8 eye, +0x14 / +0x1A angle words, +0x20 eye offset, +0x2C pivot, +0x38 counter */
    int view[12];                       /* the view matrix (0x060D1000): rows of 4 longs, p' = M p */
    int view_copy[12];                  /* its copy at 0x060D1800 (the slave CPU's) */
    int spline[12];                     /* datgbal::start_spldata (0x002994C0): the fly-by control points, x[4] y[4] z[4] */
    int sky[5];                         /* the sky's rotation parameter table (0x060B901C ..): A, B, D, E, Mx (16.16), as the V-IN's DMA2 copies it to VDP2 after the smoothing */
    int sky_t[4];                       /* the targets of set_grpara2 (0x060AA9F8 ..): Mx, A, B, D */
    int sky_base;                       /* [0x060AAA08]: the course's base Mx (16.16) */
    int replay, cut;                    /* play mode 2: the cut-scene cameras of CAMERA.DAT (camera_replay_setup); cut = they are running (until the goal's orbit) */
    CamDat camdat;
    Drive *dummy;                       /* the camera's dummy car (0x060B2600): the road block at the eye picks the visible parts */
    int pers;                           /* [0x060D008A]: the perspective distance the replay camera asks for (0x200 at the start; the pipeline's 224 in the race) */
    int pm_on;                          /* the part list of this pass comes from the eye's block (calc_camera_pmap): pm = its two block ids and the display angle (car +0xF0, +0xF1, +0xC7) */
    unsigned char pm[3];
    int fade;                           /* last fade request of the camera code (etc::fade_color_init argument: 3 = fade out at the end of the fly-by, 0 = fade in), -1 = none this pass */
} Camera;

void camera_init(Camera *cam);                      /* clr_camera_ctrl_work with the init handler (normal_cainit) */
void camera_replay_setup(Camera *cam, const CamDat *dat, Drive *dummy);   /* the replay's camera: normal_cainit in play mode 2 starts ending_camera_act; dummy = the dummy car (drive_dummy_init) */
void camera_replay_keys(Camera *cam, Drive *d, int abc, int xyz);          /* replay_cactl: the pad's A / B / C (cut-scene cameras <-> chase camera) and X / Y / Z (the chase camera's view), once per pass before camera_pass */
void camera_chase_pose(Camera *cam, Drive *d);                              /* replay_ca's first half: the eye and the angle words of the chase view in cam->w (CW_EYE +8, angle words +0x14) */
void camera_pass(Camera *cam, Drive *d);            /* camera_ctrl: one pass of the camera handler, after the car's move function */

void camera_sky_init(Camera *cam, int course);      /* init_gscroll: the sky table of the course (A = E = 1, B = D = 0, Mx = base) */
void camera_sky_pass(Camera *cam, const Drive *d);  /* rscroll_ctrl (set_grpara2: targets from the camera's yaw / roll) and the V-IN's set_grpara_int (one IIR step towards them) */

#endif
