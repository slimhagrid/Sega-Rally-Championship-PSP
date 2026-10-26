/* M8: the record data code of the game: rec_dataset (0x0602C6C0), rec_connect (0x0602C9E8) and the ranking merges (rec_asort / rec_tsort, 0x0602E8AC .. 0x0602EB34).
 *
 * Record data layout (docs/menus.md section "Records and save data"):
 *   0x06041220 .. 0x06041E20  384 records of 8 bytes: u32 time (BCD 0x00MMSSCC, lower is better), u32 info
 *                              (bits 28..30 car class / mark, bit 24 flag, bits 16..20 / 8..12 / 0..4 the three name letters 0 = blank, 1.. = letters)
 *   0x06041000  rec_table   68 pointers to record lists (16 entries for the arcade lists, 8 for the time attack lists, 6 for the car lists), 4 per course block
 *   0x06041110  cmp_table   68 pointers to the comparison (best lap) tables that belong to the lists
 *   0x06042A38  the settings word block of the save (name table, mark flags)
 * The merges work on the pointer tables: a list is an array of (time, info) pairs; the result goes to 0x060A8CF0.. as 6 pairs with a second array (+48) of "source" tags. */
#include "satlib.h"
#include "menu_lib.h"
#include "menu_records.h"

/* rec_dataset helper (0x0602C9C0): n cumulative sums of u16 values from r4 into longs at r5 (BCD carry past 10 << 16), then a 0 long.  The original ends with `bra 0x0602C95A`,
 * i.e. it also ends the caller (rec_dataset): every case below calls it once and returns. */
static void sum_list(u32 r4, u32 r5, u32 n)             /* 0x0602C9C0 */
{
    u32 r2 = 0;
    s32 r3 = 10 << 16;
    do {
        r2 += R16(r4);
        r4 += 2;
        if ((s32)r2 >= r3) {
            r3 = 6 << 16;
            r2 += (u32)r3;
            r3 <<= 8;
        }
        W32(r5, r2);
        r5 += 4;
    } while (--n);
    W32(r5, 0);
}

RecPair record_connect(u32 r4)                          /* 0x0602C9E8: (r0, r1) = the record list and the comparison value of the current game mode / course / car */
{
    RecPair p;
    u32 t;
    switch (RS16(0x060AA860)) {
    case 0:
        t = (u32)((s32)RS8(0x060AA80C) - 1 + (s32)((u32)R8(0x060AA811) >> 1));
        t = 0x06041000u + (t << 2);
        p.r1 = R32(t + 0x110);
        p.r0 = R32(t);
        return p;
    case 2:
        t = 0x06041010u + ((u32)RS16(0x060AA87E) << 2);
        p.r1 = R32(t + 0x110);
        p.r0 = R32(t);
        return p;
    default:
        if (r4 != 1) {
            t = ((u32)RS16(0x060AA87E) << 4) + ((u32)RS8(0x060AA80A) << 2) + ((u32)RS8(0x060AA811) << 1) + (u32)RS8(0x060AA809);
            t = 0x06041020u + (t << 2);
            p.r1 = R32(t + 0x110);
            if (RS16(0x060AA860) == 6) p.r1 += 40;
            p.r0 = R32(t);
            return p;
        }
        record_sort_c(((u32)RS16(0x060AA87E) << 6) + 0x06041020u);
        p.r1 = R32(R32(0x060A8D20) + 0x110);
        if (RS16(0x060AA860) == 6) p.r1 += 40;
        p.r0 = 0x060A8CF0;
        return p;
    }
}

