/* M8: library routines of the original program (etc / vdp / scroll / switch), see satlib.h. */
#include "satlib.h"

/* ---- etc (0x06005674 ..) ---- */
void rcopy(u32 src, u32 dst, u32 n)
{
    while (n--) { W32(dst, R32(src)); src += 4; dst += 4; }
}

void rcopyw(u32 src, u32 dst, u32 n)
{
    while (n--) { W16(dst, R16(src)); src += 2; dst += 2; }
}

void rfill(u32 dst, u32 val, u32 n)
{
    while (n--) { W32(dst, val); dst += 4; }
}

void rfillff(u32 end, u32 val, u32 n)
{
    while (n--) { end -= 4; W32(end, val); }
}

/* ---- scroll ---- */
static s32 avg2(s32 a, s32 b) { return (s32)((u32)a + (u32)b) >> 1; }

void set_mov_scr(void)                                  /* 0x060159DC */
{
    u32 r0;
    W32(0x25F80070, R32(0x060AA864) & 0x03FF0000);
    W32(0x25F80074, R32(0x060AA868) & 0x03FF0000);
    W32(0x25F80080, R32(0x060AA86C) & 0x03FF0000);
    W32(0x25F80084, R32(0x060AA870) & 0x03FF0000);
    r0 = R32(0x060AA874) & 0x03FF03FF;
    W16(0x25F80092, r0);
    W16(0x25F80090, r0 >> 16);
    r0 = R32(0x060AA878) & 0x03FF03FF;
    W16(0x25F80096, r0);
    W16(0x25F80094, r0 >> 16);
}

void set_mov_scri(void)                                 /* 0x06015A14: the scroll positions follow their targets halfway each tick */
{
    static const u32 dst[4] = { 0x25F80070, 0x25F80074, 0x25F80080, 0x25F80084 };
    int k;
    for (k = 0; k < 4; k++) {
        s32 v = avg2(RS32(0x060AA864 + 4 * k), RS32(0x060AA8B8 + 4 * k));
        if (v == (s32)0xFFFF8000) v = 0;
        W32(dst[k], (u32)v);
        W32(0x060AA8B8 + 4 * k, (u32)v);
    }
    for (k = 0; k < 4; k++) {
        s32 a = RS16(0x060AA874 + 2 * k), b = RS16(0x060AA8C8 + 2 * k), v;
        if (a == 0 && b == -1) v = 0;
        else v = (a + b) >> 1;
        W16(0x25F80090 + 2 * k, (u32)v);
        W16(0x060AA8C8 + 2 * k, (u32)v);
    }
}

void init_mov_scri(void) { rcopy(0x060AA864, 0x060AA8B8, 6); }                 /* 0x06015AFC */

void set_window_scr(void)                               /* 0x06015B08 */
{
    int k;
    for (k = 0; k < 12; k++) W16(0x25F800C0 + 2 * k, R16(0x060AA8A0 + 2 * k));
}

void set_window_scri(void)                              /* 0x06015B40 */
{
    int k;
    for (k = 0; k < 8; k++) {
        s32 v = (RS16(0x060AA8A0 + 2 * k) + RS16(0x060AA8D0 + 2 * k)) >> 1;
        W16(0x25F800C0 + 2 * k, (u32)v);
        W16(0x060AA8D0 + 2 * k, (u32)v);
    }
    for (k = 0; k < 4; k++) W16(0x25F800D0 + 2 * k, R16(0x060AA8B0 + 2 * k));
}

void init_window_scri(void) { rcopy(0x060AA8A0, 0x060AA8D0, 4); }              /* 0x06015B74 */

/* smoothing of one rotation parameter of 10-bit fixed point (wrap at 0x04000000) */
static u32 wrapavg(u32 cur, u32 prev)
{
    u32 diff = cur - prev;
    u32 r0;
    if ((s32)diff < 0) diff = (u32)(-(s32)diff);
    if (diff <= 0x02000000u) r0 = prev + cur;
    else r0 = prev + (cur + 0x04000000u);
    return (u32)((s32)r0 >> 1) & 0x03FF0000u;
}

void set_rpara_int(void)                                /* 0x06016058 */
{
    W32(0x060B907C + 40, wrapavg(R32(0x060B901C + 40), R32(0x060B907C + 40)));
    W32(0x060B907C + 44, wrapavg(R32(0x060B901C + 44), R32(0x060B907C + 44)));
}

