/* M8: the PSP side of the menu code: runs the ported screens (src/menu_*.c on the Saturn memory model, src/sat*.c) and shows what they put into VDP1 / VDP2 with the software
 * renderer (src/vdpr.c).  Called from main() when data/menu.txt exists.
 *
 * The ported code is blocking, like the original: every sat_tick() (one V-blank pair of the original) ends in menu_tick() below, which reads the pad, draws the picture every
 * second tick and waits for the next vertical blank. */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspdebug.h>
#include <psprtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sat.h"
#include "satlib.h"
#include "satmenu.h"
#include "mat.h"
#include "vdpr.h"
#include "menu_pspfix.h"
#include "menu_ending.h"
#include "ending_engine.h"
#include "menu_post.h"
#include "menu_padcfg.h"
#include "devauto_menu.h"
#include "snd.h"
#include "snd_lib.h"
extern const char *snd_debug_dir;
#include "sat_save.h"
#include <setjmp.h>
extern int sat_v2_touched;
#include "menu_host.h"

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define FB_BYTES (BUF_W * SCR_H * 4)

typedef struct { float u, v; u32 col; float x, y, z; } MVtx;

static VdpFrame frame;
/* The pictures' textures live in VRAM (copied there by the GE): the GE reads a RAM texture slowly, and a real PSP needed 71 ms per picture for the sprite that shows it (data/menu_perf.txt of the first real-PSP run).
 * The race's off-screen view (render.c VIEW_OFF) uses the same VRAM after a race: the next picture is copied again (tex_valid / tex_hi_valid). */
#define TEX_VRAM    0x110000u
#define TEXHI_VRAM  0x180000u
static int tex_valid;

static void tex_upload(const u32 *pix, int w, int h, unsigned vram)
{
    sceKernelDcacheWritebackRange(pix, (unsigned)h * VDPR_MAXW * 4u);
    sceGuCopyImage(GU_PSM_8888, 0, 0, w, h, VDPR_MAXW, (void *)pix, 0, 0, 512, (void *)(0x04000000u + vram));
    sceGuTexSync();
    sceGuTexFlush();
}
static unsigned int *glist;
static int draw_off;
static unsigned tick_n;
static int da_on;                                       /* development: data/autoplay.txt (devauto_menu.c) drives the pad */
static u32 shown;                                       /* the number of frames shown */
static const char *perf_base;
static unsigned long long t_render, t_draw, t_max, t_first, t_3d, t_logic, t_prev_end;      /* t_3d: the 3D models of the screen (part of t_draw), t_logic: the ported screen code between two ticks */
static unsigned perf_n, n_same;
static unsigned long long t_fix;                        /* the PSP's button names (pspfix_apply) */
static unsigned long long clock_us(void) { return sceKernelGetSystemTimeWide(); }
extern unsigned long long rs_t_xform, rs_t_sort, rs_t_emit, rs_t_ge;
extern unsigned rs_calls, rs_verts, rs_polys, rs_tris;
static unsigned long long t_m3ops, t_caritems, t_m3ptr, t_m3draw;      /* development: the name entry's matrix ops replay and the car's parts (draw_menu3d) */
static int fix_on = 1;
static FILE *dbglog;
static unsigned nscript, script_tick[256], script_mask[256];

/* 3d_clr_zsortcnt (0x0601B220) is what game_zsort runs every pass: without it the depth buckets of the sprites / cars fill up and every picture draws all the old ones too.  A pass is
 * two ticks (the screens wait two V-blanks), and the VDP1 draws its command list at every V-blank: the buckets are cleared where a pass begins (mkpoly_frame_init), not at every tick,
 * or the sprites would be missing from every second picture (which one depends on the tick parity: PPSSPP and a real PSP differ). */
static unsigned zsort_clear_tick;
static void zsort_clear(void)
{
    memset(sat->hram + 0xE0000, 0, 0x4000);
    zsort_clear_tick = tick_n;
}

static void perf_flush(void)                           /* data/menu_perf.txt: average / worst time of the software renderer and of the GE upload, ticks per second */
{
    char path[256];
    FILE *f;
    unsigned long long now = sceKernelGetSystemTimeWide();
    snprintf(path, sizeof(path), "%s/menu_perf.txt", perf_base);
    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "frames %u  ticks %u  seconds %.1f\n", perf_n, tick_n, (double)(now - t_first) / 1e6);
    fprintf(f, "renderer avg %.2f ms  max %.2f ms   GE upload avg %.2f ms   ticks per second %.1f\n", (double)t_render / perf_n / 1000.0, (double)t_max / 1000.0,
            (double)t_draw / perf_n / 1000.0, (double)tick_n * 1e6 / (double)(now - t_first));
    fprintf(f, "of the GE upload: 3D models avg %.2f ms;  screen code per tick avg %.2f ms\n", (double)t_3d / perf_n / 1000.0, tick_n ? (double)t_logic / tick_n / 1000.0 : 0.0);
    fprintf(f, "m3: pointer table %.2f ms, the draw ops %.2f ms (per picture);  ops replay avg %.2f ms, car parts avg %.2f ms (per picture)\n", (double)t_m3ptr / perf_n / 1000.0, (double)t_m3draw / perf_n / 1000.0, (double)t_m3ops / perf_n / 1000.0, (double)t_caritems / perf_n / 1000.0);
    fprintf(f, "renderer parts per picture (ms): pspfix %.2f  VRAM scan %.2f  colour RAM %.2f  layers %.2f  composite %.2f  dots %.2f  VDP1 draw %.2f  VDP1 list %.2f;  %u of %u pictures unchanged\n", (double)t_fix / perf_n / 1000.0, (double)vdpr_t[0] / perf_n / 1000.0,
            (double)vdpr_t[1] / perf_n / 1000.0, (double)vdpr_t[2] / perf_n / 1000.0, (double)vdpr_t[3] / perf_n / 1000.0, (double)vdpr_t[4] / perf_n / 1000.0, (double)vdpr_t[5] / perf_n / 1000.0, (double)vdpr_t[6] / perf_n / 1000.0, n_same, perf_n);
    if (rs_calls) fprintf(f, "scene_draw per call (%u calls): transform %.2f ms, sort %.2f ms, emit %.2f ms, GE state %.2f ms;  %u vertices, %u polygons, %u triangles\n", rs_calls, (double)rs_t_xform / rs_calls / 1000.0, (double)rs_t_sort / rs_calls / 1000.0, (double)rs_t_emit / rs_calls / 1000.0, (double)rs_t_ge / rs_calls / 1000.0, rs_verts / rs_calls, rs_polys / rs_calls, rs_tris / rs_calls);
    fprintf(f, "main vector %08x  screen vector %08x  pad %04x\n", (unsigned)R32(0x060A001C), (unsigned)R32(0x060AA178), sat_pad);
    fclose(f);
}

/* PSP buttons -> the Saturn pad word the original reads (Up 1000, Down 2000, Left 4000, Right 8000, Start 0800, A 0400, C 0200, B 0100, X 0040, Y 0020, Z 0010, L 0008, R 0080) */
static u32 map_pad(const SceCtrlData *p)
{
    u32 b = p->Buttons, s = 0;
    if (b & PSP_CTRL_UP) s |= 0x1000;
    if (b & PSP_CTRL_DOWN) s |= 0x2000;
    if (b & PSP_CTRL_LEFT) s |= 0x4000;
    if (b & PSP_CTRL_RIGHT) s |= 0x8000;
    if (b & PSP_CTRL_START) s |= 0x0800;
    if (b & PSP_CTRL_CROSS) s |= 0x0200;                /* C: confirm */
    if (b & PSP_CTRL_CIRCLE) s |= 0x0100;               /* B: back */
    if (b & PSP_CTRL_SQUARE) s |= 0x0400;               /* A */
    if (b & PSP_CTRL_TRIANGLE) s |= 0x0040;             /* X */
    if (b & PSP_CTRL_LTRIGGER) s |= 0x0008;
    if (b & PSP_CTRL_RTRIGGER) s |= 0x0080;
    if (b & PSP_CTRL_SELECT) s |= 0x0020;               /* Y (cheat codes) */
    if (p->Lx < 64) s |= 0x4000;                        /* the analog nub as a direction pad */
    if (p->Lx > 192) s |= 0x8000;
    if (p->Ly < 64) s |= 0x1000;
    if (p->Ly > 192) s |= 0x2000;
    return s;
}

/* The pad of the next tick: every sample the controller took since the last tick, ORed.  A tick can take longer than a button press (a screen's set-up reads files, a picture takes more than one V-blank);
 * the last sample alone would miss a tap that began and ended inside it, and the original's presses are edges between two ticks. */
static int pad_flush;                                   /* a race just ended: the next read starts from the controller's present state */

