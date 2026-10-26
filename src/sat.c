/* M8: Saturn memory model (see sat.h): memory map, SCU DMA, file loader, the V-blank handlers ("recipes") and the sync calls. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sat.h"
#include "satlib.h"

SatMem *sat;
int sat_v2_touched = 1;                       /* VDP2 VRAM was accessed since the renderer last looked (vdpr.c) */
int sat_v1_touched = 1;                       /* VDP1 VRAM (the sprite textures) was accessed since the renderer last drew the sprites */
void (*sat_host_tick)(unsigned, unsigned);
int sat_axis = -1;
unsigned sat_pad;
int sat_pad_sync;
unsigned sat_ticks;
static char sat_dir[256];

int sat_init(const char *datadir)
{
    if (!sat) {
        sat = (SatMem *)calloc(1, sizeof(SatMem));
        if (!sat) return -1;
    } else {
        memset(sat, 0, sizeof(SatMem));
    }
    snprintf(sat_dir, sizeof(sat_dir), "%s", datadir);
    sat_ticks = 0;
    return sat_load_file("ROM");
}

u32 sat_v2_dirty[V2RAM_SIZE / 4096 / 32];
int sat_v2_dirty_any;

void sat_mark_range(u32 a, u32 n)                       /* a write of n bytes at the console address a: the VDP2 VRAM pages it touches are dirty, the VDP1 VRAM's sprites may have changed */
{
    u32 off, p, last;
    a &= 0x1FFFFFFFu;
    if (a >= 0x05C00000u && a < 0x05C80000u) { sat_v1_touched = 1; return; }
    if (a < 0x05E00000u || a >= 0x05E80000u || !n) return;
    off = a - 0x05E00000u;
    last = (off + n - 1) >> 12;
    if (last >= V2RAM_SIZE / 4096) last = V2RAM_SIZE / 4096 - 1;
    for (p = off >> 12; p <= last; p++) sat_v2_dirty[p >> 5] |= 1u << (p & 31);
    sat_v2_dirty_any = 1;
}

u8 *sat_ptr_slow_rd(u32 a)                              /* the memory map; a read through it changes nothing */
{
    a &= 0x1FFFFFFFu;
    if (a >= 0x06000000u && a < 0x08000000u) return sat->hram + (a & 0xFFFFFu);
    if (a >= 0x00200000u && a < 0x00300000u) return sat->lram + (a & 0xFFFFFu);
    if (a >= 0x05A00000u && a < 0x05A80000u) return sat->sndram + (a - 0x05A00000u);
    if (a >= 0x05C00000u && a < 0x05C80000u) return sat->v1ram + (a - 0x05C00000u);
    if (a >= 0x05C80000u && a < 0x05D00000u) return sat->v1fb + (a - 0x05C80000u);
    if (a >= 0x05D00000u && a < 0x05D00020u) return sat->v1reg + (a - 0x05D00000u);
    if (a >= 0x05E00000u && a < 0x05E80000u) return sat->v2ram + (a - 0x05E00000u);
    if (a >= 0x05F00000u && a < 0x05F80000u) return sat->cram + (a & 0xFFFu);
    if (a >= 0x05F80000u && a < 0x05F80000u + V2REG_SIZE) return sat->v2reg + (a - 0x05F80000u);
    if (a >= 0x05FE0000u && a < 0x05FE0100u) return sat->scu + (a - 0x05FE0000u);
    return sat->junk;                                   /* unmodelled hardware (sound RAM, SMPC, CD block, ...): writes vanish */
}

u8 *sat_ptr_slow(u32 a)                                 /* for a write of up to four bytes (W8 .. W32): marks what it changes for the renderer */
{
    sat_mark_range(a, 4);
    return sat_ptr_slow_rd(a);
}

/* SCU DMA, direct mode, software start, as the V-IN recipes use it (tools/vdp2.py _scu_dma): level n registers at 0x25FE0000 + 0x20 n:
 * +0 read address, +4 write address, +8 count, +0x10 enable / start */
void sat_hw_write(u32 a, int size, u32 v)
{
    u8 *p = sat_ptr(a);
    u32 off = a & 0x1FFFFFFFu;
    (void)size;
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
    if (off == 0x05FE0010u || off == 0x05FE0030u || off == 0x05FE0050u) {
        if ((v & 1) && (v & 0x100)) {
            u32 b = a - 0x10, src = R32(b), dst = R32(b + 4), cnt = R32(b + 8);
            if (off == 0x05FE0010u) {
                cnt &= 0xFFFFF;
                if (!cnt) cnt = 0x100000;
            } else {
                cnt &= 0xFFF;
                if (!cnt) cnt = 0x1000;
            }
            sat_memcpy(dst, src, cnt);
        }
    }
}

