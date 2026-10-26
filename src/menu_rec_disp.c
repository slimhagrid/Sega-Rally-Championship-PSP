/* M8: the digit writers of rec_connect (0x0602CAF8 .. 0x0602CC74): numbers, ranks and names as VDP2 name table cells.  Every writer takes the cell position r4 and the character r2 and returns
 * the position of the next character; the 16-bit forms write a 1 x 2 cell glyph on a 64-cell plane (row = 0x80 bytes), the 32-bit forms (u32 cells, row = 0x100 bytes) are the 2-player /
 * 32-bit-cell planes.  putnum_rec reads two registers the screens set: r11 > 0 selects the 32-bit form, r14 >= 0 selects the highlighted palette (rec_disp_ctx). */
#include "satlib.h"
#include "menu_lib.h"
#include "menu_records.h"

static s32 ctx_r11, ctx_r14;

void rec_disp_ctx(s32 r11, s32 r14) { ctx_r11 = r11; ctx_r14 = r14; }

u32 time_disp(u32 r4, u32 r5, RecPutNum put)            /* 0x0602CAF8 */
{
    u32 r2 = (r5 >> 20) & 15, r3 = (r5 >> 16) & 15;
    if (r2 != 0) r4 = put(r4, r2);
    r4 = put(r4, r3);
    r4 = put(r4, 10);
    r3 = (r5 >> 8) & 15;
    r4 = put(r4, (r5 >> 12) & 15);
    r4 = put(r4, r3);
    r4 = put(r4, 11);
    r3 = r5 & 15;
    r4 = put(r4, (r5 >> 4) & 15);
    r4 = put(r4, r3);
    return r4;
}

u32 putnum_1x2(u32 r4, u32 r2)                          /* 0x0602CB48 */
{
    r2 = ((r2 << 1) + 94) << 1;
    r2 += 0x2000;
    W16(r4, r2);
    r2 += 2;
    r4 += 128;
    W16(r4, r2);
    return r4 - 126;
}

u32 putnum_rec(u32 r4, u32 r2)                          /* 0x0602CB62 */
{
    if (ctx_r11 > 0) {
        r2 = (r2 << 1) + 0x00200238u;
        W32(r4, r2);
        r2 += 74; r2 += 74;
        r4 += 0x100;
        W32(r4, r2);
        return r4 - 126 - 126;
    }
    r2 <<= 1;
    r2 += (ctx_r14 >= 0) ? 0x6038u : 0x2038u;
    W16(r4, r2);
    r2 += 74; r2 += 74;
    r4 += 128;
    W16(r4, r2);
    return r4 - 126;
}

u32 putnum_name(u32 r4, u32 r2)                         /* 0x0602CBA4 */
{
    r2 = (r2 << 1) + 0x1EA;
    W16(r4, r2);
    r2 += 24;
    r4 += 128;
    W16(r4, r2);
    return r4 - 126;
}

u32 putnum_name2(u32 r4, u32 r2)                        /* 0x0602CBBA */
{
    r2 = (r2 << 1) + 0x00300A14u;
    W32(r4, r2);
    r2 += 24;
    r4 += 0x100;
    W32(r4, r2);
    return r4 - 126 - 126;
}

u32 putnum_rank(u32 r4, u32 r2)                         /* 0x0602CBD8: a 3 x 2 cell number */
{
    r2 = (r2 * 3) << 2;
    r2 += 0x220E;
    W16(r4, r2); r2 += 2; r4 += 2;
    W16(r4, r2); r2 += 2; r4 += 2;
    W16(r4, r2); r2 += 2; r4 += 124;
    W16(r4, r2); r2 += 2; r4 += 2;
    W16(r4, r2); r2 += 2; r4 += 2;
    W16(r4, r2);
    return r4 - 126;
}

u32 putnum_rank2(u32 r4, u32 r2)                        /* 0x0602CC0A */
{
    r2 = (r2 * 3) << 2;
    r2 += 0x00200AB8u;
    W32(r4, r2); r2 += 2; r4 += 4;
    W32(r4, r2); r2 += 2; r4 += 4;
    W32(r4, r2); r2 += 2; r4 += 124; r4 += 124;
    W32(r4, r2); r2 += 2; r4 += 4;
    W32(r4, r2); r2 += 2; r4 += 4;
    W32(r4, r2);
    return r4 - 126 - 126;
}

