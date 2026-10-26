/* Development aids for unattended test runs, see devauto.h. */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "devauto.h"
#include "fx.h"

int   devauto_pilot_mode;
float devauto_vmax = 2.1f;
float devauto_curve_k = 1.0f, devauto_steer_k = 1.6f, devauto_look = 1.0f;
int   devauto_race_shots, devauto_turbo, devauto_cheat_time, devauto_cheat_rank;

static const unsigned GUIDE[4] = { 0x00299ED8u, 0x0029B1C8u, 0x0029C4B8u, 0x0029D7A8u };      /* guide_africa / yosemite / corse / europe = desert, forest, mount, lake (PRGDATA.DAT) */

static float wrap_pi(float a)
{
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

/* the race block of the car slot starts at +0x500: the time limit at +0x34 (seconds << 8 | the pass of the second), the control byte at +0xF5 (4 count-down, 6 race, 8 goal, 10 game over), the rank at +0xB6 */
void devauto_cheats(Drive *d)
{
    unsigned char *c = d->car;
    int ctl = c[0xF5];
    if (devauto_cheat_time && ctl < 8) {
        int t = g32(c, 0x534);
        if ((t >> 8) < 20) p32(c, 0x534, (60 << 8) | (t & 0x7F));
    }
    if (devauto_cheat_rank && ctl == 8) { c[0xB6] = (unsigned char)devauto_cheat_rank; d->g300 = devauto_cheat_rank; }
}

void devauto_pilot_init(DevPilot *p, const Phys *ph, int course)
{
    unsigned g = GUIDE[course & 3];
    memset(p, 0, sizeof *p);
    for (int k = 0; k < DEVAUTO_NGUIDE; k++) {
        p->x[k] = (float)phys_r32(ph, g + 16u * (unsigned)k) / 65536.0f;
        p->z[k] = (float)phys_r32(ph, g + 16u * (unsigned)k + 8u) / 65536.0f;
    }
    p->vmax = devauto_vmax; p->curve_k = devauto_curve_k; p->steer_k = devauto_steer_k;
}

static int nearest(DevPilot *p, float x, float z)
{
    float best = 1e30f;
    int bi = p->near;
    for (int d = -6; d < 40; d++) {
        int k = ((p->near + d) % DEVAUTO_NGUIDE + DEVAUTO_NGUIDE) % DEVAUTO_NGUIDE;
        float dd = (p->x[k] - x) * (p->x[k] - x) + (p->z[k] - z) * (p->z[k] - z);
        if (dd < best) { best = dd; bi = k; }
    }
    p->near = bi;
    return bi;
}

static void ahead(const DevPilot *p, int k, float dist, float *ox, float *oz)
{
    float acc = 0;
    while (acc < dist) {
        int a = k % DEVAUTO_NGUIDE, b = (k + 1) % DEVAUTO_NGUIDE;
        acc += hypotf(p->x[b] - p->x[a], p->z[b] - p->z[a]);
        k++;
    }
    *ox = p->x[k % DEVAUTO_NGUIDE]; *oz = p->z[k % DEVAUTO_NGUIDE];
}

/* pure pursuit on the guide line with a curvature-based speed limit (the oracle's Pilot.next); the car block: x 0x18, z 0x20, yaw 0x10, speed 0x94 (16.16), manual gearbox flag 0xF4, gear 0xB0, rpm 0x8C */
void devauto_pilot(DevPilot *p, const unsigned char *car, DriveIn *in)
{
    in->accel = 0; in->brake = 0; in->axis = 127; in->shift = 0; in->view = 0;
    if (devauto_pilot_mode == 2) return;
    p->vmax = devauto_vmax; p->curve_k = devauto_curve_k; p->steer_k = devauto_steer_k;
    float x = (float)g32(car, 0x18) / 65536.0f, z = (float)g32(car, 0x20) / 65536.0f;
    float heading = -(float)g32(car, 0x10) / 65536.0f, v = (float)g32(car, 0x94) / 65536.0f, ms = v * 30.0f;
    int k = nearest(p, x, z);
    float tx, tz, fx, fz;
    ahead(p, k, devauto_look * (6.0f + 0.45f * ms), &tx, &tz);
    ahead(p, k, devauto_look * (25.0f + 1.6f * ms), &fx, &fz);
    float e1 = wrap_pi(atan2f(tx - x, tz - z) - heading), e2 = wrap_pi(atan2f(fx - x, fz - z) - heading);
    float steer = p->steer_k * e1;
    if (steer > 1) steer = 1;
    if (steer < -1) steer = -1;
    float curve = fabsf(wrap_pi(e2 - e1));
    float vlim = curve > 0.05f ? p->vmax / (1.0f + p->curve_k * curve) : p->vmax;
    int brake = v > vlim * 1.12f && fabsf(e1) < 1.0f;
    in->brake = brake; in->accel = v < vlim && !brake;
    in->axis = (int)(127.5f + 128.0f * steer);
    if (in->axis < 0) in->axis = 0;
    if (in->axis > 255) in->axis = 255;
    if (car[0xF4]) {
        int gear = (car[0xB0] << 8) | car[0xB1];
        float rpm = (float)g32(car, 0x8C) / 65536.0f;
        if (p->cool > 0) p->cool--;
        else if (gear < 4 && (gear == 0 || rpm > 7600.0f)) { in->shift = 1; p->cool = 6; }
        else if (gear > 1 && rpm < 3000.0f) { in->shift = -1; p->cool = 6; }
    }
}

/* ---- screenshots: PPSSPP keeps the frame buffer on the GPU (the CPU reads old memory), so the picture is taken from outside: the request goes to <base>/shot.req (the name) and the game stands still
 * until the test script (the PowerShell runner) has captured the window and deleted the file ---- */
void devauto_shot(const char *base, const char *name)
{
    static int n;
    char path[256];
    snprintf(path, sizeof path, "%s/shot.req", base);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%04d_%s\n", n++, name);
    fclose(f);
    for (int i = 0; i < 3000; i++) {                    /* at most 30 s (the runner needs real time: PPSSPP may be fast-forwarding) */
        sceKernelDelayThread(10000);
        f = fopen(path, "r");
        if (!f) return;
        fclose(f);
    }
    sceIoRemove(path);
}