void sat_memcpy(u32 dst, u32 src, u32 n)
{
    /* the oracle reads the source first and then loads it (memmove); regions are contiguous, so one mapping per side is enough */
    u8 *d = sat_ptr(dst), *s = sat_ptr_rd(src);
    if (d == sat->junk) return;
    memmove(d, s, n);
    sat_mark_range(dst, n);
}

void sat_memset(u32 dst, int v, u32 n) { memset(sat_ptr(dst), v, n); sat_mark_range(dst, n); }

/* ---- files: MBK1 = "MBK1", u32 nblocks, per block: u32 dest, u32 size, bytes (little endian header words, block bytes as on the console) */
void (*sat_cd_hook)(void);

int sat_load_file(const char *name)
{
    char path[512];
    FILE *f;
    unsigned char hdr[8];
    u32 n, i;
    if (sat_cd_hook) sat_cd_hook();
    snprintf(path, sizeof(path), "%s/menu/%s.blk", sat_dir, name);
    f = fopen(path, "rb");
    if (!f) return -1;
    if (fread(hdr, 1, 8, f) != 8 || memcmp(hdr, "MBK1", 4)) { fclose(f); return -2; }
    n = hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) | ((u32)hdr[7] << 24);
    for (i = 0; i < n; i++) {
        u32 dest, size;
        if (fread(hdr, 1, 8, f) != 8) { fclose(f); return -3; }
        dest = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | ((u32)hdr[3] << 24);
        size = hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) | ((u32)hdr[7] << 24);
        if (size) {
            u8 *d = sat_ptr(dest);
            if (d == sat->junk) {                       /* destination is not modelled: skip */
                fseek(f, (long)size, SEEK_CUR);
            } else if (fread(d, 1, size, f) != size) {
                fclose(f);
                return -4;
            } else {
                sat_mark_range(dest, size);
            }
        }
    }
    fclose(f);
    return 0;
}

/* raw files (not scatter-load): the whole file is copied to a destination the caller names (MBK1 file with one block, see conv_menu.py) */
int sat_load_raw(const char *name, u32 dest)
{
    char path[512];
    FILE *f;
    unsigned char hdr[8];
    u32 size;
    if (sat_cd_hook) sat_cd_hook();
    snprintf(path, sizeof(path), "%s/menu/%s.raw", sat_dir, name);
    f = fopen(path, "rb");
    if (!f) return -1;
    if (fread(hdr, 1, 8, f) != 8 || memcmp(hdr, "MBK1", 4) || fread(hdr, 1, 8, f) != 8) { fclose(f); return -2; }
    size = hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) | ((u32)hdr[7] << 24);
    if (fread(sat_ptr(dest), 1, size, f) != size) { fclose(f); return -3; }
    fclose(f);
    sat_mark_range(dest, size);
    return 0;
}

/* a CD request list (0x0600F550): 24-byte entries ended by an entry whose first word is -1.  Entry type (word at +0): 0 = raw file (+4 destination, name at +10), 2 = scatter-load
 * file (name at +2) */
void sat_cd_request(u32 req)
{
    for (; RS16(req) != -1; req += 24) {
        u32 a = req + 2, raw_dest = 0;
        char name[40];
        int i = 0;
        if (RS16(req) == 0 && R8(req + 10)) { raw_dest = R32(req + 4); a = req + 10; }
        while (R8(a) && i < 39) name[i++] = (char)R8(a++);
        name[i] = 0;
        if (raw_dest) { if (sat_load_raw(name, raw_dest) == 0) continue; }
        if (name[0]) sat_load_file(name);
    }
}

void (*snd_call_hook)(u32 fn, u32 r4, u32 r5, u32 r6);
SndCall snd_log[4096];
unsigned snd_nlog;

void snd_call(u32 fn, u32 a, u32 b, u32 c)
{
    SndCall *e = &snd_log[snd_nlog++ & 4095];
    e->tick = sat_ticks; e->fn = fn; e->a = a; e->b = b; e->c = c;
    if (snd_call_hook) snd_call_hook(fn, a, b, c);          /* the PSP host: the sound library (snd_lib.c) does the calls' work */
}

/* ---- the V-blank handlers (int module, 0x06004CE4 vint_in / 0x06005148 vint_out): the byte [0x060AA800] selects the V-IN recipe, [0x060AA801] the V-OUT recipe.
 * Every recipe starts by clearing its own byte; some write the next recipes (word stores at 0x060AA800 = V-IN byte << 8 | V-OUT byte). */