void record_dataset(void)                               /* 0x0602C6C0: the record data of the course / car / mode the race or the record screen works with -> 0x060B0500.. , 0x060AA538 .. */
{
    u32 r2 = 0x06042A38, r5, r10;
    RecPair rc;
    s32 mode;
    if (RS16(0x060AA860) == 0) W16(0x060AA87E, 0);
    if (RS8(0x060A8C17) != 1) {
        W32(0x060AAA5C, 0x002CF800);
        r5 = 0;
        if (R8(0x060AA803) & 4) r5 = 4;
        W8(0x060AA80F, R8(0x0602C7AC + (R8(0x06042A3F) & 15) + r5));
        r5 = 0;
        if (R8(0x060AA803) & 8) r5 = 4;
        W8(0x060AA814, R8(0x0602C7AC + ((R8(0x06042A3F) & 0xF0) >> 4) + r5));
        if (RS8(0x060AA811) != 1) {
            u32 v = R8(r2) + (R8(0x060AA80A) >> 1);
            W8(0x060AA817, v);
            W8(0x060AA81C, v);
        }
        W8(0x0604000C, R8(r2 + 9) << 5);
        W8(0x060AA828, R8(0x0602C7A4 + R8(r2 + 10 + (u32)RS16(0x060AA87E))));
        mode = RS16(0x060AA860);
        if (mode == 0) {
            W8(0x060AA828, 0);
            W16(0x060AA87E, 0);
            mode = 2;
        }
        if (mode == 2) {
            W8(0x060AA815, 0);
            W8(0x060AA80E, 0);
            if (R8(0x06042A3B) == 0) W8(0x060AA80E, 1);
            W8(0x060AA808, R8(r2 + 1));
            W8(0x060AA80C, (R8(r2 + 2) << 1) + 1);
            rc = record_connect(0);
            W32(0x060B0524, R32(rc.r0 + 0));
            W32(0x060B0528, R32(rc.r0 + 8));
            W32(0x060B052C, R32(rc.r0 + 16));
        } else if (mode == 6) {
            W8(0x060AA815, 0);
            W8(0x060AA80E, 0);
            rc = record_connect(0);
            W32(0x060B0530, R32(rc.r0 + 40));
            if (R8(0x06042A3D) == 0) W8(0x060AA80E, 2);
        } else if (mode == 4) {
            W8(0x060AA815, 0);
            W8(0x060AA80E, 0);
            if (R8(0x06042A3D) == 0) W8(0x060AA80E, 1);
            rc = record_connect(0);
            W32(0x060B0524, R32(rc.r0 + 0));
            W32(0x060B0528, R32(rc.r0 + 8));
            W32(0x060B052C, R32(rc.r0 + 16));
            W32(0x060B0530, R32(rc.r0 + 40));
            rc = record_connect(1);
            {
                u32 v = R32(rc.r0 + 0);
                W32(0x060B0570, v);
                W32(0x060AA184, v);
                W32(0x060AA188, v);
                W32(0x060AA18C, v);
            }
            r10 = 0x002C0000u + (u32)(u16)RS16(0x060AA87E) * 0x3D00u;
            W32(0x060AAA60, r10 + 104);
            if (R32(r10) != 0) {
                if (R8(0x06042A3C) == 0) W8(0x060AA815, 4);
                copy_l(r10 + 32, 0x060AA538, 18);
                W8(0x060AA81C, R8(r10 + 24));
                W8(0x060AA81D, R8(r10 + 25));
                W8(0x060AA81E, R8(r10 + 26));
                W8(0x060AA81F, R8(r10 + 27));
                W8(0x060AA820, R8(r10 + 28));
                W8(0x060AA813, R8(r10 + 20));
                W8(0x060AA816, R8(r10 + 21));
                W8(0x060AA812, R8(r10 + 22));
            }
            if (RS8(0x060AA810) == 2) {
                W8(0x060AA80E, 0);
                W8(0x060AA815, 0);
                W32(0x060AAA60, 0x002CF800);
            }
        } else {
            return;
        }
    }
    rc = record_connect(0);
    r5 = 0x060AA538;
    switch (RS16(0x060AA860)) {
    case 0:
        if (RS8(0x060AA80C) != 3) { sum_list(rc.r1 + 4, r5, 16); return; }
        sum_list(rc.r1 + 4, r5, 48);
        return;
    case 2:
        if (RS16(0x060AA87E) == 0) { sum_list(rc.r1 + 4, r5, 15); return; }
        sum_list(rc.r1 + 4, r5, 12);
        return;
    case 4:
        if (RS8(0x060AA815) == 4) return;
        sum_list(rc.r1 + 4, r5, 18);
        return;
    default:
        sum_list(rc.r1 + 4, r5 + 288, 6);
        return;
    }
}

/* ---- ranking merges (the lists are ascending in time; ties keep the earlier list) ---- */
static u32 merge16(u32 r4)                              /* 0x0602E8AC (record_menu): the 16 best of two lists (r4 -> two list pointers) -> 0x060A8CF0; returns the original's r5 (the end pointer of list B) */
{
    u32 a = R32(r4), b = R32(r4 + 4), out = 0x060A8CF0;
    s32 ka = RS32(a), kb = RS32(b);
    int n = 16;
    a += 4; b += 4;
    do {
        if (ka > kb) {
            W32(out, (u32)kb); out += 4;
            kb = RS32(b); b += 4;
            W32(out, (u32)kb); out += 4;
            kb = RS32(b); b += 4;
        } else {
            W32(out, (u32)ka); out += 4;
            ka = RS32(a); a += 4;
            W32(out, (u32)ka); out += 4;
            ka = RS32(a); a += 4;
        }
    } while (--n);
    return b;
}

u32 record_merge16(u32 r4) { return merge16(r4); }

