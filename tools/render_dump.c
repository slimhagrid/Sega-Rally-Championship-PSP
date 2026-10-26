/* development: draws a VDP2 dump of the PSP host (data/dump_*.v2ram / .cram / .v2reg, written when data/dbglog exists) with the software renderer, one picture per layer:
 *   render_dump <datadir> <dump prefix> <out prefix>      -> <out>_all.ppm and <out>_nbg0..3 / rbg0 .ppm (only that layer on) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/sat.h"
#include "../src/vdpr.h"

extern int sat_v2_touched;
extern int vdpr_nocache;

static void load(const char *prefix, const char *ext, void *dst, size_t n)
{
    char p[600];
    FILE *f;
    snprintf(p, sizeof p, "%s.%s", prefix, ext);
    f = fopen(p, "rb");
    if (!f || fread(dst, 1, n, f) != n) { fprintf(stderr, "cannot read %s\n", p); exit(1); }
    fclose(f);
}

static void write_ppm(const char *path, const VdpFrame *fr)
{
    FILE *f = fopen(path, "wb");
    int i;
    fprintf(f, "P6\n%d %d\n255\n", fr->w, fr->h);
    for (i = 0; i < fr->w * fr->h; i++) { u32 c = fr->pix[(i / fr->w) * VDPR_MAXW + i % fr->w]; fputc(c & 255, f); fputc((c >> 8) & 255, f); fputc((c >> 16) & 255, f); }
    fclose(f);
}

int main(int argc, char **argv)
{
    static VdpFrame fr;
    u8 reg[0x120];
    int k;
    if (argc < 4) { fprintf(stderr, "usage: render_dump datadir dumpprefix outprefix\n"); return 1; }
    if (sat_init(argv[1]) != 0) { fprintf(stderr, "no data\n"); return 1; }
    load(argv[2], "v2ram", sat->v2ram, 0x80000);
    load(argv[2], "cram", sat->cram, 0x1000);
    load(argv[2], "v2reg", reg, sizeof reg);
    for (k = -1; k < 5; k++) {
        char p[700];
        unsigned bgon = ((unsigned)reg[0x20] << 8) | reg[0x21];
        memcpy(sat->v2reg, reg, sizeof reg);
        if (k >= 0) { bgon = (bgon & ~0x1Fu) | (1u << k); sat->v2reg[0x21] = (u8)bgon; }
        sat_v2_touched = 1;
        vdpr_nocache = 1;
        vdp_render_frame(&fr);
        snprintf(p, sizeof p, "%s_%s.ppm", argv[3], k < 0 ? "all" : k < 4 ? (const char *[]){ "nbg0", "nbg1", "nbg2", "nbg3" }[k] : "rbg0");
        write_ppm(p, &fr);
    }
    printf("bgon %02x%02x  prisa %02x%02x  prin_a %02x%02x prin_b %02x%02x  clofen %02x%02x  coff A %02x%02x %02x%02x %02x%02x\n", reg[0x20], reg[0x21], reg[0xF0], reg[0xF1], reg[0xF8], reg[0xF9], reg[0xFA], reg[0xFB],
           reg[0x110], reg[0x111], reg[0x114], reg[0x115], reg[0x116], reg[0x117], reg[0x118], reg[0x119]);
    return 0;
}