#define A_VIN   0x060AA800u
#define A_VOUT  0x060AA801u
#define A_FLAGS 0x060AA803u
#define A_FRAME 0x060AA806u
#define A_BGON  0x060AA8E6u
#define A_VDP1PTR 0x060AA87Cu
#define A_MAIN_VOUT_IMM 0x060A0001u         /* the immediates of the main loop copy: `mov #n,r0` (the V-OUT byte it sets) and `mov #n,r4` (its sync argument) */
#define A_MAIN_SYNC_IMM 0x060A0013u

static void dma2_start(u32 src) { W32(0x25FE0040, src); W32(0x25FE0050, 0x101); }
static void dma1_cram(void) { W32(0x25FE0030, 0x101); }
static void dma0_vdp1(void) { W32(0x25FE0010, 0x101); }

/* the VDP1 draw-end check at the head of most recipes: while the draw has not ended (EDSR bit 1 clear) force the end and remember the VDP1 command pointer */
static void vdp1_end_check(void)
{
    if ((R16(0x25D00010) & 2) == 0) {
        W16(0x25D0000C, 0);
        W16(A_VDP1PTR, R16(0x25D00014));
    }
}

static void recipe_in_L4dbc(void)                   /* 0x06004DBC */
{
    vdp1_end_check();
    dma0_vdp1();
}

static void recipe_in_L4e02(void)                   /* 0x06004E02 */
{
    set_mov_scr();
    dma2_start(R32(0x060AAA10));
    dma1_cram();
    W16(0x25F80020, R16(A_BGON));
}

static void recipe_in_L4f2a(void)                   /* 0x06004F2A */
{
    dma2_start(0x060B9000);
    dma1_cram();
    W16(0x25F80020, R16(A_BGON));
}

/* frame-swap handshake shared by recipes 8, 26, 32 */
static void recipe_in_swap(u32 busy_wait_unused)
{
    (void)busy_wait_unused;
}

static void vint_in(void)
{
    u32 n = R8(A_VIN);
    if (!n) return;
    W8(A_VIN, 0);
    switch (n) {
    case 0: case 2:
        break;
    case 38:                                        /* 0x26: title / attract: smooth the rotation parameters, DMA them, draw */
        set_grpara_int();
        dma2_start(0x060B9000);
        recipe_in_L4dbc();
        break;
    case 50:                                        /* 0x32 */
        set_grpara_int2p();
        dma2_start(0x060B9000);
        recipe_in_L4dbc();
        break;
    case 4:
        recipe_in_L4dbc();
        break;
    case 44:                                        /* 0x2C */
        set_scr_int();
        dma2_start(0x060B9060);
        recipe_in_L4dbc();
        break;
    case 6:
        recipe_in_L4e02();
        break;
    case 8:
        if ((R16(0x25D00010) & 2) == 0) {
            W16(0x25D0000C, 0);
            W16(A_VDP1PTR, R16(0x25D00014));
        }
        if ((R8(A_FRAME) & 1) == 0) {
            W16(0x25D00004, 0);
            W8(A_VIN, 10);
            W8(A_MAIN_SYNC_IMM, 10);
            W8(A_VOUT, 4);
            W8(A_MAIN_VOUT_IMM, 4);
        } else {
            W8(A_FRAME, R8(A_FRAME) & 0xFE);
            W16(0x25D00004, 2);
            dma0_vdp1();
            W8(A_VOUT, 4);
            W8(A_MAIN_VOUT_IMM, 4);
        }
        break;
    case 10:
        if ((R8(A_FRAME) & 1) == 0) {
            W8(A_VIN, 10);
            W8(A_VOUT, 2);
            W8(A_MAIN_VOUT_IMM, 2);
        } else {
            W8(A_FRAME, R8(A_FRAME) & 0xFE);
            dma0_vdp1();
            W8(A_VOUT, 10);
            W8(A_MAIN_VOUT_IMM, 10);
        }
        break;
    case 12:
        W16(A_VIN, 0x0E02);
        break;
    case 14:
        W16(A_VIN, 6);
        recipe_in_L4e02();
        break;
    case 42:                                        /* 0x2A */
        W16(A_VIN, 6);
        set_scr_int();
        dma2_start(0x060B9060);
        dma1_cram();
        W16(0x25F80020, R16(A_BGON));
        break;
    case 40:                                        /* 0x28 */
        W16(A_VIN, 6);
        set_mov_scr();
        break;
    case 36:                                        /* 0x24 */
        W16(A_VIN, 6);
        set_mov_scr();
        set_grpara_int();
        recipe_in_L4f2a();
        break;
    case 48:                                        /* 0x30 */
        W16(A_VIN, 6);
        set_mov_scr();
        set_grpara_int2p();
        recipe_in_L4f2a();
        break;
    case 16:
        W16(A_VIN, 0x1204);
        recipe_in_L4dbc();
        break;
    case 18:
        W16(A_VIN, 2);
        break;
    case 20:
        W16(A_VIN, 4);
        recipe_in_L4dbc();
        break;
    case 22:
        W16(A_VIN, 0x1802);
        break;
    case 24:
        W16(A_VIN, 0x1A06);
        recipe_in_L4e02();
        break;
    case 26:
        W16(A_VIN, 0x1604);
        if ((R16(0x25D00010) & 2) == 0) {
            W16(0x25D0000C, 0);
            W16(A_VDP1PTR, R16(0x25D00014));
        }
        if ((R8(A_FRAME) & 1) == 0) {
            W16(0x25D00004, 0);
            W8(A_VIN, 28);
        } else {
            W8(A_FRAME, R8(A_FRAME) & 0xFE);
            W16(0x25D00004, 2);
            dma0_vdp1();
        }
        break;
    case 28:
        if ((R8(A_FRAME) & 1) == 0) {
            W16(A_VIN, 0x1C02);
        } else {
            W8(A_FRAME, R8(A_FRAME) & 0xFE);
            W16(A_VIN, 0x160A);
            dma0_vdp1();
        }
        break;
    case 30:                                        /* 0x1E */
        W16(A_VIN, 0x2006);
        recipe_in_L4e02();
        break;
    case 32:                                        /* 0x20 */
        W16(A_VIN, 0x1E04);
        if ((R16(0x25D00010) & 2) == 0) {
            W16(0x25D0000C, 0);
            W16(A_VDP1PTR, R16(0x25D00014));
        }
        if ((R8(A_FRAME) & 1) == 0) {
            W16(0x25D00004, 0);
            W8(A_VIN, 34);
        } else {
            W8(A_FRAME, R8(A_FRAME) & 0xFE);
            W16(0x25D00004, 2);
            dma0_vdp1();
        }
        break;
    case 34:                                        /* 0x22 */
        if ((R8(A_FRAME) & 1) == 0) {
            W16(A_VIN, 0x2202);
        } else {
            W8(A_FRAME, R8(A_FRAME) & 0xFE);
            W16(A_VIN, 0x1E0A);
            dma0_vdp1();
        }
        break;
    case 46:                                        /* 0x2E */
        W16(A_VIN, 2);
        set_mov_scr();
        dma2_start(R32(0x060AAA10));
        dma1_cram();
        dma0_vdp1();
        W16(0x25F80020, R16(A_BGON));
        set_window_scr();
        break;
    default:
        break;
    }
}

