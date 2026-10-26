/* Sega Rally PSP - M5 frame viewer: draws the captured reference frames (data/frames/NAME.txt: course, the game's own camera matrix, the game's part list)
 * with the course renderer (render.c) so they can be compared with the reference pictures made from the original code (tools/export_frames.py).
 *
 *   L / R      previous / next frame      SELECT   picture placement: 1:1 top-left (for comparisons) / 4:3 fit / stretched
 *   START      text overlay (placed below the 352x224 picture)       HOME exits
 * Data: ms0:/PSP/GAME/SegaRallyPSP/data/course/<name>.rcp, data/frames/<name>.txt + index.txt, optional data/cam.txt = name of the first frame (test harness).
 */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspiofilemgr.h>
#include <psppower.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"
#include "car.h"
#include "event.h"
#include "hud.h"
#include "play.h"
#include "menu_host.h"

PSP_MODULE_INFO("SegaRallyPSP", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(-1024);

#include "gamedir.h"
#define BASE SEGA_BASE                                  /* each build variant reads its own data folder (gamedir.h) */
#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define FB_BYTES (BUF_W * SCR_H * 4)

static unsigned int __attribute__((aligned(16))) list[262144];

static int exit_cb(int a1, int a2, void *c) { (void)a1; (void)a2; (void)c; sceKernelExitGame(); return 0; }
static int cb_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    sceKernelRegisterExitCallback(sceKernelCreateCallback("exit", exit_cb, NULL));
    sceKernelSleepThreadCB();
    return 0;
}

static const char *course_names[4] = { "desert", "forest", "mount", "lake" };      /* game order: DESERT FOREST MOUNT LAKE */

#define MAX_INST 256

typedef struct {
    char name[40];
    int course;
    float view[12];
    unsigned char parts[32];
    int nparts;
    int has_sky;
    unsigned int sky_yaw;
    int sky_roll;
    int has_car, car_mode;                                        /* "car" line: the player's car structure (src/car.c builds the model draws) */
    CarState car;
    int has_ev;
    int ev[12];                                                   /* "ev" line: game type, random seed, car fields and globals the object code reads (src/event.c re-runs the dispatcher) */
    int has_nt;                                                   /* "nt2" / "nt3" lines: the HUD name tables (src/hud.c) */
    unsigned short nt[3][HUD_CELLS];                              /* NBG2, NBG3 planes A / B, NBG3 planes C / D */
    int scr[4];                                                   /* "scr" line: scroll x2 y2 x3 y3 */
    int has_tach, tach;                                           /* "tach" line: the engine word the tachometer needle is drawn from */
    int cd_idx, cd_scale;                                         /* "cdown" line: count-down numeral (0 GO .. 3) and size; cd_idx -1 = none */
    int has_heli;                                                 /* "heli" line: the helicopter object and its dust list as the dispatcher finds them (desert) */
    unsigned char heli[512], dust[512];
    int ninst;
    struct { int part, clip, kind; float m[12]; } inst[MAX_INST]; /* recorded model draws, in the order the game made them (kind 1 = spray, always drawn) */
} Frame;

static char *read_file(const char *path, int *len)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return NULL;
    int size = (int)sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    char *b = (char *)malloc((size_t)size + 1);
    if (!b) { sceIoClose(fd); return NULL; }
    int got = 0;
    while (got < size) {
        int n = sceIoRead(fd, b + got, (unsigned)(size - got));
        if (n <= 0) break;
        got += n;
    }
    sceIoClose(fd);
    b[got] = 0;
    if (len) *len = got;
    return b;
}