static u32 read_pad(void)
{
    static unsigned last_ts;
    static u32 held;
    SceCtrlData buf[64];                                /* the controller keeps the last 64 samples (about a second) */
    int n = sceCtrlPeekBufferPositive(buf, 64), i, any = 0;
    unsigned newest = last_ts;
    u32 s = 0;
    if (pad_flush && n > 0) {                           /* what the race's samples hold is not for the screens that follow */
        unsigned top = buf[0].TimeStamp;
        u32 cur = 0;
        for (i = 0; i < n; i++) if ((int)(buf[i].TimeStamp - top) >= 0) { top = buf[i].TimeStamp; cur = map_pad(&buf[i]); }
        last_ts = top; held = cur; pad_flush = 0;
        sat_pad_sync = 1;                               /* the buttons that are still held from the race are no new presses (the original's pad routine ran every V-blank of the race, too) */
        return held;
    }
    for (i = 0; i < n; i++) {
        if ((int)(buf[i].TimeStamp - last_ts) <= 0) continue;
        s |= map_pad(&buf[i]);
        if ((int)(buf[i].TimeStamp - newest) > 0) newest = buf[i].TimeStamp;
        any = 1;
    }
    if (any) { held = s; last_ts = newest; }
    return held;
}

/* ---- the DIY type of KEY CONFIG: a text screen where every function gets its buttons (menu_padcfg.c keeps the table, data/keycfg.txt the file).  It is drawn into the same 352 x 224 picture the
 * menus use (the 8 x 8 font of libpspdebug), so it shows like every other screen. ---- */
extern unsigned char msx[];                             /* libpspdebug's font: 8 rows per character, the left dot is bit 7 */
static void draw_frame(void);
static const struct { int act; const char *name; } diy_rows[5] = { { PC_ACCEL, "ACCEL" }, { PC_BRAKE, "BRAKE" }, { PC_SHIFT_UP, "SHIFT UP" }, { PC_SHIFT_DOWN, "SHIFT DOWN" }, { PC_VIEW, "CHANGE VIEW" } };

static unsigned diy_button_bit(int b)                   /* the PSP button of a label slot */
{
    static const unsigned bits[PC_NBTN] = { PSP_CTRL_LTRIGGER, 0, 0, PSP_CTRL_TRIANGLE, PSP_CTRL_RTRIGGER, PSP_CTRL_CIRCLE, PSP_CTRL_CROSS, PSP_CTRL_SQUARE };       /* the D-pad up / down have no function */
    return bits[b];
}

static void diy_print(int col, int row, const char *s, u32 rgb)
{
    for (; *s; s++, col++) {
        const unsigned char *g = msx + (unsigned)(unsigned char)*s * 8u;
        int y, x;
        for (y = 0; y < 8; y++) for (x = 0; x < 8; x++)
            if ((g[y] & (128 >> x)) && col * 8 + x < 352 && row * 8 + y < 224) frame.pix[(row * 8 + y) * VDPR_MAXW + col * 8 + x] = rgb;
    }
}

static unsigned diy_pad(int *frames)                    /* the PSP buttons held now (development: the pad script of data/menu_pad.txt, Saturn bits, from the tick the editor opened, one line per V-blank) */
{
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(&pad, 1);
    if (nscript) {
        unsigned m = 0, i, vt = tick_n + (unsigned)(*frames)++, now = 0;
        for (i = 0; i < nscript; i++) if (script_tick[i] <= vt) m = script_mask[i];
        if (m & 0x1000) now |= PSP_CTRL_UP;
        if (m & 0x2000) now |= PSP_CTRL_DOWN;
        if (m & 0x0200) now |= PSP_CTRL_CROSS;
        if (m & 0x0100) now |= PSP_CTRL_CIRCLE;
        if (m & 0x0400) now |= PSP_CTRL_SQUARE;
        if (m & 0x0040) now |= PSP_CTRL_TRIANGLE;
        if (m & 0x0008) now |= PSP_CTRL_LTRIGGER;
        if (m & 0x0080) now |= PSP_CTRL_RTRIGGER;
        if (m & 0x0800) now |= PSP_CTRL_START;
        if (m & 0x0020) now |= PSP_CTRL_SELECT;
        return now;
    }
    return pad.Buttons;
}

static void diy_present(void)
{
    frame.w = 352; frame.h = 224;
    frame.same = 0;                                     /* the editor drew into the picture itself */
    draw_frame();
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
    draw_off ^= FB_BYTES;
}

static int host_diy(void)                               /* menu_option_diy_hook: the editor; 1 = it ran (the page stays) */
{
    const unsigned any = PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_UP | PSP_CTRL_DOWN | PSP_CTRL_TRIANGLE | PSP_CTRL_CIRCLE | PSP_CTRL_CROSS | PSP_CTRL_SQUARE | PSP_CTRL_START | PSP_CTRL_SELECT;
    unsigned prev = 0xFFFFFFFFu;
    int cur = 0, listening = 0, k, warn = 0, frames = 0;
    unsigned char *act = padcfg_act[PC_TYPES - 1];
    if (dbglog) { fprintf(dbglog, "DIY open tick %u\n", tick_n); fflush(dbglog); }
    for (;;) {
        unsigned now = diy_pad(&frames), down = now & ~prev;
        char line[80];
        int i;
        prev = now;
        if (!listening) {
            if (down & PSP_CTRL_UP) cur = (cur + 4) % 5;
            if (down & PSP_CTRL_DOWN) cur = (cur + 1) % 5;
            if ((down & PSP_CTRL_CROSS) && !(now & ~PSP_CTRL_CROSS & any)) {         /* the function's buttons are cleared; the next presses are its new ones */
                for (k = 0; k < PC_NBTN; k++) if (act[k] == diy_rows[cur].act) act[k] = PC_NONE;
                listening = 1; warn = 0;
            }
            if (down & PSP_CTRL_SELECT) { memcpy(act, padcfg_act[0], PC_NBTN); warn = 0; }
            if (down & PSP_CTRL_START) {
                int has = 0;
                for (k = 0; k < PC_NBTN; k++) if (act[k] == PC_ACCEL) has = 1;
                if (has) break;
                warn = 1;
            }
        } else if (down & PSP_CTRL_START) {
            listening = 0;
        } else {
            for (k = 0; k < PC_NBTN; k++)
                if (down & diy_button_bit(k)) act[k] = (unsigned char)diy_rows[cur].act;        /* the button leaves its old function */
        }
        for (i = 0; i < 224; i++) { int x; for (x = 0; x < 352; x++) frame.pix[i * VDPR_MAXW + x] = 0xFF401810u; }
        diy_print(2, 2, "KEY CONFIG - DIY", 0xFF40FFFFu);
        diy_print(2, 4, "The D-pad left / right and the analog", 0xFFC0C0C0u);
        diy_print(2, 6, "nub always steer the car.", 0xFFC0C0C0u);
        for (k = 0; k < 5; k++) {
            int b, any_b = 0;
            snprintf(line, sizeof line, "%c %-12s", k == cur ? '>' : ' ', diy_rows[k].name);
            for (b = 0; b < PC_NBTN; b++) if (b != PCB_UP && b != PCB_DOWN && act[b] == diy_rows[k].act) {
                size_t n = strlen(line);
                snprintf(line + n, sizeof line - n, "%s%s", any_b ? " + " : "", padcfg_btn_name[b]);
                any_b = 1;
            }
            if (!any_b) strncat(line, "-", sizeof line - strlen(line) - 1);
            diy_print(2, 9 + k * 2, line, k == cur ? 0xFF40FFFFu : 0xFFFFFFFFu);
        }
        if (listening) {
            snprintf(line, sizeof line, "Press the buttons for %s,", diy_rows[cur].name);
            diy_print(2, 21, line, 0xFF80FF80u);
            diy_print(2, 23, "then START.", 0xFF80FF80u);
        } else {
            diy_print(2, 19, "UP / DOWN: choose a function", 0xFFC0C0C0u);
            diy_print(2, 21, "CROSS: give it new buttons", 0xFFC0C0C0u);
            diy_print(2, 23, "SELECT: start again from type A", 0xFFC0C0C0u);
            diy_print(2, 25, "START: done", 0xFFC0C0C0u);
        }
        if (warn) diy_print(2, 27, "ACCEL needs a button.", 0xFF4040FFu);
        diy_present();
    }
    padcfg_save(perf_base);
    vdpr_forget();
    sat_v2_touched = 1;                                 /* the buttons changed: the KEY CONFIG picture is made again */
    for (;;) {                                          /* all buttons up again before the menus go on */
        if (!(diy_pad(&frames) & any)) break;
        diy_present();
    }
    return 1;
}

/* ---- the 3D cars of the screens (the requests of satmenu.c: menu3d_car) ---- */
static const CarData *m_cars;
static const Course *m_objects;

static const PlayAssets *m_pas;

void menu_host_assets(const PlayAssets *pas)
{
    m_pas = pas;
    m_cars = pas->cars_ok ? pas->cars : NULL;
    m_objects = pas->objects->file ? pas->objects : NULL;
}

static const Mat *car_base_mat;                         /* the ceremony / ending put the car into the world: this view matrix replaces the unit matrix the menu cars start from */