static void vint_out(void)
{
    u32 n = R8(A_VOUT);
    if (!n) return;
    W8(A_VOUT, 0);
    switch (n) {
    case 4: W16(0x25D00002, 3); break;
    case 6: W16(0x25D00002, 2); break;
    case 8:
        W16(0x25D00002, 2);
        W8(A_VIN, 8);
        W8(A_MAIN_SYNC_IMM, 8);
        break;
    case 10: W16(0x25D00004, 1); break;
    default: break;
    }
    if (R8(A_FLAGS) & 1) pad_read();
}

void sat_tick(void)
{
    unsigned rin = R8(A_VIN), rout = R8(A_VOUT);
    vint_in();
    vint_out();
    sat_ticks++;
    if (sat_host_tick) sat_host_tick(rin, rout);
}

/* ---- the sync calls (int module): a recipe byte is written and the V-blank handlers are run until the handler cleared it; every loop test is preceded by one tick */
static void sync_wait(u32 flag, u32 val)
{
    W8(flag, val);
    do { sat_tick(); } while (R8(flag));
}

void sync_vin(void) { sync_wait(A_VIN, 2); }
void sync_vout(void) { sync_wait(A_VOUT, 2); }
void sync_dsp(void) { sync_wait(0x060AA802u, 2); }
void sync_n(u32 n) { sync_wait(A_VIN, n); }                     /* sync(n) 0x0600520C with r8 = 0x060AA800 */
void intm_sync(u32 n)                                           /* 0x0600521C: every loop test is preceded by a tick (the oracle takes the V-blanks at the loop heads) */
{
    do { sat_tick(); } while (R8(A_VIN));
    W8(A_VIN, n);
    do { sat_tick(); } while (R8(A_VIN));
}
void ints_sync(void)                                            /* 0x06005230 */
{
    W8(A_FRAME, R8(A_FRAME) | 1);
    do { sat_tick(); } while (R8(A_FRAME) & 1);
}