void init_rpara_int(void)                               /* 0x060160B4 */
{
    W32(0x060AAA10, 0x060B9000);
    rcopy(0x060B9000, 0x060B9060, 24);
}

void set_scr_int(void) { set_mov_scri(); set_window_scri(); set_rpara_int(); }      /* 0x060159BC */
void init_scr_int(void) { init_mov_scri(); init_window_scri(); init_rpara_int(); }  /* 0x060159CC */

static void grpara_int_body(u32 r8, u32 r3)             /* 0x06015FF4 with r8 = parameter table, r3 = target block */
{
    u32 r4, r1, t;
    W32(r8 + 40, wrapavg(R32(r8 + 40), R32(r3 + 8)));
    r4 = (u32)avg2(RS32(r8 + 0), RS32(r3 + 12));
    r1 = r4;
    W32(r8 + 0, r4);
    t = (u32)avg2(RS32(r8 + 4), RS32(r3 + 16));
    W32(r8 + 4, t);
    r4 = (u32)avg2(RS32(r8 + 12), RS32(r3 + 20));
    W32(r8 + 12, r4);
    W32(r8 + 16, r1);
}

void set_grpara_int(void) { grpara_int_body(0x060B901C, 0x060AA9F0); }                        /* 0x06015FEC */
void set_grpara_int2p(void) { set_grpara_int(); grpara_int_body(0x060B909C, 0x060AAAA4); }    /* 0x06016044 */

/* ---- vdp ---- */
void tv_off_boot(void)                                  /* 0x06005580 */
{
    W16(0x25E7FFFC, 0x7BDE);
    W16(0x25F80000, R16(0x25F80000) & 0x7FFF);
}

void tv_on(void)                                        /* 0x0600553C */
{
    sync_vin();
    W16(0x25E7FFFC, 0x0421);
    W16(0x25F80000, R16(0x25F80000) | 0x8000);
}

void tv_off(void)                                       /* 0x06005560 */
{
    W16(0x25E7FFFC, 0x7BDE);
    sync_vin();
    W16(0x25F80000, R16(0x25F80000) & 0x7FFF);
}

void init_vdp1sys(void)                                 /* 0x060054B0: VDP1 end command, mode registers (the stores of the jsr delay slots come before the sync) */
{
    W16(0x25C00000, 0x8000);
    W16(0x25D00000, 0);
    sync_vout();
    sync_vout();
    W16(0x25D00002, 3);
    W16(0x25D00004, 2);
    W16(0x25D00006, 0);
    W16(0x25D00008, 0);
    W16(0x25D0000A, 0x58DF);
    sync_vout();
    W16(0x25D00002, 2);
    sync_vout();
    W16(0x25D00002, 3);
    sync_vout();
    W16(0x25D00002, 2);
    sync_vout();
    W16(0x25D00002, 3);
}

void init_vdp1sys2(void)                                /* 0x06005502 */
{
    sync_vout();
    W16(0x25D00002, 0);
}

void init_vdp2sys(void)                                 /* 0x06005518: VDP2 registers from the table at 0x00298000 */
{
    u32 n = R32(0x00298000), p = 0x00298004;
    while (n--) {
        u32 src = R32(p);
        u32 off = R16(p + 4), cnt = R16(p + 6);
        p += 8;
        while (cnt--) { W16(0x25F80000 + off, R16(src)); src += 2; off += 2; }
    }
    W16(0x060AA8E6, 15);
}

void vram_allclr(void)                                  /* 0x06005408 */
{
    rfillff(0x25C80000, 0, 0x20000);
    rfillff(0x25E7FFFC, 0, 0x1FFFF);
}

void init_vram(void)                                    /* 0x060053EC */
{
    vram_allclr();
    W16(0x25E7FFFE, 0);
    init_vdp1sys();
    init_vdp2sys();
}

/* ---- switch: pad_read (the original reads the SMPC; here the host's pad word, as tools/menu_env.py presents it) ---- */
void pad_read(void)
{
    static u16 prev;
    u16 held = (u16)sat_pad, edge;
    if (sat_pad_sync) { prev = held; sat_pad_sync = 0; }
    edge = (u16)(held & ~prev);
    prev = held;
    W16(0x060AA990, held);
    W16(0x060AA992, edge);
    W16(0x060AA994, R16(0x060AA994) | edge);
    W8(0x060AA9A9, 2);
    if (sat_axis >= 0) W8(0x060AA99C, (u32)sat_axis);
}

