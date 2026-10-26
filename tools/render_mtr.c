/* M8: render the pictures of an oracle trace (MTR1) with the software VDP renderer (src/vdpr.c).
 *   render_mtr <trace.mtr> <tick> <out.ppm>     the picture at one tick
 *   render_mtr <trace.mtr> -all                 every tick with the layer caches, compared with a render without caches (they must be identical), and the time per frame */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>
#include "../src/sat.h"
#include "../src/vdpr.h"
#include "../src/menu_pspfix.h"

extern int vdpr_nocache;
extern int vdpr_unsupported;
static unsigned char *tr;
static size_t trpos;
static u32 rd32(void) { u32 v = tr[trpos] | (tr[trpos + 1] << 8) | (tr[trpos + 2] << 16) | ((u32)tr[trpos + 3] << 24); trpos += 4; return v; }

static void write_ppm(const char *path, const VdpFrame *fr)
{
    FILE *f = fopen(path, "wb");
    int i;
    fprintf(f, "P6\n%d %d\n255\n", fr->w, fr->h);
    for (i = 0; i < fr->w * fr->h; i++) {
        u32 c = fr->pix[(i / fr->w) * VDPR_MAXW + i % fr->w];
        fputc(c & 255, f); fputc((c >> 8) & 255, f); fputc((c >> 16) & 255, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    FILE *f;
    unsigned char *raw;
    long rawlen;
    z_stream z;
    u32 k, nreg, ntick, tick, np, i;
    u32 raddr[16], rsize[16];
    int all = argc > 2 && !strcmp(argv[2], "-all");
    u32 want = all ? 0 : (u32)atoi(argv[2]);
    static VdpFrame fr, fr2;
    if (argc < 3) { fprintf(stderr, "usage: render_mtr trace.mtr (tick out.ppm | -all)\n"); return 1; }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); rawlen = ftell(f); fseek(f, 0, SEEK_SET);
    raw = (unsigned char *)malloc(rawlen);
    if (fread(raw, 1, rawlen, f) != (size_t)rawlen) return 1;
    fclose(f);
    memset(&z, 0, sizeof(z));
    inflateInit(&z);
    tr = (unsigned char *)malloc(256u << 20);
    z.next_in = raw + 4; z.avail_in = (uInt)(rawlen - 4); z.next_out = tr; z.avail_out = 256u << 20;
    if (inflate(&z, Z_FINISH) != Z_STREAM_END) { fprintf(stderr, "inflate failed\n"); return 1; }
    sat_init("extracted/psp");
    rd32();                                             /* start vector */
    np = rd32();
    for (i = 0; i < np; i++) { u32 a = rd32(), n = rd32(); u32 j; for (j = 0; j < n; j++) W8(a + j, tr[trpos + j]); trpos += n; }
    nreg = rd32();
    for (k = 0; k < nreg; k++) { raddr[k] = rd32(); rsize[k] = rd32(); }
    for (k = 0; k < nreg; k++) { memcpy(sat_ptr(raddr[k]), tr + trpos, rsize[k]); trpos += rsize[k]; }
    ntick = rd32();
    {
        clock_t tc = 0;
        u32 frames = 0, bad = 0;
        for (tick = 0; tick < ntick && (all || tick <= want || getenv("PSPFIX_SCAN")); tick++) {
            u32 nch, c;
            trpos += 8;                                 /* pad, recipes, vector */
            nch = rd32();
            for (c = 0; c < nch; c++) { u32 a = rd32(), n = rd32(); memcpy(sat_ptr(a), tr + trpos, n); trpos += n; }
            if (getenv("WATCH") && tick >= (u32)atoi(getenv("WATCH_FROM") ? getenv("WATCH_FROM") : "0") && tick <= (u32)atoi(getenv("WATCH_TO") ? getenv("WATCH_TO") : "99999999")) {    /* development: WATCH="25E3CF14,25E3D414" prints those words (and the option page state) every tick */
                char list[256], *tok;
                snprintf(list, sizeof list, "%s", getenv("WATCH"));
                printf("tick %u state %02x:", tick, (unsigned)R8(0x060A8CC0));
                for (tok = strtok(list, ","); tok; tok = strtok(NULL, ",")) printf(" %s=%08x", tok, (unsigned)R32((u32)strtoul(tok, NULL, 16)));
                printf("\n");
            }
            if (getenv("PSPFIX_SCAN") && tick % (u32)atoi(getenv("PSPFIX_SCAN")) == 0) pspfix_scan_tick(tick);
            if (all && (tick & 1)) {
                clock_t t0 = clock();
                vdpr_nocache = 0;
                vdp_render_frame(&fr);
                tc += clock() - t0;
                vdpr_nocache = 1;
                vdp_render_frame(&fr2);
                vdpr_nocache = 0;
                frames++;
                if (memcmp(fr.pix, fr2.pix, sizeof(fr.pix)) != 0) {
                    if (!bad) printf("cached render differs from the uncached one at tick %u\n", tick);
                    bad++;
                }
            }
        }
        if (all) {
            printf("%u frames, %u differ; %.2f ms per frame (cached)\n", frames, bad, frames ? 1000.0 * (double)tc / CLOCKS_PER_SEC / frames : 0.0);
            if (vdpr_unsupported) printf("unsupported line features used: %d (1 vertical cell scroll, 2 line scroll Y, 4 line zoom)\n", vdpr_unsupported);
            return bad != 0;
        }
    }
    if (getenv("PSPFIX_LEARN")) pspfix_learn();
    if (getenv("PSPFIX")) printf("pspfix: %d fixes applied\n", pspfix_apply());
    if (getenv("SPLIT")) {                              /* the host's 3D split (menu_host.c photo scene): the lower picture, then the upper one (transparent dots black) as <out>.hi.ppm */
        static VdpFrame hi;
        char hp[600];
        int i;
        vdpr_no_offset = 1;
        vdpr_hi = &hi;
        vdpr_hi_prio = atoi(getenv("SPLIT"));
        vdp_render_frame(&fr);
        vdpr_hi = NULL; vdpr_no_offset = 0;
        for (i = 0; i < hi.w * hi.h; i++) { u32 *p = &hi.pix[(i / hi.w) * VDPR_MAXW + i % hi.w]; if (!(*p >> 24)) *p = 0xFF00FF00u; }
        snprintf(hp, sizeof(hp), "%s.hi.ppm", argv[3]);
        write_ppm(hp, &hi);
        write_ppm(argv[3], &fr);
        return 0;
    }
    vdp_render_frame(&fr);
    write_ppm(argv[3], &fr);
    {                                                   /* the video memory at that tick for tools/ (python analysis): <out>.vram .cram .reg */
        char nm[600];
        FILE *o;
        snprintf(nm, sizeof(nm), "%s.vram", argv[3]); o = fopen(nm, "wb"); fwrite(sat_ptr(0x25E00000), 1, 0x80000, o); fclose(o);
        snprintf(nm, sizeof(nm), "%s.cram", argv[3]); o = fopen(nm, "wb"); fwrite(sat_ptr(0x25F00000), 1, 0x1000, o); fclose(o);
        snprintf(nm, sizeof(nm), "%s.reg", argv[3]); o = fopen(nm, "wb"); fwrite(sat_ptr(0x25F80000), 1, 0x120, o); fclose(o);
    }
    return 0;
}