static int car_items(const Menu3D *m, SceneItem *out, int max)
{
    const CarDesc *ds;
    Mat car;
    /* dispSelectCar reads the table at 0x06010874 (14 types x {near, near damaged, far, far damaged}) at id * 16, + 4 when the block byte +94 is set (the turntable cars that are not
     * highlighted); dispSelectCar2 (time attack, set-up: ids 0 Celica, 1 Delta, 2 Stratos of SELECT.MDL's own models) has no second variant and no brake light part; the PSP draws the race model there (see EMIT(ds->br_off)) */
    int id = m->model, type = (m->car2 && id == 2) ? 10 : id, n = 0, i;
    float tyre_dy = id == 10 ? 0.109375f : id == 1 ? 0.125f : id == 2 ? 0.109375f : 0.15625f;     /* r11 of dispSelectCar: 0x2800 / 0x2000 / 0x1c00 (16.16) added to the wheel height (id 10 = 2 = the Stratos) */
    float shadow_dy = id == 0 ? -0.28125f : -0.25f;     /* the translate of the shadow part: 0xFFFFB800 / 0xFFFFC000 (16.16) */
    if (!m_cars || !m_objects || type < 0 || type >= m_cars->ntypes) return 0;
    ds = &m_cars->desc[type * 4 + ((m->car2 || m->still) ? 0 : 1)];
    if (car_base_mat) car = *car_base_mat;
    else {
        memset(&car, 0, sizeof car);
        car.m[0] = car.m[5] = car.m[10] = 1.0f;         /* initial_matrix: the unit matrix; then translate, rotate x / y / z of dispSelectCar */
    }
    m_translate(&car, (float)m->pos[0] / 65536.0f, (float)m->pos[1] / 65536.0f, (float)m->pos[2] / 65536.0f);
    m_rot_turns(&car, 0, m->ang[0]);
    m_rot_turns(&car, 1, m->ang[1]);
    m_rot_turns(&car, 2, m->ang[2]);
#define EMIT(P, MAT) do { if ((P) >= 0 && n < max) { out[n].c = m_objects; out[n].clip = 0; out[n].part = (P); memcpy(out[n].m, (MAT).m, sizeof out[n].m); n++; } } while (0)
    EMIT(ds->body, car);
    EMIT(ds->br_off, car);                              /* the time attack / set-up car (car2) is the race model here, not SELECT.MDL's own low-detail car: without the unlit brake light part (the panel between the tail lights) the back has a hole */
    if (m->car2 || !m->noshadow) {                      /* dispSelectCar's r5 (block +900) != 0: no shadow (the name entry) */
        Mat sh = car;
        m_translate(&sh, 0.0f, shadow_dy, 0.0f);
        EMIT(ds->shadow, sh);
    }
    for (i = 0; i < 4; i++) {
        Mat t = car;
        m_translate(&t, ds->wheel[i][0], ds->wheel[i][1] + tyre_dy, ds->wheel[i][2]);
        EMIT(ds->tyre[i], t);
    }
#undef EMIT
    return n;
}

/* the models of SELECT.MDL (select.rcp, tools/conv_select.py): the parts in the order of select.names */
static Course m_select;
static const u32 select_part[11] = { 0x06066C74u, 0x06069D74u, 0x0606C2F4u, 0x0606F1F8u, 0x060545D0u, 0x0605672Cu, 0x06061264u, 0x0605CF2Cu, 0x06059C18u, 0x0604FEA8u, 0x06064E00u };

static int model_items(const Menu3D *m, SceneItem *out)
{
    Mat mat;
    int i;
    if (!m_select.file) return 0;
    for (i = 0; i < 11 && select_part[i] != m->block; i++) { }
    if (i == 11 || (unsigned)i >= m_select.nparts) return 0;
    memset(&mat, 0, sizeof mat);
    mat.m[0] = mat.m[5] = mat.m[10] = 1.0f;             /* initial_matrix, translate(block + 12), rotate_x(+24), rotate_y(+28) */
    m_translate(&mat, (float)m->pos[0] / 65536.0f, (float)m->pos[1] / 65536.0f, (float)m->pos[2] / 65536.0f);
    m_rot_turns(&mat, 0, m->ang[0]);
    m_rot_turns(&mat, 1, m->ang[1]);
    out[0].c = &m_select; out[0].part = i; out[0].clip = 0;
    memcpy(out[0].m, mat.m, sizeof out[0].m);
    return 1;
}

/* the matrix calls of the name entry's letters (satmenu.h m3): replayed on the host's own matrix, the polygon models of ALPHA.MDL (tools/conv_alpha.py: the parts in the order of the two
 * tables at 0x06048000 / 0x06048074) drawn in the colour the game gives (draw_color_polygon) */
static Course m_alpha;

static void m3_replay(const RenderRect *rect, const SceneItem *extra, int nextra)       /* extra: models of the same screen (the name entry's car) that share the depth sorting with the letters */
{
    SceneItem it[96 + 16];
    short cols[96 + 16];
    Mat mat;
    int i, n = 0;
    if (!m_alpha.file) {
        char p[256];
        FILE *g;
        snprintf(p, sizeof(p), "%s/m3dbg", perf_base);
        if ((g = fopen(p, "r")) != NULL) { fclose(g); snprintf(p, sizeof(p), "%s/m3dbg.txt", perf_base); g = fopen(p, "w"); if (g) { fprintf(g, "alpha.rcp not loaded (ops %d)\n", m3_n); fclose(g); } }
        return;
    }
    unsigned long long tm0 = sceKernelGetSystemTimeWide();
    u32 part_ptr[58];
    for (i = 0; i < 58; i++) part_ptr[i] = R32(0x06048000 + 4 * (u32)i);       /* the pointer table of the ALPHA.MDL parts (read once per picture, not once per letter) */
    t_m3ptr += sceKernelGetSystemTimeWide() - tm0;
    for (i = 0; i < nextra && n < 16; i++, n++) { it[n] = extra[i]; cols[n] = -1; }
    memset(&mat, 0, sizeof mat);
    mat.m[0] = mat.m[5] = mat.m[10] = 1.0f;
    for (i = 0; i < m3_n; i++) {
        const M3Op *o = &m3_ops[i];
        switch (o->op) {
        case M3_INIT: memset(&mat, 0, sizeof mat); mat.m[0] = mat.m[5] = mat.m[10] = 1.0f; break;
        case M3_TRANSLATE: m_translate(&mat, (float)o->a / 65536.0f, (float)o->b / 65536.0f, (float)o->c / 65536.0f); break;
        case M3_ROT_X: m_rot_turns(&mat, 0, o->a); break;
        case M3_ROT_Y: m_rot_turns(&mat, 1, o->a); break;
        case M3_ROT_Z: m_rot_turns(&mat, 2, o->a); break;
        case M3_SCALE: m_scale(&mat, (float)o->a / 65536.0f, (float)o->b / 65536.0f, (float)o->c / 65536.0f); break;
        case M3_DRAW: {
            u32 ptr = (u32)o->a;
            int k;
            unsigned long long td0 = sceKernelGetSystemTimeWide();
            for (k = 0; k < 58 && part_ptr[k] != ptr; k++) { }
            if (k == 58 || (unsigned)k >= m_alpha.nparts || n >= 96 + 16) break;
            it[n].c = &m_alpha; it[n].part = k; it[n].clip = 0;
            memcpy(it[n].m, mat.m, sizeof it[n].m);
            cols[n] = (short)(o->b & 0x7FFF);
            n++;
            t_m3draw += sceKernelGetSystemTimeWide() - td0;
            break;
        }
        default: break;
        }
    }
    {                                                   /* development: data/m3dbg exists -> data/m3dbg.txt gets the first replay (ops, parts, matrices) */
        static int logged, calls, enabled = -1;
        char p[256];
        FILE *g;
        if (enabled < 0) {                              /* is data/m3dbg there?  asked once: the failed fopen of every picture cost milliseconds on the memory stick (the name entry's rough frame rate) */
            snprintf(p, sizeof(p), "%s/m3dbg", perf_base);
            g = fopen(p, "r");
            enabled = g != NULL;
            if (g) fclose(g);
        }
        if (enabled && !logged && ++calls >= 300) {
            snprintf(p, sizeof(p), "%s/m3dbg", perf_base);
            snprintf(p, sizeof(p), "%s/m3dbg.txt", perf_base);
            g = fopen(p, "w");
            if (g) {
                fprintf(g, "ops %d draws %d nparts %u kind %u\n", m3_n, n, m_alpha.nparts, m_alpha.kind);
                for (i = 0; i < n && i < 30; i++)
                    fprintf(g, "item %d part %d col %04x m %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f\n", i, it[i].part, cols[i], it[i].m[0], it[i].m[1], it[i].m[2], it[i].m[3], it[i].m[4], it[i].m[5], it[i].m[6], it[i].m[7], it[i].m[8], it[i].m[9], it[i].m[10], it[i].m[11]);
                fclose(g);
                logged = n > 0;
            }
        }
    }
    t_m3ops += sceKernelGetSystemTimeWide() - tm0;
    if (n == 0) return;
    scene_set_colors(cols);
    scene_set_offset(0, 0);
    scene_draw(it, n, rect, NULL);
    scene_set_colors(NULL);
}

