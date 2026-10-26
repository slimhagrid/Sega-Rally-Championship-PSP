/* M8: the 2D sprite command writer of the polygon pipeline (sprite module, master CPU path) and the per-frame pipeline init.  The VDP1 command table is at 0x060C0000 (8-byte
 * units, one command = 4 units); the z-buffer entries (entry_zbuf) and the final sort (game_zsort) belong to the 3D pipeline and are not ported here: the oracle does not run
 * game_zsort either, and its work areas (0x060D1000 ..) are not compared. */
#include "menu_lib.h"

#define PIPE_M 0x060D1400u          /* master workspace (r14 / GBR of the original) */
#define PIPE_S 0x060D1C00u          /* slave workspace */

void (*sat_frame_init_hook)(void);                  /* the PSP host clears the depth buckets of the sprites here: game_zsort of the original does it once per pass */
void m_mkpoly_frame_init(void)                          /* 0x06018634 */
{
    if (sat_frame_init_hook) sat_frame_init_hook();
    W16(PIPE_M + 136, 0x200);
    W16(PIPE_M + 144, 0x1600);
    W32(PIPE_M + 132, 0);
    W32(PIPE_M + 168, 0);
}

void s_mkpoly_frame_init(void)                          /* 0x0601864A */
{
    W16(PIPE_S + 136, 0xC00);
    W16(PIPE_S + 144, 0x1800);
    W32(PIPE_S + 132, 0);
    W32(PIPE_S + 168, 0);
}

/* entry_zbuf (0x06018EBC): the command index goes into the depth bucket of its z; a bucket holds 8 entries (the counter at W+36 + bucket counts bytes); when it is full the next bucket is used */
static int entry_zbuf(u32 w, u32 idx, u32 z)
{
    u32 zbuf = R32(w + 32), cnt = R32(w + 36), b = (z >> 10) & ~1u, c, n;
    c = cnt + b;
    n = R8(c);
    if (n == 16) {
        n = R8(c + 2);
        c += 2;
        b += 2;
        if (n == 16) return 0;                          /* both buckets full: the original returns past the index increment of its caller */
    }
    W16(zbuf + (b << 4) + n, idx);
    W8(c, n + 2);
    W16(w + 134, R16(w + 134) + 1);
    return 1;
}

/* draw_sprite(pos, desc, z): desc -> sprite descriptor (+8 -> frame table, +12 -> sprite set); the command gets CMDCTRL 0x1000, the position word, and the colour / source address / size words of the frame */
void draw_sprite(u32 pos, u32 desc, u32 z)              /* 0x06018360 */
{
    u32 w = PIPE_M, d, set, r7, frame;
    W32(w + 48, desc);
    d = R32(w + 48);
    W32(w + 40, R32(d + 12));
    set = R32(w + 40);
    r7 = 0x060C0000u + ((u32)(u16)RS16(w + 136) << 3);
    W32(r7 + 12, pos);
    frame = set + 8 + (u32)RS16(R32(d + 8) + 2);
    W32(r7 + 4, R32(frame));
    W32(r7 + 8, R32(frame + 4));
    W16(r7 + 0, 0x1000);
    /* FUN_060183C0: entry_zbuf(z), then the command index moves on by one command */
    if (entry_zbuf(w, (u16)RS16(w + 136), z)) W16(w + 136, RS16(w + 136) + 4);
}

/* ---- setup_texture (texture module 0x0601A808): the model pack's texture data goes to VDP1 VRAM and the polygon tables learn where it is.
 * Table = list of u32 (address | type), ended by -1: type 0 = a polygon table at that address (absolute), type 1 / 2 = texture data at (address + base): a u32 byte count, then
 * the data, copied to VDP1 VRAM at 8 * [0x060D0048 / 0x060D004A] (units of 8 bytes); the offset then points behind the copy. */
static void tex_poly(u32 t)                             /* 0x0601A836 */
{
    if (R32(t + 4) == 0) {
        u32 o1 = R16(0x060D0048), o2 = R16(0x060D004A);
        u32 n = R16(t + 0), p = t + 8;
        W16(t + 4, o1);
        W16(t + 6, o2);
        do {
            W16(p + 4, R16(p + 4) + o1);
            W16(p + 2, R16(p + 2) + o2);
            p += 8;
        } while (--n);
    } else {
        u32 o1 = R16(t + 4), o2 = R16(t + 6);
        u32 n = R16(t + 0), p = t + 8;
        W32(t + 4, 0);
        do {
            W16(p + 4, R16(p + 4) - o1);
            W16(p + 2, R16(p + 2) - o2);
            p += 8;
        } while (--n);
        tex_poly(t);
    }
}

static void tex_copy(u32 a, u32 base, u32 var)          /* 0x0601A894 / 0x0601A8C6 */
{
    u32 dst = 0x25C00000u + ((u32)R16(var) << 3);
    u32 src = a + base, n = R32(src) >> 2;
    src += 4;
    sat_memcpy(dst, src, n * 4);
    W16(var, (dst + n * 4 - 0x25C00000u) >> 3);
}

void setup_texture(u32 tbl, u32 base)
{
    for (;;) {
        u32 w = R32(tbl);
        tbl += 4;
        if (w == 0xFFFFFFFFu) break;
        switch (w & 3) {
        case 0: tex_poly(w & ~3u); break;
        case 1: tex_copy(w & ~3u, base, 0x060D0048); break;
        case 2: tex_copy(w & ~3u, base, 0x060D004A); break;
        default: break;
        }
    }
}

void s_mkpoly_init(void)                                /* 0x06018408: the slave's work area tables only (the VDP1 shade table is mkpoly_init's) */
{
    int i;
    for (i = 0; i < 48; i++) W32(PIPE_S + 4 * i, R32(0x06018500 + 4 * i));
}

/* mkpoly_init (0x060183D4): the pipeline's own work areas (not ported) and the flat gouraud shade table for VDP1 */
void mkpoly_init(void)
{
    u32 a = 0x25C10000, v = 0;
    int i;
    for (i = 0; i < 48; i++) W32(PIPE_S + 4 * i, R32(0x06018500 + 4 * i));         /* FUN_0601841C: the work area tables of the slave / master (the rest of the init is the 3D parts') */
    for (i = 0; i < 48; i++) W32(PIPE_M + 4 * i, R32(0x06018440 + 4 * i));
    for (i = 0; i < 32; i++) {
        W32(a, v);
        W32(a + 4, v);
        v += 0x04210421;
        a += 8;
    }
}