static int load_frame(Frame *f, const char *name)
{
    char path[160];
    snprintf(path, sizeof path, BASE "/frames/%s.txt", name);
    char *t = read_file(path, NULL);
    if (!t) return -1;
    memset(f, 0, sizeof *f);
    snprintf(f->name, sizeof f->name, "%s", name);
    char *p = t;
    int n;
    if (sscanf(p, "course %d%n", &f->course, &n) != 1) { free(t); return -2; }
    p += n;
    if (sscanf(p, " view%n", &n) != 0 && n == 0) { free(t); return -2; }
    p = strstr(p, "view") + 4;
    for (int i = 0; i < 12; i++) {
        float v;
        if (sscanf(p, " %f%n", &v, &n) != 1) { free(t); return -2; }
        f->view[i] = v; p += n;
    }
    p = strstr(p, "parts") + 5;
    int np;
    if (sscanf(p, " %d%n", &np, &n) != 1) { free(t); return -2; }
    p += n;
    for (int i = 0; i < np && i < 32; i++) {
        int v;
        if (sscanf(p, " %d%n", &v, &n) != 1) break;
        f->parts[f->nparts++] = (unsigned char)v; p += n;
    }
    {
        char *q = strstr(p, "sky");
        int yaw, roll;
        if (q && sscanf(q + 3, " %d %d", &yaw, &roll) == 2) {
            f->has_sky = 1; f->sky_yaw = (unsigned)yaw & 0xFFFF; f->sky_roll = (short)roll;
        }
    }
    {
        char *q = strstr(p, "\ncar ");
        if (q) {
            float v[16];
            int mode, type, brake, dmg, k;
            q += 5;
            if (sscanf(q, " %d %d %d %d%n", &mode, &type, &brake, &dmg, &n) == 4) {
                q += n;
                for (k = 0; k < 16; k++) {
                    if (sscanf(q, " %f%n", &v[k], &n) != 1) break;
                    q += n;
                }
                if (k == 16) {
                    f->has_car = 1; f->car_mode = mode;
                    f->car.type = type; f->car.brake = brake; f->car.damage = dmg;
                    for (k = 0; k < 3; k++) { f->car.ang[k] = v[k]; f->car.pos[k] = v[3 + k]; }
                    f->car.steer = v[6]; f->car.spin = v[7];
                    for (k = 0; k < 4; k++) { f->car.susp[k] = v[8 + k]; f->car.ground[k] = v[12 + k]; }
                }
            }
        }
    }
    {
        char *q = strstr(p, "\nev ");
        if (q) {
            int k;
            q += 4;
            for (k = 0; k < 12; k++) {
                if (sscanf(q, " %d%n", &f->ev[k], &n) != 1) break;
                q += n;
            }
            f->has_ev = (k == 12);
        }
    }
    {
        char *q = strstr(p, "\nheli ");
        if (q) {
            unsigned char *dst[2] = { f->heli, f->dust };
            int ok = 1;
            q += 6;
            for (int k = 0; k < 2 && ok; k++) {
                for (int i = 0; i < 512; i++) {
                    unsigned int v;
                    char h[3] = { q[0], q[1], 0 };
                    if (!q[0] || !q[1] || sscanf(h, "%2x", &v) != 1) { ok = 0; break; }
                    dst[k][i] = (unsigned char)v;
                    q += 2;
                }
                if (ok && *q == ' ') q++;
            }
            f->has_heli = ok;
        }
    }
    f->cd_idx = -1;
    {
        char *q = strstr(p, "\ntach ");
        unsigned int v;
        if (q && sscanf(q + 6, "%u", &v) == 1) { f->has_tach = 1; f->tach = (int)v; }
        q = strstr(p, "\ncdown ");
        if (q) sscanf(q + 7, "%d %d", &f->cd_idx, &f->cd_scale);
    }
    for (int layer = 0; layer < 3; layer++) {
        char key[8];
        snprintf(key, sizeof key, "\nnt%d ", layer + 2);
        char *q = strstr(p, key);
        int cnt;
        if (!q) continue;
        q += 5;
        if (sscanf(q, "%d%n", &cnt, &n) != 1) continue;
        q += n;
        for (int i = 0; i < cnt; i++) {
            int idx, val;
            if (sscanf(q, " %d:%x%n", &idx, &val, &n) != 2) break;
            q += n;
            if ((unsigned)idx < HUD_CELLS) f->nt[layer][idx] = (unsigned short)val;
        }
        f->has_nt = 1;
    }
    {
        char *q = strstr(p, "\nscr ");
        if (q) sscanf(q + 5, "%d %d %d %d", &f->scr[0], &f->scr[1], &f->scr[2], &f->scr[3]);
    }
    for (char *q = strstr(p, "inst "); q && f->ninst < MAX_INST; q = strstr(q, "inst ")) {
        int part, clip, kind;
        q += 5;
        if (sscanf(q, " %d %d %d%n", &part, &clip, &kind, &n) != 3) break;
        q += n;
        int k;
        for (k = 0; k < 12; k++) {
            if (sscanf(q, " %f%n", &f->inst[f->ninst].m[k], &n) != 1) break;
            q += n;
        }
        if (k < 12) break;
        f->inst[f->ninst].part = part;
        f->inst[f->ninst].clip = clip;
        f->inst[f->ninst++].kind = kind;
    }
    free(t);
    return 0;
}

