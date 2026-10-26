/* M8: the PSP host's menu loop on the PC (no PSP code): runs the ported menus from a start vector with a pad script (lines "tick hexmask", as data/menu_pad.txt) and renders the pictures
 * with the software renderer (with the PSP button names, src/menu_pspfix.c):  menu_pc <datadir> <start vector hex> <script> <maxticks> <outprefix> [shot ticks...]
 * Reports the time of every picture (a slow or hanging renderer shows here). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <setjmp.h>
#include "../src/sat.h"
#include "../src/satlib.h"
#include "../src/satmenu.h"
#include "../src/vdpr.h"
#include "../src/menu_pspfix.h"
#include "../src/menu_ending.h"

extern int sat_v2_touched;
static unsigned nscript, st[256], sm[256], tick_n, maxticks, nshots, shots[64];
static const char *prefix;
static VdpFrame fr;
static jmp_buf done;
static double worst, fix_ms;
static unsigned npic;

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    int i;
    fprintf(f, "P6\n%d %d\n255\n", fr.w, fr.h);
    for (i = 0; i < fr.w * fr.h; i++) { u32 c = fr.pix[(i / fr.w) * VDPR_MAXW + i % fr.w]; fputc(c & 255, f); fputc((c >> 8) & 255, f); fputc((c >> 16) & 255, f); }
    fclose(f);
}

static void dump_mem(const char *pre, unsigned t)       /* the VDP2 VRAM, colour RAM and registers of the shot (tools/pspfix_layers.py style analysis) */
{
    char p[600];
    FILE *f;
    snprintf(p, sizeof(p), "%s_%u.v2ram", pre, t); f = fopen(p, "wb"); fwrite(sat->v2ram, 1, sizeof sat->v2ram, f); fclose(f);
    snprintf(p, sizeof(p), "%s_%u.cram", pre, t); f = fopen(p, "wb"); fwrite(sat->cram, 1, sizeof sat->cram, f); fclose(f);
    snprintf(p, sizeof(p), "%s_%u.v2reg", pre, t); f = fopen(p, "wb"); fwrite(sat->v2reg, 1, sizeof sat->v2reg, f); fclose(f);
}

static int diy_hook(void) { printf("DIY hook at tick %u (type byte %02x)\n", tick_n, (unsigned)R8(0x06042A3F)); return 1; }

static void tick(unsigned rin, unsigned rout)
{
    unsigned i;
    (void)rin; (void)rout;
    tick_n++;
    for (i = 0; i < nscript; i++) if (st[i] <= tick_n) sat_pad = sm[i];
    if (tick_n & 1) {
        clock_t t0 = clock(), t1;
        double ms;
        if (sat_v2_touched || sat_v2_dirty_any) { if (pspfix_apply()) sat_v2_touched = 1; }
        t1 = clock();
        vdp_render_frame(&fr);
        ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
        fix_ms += 1000.0 * (double)(t1 - t0) / CLOCKS_PER_SEC; npic++;
        if (ms > worst) worst = ms;
        if (ms > 100.0) printf("tick %u: picture took %.0f ms (vector %08x)\n", tick_n, ms, (unsigned)R32(0x060A001C));
    }
    for (i = 0; i < nshots; i++) if (shots[i] == tick_n) { char p[600]; snprintf(p, sizeof(p), "%s_%u.ppm", prefix, tick_n); write_ppm(p); dump_mem(prefix, tick_n); printf("tick %u: [060AA80F] %02x  option state %u  vector %08x\n", tick_n, (unsigned)R8(0x060AA80F), (unsigned)R8(0x060A8CC0), (unsigned)R32(0x060A001C)); }
    if (tick_n >= maxticks) longjmp(done, 1);
}

static void trace_hook(u32 addr) { fprintf(stderr, "call %08x\n", (unsigned)addr); }

int main(int argc, char **argv)
{
    unsigned start;
    int i;
    if (argc < 6) { fprintf(stderr, "usage: menu_pc datadir start script maxticks outprefix [shots...]\n"); return 1; }
    start = (unsigned)strtoul(argv[2], NULL, 16);
    {
        FILE *f = fopen(argv[3], "r");
        char line[64];
        unsigned t, m;
        while (f && nscript < 256 && fgets(line, sizeof(line), f)) if (sscanf(line, "%u %x", &t, &m) == 2) { st[nscript] = t; sm[nscript++] = m; }
        if (f) fclose(f);
    }
    maxticks = (unsigned)atoi(argv[4]);
    prefix = argv[5];
    for (i = 6; i < argc && nshots < 64; i++) shots[nshots++] = (unsigned)atoi(argv[i]);
    if (sat_init(argv[1]) != 0) { fprintf(stderr, "no data in %s\n", argv[1]); return 1; }
    sat_boot();
    if (getenv("MENU_PRE") && !strcmp(getenv("MENU_PRE"), "gover")) init_game_over();       /* what the screen at the start vector expects to find (game over: GMGOVER.DAT) */
    (void)pspfix_load(argv[1]);
    if (getenv("MENU_PC_DIY")) { menu_option_ntypes = 3; menu_option_diy_hook = diy_hook; }       /* the PSP host's KEY CONFIG: three types, the confirm key on DIY opens its editor */
    if (getenv("MENU_TRACE")) menu_call_hook = trace_hook;                  /* development: every menu_call address on stderr */
    sat_host_tick = tick;
    if (!setjmp(done)) sat_main_loop_from(start);
    if (npic) printf("%u pictures, pspfix_apply avg %.2f ms\n", npic, fix_ms / npic);
    printf("%u ticks, slowest picture %.1f ms, main vector %08x\n", tick_n, worst, (unsigned)R32(0x060A001C));
    return 0;
}