static int menu3d_fresh(void)                           /* a model, a car or the name entry's letters were asked for in the last pictures */
{
    return (m_cars && menu3d_n > 0 && sat_ticks - menu3d_tick <= 6) || (m3_n > 0 && sat_ticks - m3_tick <= 6);
}

static void draw_menu3d_items(void)
{
    static const RenderRect rect = { 26, 0, 428, 272 };      /* where the 352 x 224 picture of the screens is drawn */
    int i;
    if (m3_n > 0 && sat_ticks - m3_tick <= 6) {
        Mat l;
        SceneItem car[16];
        int ncar = 0, shared = m_cars && menu3d_n == 1 && sat_ticks - menu3d_tick <= 6 && menu3d_req[0].kind == 0 && menu3d_req[0].offset == 0;
        memset(&l, 0, sizeof l);
        l.m[0] = l.m[5] = l.m[10] = 1.0f;
        m_rot_turns(&l, 0, 0xA000);
        scene_set_light(l.m[2], l.m[6], l.m[10]);
        if (shared) { unsigned long long tc0 = sceKernelGetSystemTimeWide(); ncar = car_items(&menu3d_req[0], car, 16); t_caritems += sceKernelGetSystemTimeWide() - tc0; }     /* the name entry: the car stands in the middle of the letter wheel, the letters in front of it hide it (one depth sorted list, like the pipeline's) */
        m3_replay(&rect, car, ncar);
        if (shared) return;
    }
    if (!m_cars || menu3d_n == 0 || sat_ticks - menu3d_tick > 6) return;
    {                                                   /* rotate_light(0xA000, 0, 0): the light of the screens' models, in view space */
        Mat l;
        memset(&l, 0, sizeof l);
        l.m[0] = l.m[5] = l.m[10] = 1.0f;
        m_rot_turns(&l, 0, 0xA000);
        scene_set_light(l.m[2], l.m[6], l.m[10]);
    }
    for (i = 0; i < menu3d_n; i++) {
        SceneItem it[16];
        int n = menu3d_req[i].kind == 1 ? model_items(&menu3d_req[i], it) : car_items(&menu3d_req[i], it, 16);
        if (n == 0) continue;
        scene_set_offset((float)(s16)(menu3d_req[i].offset >> 16), (float)(s16)menu3d_req[i].offset);
        scene_draw(it, n, &rect, NULL);
    }
    scene_set_offset(0, 0);
}

static void draw_menu3d(void)                           /* the models of the screen with the colour offset of the sprite layer (the original draws them as VDP1 polygons: the fades dim them with the rest) */
{
    int c[3], k;
    u32 clofen = (u32)((sat->v2reg[0x110] << 8) | sat->v2reg[0x111]), clofsl = (u32)((sat->v2reg[0x112] << 8) | sat->v2reg[0x113]);
    u32 base = (clofen & 0x40) ? ((clofsl & 0x40) ? 0x11Au : 0x114u) : 0;
    for (k = 0; k < 3; k++) {
        int v = base ? ((sat->v2reg[base + 2 * (u32)k] << 8) | sat->v2reg[base + 2 * (u32)k + 1]) & 0x1FF : 0;
        c[k] = v >= 256 ? v - 512 : v;
    }
    scene_set_coloff(c[0], c[1], c[2]);
    draw_menu3d_items();
    scene_set_coloff(0, 0, 0);
}

/* ---- the photo scene (menu_photo.c, satmenu.h PhotoView): the staff roll's fly-by over the course ----
 * The sky (VDP2 RBG0) and the text (NBG3) come from the software renderer in two parts split at the sprite priority (vdpr.c vdpr_hi: one composition, two pictures); between them the race renderer draws the course
 * blocks the original chose (get_poly_map) and the scenery objects (src/event.c); the colour offset of the fades is applied to the whole picture afterwards. */
static int tex_hi_valid;
static VdpFrame frame_hi;
static Course m_pcourse;
static int m_pcourse_n = -1, m_pev_course = -1;
static int photo_split;                                 /* the pictures of this tick were rendered in two parts */
static int menu_split;                                  /* ... for a screen with 3D models: the models are drawn between the parts */

/* ---- the ceremony and the ending (menu_ending.c, cer3d): the ceremony's camera on its spline with the car, the animated crew (ENDING.MDL, endcar.rcp), the confetti and the shadow over the VDP2 ground; the ending
 * is the photo scene with the PSP's ghost car (ending_ghost_engine).  Missing: the object next to the car, the rivals of the ending. ---- */
static Course m_ending;                                 /* the five confetti quads of ENDING.MDL (tools/conv_ending.py) */
static Course m_endcar;                                 /* the 40 + 2 textured parts of the animated model (the crew of the car; tools/conv_endcar.py) */
static u32 endcar_addr[48];                             /* their addresses in the model image (endcar.names): the part of a logged model address */
static int endcar_n;
static int cer_fresh(void) { return cer3d.have && cer3d_tick != 0 && sat_ticks - cer3d_tick <= 6; }

static int ghost_items(const Mat *v, SceneItem *out, int max)       /* the ending's ghost car in the world of the photo scene's view v */
{
    Menu3D m;
    int n;
    if (!cer3d.ending || sat_ticks - cer3d_tick > 6) return 0;
    memset(&m, 0, sizeof m);
    m.model = cer3d.car_id;
    m.still = 1;
    m.pos[0] = (s32)((cer3d.car[24] << 24) | (cer3d.car[25] << 16) | (cer3d.car[26] << 8) | cer3d.car[27]);
    m.pos[1] = (s32)((cer3d.car[28] << 24) | (cer3d.car[29] << 16) | (cer3d.car[30] << 8) | cer3d.car[31]);
    m.pos[2] = (s32)((cer3d.car[32] << 24) | (cer3d.car[33] << 16) | (cer3d.car[34] << 8) | cer3d.car[35]);
    m.ang[1] = (s32)(((long long)(s32)((cer3d.car[16] << 24) | (cer3d.car[17] << 16) | (cer3d.car[18] << 8) | cer3d.car[19]) * 0x28BE60DCLL) >> 32);   /* the yaw in radians -> turns */
    car_base_mat = v;
    n = car_items(&m, out, max);
    car_base_mat = NULL;
    return n;
}

