/* M8: the Saturn memory model the ported menu code runs on.
 *
 * The menus are ported function by function from the original program (tools/lift.py is the reading aid) and keep the original's data layout: the console's high work RAM
 * (0x06000000), low work RAM (0x00200000), VDP1 / VDP2 VRAM, colour RAM and the VDP2 registers are plain byte arrays here, addressed with the console's addresses and
 * big-endian accessors.  That makes every port step checkable byte for byte against the original running in the oracle (tools/menu_oracle.py, tools/test_menu.c).
 * Hardware that the original polls or kicks (SCU DMA, V-blank handshakes) is reduced to its memory effect (sat.c).
 */
#ifndef SAT_H
#define SAT_H

#include <stdint.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef uint64_t u64;

#define HWRAM_SIZE 0x100000
#define LWRAM_SIZE 0x100000
#define V1RAM_SIZE 0x80000
#define V2RAM_SIZE 0x80000
#define CRAM_SIZE  0x1000
#define V2REG_SIZE 0x120

typedef struct {
    u8 hram[HWRAM_SIZE];        /* 0x06000000 (mirrors 0x26000000) */
    u8 lram[LWRAM_SIZE];        /* 0x00200000 (mirrors 0x20200000) */
    u8 v1ram[V1RAM_SIZE];       /* 0x25C00000 */
    u8 v1fb[V1RAM_SIZE];        /* 0x25C80000 frame buffer (not drawn here) */
    u8 v1reg[0x20];             /* 0x25D00000 */
    u8 v2ram[V2RAM_SIZE];       /* 0x25E00000 */
    u8 cram[CRAM_SIZE];         /* 0x25F00000 */
    u8 v2reg[V2REG_SIZE];       /* 0x25F80000 */
    u8 scu[0x100];              /* 0x25FE0000: DMA registers */
    u8 sndram[0x80000];         /* 0x25A00000: the sound CPU's RAM (M9: the sound driver and the SCSP read it, src/snd.c) */
    u8 junk[16];                /* writes to unmodelled hardware */
} SatMem;

extern SatMem *sat;

/* host side (PSP: render + wait for the vertical blank + read the pad; test: compare with the oracle) */
extern void (*sat_host_tick)(unsigned recipe_in, unsigned recipe_out);
extern int sat_axis;                        /* analog pad / wheel axis byte pad_read presents at 0x060AA99C every tick (127 = centre); -1: not written */
extern int sat_pad_sync;                    /* 1: the next pad_read takes the pad word as the state before it (no edges): the host sets it when the controller was not read for a while (a race ran in between) */
extern unsigned sat_pad;                    /* Saturn pad word presented to pad_read at the next tick (bits: R 8000, L 4000, Down 2000, Up 1000, Start 0800, A 0400, C 0200, B 0100, ...) */
extern unsigned sat_ticks;
extern int sat_v1_touched;                  /* VDP1 VRAM (sprite textures) accessed since the sprites were last drawn (vdpr.c) */

int sat_init(const char *datadir);          /* allocates the memory, loads ROM.blk; 0 = ok */
void sat_zero(void);
u8 *sat_ptr_slow(u32 a);                    /* the pointer for a write of up to four bytes: the VDP1 / VDP2 VRAM pages it touches are marked for the renderer */
u8 *sat_ptr_slow_rd(u32 a);                 /* the pointer for a read: marks nothing */
extern u32 sat_v2_dirty[];                  /* the 4 KB pages of the VDP2 VRAM written through the memory model since the renderer last looked (the renderer compares only those with its copy) */
extern int sat_v2_dirty_any;
void sat_mark_range(u32 a, u32 n);          /* a bulk write of n bytes at the console address a (sat_ptr + memcpy / fread): mark it */

static inline u8 *sat_ptr(u32 a)
{
    u32 h = a & 0x1FF00000u;
    if (h == 0x06000000u) return sat->hram + (a & 0xFFFFFu);
    if (h == 0x00200000u) return sat->lram + (a & 0xFFFFFu);
    return sat_ptr_slow(a);
}

static inline u8 *sat_ptr_rd(u32 a)
{
    u32 h = a & 0x1FF00000u;
    if (h == 0x06000000u) return sat->hram + (a & 0xFFFFFu);
    if (h == 0x00200000u) return sat->lram + (a & 0xFFFFFu);
    return sat_ptr_slow_rd(a);
}

static inline u8 R8(u32 a) { return *sat_ptr_rd(a); }
static inline u16 R16(u32 a) { const u8 *p = sat_ptr_rd(a); return (u16)((p[0] << 8) | p[1]); }
static inline u32 R32(u32 a) { const u8 *p = sat_ptr_rd(a); return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static inline s8 RS8(u32 a) { return (s8)R8(a); }
static inline s16 RS16(u32 a) { return (s16)R16(a); }
static inline s32 RS32(u32 a) { return (s32)R32(a); }
void sat_hw_write(u32 a, int size, u32 v);          /* stores into the SCU register block run the DMA */
static inline int sat_is_hw(u32 a) { return (a & 0x1FFFFF00u) == 0x05FE0000u; }
static inline void W8(u32 a, u32 v) { *sat_ptr(a) = (u8)v; }
static inline void W16(u32 a, u32 v) { u8 *p = sat_ptr(a); p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static inline void W32(u32 a, u32 v)
{
    u8 *p;
    if (sat_is_hw(a)) { sat_hw_write(a, 4, v); return; }
    p = sat_ptr(a);
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
}

/* files: blocks (dest, size, bytes) as the original's loader leaves them */
extern void (*sat_cd_hook)(void);                  /* called when the console reads a disc file (sat_load_file / sat_load_raw / sat_cd_request): the PSP host stops the CD audio, the drive cannot play and read at once */
int sat_load_file(const char *name);                /* "PRGDATA.DAT" -> data/menu/PRGDATA.DAT.blk; 0 = ok */
int sat_load_raw(const char *name, u32 dest);       /* data/menu/<name>.raw (plain file) -> dest */
void sat_cd_request(u32 req);                       /* a CD request block in console memory (file name / raw destination) */
/* the save data (backup memory) is in sat_save.h / menu_bup.c */
void sat_main_loop(unsigned max_ticks);
void sat_main_loop_from(u32 first);
void sat_memcpy(u32 dst, u32 src, u32 n);           /* console memory to console memory (bytes) */
void sat_memset(u32 dst, int v, u32 n);

/* the sound driver interface (0x06015F90 ..): the calls are logged (tick, function address, r4, r5, r6) the way tools/menu_env.py logs them; M9 plays them */
typedef struct { u32 tick, fn, a, b, c; } SndCall;
extern SndCall snd_log[4096];
extern unsigned snd_nlog;
void snd_call(u32 fn, u32 a, u32 b, u32 c);

/* tick = one V-IN + V-OUT handler pair (the original's interrupts run at the sync loops) */
void sat_tick(void);

#endif