#define MAX_SCENE 512
static SceneItem scene[MAX_SCENE];
static int nscene;
static Events events;
static Coli coli;

/* everything one frame draws, in the order the game enters it: the car (src/car.c from the car structure), the event objects (src/event.c, one dispatch from the recorded inputs),
 * the spray (recorded), then the course parts.  Frames without car / ev lines (and the object gallery) use the recorded draws instead; data/dbg.txt "recorded" forces that. */
static int build_scene(const Frame *f, const Course *course, const Course *objects, const CarData *cars, int carerr, int everr, int recorded, unsigned mask, int nocourse)
{
    int n = 0;
    int ported = !recorded && (f->has_car || f->has_ev) && carerr == 0 && everr == 0 && objects->file;
    if (ported) {
        if (f->has_car) n = car_display(cars, &f->car, f->car_mode, f->view, objects, scene, 32);
        if (f->has_ev) {
            EvWorld w;
            memcpy(w.view, f->view, sizeof w.view);
            w.game_type = f->ev[0]; w.car_byte_c5 = f->ev[2]; w.car_seg = f->ev[3]; w.car_c8 = f->ev[4]; w.car_ca = f->ev[5];
            w.car_x = f->ev[6]; w.car_y = f->ev[7]; w.car_z = f->ev[8]; w.car_yaw = f->ev[9]; w.nseg_word = f->ev[10];
            ev_start(&events, f->course, w.game_type);
            events.rng = (unsigned)f->ev[1];
            if (f->has_heli) ev_set_heli(&events, f->heli, f->dust);
            n += ev_frame(&events, &w, f->parts, f->nparts, scene + n, MAX_SCENE - 16 - n);
        }
        n += ev_hud(&events, f->has_tach ? f->tach : -1, f->cd_idx, f->cd_scale, scene + n, 4);
    }
    if (objects->file)
        for (int i = 0; i < f->ninst && n < MAX_SCENE - 16; i++) {
            if (ported && f->inst[i].kind != 1) continue;
            if ((unsigned)f->inst[i].part >= objects->nparts || (i < 32 && !((mask >> i) & 1))) continue;
            scene[n].c = objects; scene[n].part = f->inst[i].part; scene[n].clip = f->inst[i].clip;
            memcpy(scene[n].m, f->inst[i].m, sizeof scene[n].m);
            n++;
        }
    for (int i = 0; i < f->nparts && i < 16 && !nocourse && course->file; i++) {
        scene[n].c = course; scene[n].part = f->parts[i] & 127; scene[n].clip = 0;
        memcpy(scene[n].m, f->view, sizeof scene[n].m);
        n++;
    }
    return n;
}