/* ---- init module: the game's VDP set-up (0x06004600 ..) ---- */
static void dma_regs(u32 base, u32 src, u32 dst, u32 count)          /* SCU DMA registers as the original programs them; a start (+0x10 = 0x101) is separate */
{
    W32(base + 0x00, src);
    W32(base + 0x04, dst);
    W32(base + 0x08, count);
    W32(base + 0x0C, 0x101);
    W32(base + 0x14, 7);
}

void vram_scrmap_clr2(void)                             /* 0x0600543C */
{
    rfillff(0x25E2C000, 0, 0x1000);
    rfillff(0x25E3A000, 0, 0x800);
    rfillff(0x25E3C000, 0, 0x800);
    rfillff(0x25E3E000, 0, 0x800);
    rfillff(0x25E42000, 0, 0x800);
}

void vram_scrmap_a_clr(void)                            /* 0x06005474 */
{
    rfillff(0x25E38000, 0, 0x4000);
}

void init_colorwk(void)                                 /* 0x060041E4 */
{
    rfillff(0x060B9000, 0, 0x400);
}

void init_tasc_cell0(void)                              /* 0x06004B9A: the first cell of four scroll planes cleared */
{
    rfillff(0x25E00040, 0, 16);
    rfillff(0x25E10040, 0, 16);
    rfillff(0x25E18040, 0, 16);
    rfillff(0x25E20040, 0, 16);
}

void init_nscr_vol(void)                                /* 0x060046C0: scroll positions of the four normal scrolls */
{
    W32(0x060AA864, 0x02000000);
    W32(0x060AA86C, 0x02000000);
    W16(0x060AA874, 0x0200);
    W16(0x060AA878, 0x0200);
    W32(0x060AA868, 0);
    W32(0x060AA870, 0);
    W16(0x060AA876, 0);
    W16(0x060AA87A, 0);
}

void init_gspr(void)                                    /* 0x0600460C */
{
    vram_scrmap_clr2();
    init_vdp1sys();
    init_vdp2sys();
    init_colorwk();
    dma_regs(0x25FE0020, 0x060B8000, 0x25F00000, 0);
    W32(0x25FE0030, 0x100);
    W32(0x25FE0030, 0x101);                             /* colour RAM <- shadow */
    rcopy(0x0029813C, 0x060B9000, 24);                  /* rpara_init_tbl */
    rcopy(0x0029813C, 0x060B9060, 24);
    W32(0x060AAA10, 0x060B9000);
    dma_regs(0x25FE0040, 0x060B9000, 0x25E7FF00, 96);
    W32(0x25FE0050, 0x100);
    W32(0x25FE0050, 0x101);                             /* rotation parameters -> VDP2 VRAM */
    init_tasc_cell0();
    W16(0x060C0000, 0x8000);
    W16(0x25C00000, 0x8000);
    dma_regs(0x25FE0000, 0x060C0000, 0x25C00000, 0xD000);
    W32(0x25FE0010, 0x100);
    init_nscr_vol();                                    /* falls through into init_nscr_vol (0x060046C0) */
}

void init_gspr2(void)                                   /* 0x06004600 */
{
    init_gspr();
    init_vdp1sys2();
}

void init_oscroll(void)                                 /* 0x06015E40 */
{
    W16(0x25F80038, 0xC00C);
    W16(0x25F80020, 31);
    W16(0x060AA8E6, 31);
    rcopy(0x0029819C, 0x060B9000, 24);
    rcopy(0x0029819C, 0x060B9060, 24);
    W32(0x060AAA10, 0x060B9000);
    W16(0x060B9044, 0x200);
    W32(0x25FE0040, 0x060B9000);
    W32(0x25FE0044, 0x25E7FF00);
    W32(0x25FE0048, 96);
    W32(0x25FE004C, 0x101);
    W32(0x25FE0054, 7);
    W32(0x25FE0050, 0x100);
    W32(0x25FE0050, 0x101);
    W16(0x25F8000E, (R16(0x25F8000E) & 0x3300) | 224);
}