static int ceremony_items(SceneItem *out, int max)
{
    Mat v;
    Menu3D m;
    int i, n;
    memset(&v, 0, sizeof v);
    v.m[0] = v.m[5] = v.m[10] = 1.0f;                   /* the camera actor: initial_matrix, reverse_x on the mirror course, rotate_z / x / y of minus its angles, translate2(-eye) */
    if (cer3d.mirror) m_scale(&v, -1.0f, 1.0f, 1.0f);
    m_rot_turns(&v, 2, -cer3d.cam_ang[2]);
    m_rot_turns(&v, 0, -cer3d.cam_ang[0]);
    m_rot_turns(&v, 1, -cer3d.cam_ang[1]);
    m_translate(&v, -(float)cer3d.cam_pos[0] / 65536.0f, -(float)cer3d.cam_pos[1] / 65536.0f, -(float)cer3d.cam_pos[2] / 65536.0f);
    memset(&m, 0, sizeof m);                            /* dispCar: translate, rotate_y / x / z of the car block (radians) */
    m.model = cer3d.car_id;
    m.still = 1;
    for (i = 0; i < 3; i++) {
        const u8 *p = cer3d.car + 24 + 4 * i, *q = cer3d.car + 12 + 4 * i;
        m.pos[i] = (s32)(((u32)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
        m.ang[i] = (s32)(((long long)(s32)(((u32)q[0] << 24) | (q[1] << 16) | (q[2] << 8) | q[3]) * 0x28BE60DCLL) >> 32);
    }
    car_base_mat = &v;
    n = car_items(&m, out, max);
    car_base_mat = NULL;
    if (cer3d.anim && m_endcar.file) {                  /* the animated model: the matrices FUN_0601CF44 gives draw_polygon2 (menu_ending.c anim_parts), the part models in the order of the animation table */
        int k, j;
        for (k = 0; k < cer3d.anim_np && n < max; k++) {
            for (j = 0; j < endcar_n && endcar_addr[j] != cer3d.anim_model[k]; j++) { }
            if (j == endcar_n || (unsigned)j >= m_endcar.nparts) continue;
            out[n].c = &m_endcar; out[n].part = j; out[n].clip = 0;
            for (i = 0; i < 12; i++) out[n].m[i] = (float)cer3d.anim_m[k][i] / 65536.0f;
            n++;
        }
    }
    if (m_ending.file) {
        Mat l;
        memset(&l, 0, sizeof l);                        /* the light of the kind 2 models: rotate_light(0xA000, 0, 0) as in the menus */
        l.m[0] = l.m[5] = l.m[10] = 1.0f;
        m_rot_turns(&l, 0, 0xA000);
        scene_set_light(l.m[2], l.m[6], l.m[10]);
        for (i = 0; i < cer3d.nconf && n < max; i++) {                      /* the confetti pieces: the five flat quads of ENDING.MDL */
            static const u32 model[5] = { 0x0604AB78u, 0x0604ABB8u, 0x0604ABF8u, 0x0604AC38u, 0x0604AC78u };
            const CerConf *c = &cer3d.conf[i];
            Mat w = v;
            int k;
            for (k = 0; k < 5 && model[k] != c->model; k++) { }
            if (k == 5) continue;
            m_translate(&w, (float)c->pos[0] / 65536.0f, (float)c->pos[1] / 65536.0f, (float)c->pos[2] / 65536.0f);
            m_rot_turns(&w, 2, (int)(((long long)c->ang[2] * 0x28BE60DCLL) >> 32));
            m_rot_turns(&w, 0, (int)(((long long)c->ang[0] * 0x28BE60DCLL) >> 32));
            m_rot_turns(&w, 1, (int)(((long long)c->ang[1] * 0x28BE60DCLL) >> 32));
            out[n].c = &m_ending; out[n].part = k; out[n].clip = 0;
            memcpy(out[n].m, w.m, sizeof out[n].m);
            n++;
        }
    } else if (m_objects) {
        for (i = 0; i < cer3d.nconf && n < max; i++) {                      /* without ending.rcp: the cube of the object pack */
            const CerConf *c = &cer3d.conf[i];
            Mat w = v;
            m_translate(&w, (float)c->pos[0] / 65536.0f, (float)c->pos[1] / 65536.0f, (float)c->pos[2] / 65536.0f);
            m_rot_turns(&w, 2, (int)(((long long)c->ang[2] * 0x28BE60DCLL) >> 32));
            m_rot_turns(&w, 0, (int)(((long long)c->ang[0] * 0x28BE60DCLL) >> 32));
            m_rot_turns(&w, 1, (int)(((long long)c->ang[1] * 0x28BE60DCLL) >> 32));
            out[n].c = m_objects; out[n].part = 0; out[n].clip = 0;
            memcpy(out[n].m, w.m, sizeof out[n].m);
            n++;
        }
    }
    return n;
}

/* ---- the title's demo run (menu_adv.c, adv_cars): the two cars of the recorded run on the course of the fly-over.  They are the race models (the time attack's car: the full body with the tail light panel),
 * put into the world with the original's dispCar order: translate, rotate_y, rotate_x, rotate_z of the car block (radians). ---- */
static int adv_car_items(const Mat *v, SceneItem *out, int max)
{
    int k, n = 0;
    if (adv_cars.tick == 0 || sat_ticks - adv_cars.tick > 6) return 0;
    for (k = 0; k < 2; k++) {
        const AdvCar *c = &adv_cars.car[k];
        Menu3D m;
        Mat w = *v;
        int i, a[3];
        if (!c->shown || n >= max) continue;
        for (i = 0; i < 3; i++) a[i] = (int)(((long long)c->ang[i] * 0x28BE60DCLL) >> 32);
        m_translate(&w, (float)c->pos[0] / 65536.0f, (float)c->pos[1] / 65536.0f, (float)c->pos[2] / 65536.0f);
        m_rot_turns(&w, 1, a[1]);
        m_rot_turns(&w, 0, a[0]);
        m_rot_turns(&w, 2, a[2]);
        memset(&m, 0, sizeof m);
        m.model = c->model;
        m.car2 = 1;
        car_base_mat = &w;
        n += car_items(&m, out + n, max - n);
        car_base_mat = NULL;
    }
    return n;
}

static int photo_fresh(void) { return (photo_view.tick != 0 && sat_ticks - photo_view.tick <= 6) || cer_fresh(); }

static void photo_release(void)
{
    if (m_pcourse.file) rcp_free(&m_pcourse);
    memset(&m_pcourse, 0, sizeof m_pcourse);
    m_pcourse_n = -1;
    m_pev_course = -1;
    photo_view.tick = 0;
}

static int photo_items(SceneItem *out, int max)
{
    static const char *const cn[4] = { "desert", "forest", "mount", "lake" };
    Mat v;
    int i, n = 0, nev;
    if (cer_fresh()) return ceremony_items(out, max);
    if (m_pcourse_n != photo_view.course) {
        char p[256];
        if (m_pcourse.file) rcp_free(&m_pcourse);
        memset(&m_pcourse, 0, sizeof m_pcourse);
        snprintf(p, sizeof(p), "%s/course/%s.rcp", perf_base, cn[photo_view.course & 3]);
        (void)rcp_load(&m_pcourse, p);
        m_pcourse_n = photo_view.course;
        m_pev_course = -1;
    }
    if (!m_pcourse.file) return 0;
    memset(&v, 0, sizeof v);
    if (photo_view.have_mat) {                          /* the title's script cameras: the original's own matrix (c_turnv, translate2) */
        for (i = 0; i < 12; i++) v.m[i] = (float)photo_view.mat[i] / 65536.0f;
    } else {
        v.m[0] = v.m[5] = v.m[10] = 1.0f;               /* photoExec: unit_matrix, rotate_x(-pitch), rotate_y(-yaw), translate2(-x, -y, -z) */
        if (photo_view.stretch) m_scale(&v, 1.0625f, 1.0f, 1.0f);       /* (the chase camera's view: horizontal stretch) */
        m_rot_turns(&v, 0, -photo_view.pitch);
        m_rot_turns(&v, 1, -photo_view.yaw);
        m_translate(&v, -(float)photo_view.pos[0] / 65536.0f, -(float)photo_view.pos[1] / 65536.0f, -(float)photo_view.pos[2] / 65536.0f);
    }
    n += ghost_items(&v, out + n, max - 40);            /* the ending's car (cars come first in the draw list) */
    n += adv_car_items(&v, out + n, max - 40 - n);      /* the title's demo run */
    if (m_pas && m_pas->cars_ok && m_pas->objects->file) n += ending_rival_items(v.m, out + n, max - 40 - n, m_pas->cars, m_pas->objects);        /* the rival cars of the replay */
    if (m_pas && m_pas->events_ok && m_pas->objects->file) {
        EvWorld w;
        if (m_pev_course != photo_view.course) {
            ev_set_collision(m_pas->events, NULL);
            ev_start(m_pas->events, photo_view.course, 2);
            m_pev_course = photo_view.course;
        }
        memset(&w, 0, sizeof w);
        memcpy(w.view, v.m, sizeof w.view);
        w.game_type = 2;
        w.car_byte_c5 = 0;
        w.car_x = w.car_z = 0x10000000;                 /* no car: far away (birds scare at 64 m) */
        w.car_y = 0;
        nev = ev_frame(m_pas->events, &w, photo_view.parts, photo_view.nparts, out + n, max - 20 - n);
        n += nev;
    }
    for (i = 0; i < photo_view.nparts && n < max; i++) {
        out[n].c = &m_pcourse;
        out[n].part = photo_view.parts[i] & 127;
        out[n].clip = 0;
        if ((unsigned)out[n].part >= m_pcourse.nparts) continue;
        memcpy(out[n].m, v.m, sizeof out[n].m);
        n++;
    }
    return n;
}

static void photo_fade(void)                            /* the VDP2 colour offset A of the fades over the picture (the software renderer left it out): + = towards white, - = towards black */
{
    int c[3], k, neg;
    for (k = 0; k < 3; k++) {
        int v = (sat->v2reg[0x114 + 2 * k] << 8) | sat->v2reg[0x115 + 2 * k];
        v &= 0x1FF;
        c[k] = v >= 256 ? v - 512 : v;
    }
    for (neg = 0; neg < 2; neg++) {
        u32 fix = 0;
        MVtx *q;
        for (k = 0; k < 3; k++) {
            int v = neg ? -c[k] : c[k];
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            fix |= (u32)v << (8 * k);
        }
        if (!fix) continue;
        sceGuDisable(GU_TEXTURE_2D);
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(neg ? GU_REVERSE_SUBTRACT : GU_ADD, GU_FIX, GU_FIX, fix, 0xFFFFFF);
        q = (MVtx *)sceGuGetMemory(2 * sizeof(MVtx));
        q[0].u = q[0].v = q[1].u = q[1].v = 0; q[0].col = q[1].col = 0xFFFFFFFFu;
        q[0].x = 26.0f; q[0].y = 0; q[0].z = 0; q[1].x = 454.0f; q[1].y = 272; q[1].z = 0;
        sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, q);
    }
    sceGuDisable(GU_BLEND);
}

static void draw_hi_layer(void)                         /* the upper part of a split picture (the layers above the sprite priority; transparent where there is none) over what the 3D drew */
{
    int y;
    if (!(frame_hi.same && tex_hi_valid)) {
        tex_upload(frame_hi.pix, frame_hi.w, frame_hi.h, TEXHI_VRAM);
        tex_hi_valid = 1;
    }
    sceGuOffset(2048 - 240, 2048 - 136);                /* scene_draw left its own viewport / clip state */
    sceGuViewport(2048, 2048, 480, 272);
    sceGuScissor(0, 0, 480, 272);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CLIP_PLANES);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuShadeModel(GU_FLAT);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, 512, 256, 512, (void *)(0x04000000u + TEXHI_VRAM));
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    {
        MVtx *q = (MVtx *)sceGuGetMemory(2 * sizeof(MVtx));
        q[0].u = 0; q[0].v = 0; q[0].col = 0xFFFFFFFFu; q[0].x = 26.0f; q[0].y = 0; q[0].z = 0;
        q[1].u = (float)frame_hi.w; q[1].v = (float)frame_hi.h; q[1].col = 0xFFFFFFFFu; q[1].x = 454.0f; q[1].y = 272; q[1].z = 0;
        sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, q);
    }
}