u32 putnum_Grn(u32 r4, u32 r2)                          /* 0x0602CC44 */
{
    r2 = ((r2 + 2) << 1) + 0x3000;
    W16(r4, r2);
    r2 += 26;
    r4 += 128;
    W16(r4, r2);
    return r4 - 126;
}

u32 putnum_Grn2(u32 r4, u32 r2)                         /* 0x0602CC5C */
{
    r2 = ((r2 + 2) << 1) + 0x4000;
    W16(r4, r2);
    r2 += 26;
    r4 += 128;
    W16(r4, r2);
    return r4 - 126;
}

/* ---- the record rows (rec_asort / rec_disp: 0x0602EB34 .. 0x0602EE66, 0x0602EE68 ..) ---- */
#define PB(i) ((s32)RS8(0x06040040u + (i)))

static u32 tile16(u32 r4, u32 r5, u32 r6, u32 r7)       /* 0x0602EF2E */
{
    do {
        u32 r3 = r6, r1 = r5;
        do { W16(r1, r4); r4 += 2; r1 += 2; } while (--r3);
        r5 += 0x80;
    } while (--r7);
    return r5;
}

static u32 tile32(u32 r4, u32 r5, u32 r6, u32 r7)       /* 0x0602EF4A */
{
    do {
        u32 r3 = r6, r1 = r5;
        do { W32(r1, r4); r4 += 2; r1 += 4; } while (--r3);
        r5 += 0x100;
    } while (--r7);
    return r5;
}

static u32 put_char(u32 r4, u32 r2)                     /* 0x0602EE88: one name letter (0 = blank) */
{
    r2 &= 0xFF;
    if (ctx_r11 > 0) {
        if ((s32)r2 > 0) {
            r2 = ((r2 + 39) << 1) + 0x00200200u;
            W32(r4, r2);
            r4 += 0x100;
            r2 += 74; r2 += 74;
            W32(r4, r2);
            r4 -= 256;
        }
        return r4 + 4;
    }
    if ((s32)r2 > 0) {
        u32 r0 = (ctx_r14 >= 0) ? 0x6000u : 0x2000u;
        r2 = ((r2 + 39) << 1) + r0;
        W16(r4, r2);
        r4 += 128;
        r2 += 148;
        W16(r4, r2);
        r4 -= 128;
    }
    return r4 + 2;
}

u32 record_name_disp(u32 r4, u32 r5)                    /* 0x0602EE68: the three letters of a name word (bits 16..20, 8..12, 0..4) */
{
    r4 = put_char(r4, (r5 >> 16) & 0xFF);
    r4 = put_char(r4, (r5 >> 8) & 0xFF);
    r4 = put_char(r4, r5 & 0xFF);
    return r4;
}