int main(void)
{
    int th = sceKernelCreateThread("exit_thread", cb_thread, 0x11, 0xFA0, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    scePowerSetClockFrequency(333, 333, 166);

    void *fbp0 = (void *)0, *fbp1 = (void *)FB_BYTES;
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, fbp0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, fbp1, BUF_W);
    sceGuOffset(2048 - SCR_W / 2, 2048 - SCR_H / 2);
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    pspDebugScreenInit();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    /* frame list */
    static char names[64][40];
    int nnames = 0;
    static const char *const index_files[2] = { BASE "/frames/index.txt", BASE "/frames/index_obj.txt" };      /* course frames, object gallery */
    for (int fi = 0; fi < 2; fi++) {
        char *t = read_file(index_files[fi], NULL);
        if (t) {
            char *s = t;
            while (*s && nnames < 64) {
                char *e = s;
                while (*e && *e != '\n' && *e != '\r') e++;
                int len = (int)(e - s);
                if (len > 0 && len < 39) { memcpy(names[nnames], s, (size_t)len); names[nnames][len] = 0; nnames++; }
                while (*e == '\n' || *e == '\r') e++;
                s = e;
            }
            free(t);
        }
    }
    int cur = 0;
    {
        char *t = read_file(BASE "/cam.txt", NULL);
        if (t) {
            size_t tl = 0;
            while (t[tl] && t[tl] != '\n' && t[tl] != '\r' && t[tl] != ' ') tl++;
            for (int i = 0; i < nnames; i++) if (strlen(names[i]) == tl && strncmp(t, names[i], tl) == 0) cur = i;
            free(t);
        }
    }

    Course course;
    Sky sky;
    Course objects;
    memset(&objects, 0, sizeof objects);
    (void)rcp_load(&objects, BASE "/course/object.rcp");       /* objects->file stays NULL when the pack is missing: build_scene then draws nothing of it */
    memset(&course, 0, sizeof course);
    memset(&sky, 0, sizeof sky);
    int skyerr = -1;
    int course_idx = -1, cerr = 0, ferr = 0;
    static Frame frame;
    memset(&frame, 0, sizeof frame);
    int loaded = -1;
    int show = 1, mode = 0;
    unsigned int prev = 0, frames = 0;
    int draw_off = 0;
    unsigned int dbg_mask = 0xFFFFFFFFu;              /* data/dbg.txt: "mask <hex>" = which instances to draw, "nocourse" (test harness only) */
    int dbg_nocourse = 0, dbg_recorded = 0, dbg_nohud = 0;           /* "recorded": use the recorded model draws (inst lines) instead of car_display */
    CarData cars;
    Hud hud;
    int huderr = hud_load(&hud, BASE "/course/hud.dat");
    int carerr = cars_load(&cars, BASE "/course/cars.dat");
    int everr = ev_load(&events, BASE "/course/events.dat", BASE "/course/object.names", &objects);
    {
        char *t = read_file(BASE "/dbg.txt", NULL);
        if (t) {
            char *q = strstr(t, "mask ");
            if (q) dbg_mask = (unsigned)strtoul(q + 5, NULL, 16);
            dbg_nocourse = strstr(t, "nocourse") != NULL;
            dbg_recorded = strstr(t, "recorded") != NULL;
            dbg_nohud = strstr(t, "nohud") != NULL;
            free(t);
        }
    }
    static PlayAssets pas;
    pas.objects = &objects; pas.cars = &cars; pas.events = &events; pas.hud = &hud; pas.cars_ok = carerr == 0; pas.events_ok = everr == 0; pas.hud_ok = huderr == 0; pas.gu_list = list;
    {
        PlayCfg pcfg;                                   /* data/play.txt: play mode (M6) instead of the frame viewer */
        if (play_cfg_load(&pcfg, BASE "/play.txt")) play_run(&pcfg, &pas, BASE, NULL, NULL);
    }
    {
        char *t = read_file(BASE "/menu.txt", NULL);        /* data/menu.txt: the ported menus (M8); the first word = the start screen handler in hex (empty = the title) */
        if (t) {
            u32 vec = (u32)strtoul(t, NULL, 16);
            free(t);
            menu_host_assets(&pas);
            menu_host_run(list, BASE, vec);
        }
    }
    RenderStats st = { 0, 0, 0, 0 };
    unsigned long long t_prep = 0, t_ge = 0;
    int t_n = 0;

    for (;;) {
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~prev;
        prev = pad.Buttons;
        if (nnames > 0) {
            if (pressed & PSP_CTRL_RTRIGGER) cur = (cur + 1) % nnames;
            if (pressed & PSP_CTRL_LTRIGGER) cur = (cur + nnames - 1) % nnames;
        }
        if (pressed & PSP_CTRL_START) show ^= 1;
        if (pressed & PSP_CTRL_SELECT) mode = (mode + 1) % 3;

        if (nnames > 0 && loaded != cur) {
            ferr = load_frame(&frame, names[cur]);
            loaded = cur;
            if (ferr == 0 && frame.course < 0 && course_idx != -2) {        /* object gallery frames: no course, no sky */
                rcp_free(&course);
                sky_free(&sky);
                cerr = 0; skyerr = -1;
                course_idx = -2;
            } else if (ferr == 0 && frame.course >= 0 && frame.course != course_idx) {
                char path[160];
                rcp_free(&course);
                sky_free(&sky);
                snprintf(path, sizeof path, BASE "/course/%s.rcp", course_names[frame.course & 3]);
                cerr = rcp_load(&course, path);
                snprintf(path, sizeof path, BASE "/course/%s.rbg", course_names[frame.course & 3]);
                skyerr = sky_load(&sky, path);
                snprintf(path, sizeof path, BASE "/course/%s.rct", course_names[frame.course & 3]);
                coli_free(&coli);
                if (coli_load(&coli, path) == 0) ev_set_collision(&events, &coli);
                else ev_set_collision(&events, NULL);
                course_idx = frame.course;
            }
            nscene = ferr == 0 ? build_scene(&frame, &course, &objects, &cars, carerr, everr, dbg_recorded, dbg_mask, dbg_nocourse) : 0;
        }

        const RenderRect rect = VIEW_RECT;                                              /* drawn at 352 x 224 off screen, copied by view_end (mode 0: 1:1, 1: screen height, 2: smoothed) */

        unsigned long long t0 = sceKernelGetSystemTimeWide();
        sceGuStart(GU_DIRECT, list);
        view_begin();
        if (nnames > 0 && ferr == 0 && skyerr == 0 && frame.has_sky) sky_draw(&sky, frame.sky_yaw, frame.sky_roll, &rect);
        if (nnames > 0 && ferr == 0 && cerr == 0) {
            scene_draw(scene, nscene, &rect, &st);
        }
        if (nnames > 0 && ferr == 0 && cerr == 0 && huderr == 0 && frame.has_nt && !dbg_nohud) hud_draw(&hud, frame.nt[0], frame.nt[1], frame.nt[2], frame.scr, &rect);
        view_end(draw_off, mode);
        sceGuFinish();
        unsigned long long t1 = sceKernelGetSystemTimeWide();
        sceGuSync(0, 0);
        unsigned long long t2 = sceKernelGetSystemTimeWide();
        if (frames >= 30 && t_n < 120) { t_prep += t1 - t0; t_ge += t2 - t1; t_n++; }
        if (t_n == 120) {                              /* average of frames 30..149 -> data/timing.txt (read it from the memory stick) */
            char tb[160];
            int len = snprintf(tb, sizeof tb, "%s items %d polys %d tris %d draws %d  cpu %.2f ms  ge %.2f ms\n", frame.name, nscene, st.polys_in, st.tris, st.draws,
                               (double)t_prep / t_n / 1000.0, (double)t_ge / t_n / 1000.0);
            SceUID fd = sceIoOpen(BASE "/timing.txt", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
            if (fd >= 0) { sceIoWrite(fd, tb, (unsigned)len); sceIoClose(fd); }
            t_n++;
        }

        if (show) {
            pspDebugScreenSetBase((u32 *)(0x44000000u + (unsigned)draw_off));
            pspDebugScreenSetXY(0, 28);
            if (nnames == 0) pspDebugScreenPrintf("no data/frames/index.txt\n");
            else if (ferr || cerr) pspDebugScreenPrintf("%s: frame error %d course error %d\n", names[cur], ferr, cerr);
            else pspDebugScreenPrintf("%s  parts %d inst %d  polys %d  tris %d  draws %d  mode %d  frame %u\n", frame.name, frame.nparts, frame.ninst, st.polys_in, st.tris, st.draws, mode, frames);
        }
        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
        draw_off ^= FB_BYTES;
        frames++;
    }
    return 0;
}