static void draw_photo(void)                            /* after the lower part of the picture (the sky) is on the screen: the course, then the upper part (the text) over it, then the fade */
{
    static SceneItem items[160];
    static const RenderRect rect = { 26, 0, 428, 272 };
    int n = photo_items(items, 160);
    if (n > 0) {
        scene_set_offset(0, 0);
        scene_draw(items, n, &rect, NULL);
    }
    draw_hi_layer();
    photo_fade();
}


static void draw_frame(void)
{
    int x, y;
    sceGuStart(GU_DIRECT, glist);
    if (!(frame.same && tex_valid)) {                   /* an unchanged picture (vdpr.c) is in the texture already */
        tex_upload(frame.pix, frame.w, frame.h, TEX_VRAM);
        tex_valid = 1;
    }
    sceGuDrawBuffer(GU_PSM_8888, (void *)(unsigned long)draw_off, BUF_W);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_BLEND);
    sceGuScissor(0, 0, 480, 272);
    sceGuClearColor(0xFF000000);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, 512, 256, 512, (void *)(0x04000000u + TEX_VRAM));
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    {
        MVtx *q = (MVtx *)sceGuGetMemory(2 * sizeof(MVtx));
        float x0 = 26.0f, x1 = 454.0f;                  /* the picture's own shape at the screen height */
        q[0].u = 0; q[0].v = 0; q[0].col = 0xFFFFFFFFu; q[0].x = x0; q[0].y = 0; q[0].z = 0;
        q[1].u = (float)frame.w; q[1].v = (float)frame.h; q[1].col = 0xFFFFFFFFu; q[1].x = x1; q[1].y = 272; q[1].z = 0;
        sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, q);
    }
    {
        unsigned long long t3 = sceKernelGetSystemTimeWide();
        if (photo_split) draw_photo();
        else {
            draw_menu3d();
            if (menu_split) draw_hi_layer();                    /* the layers above the sprite priority cover the models */
        }
        t_3d += sceKernelGetSystemTimeWide() - t3;
    }
    sceGuFinish();
    sceGuSync(0, 0);
    (void)x;
}

static int dump_done;
static void dump_vdp(void)                              /* development: data/dump_gover.v2ram / .cram / .v2reg (tools/menu_pc.c writes the same files) */
{
    static const char *const ext[3] = { "v2ram", "cram", "v2reg" };
    const u8 *src[3] = { sat->v2ram, sat->cram, sat->v2reg };
    unsigned n[3] = { 0x80000u, 0x1000u, 0x120u };
    int i;
    for (i = 0; i < 3; i++) {
        char p[300];
        FILE *f;
        snprintf(p, sizeof(p), "%s/dump_gover.%s", perf_base, ext[i]);
        f = fopen(p, "wb");
        if (f) { fwrite(src[i], 1, n[i], f); fclose(f); }
    }
    {
        char p[300];
        FILE *f;
        snprintf(p, sizeof(p), "%s/dump_gover.v1", perf_base);
        f = fopen(p, "wb");
        if (f) { fwrite(sat->hram + 0xC0000, 1, 0x4000, f); fclose(f); }      /* the VDP1 command table 0x060C0000 */
    }
}

/* One tick = one V-blank of the original.  The ticks are locked to the display's V-blank count: a tick that took longer than a V-blank (the picture every second tick can) is made up for by the
 * next ones starting at once, so the game runs at 60 ticks a second as long as the average cost fits, instead of losing a whole V-blank every time one picture is late; a tick that finds itself a
 * V-blank behind does not draw its picture (frames_skipped: at most three in a row).  A hitch of more than six V-blanks (a screen reading its files) is not made up for. */
static unsigned vc_next, swap_vc;
static int frames_skipped;

static void pace_wait(void)
{
    unsigned vc = sceDisplayGetVcount();
    if (!vc_next) vc_next = vc;
    vc_next++;
    if ((int)(vc - vc_next) > 6) vc_next = vc + 1;
    while ((int)(sceDisplayGetVcount() - vc_next) < 0) sceDisplayWaitVblankStart();
}

static void menu_tick(unsigned rin, unsigned rout)
{
    (void)rin; (void)rout;
    if (t_prev_end) t_logic += sceKernelGetSystemTimeWide() - t_prev_end;
    sat_pad = read_pad();
    {                                                   /* a type D of an older save (the PSP has A, B and DIY) */
        static const u32 types_at[2] = { 0x06042A3Fu, 0x060AA80Fu };      /* the options save area and the copy the race reads */
        int ti;
        for (ti = 0; ti < 2; ti++) {
            u32 v = R8(types_at[ti]);
            if ((v & 0x0F) >= PC_TYPES) v &= 0xF0;
            if (((v >> 4) & 7) >= PC_TYPES) v &= 0x8F;
            if (v != R8(types_at[ti])) W8(types_at[ti], v);
        }
    }
    tick_n++;
    snd_lib_vint();                                     /* the V-blank handler's share of the sound system: the co-driver voice stream */
    if (dbglog && !dump_done && R32(0x060A001C) == 0x0601F978 && R8(0x060A8C61) == 1 && R32(0x060A8C30) == 30) { dump_vdp(); dump_done = 1; }      /* development (data/dbglog): the VDP memory of the game over screen */
    if (dbglog && (tick_n & 255) == 0 && perf_n) perf_flush();      /* development only (an empty data/dbglog): the speed log is not written in a normal run */    /* a file write to the memory stick every 16 ticks was a hitch of its own: every 256 ticks (about 4 s) is enough for the log */
    if (nscript) {                                      /* development: data/menu_pad.txt "tick hexmask" lines: from that tick on the pad word is the mask (Saturn bits) */
        unsigned i;
        for (i = 0; i < nscript; i++) if (script_tick[i] <= tick_n) sat_pad = script_mask[i];
    }
    if (da_on) { u32 dm = (u32)devauto_menu_pad(tick_n); if (!devauto_realpad) sat_pad = dm; }
    if ((tick_n & 1) && vc_next && (int)(sceDisplayGetVcount() - vc_next) >= 1 && frames_skipped < 3) {
        frames_skipped++;                               /* a V-blank or more behind: the game's clock matters more than this picture (at most three in a row) */
    } else if (tick_n & 1) {                            /* the original shows a new picture at most every second V-blank */
        unsigned long long t0 = sceKernelGetSystemTimeWide(), t1, t2;
        frames_skipped = 0;
        if (!t_first) t_first = t0;
        if (dbglog) { fprintf(dbglog, "S %u\n", tick_n); fflush(dbglog); }
        vdpr_clock = clock_us;
        if ((sat_v2_touched || sat_v2_dirty_any) && fix_on) {            /* the Saturn pad's button names -> the PSP's (src/menu_pspfix.c): only where the screen wrote the VRAM */
            if (pspfix_apply()) sat_v2_touched = 1;                       /* it wrote into the VRAM itself: the renderer compares all of it */
            t_fix += sceKernelGetSystemTimeWide() - t0;
        }
        photo_split = photo_fresh();
        vdpr_rbg_half = photo_split;                    /* the 3D scenes' sky in 2 x 2 dot blocks: the software rotation is the most expensive part of their picture */
        menu_split = 0;
        if (photo_split) {                              /* the staff roll: the sky and the text separately, the course between them (draw_photo) */
            vdpr_no_offset = 1;
            vdpr_hi = &frame_hi;
            vdpr_hi_prio = (int)(sat->v2reg[0xF1] & 7);      /* the sprite priority (the course): the layers above it are the text */
            vdp_render_frame(&frame);
            vdpr_hi = NULL; vdpr_no_offset = 0;
        } else {
            if (m_pcourse.file) photo_release();
            menu_split = menu3d_fresh();
            if (menu_split) {                           /* the models of the screen (VDP1 polygons in the original) sit at the sprite priority between the layers: render the picture in two parts */
                vdpr_hi = &frame_hi;
                vdpr_hi_prio = (int)(sat->v2reg[0xF1] & 7);
                vdp_render_frame(&frame);
                vdpr_hi = NULL;
            } else {
                vdp_render_frame(&frame);
            }
        }
        t1 = sceKernelGetSystemTimeWide();
        if (dbglog) { fprintf(dbglog, "E %u %.1f ms\n", tick_n, (double)(t1 - t0) / 1000.0); fflush(dbglog); }
        while (sceDisplayGetVcount() == swap_vc) sceDisplayWaitVblankStart();      /* the buffer that is drawn into must not be the one still on the screen: the last swap has to have happened */
        draw_frame();
        t2 = sceKernelGetSystemTimeWide();
        t_render += t1 - t0; t_draw += t2 - t1; if (t1 - t0 > t_max) t_max = t1 - t0;
        perf_n++;
        n_same += (unsigned)frame.same;
        shown++;
        sceGuSwapBuffers();                             /* the new picture shows from the next V-blank on (no wait here: the pacing below does it) */
        swap_vc = sceDisplayGetVcount();
        draw_off ^= FB_BYTES;
    }
    pace_wait();
    if (tick_n - zsort_clear_tick > 6) zsort_clear();   /* a screen that does not start its passes with mkpoly_frame_init */
    t_prev_end = sceKernelGetSystemTimeWide();
}