static u32 disp_row(u32 r7, u32 r8, s32 r9, s32 r11, u32 r12)   /* 0x0602EC90: one ranking row at the cell position r8 (r7 -> the record, advanced by 8; r9 = the rank to show or -1; r12 = the row number) */
{
    s32 r10 = -1, r14;
    u32 r13 = 0, r4, r5, r3;
    u32 sr12 = r12;
    ctx_r11 = r11;
    if (PB(8) == 5) {
        u32 g = 0x002C0000u + (u32)(u16)(PB(11) - 4) * 0x3D00u;
        if (R32(r7) == R32(g + 4)) {
            u32 info = R8(r7 + 4);
            if ((s32)((info >> 4) & 3) == RS8(g + 20) && (s32)((info & 0x40) >> 6) == RS8(g + 21) && (s32)(info & 1) == RS8(g + 22)) {
                r10 = -1;
                if (RS8(0x060A8C39) < 0) {
                    W8(0x060A8C39, sr12);
                    r10 = (s32)sr12;
                }
            }
        }
    }
    r14 = r10;
    ctx_r14 = r14;
    r12 = 0;
    if (PB(8) == 4 && PB(9) == 4) {
        r12 = 2;
        if (PB(10) == 5) r13 = 2;
    }
    r4 = r8 + r12;
    if (r9 >= 0) {
        if (r9 >= 10) {
            r4 = putnum_rec(r4, 1);
            r9 -= 10;
            r4 = putnum_rec(r4, (u32)r9);
            r9 += 10;
        } else {
            r4 += 2;
            r4 = putnum_rec(r4, (u32)r9);
        }
    }
    r4 = r8 + r12;
    r5 = R32(r7); r7 += 4;
    r4 += 14;
    if (r11 > 0) r4 += 14;
    if (r13 > 0 && !(r5 & 0x00F00000u)) r4 += 2;
    time_disp(r4, r5, putnum_rec);
    r4 = r8 + r12 + 6;
    if (r11 > 0) r4 += 6;
    r5 = R32(r7); r7 += 4;
    record_name_disp(r4, r5);
    r5 >>= 24;
    r3 = ((r5 & 1) << 2) + ((r5 & 64) >> 3);
    r4 = r8 + r12 + r13 + 38;
    if (r11 <= 0) {
        u32 r2 = 0x2086u + r3;
        W16(r4, r2); r4 += 2; r2 += 2;
        W16(r4, r2); r4 += 126; r2 += 74; r2 += 72;
        W16(r4, r2); r4 += 2; r2 += 2;
        W16(r4, r2);
    } else {
        u32 r2 = 0x00200286u + r3;
        r4 += 38;
        W32(r4, r2); r4 += 4; r2 += 2;
        W32(r4, r2); r4 += 126; r4 += 126; r2 += 74; r2 += 72;
        W32(r4, r2); r4 += 4; r2 += 2;
        W32(r4, r2);
    }
    r5 >>= 4;
    r5 &= 3;
    {
        u32 dst = r8 + r12 + r13 + 30;
        u32 tile = (r11 > 0) ? 0x0030032Cu : 0x312Cu;
        if (r5 != 1) {
            tile += 12;
            if (r5 != 0) tile += 12;
        }
        if (r11 > 0) tile32(tile, dst + 30, 3, 2);
        else tile16(tile, dst, 3, 2);
    }
    return r7;
}

static u32 rank_loop(u32 r7, u32 r10)                   /* 0x0602EC3C: r10 rows from the record list r7 at 0x25E38782 downwards, equal times share the rank */
{
    u32 r8 = 0x25E38782, r12 = 1;
    s32 r9 = 1;
    W8(0x060A8C39, 0xFF);
    do {
        u32 r13 = R32(r7);
        r7 = disp_row(r7, r8, r9, 0, r12);
        r12++;
        r8 += 0x100;
        if (R32(r7) != r13) r9 = (s32)r12;
    } while (--r10);
    return r7;
}

void record_page_list(u32 r4, u32 r5)                   /* 0x0602EB34: the ranking page of type r4 (0 arcade course, 1 arcade car, 2 time attack lists, 3 / 4 / 5 the merged lists), r5 = the list table offset */
{
    u32 r7;
    switch (r4) {
    case 0:
        rank_loop(R32(0x06041000u + r5), 16);
        break;
    case 3:
        rank_loop(0x060A8CF0, 16);
        break;
    case 1:
        rank_loop(R32(0x06041010u + r5), 8);
        break;
    case 4:
        rank_loop(0x060A8CF0, 8);
        break;
    case 2:
    case 5:
        r7 = (r4 == 2) ? R32(0x06041020u + r5) : 0x060A8CF0;
        tile32(0x00200202, 0x25E32F6C, 11, 1);
        tile32(0x00200296, 0x25E3306C, 11, 1);
        tile32(0x00600218, 0x25E33364, 16, 1);
        tile32(0x006002AC, 0x25E33464, 16, 1);
        r7 = rank_loop(r7, 5);
        disp_row(r7, 0x25E33154, -1, 1, 6);
        break;
    default:
        break;
    }
}