static u32 merge3(u32 r4, u32 r1)                       /* 0x0602E968: the 6 best of the three lists whose pointers are at r4, r4+16, r4+32 -> r1 (6 pairs, tags at +48) */
{
    u32 t1 = r4, t2 = r4 + 16, t3 = r4 + 32;
    u32 l1 = R32(t1), l2 = R32(t2), l3 = R32(t3);
    u32 r8 = l1 + 40, r9 = l2 + 40, r10 = l3 + 40;
    u32 p1 = l1, p2 = l2, p3 = l3;
    s32 k1 = RS32(p1), k2 = RS32(p2), k3 = RS32(p3);
    u32 r0;
    p1 += 4; p2 += 4; p3 += 4;
    for (r0 = 6; r0 != 0; r0--) {
        int take;
        int third = (R8(0x06040062) & 4) != 0;           /* unlock bit 2: the third list takes part */
        if (k1 > k2 || (third && k1 > k3)) {
            if (!third) take = 2;
            else take = (k2 > k3) ? 3 : 2;
        } else {
            take = 1;
        }
        if (take == 1) {
            W32(r1, (u32)k1);
            W32(r1 + 48, t1);
            r1 += 4;
            k1 = RS32(p1); p1 += 4;
            W32(r1, (u32)k1);
            r1 += 4;
            k1 = RS32(p1); p1 += 4;
        } else if (take == 2) {
            W32(r1, (u32)k2);
            W32(r1 + 48, t2);
            r1 += 4;
            k2 = RS32(p2); p2 += 4;
            W32(r1, (u32)k2);
            r1 += 4;
            k2 = RS32(p2); p2 += 4;
        } else {
            W32(r1, (u32)k3);
            W32(r1 + 48, t3);
            r1 += 4;
            k3 = RS32(p3); p3 += 4;
            W32(r1, (u32)k3);
            r1 += 4;
            k3 = RS32(p3); p3 += 4;
        }
        if (r0 == 2) {
            p1 = r8; k1 = RS32(p1); p1 += 4;
            p2 = r9; k2 = RS32(p2); p2 += 4;
            p3 = r10; k3 = RS32(p3); p3 += 4;
        }
    }
    return (u32)k1;                                     /* the original leaves the current key of list 1 in r5 */
}

static u32 merge2(u32 la, u32 lb, u32 out)              /* 0x0602EA0E: the 6 best of two merged lists (la, lb: their addresses) -> out; returns the original's r5 (the end pointer of list B) */
{
    u32 r4 = la, r5 = lb, r7 = la + 40, r8 = lb + 40;
    s32 r2 = RS32(r4), r3 = RS32(r5);
    u32 r0;
    for (r0 = 6; r0 != 0; r0--) {
        if (r2 > r3) {
            W32(out, (u32)r3);
            W32(out + 48, R32(r5 + 48));
            r5 += 4; out += 4;
            r3 = RS32(r5); r5 += 4;
            W32(out, (u32)r3);
            out += 4;
            r3 = RS32(r5);
        } else {
            W32(out, (u32)r2);
            W32(out + 48, R32(r4 + 48));
            r4 += 4; out += 4;
            r2 = RS32(r4); r4 += 4;
            W32(out, (u32)r2);
            out += 4;
            r2 = RS32(r4);
        }
        if (r0 == 2) {
            r4 = r7; r2 = RS32(r4);
            r5 = r8; r3 = RS32(r5);
        }
    }
    return r5;
}

u32 record_merge3(u32 r4, u32 r5) { (void)r5; return merge3(r4, r5); }
u32 record_merge2(u32 r4, u32 r5, u32 r6) { return merge2(R32(r4), R32(r5), r6); }

static u32 merge_final(void)                            /* 0x0602EAF6 */
{
    return merge2(0x060A8DB0, 0x060A8E10, 0x060A8CF0);
}

u32 record_sort_a(u32 r4)                               /* 0x0602EA84: lists r4 / r4+8, r4+4 / r4+12 -> 0x060A8CF0 (returns the original's r5) */
{
    merge2(R32(r4), R32(r4 + 8), 0x060A8DB0);
    merge2(R32(r4 + 4), R32(r4 + 12), 0x060A8E10);
    return merge_final();
}

u32 record_sort_b(u32 r4, u32 r5)                       /* 0x0602EA62: 3-way merges of r4 and r4 + r5 -> 0x060A8CF0 */
{
    u32 r14 = r4 + r5;
    merge3(r4, 0x060A8DB0);
    merge3(r14, 0x060A8E10);
    return merge_final();
}

u32 record_sort_c(u32 r4)                               /* 0x0602EAAC: the 12 lists of a course block -> 0x060A8CF0 */
{
    u32 r14 = r4;
    merge3(r14, 0x060A8CF0);
    merge3(r14 + 8, 0x060A8D50);
    merge2(0x060A8CF0, 0x060A8D50, 0x060A8DB0);
    r14 += 4;
    merge3(r14, 0x060A8CF0);
    merge3(r14 + 8, 0x060A8D50);
    merge2(0x060A8CF0, 0x060A8D50, 0x060A8E10);
    return merge_final();
}