static jmp_buf restart;
static u32 unported_addr, resume_vector;
static char save_dir[256];
static int race_chain, race_view;                       /* the arcade chain: the stage that comes next starts with the camera view of the one before */

/* The race (game_init 0x06010F0C and everything the main vector does until the goal / game over screens are done) is play.c.  What the screens before it decided is in the console memory
 * (docs/menus.md: [0x060AA860] game type, [0x060AA87E] stage, [0x060AA80A] car ...); what the race leaves is the car slot at 0x060B0000, and game_exit (menu_race.c) picks the next screen. */
static void race_snd_call(void *ud, unsigned fn, int r4, int r5, int r6) { (void)ud; snd_lib_call(fn, (u32)r4, (u32)r5, (u32)r6); }
static int race_snd_status(void *ud, int id) { (void)ud; return snd_lib_status(id); }
static void race_snd_engine(void *ud, const unsigned eng[4]) { (void)ud; W32(0x25A007C0u, eng[0]); W32(0x25A007C4u, eng[1]); }
static void race_snd_flush(void *ud) { (void)ud; snd_lib_flush(); }
static void race_snd_vint(void *ud) { (void)ud; snd_lib_vint(); }
static int race_snd_busy(void *ud) { (void)ud; return R8(0x060AAA44u); }
static void race_snd_put(void *ud, int msg) { (void)ud; W8(0x060AAA45u, (u32)msg); }
static const SndHost race_snd = { race_snd_call, race_snd_status, race_snd_engine, race_snd_flush, race_snd_vint, race_snd_busy, race_snd_put, NULL };

/* A race left through the pause menu (item 1 or 2): the race loop 0x06011170 does not call game_exit but chooses the next screen by the game type (the table 0x06011200), then plays out the sound
 * clean-up (0x06017514) and set_gover (0x06006BA8: the control byte becomes 10 for item 1, 12 for item 2; the history of game_exit's tail). */
static void host_retire(int type, int item)
{
    W32(0x060A001C, 0x0602FF84);                        /* the post-goal dispatcher */
    if (item == 2) W8(0x060AA825, R8(0x060AA825) | 1);
    if (type == 0) {                                    /* 0x0601124C: the arcade game */
        W8(0x060AA825, R8(0x060AA825) & 0xFE);
        if (RS16(0x060AA87E) != 3) {
            W16(0x060AA87E, 0);
            W32(0x060A001C, item == 1 ? 0x0601F978 : 0x06015408);           /* SELECT GAME / the first stage again */
        }
        W8(0x060AA82A, R8(0x060AA82A) | 1);
    } else if (type == 2) {                             /* 0x0601121C: practice */
        W32(0x060A001C, item == 2 ? 0x06010F0C : 0x0601F978);                /* the race again / SELECT GAME */
    } else if (type == 4 && R8(0x060AA810) == 2 && item == 2) {          /* 0x06011234: a time attack replay: RESTART plays it again (YES: the post-goal dispatcher, which skips the history in a replay) */
        W32(0x060A001C, 0x06010F0C);
    }
    snd_lib_call(0x0601753C, 0, 0, 0);                  /* the engine sounds off */
    snd_lib_call(0x06016FA4, (u32)-64, 0, 0);               /* FUN_06017514 */
    snd_lib_call(0x06016F10, 22, 0, 0);
    snd_lib_flush();
    W8(0x060B00F5, item == 1 ? 10 : 12);                /* set_gover */
    race_history();
}

static int host_race(void)
{
    PlayCfg pc;
    PlayOut out;
    int type = RS16(0x060AA860), stage = RS16(0x060AA87E), k;
    if ((type != 0 && type != 2 && type != 4 && type != 6) || (R8(0x060AA810) != 0 && !(R8(0x060AA810) == 2 && type == 4)) || !m_pas) return 0;      /* two player and the replays of the other game types are not part of the race yet */
    memset(&pc, 0, sizeof pc);
    pc.game_type = type;
    pc.course = stage & 3;
    pc.compare = R8(0x060AA80E);                        /* the time attack's compare with the best run (records: record_dataset) */
    pc.rec184 = RS32(0x060AA184); pc.rec188 = RS32(0x060AA188); pc.rec18c = RS32(0x060AA18C);
    pc.car = R8(0x060AA80A);
    pc.mt = R8(0x060AA809);
    pc.diff = R8(0x060AA808);
    pc.mirror = R8(0x060AA82E);
    pc.setup_mode = R8(0x060AA811) == 1;
    for (k = 0; k < 4; k++) pc.setup[k] = R8(0x060AA817 + (u32)k);
    pc.view = stage == 0 || !race_chain ? 0 : race_view;      /* init_game_work keeps the view byte of the stage before */
    pc.rect = 1;
    pc.hold = -1;
    pc.from_menu = 1;
    pc.init_g300 = R8(0x060AA300);                      /* the rank byte of the stage before: the arcade stages after the first start with the placing the player had (the opponents spawn from it) */
    pc.init_r = sat->hram + 0xB0500;                    /* the R block and the record buffers: rec_dataset's (stage start) and the earlier stages' */
    pc.init_rec = sat->hram + 0xAA400;
    pc.snd = &race_snd;                                 /* M9: the sound events of the race (sndev.c) go to the sound library */
    pc.snd_stereo = (R8(0x06040062) & 0x20) != 0;
    pc.snd_opt_vol = R8(0x0604000C);
    pc.snd_bgm_sel = R8(0x060AA828);
    for (k = 0; k < 4; k++) pc.init_wall[k] = R8(0x060AFE68 + (u32)k);
    pc.padrow = R8(0x060AA80F) & 0x7F;                  /* the KEY CONFIG type, set by rec_dataset */
    pc.rec_win.p = sat->lram; pc.rec_win.base = 0x00200000u; pc.rec_win.size = 0x100000u;      /* the input words of the race are recorded in the work RAM (from [0x060AAA5C] = 0x002CF800 on) where the name entry stores them */
    pc.rec_start = (u32)R32(0x060AAA5C);
    pc.mode2 = R8(0x060AA810) == 2;                    /* the result screen's replay / a record's replay: the run is played back from the work RAM (R+0x74 starts at [0x060AAA5C]) */
    pc.camdat.p = sat->lram + 0xA2000; pc.camdat.base = 0x002A2000u; pc.camdat.size = 0x1000;       /* CAMERA.DAT (loaded at the boot) */
    if (type == 4 && R8(0x060AA815) == 4) {             /* rec_dataset found a stored run of the stage: it races in slot 1 (myline_set) */
        pc.ghost = 1;
        pc.ghost_win = pc.rec_win;
        pc.ghost_start = (u32)R32(0x060AAA60);
        pc.ghost_car = R8(0x060AA813); pc.ghost_mode = R8(0x060AA816); pc.ghost_manual = R8(0x060AA812);
        for (k = 0; k < 4; k++) pc.ghost_setup[k] = R8(0x060AA81C + (u32)k);
        pc.ghost_solid = (R8(0x06040062) & 8) != 0;
    }
    {                                                   /* development: data/menu_race.txt "replay NAME" plays recorded inputs (data/replay/NAME) instead of the pad */
        char path[256], line[128];
        FILE *f;
        snprintf(path, sizeof(path), "%s/menu_race.txt", perf_base);
        f = fopen(path, "r");
        if (f) { while (fgets(line, sizeof(line), f)) { sscanf(line, "replay %95s", pc.replay); if (strncmp(line, "opponents 0", 11) == 0) pc.nopp = 1; sscanf(line, "laps %d", &pc.laps); } fclose(f); }
    }
    {                                                   /* development: what the menus asked for (data/race_cfg.txt, the last race) */
        char path[256];
        FILE *f;
        snprintf(path, sizeof(path), "%s/race_cfg.txt", perf_base);
        f = fopen(path, "w");
        if (f) { fprintf(f, "type %d course %d car %d mt %d diff %d mirror %d setup_mode %d laps %d compare %d view %d setup %d %d %d %d\n", pc.game_type, pc.course, pc.car, pc.mt, pc.diff, pc.mirror, pc.setup_mode, pc.laps, pc.compare, pc.view, pc.setup[0], pc.setup[1], pc.setup[2], pc.setup[3]); fclose(f); }
    }
    memset(&out, 0, sizeof out);
    play_run(&pc, m_pas, perf_base, &out, &draw_off);
    tex_valid = 0; tex_hi_valid = 0;                    /* the race's off-screen view used that VRAM */
    memcpy(sat->hram + 0xB0000, out.car, out.car_bytes);
    memcpy(sat->hram + 0xAA400, out.rec, out.rec_bytes);
    race_view = out.view;
    W8(0x060AA300, out.g300);
    for (k = 0; k < 4; k++) W8(0x060AFE68 + (u32)k, out.wall[k]);
    if (out.retire) { host_retire(type, out.retire); race_chain = R32(0x060A001C) == 0x06015408; return 1; }
    race_exit();
    race_chain = R32(0x060A001C) == 0x06015408;
    return 1;
}

/* The game over screen (0x06032378) shows the GAME OVER picture the original race leaves in NBG0's name table (maps 0x0A .. 0x0D, VRAM 0x28000 ..); the PSP race does not write it,
 * and the screen is reached by two ways: straight from the race (arcade game over) and from the ranking screen (practice: REPLAY - NO).  Its own set-up (init_gspr2) resets the
 * VDP2 registers to the game's table, so the planes are the same four maps whatever the screens before left there. */
static void host_gover_picture(void)
{
    memset(sat->v2ram + 0x28000, 0, 0x10000);
    sat_v2_touched = 1;
    init_game_over();
}

static void host_unported(u32 addr)
{
    if (addr == 0x06010F0C && host_race()) {            /* the stage start screen has faded out: the race runs, then the next screen continues */
        pad_flush = 1;
        resume_vector = R32(0x060A001C);
        longjmp(restart, 2);
    }
    unported_addr = addr;
    longjmp(restart, 1);
}

static int host_stub(u32 addr)
{
    if (addr != 0x060041F0) return 0;
    {                                                   /* init_game_work_start: what it leaves in the console memory (the CD loads of the course data are play.c's own) */
        int type = RS16(0x060AA860), stage = RS16(0x060AA87E);
        W8(0x060AA825, R8(0x060AA825) & 0xFE);
        if (type != 10 && (type != 0 || stage == 0)) {
            W32(0x060AA538, 0);
            W32(0x060AA658, 0);
            record_dataset();                           /* rec_dataset: the best times for the HUD, the lap sums, the key config row and tuning bytes of the race */
            if (R8(0x060AA810) != 2) W32(0x060AFE68, 0);
        }
        W8(0x060D0089, (u32)stage);
        W16(0x060AA880, (u32)(stage * 2));
        W16(0x060AA882, (u32)(stage * 4));
        W16(0x060AA884, (u32)(stage * 8));
    }
    return 1;
}

/* the backup memory: BUP0.BIN / BUP1.BIN in the data folder (records, options, set-ups, unlocks; the stored replays go to the "memory cartridge" image).  The original's backup check
 * screen (title -> mode select) is not ported; this is what it leaves behind when the memory works (menu_bup.c).  The data read at the power-on (rec_init) has to come first. */
static void host_patch(void)                            /* development: data/menu_patch.txt "ADDR HEXBYTES" lines = console memory set after the boot (the state a screen expects, like the oracle scenarios' patches) */
{
    char path[256], line[512];
    FILE *f;
    snprintf(path, sizeof(path), "%s/menu_patch.txt", save_dir);
    f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        unsigned a;
        char hex[400];
        if (sscanf(line, "%x %399s", &a, hex) == 2) {
            size_t i;
            for (i = 0; hex[i] && hex[i + 1]; i += 2) {
                unsigned v;
                if (sscanf(hex + i, "%2x", &v) != 1) break;
                W8(a + (unsigned)(i / 2), v);
            }
        }
    }
    fclose(f);
}

static int host_boot(void)
{
    if (sat_init(save_dir) != 0) return -1;
    sat_save_cart(1);
    if (sat_save_open(save_dir) == 0) {
        sat_boot();
        bup_host_attach(1);
    } else {
        sat_boot();
    }
    host_patch();
    snd_debug_dir = save_dir;
    {
        char np[300];
        FILE *g;
        snprintf(np, sizeof np, "%s/nosnd", save_dir);                            /* development: data/nosnd = no sound (the speed of the rest without the audio thread) */
        g = fopen(np, "r");
        if (g) fclose(g);
        else if (snd_audio_start() == 0) snd_lib_boot();
    }                                                   /* the sound system: the audio thread runs the original sound driver; the power-on load of the driver */
    return 0;
}

int menu_host_run(unsigned int *gulist, const char *base, u32 start_vector)
{
    glist = gulist;
    perf_base = base;
    draw_off = FB_BYTES;                                /* the first frame goes to the buffer that is not shown */
    snprintf(save_dir, sizeof(save_dir), "%s", base);
    if (host_boot() != 0) return -1;
    (void)pspfix_load(base);
    { char sp[256]; snprintf(sp, sizeof(sp), "%s/course/select.rcp", base); (void)rcp_load(&m_select, sp); }
    { char sp[256]; snprintf(sp, sizeof(sp), "%s/course/alpha.rcp", base); (void)rcp_load(&m_alpha, sp); }
    { char sp[256]; snprintf(sp, sizeof(sp), "%s/course/ending.rcp", base); (void)rcp_load(&m_ending, sp); }
    {
        char sp[256], line[64];
        FILE *nf;
        snprintf(sp, sizeof(sp), "%s/course/endcar.rcp", base);
        if (rcp_load(&m_endcar, sp) == 0) {
            snprintf(sp, sizeof(sp), "%s/course/endcar.names", base);
            nf = fopen(sp, "r");
            endcar_n = 0;
            while (nf && endcar_n < 48 && fgets(line, sizeof(line), nf)) { unsigned idx, a; if (sscanf(line, "%u %x", &idx, &a) == 2 && idx == (unsigned)endcar_n) endcar_addr[endcar_n++] = a; }
            if (nf) fclose(nf);
        }
    }
    {
        char path[256], line[64];
        snprintf(path, sizeof(path), "%s/nopspfix", base);
        { FILE *g = fopen(path, "r"); if (g) { fix_on = 0; fclose(g); } }
        FILE *f;
        snprintf(path, sizeof(path), "%s/menu_pad.txt", base);
        f = fopen(path, "r");
        if (f) {
            unsigned t, m;
            while (nscript < 256 && fgets(line, sizeof(line), f)) if (sscanf(line, "%u %x", &t, &m) == 2) { script_tick[nscript] = t; script_mask[nscript++] = m; }
            fclose(f);
        }
    }
    da_on = devauto_menu_load(base);
    { char p2[256]; FILE *g; snprintf(p2, sizeof(p2), "%s/dbglog", base); g = fopen(p2, "r"); if (g) { fclose(g); snprintf(p2, sizeof(p2), "%s/menu_frames.txt", base); dbglog = fopen(p2, "w"); } }
    sat_host_tick = menu_tick;
    menu_unported_hook = host_unported;
    sat_frame_init_hook = zsort_clear;
    menu_gover_hook = host_gover_picture;
    menu_gover_early_key = 1;
    menu_no_battle = 1;
    menu_stub_hook = host_stub;
    padcfg_load(base);
    menu_option_ntypes = PC_TYPES;
    menu_option_diy_hook = host_diy;
    ending_engine_set_base(perf_base);
    menu_engine_hook = ending_real_engine;              /* the ending's race engine: the recorded run on the native engine (ending_engine.c; without the course data the ghost car of menu_ending.c stands in) */
    for (;;) {
        int jr = setjmp(restart);
        if (jr == 0 || jr == 2) {                       /* 2: back from a race, the menus go on at the vector the race left */
            sat_main_loop_from(jr == 2 ? resume_vector : start_vector);
            break;
        }
        /* a screen handed over to something that is not ported yet (the race, the title, the 2 player battle...): note it, and start the menus again at the mode select */
        {
            char path[256];
            FILE *f;
            snprintf(path, sizeof(path), "%s/menu_unported.txt", base);
            f = fopen(path, "a");
            if (f) { fprintf(f, "no port for %08x (tick %u)\n", (unsigned)unported_addr, tick_n); fclose(f); }
        }
        race_chain = 0;
        if (host_boot() != 0) return -1;
        start_vector = 0x06022894;
    }
    return 0;
}
