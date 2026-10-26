/* M8: the photo module that follows a won practice race (module photo 0x0600E6A4 .. 0x0600ECF0 with photoctrl 0x0600ECF0 .. 0x0600EE88), ported from the original and checked tick for
 * tick against it (tools/scn_photo.py, scenarios photo_*).
 *
 * What it is: the staff roll.  A fly-by of the course the player just won (five camera shots, the shot table PHOTOTBL.DAT) with the staff text (STAFF.CG / STAFF.MAP, 11 pages) over it.
 * The scene is the race engine's: the course data of AD<course>.DAT (sky bitmap on RBG0, the road block grid and polygon lists), the course model of <COURSE>.MAD + OBJECT.MAD textures
 * in VDP1 VRAM, the scenery objects of EVENTTBL.MAD.  No car is placed: all car control slots (0x060B0000 ..) are cleared, the camera control function [0x060AA9B0] is the module's own
 * 0x0600EBC8 and the scroll control [0x060AA9F0] is `normal_endmove`.  The module ends by setting the main vector to 0x0602FFD0 (the post-goal dispatcher's continuation after a
 * ceremony: menu_post.c post_goal_ceremony -> the name entry / ranking screen).
 *
 * Flow: photo_init 0x0600E6A4 (loads, sets the scene up, vector 0x0600E8F4), photo_main 0x0600E8F4 (a blocking loop like the other screens: state handler [0x060AA807] -> sound ->
 * fade -> staff roll -> intm_sync(38)).  States (a byte offset into the table at 0x0600E950): 0 run (0x0600E974: one scene frame, START = leave), 8 start of the fade out (0x0600EA20),
 * 10 wait for the fade (0x0600EA40, sets bit 1 of [0x060AA803]); 2 / 4 / 6 (0x0600E9C8 / E9E0 / EA08) are the development build's pause: nothing in the shipped program enters them
 * (the only writers of [0x060AA807] are in this module and set 0, 8 or 10), they are not ported (menu_unported).
 *
 * The 3D pipeline is not ported: the matrix calls, the course drawing (draw_course 0x060187C0 with the block list [0x060AA104] / [0x060AA140], count [0x060AA000]) and the scenery
 * objects are the host's (the camera of the scene is the block 0x060AA9B0: eye position +8 / +12 / +16 .. see photo_camera).  What does matter to the memory is ported: the shot
 * table walk (photoctrl), the camera interpolation (photoExec), the road block search of the camera position (get_block / calc_dispang2 / get_poly_map) and the scroll control. */
#include "satlib.h"
#include "menu_lib.h"
#include "satmenu.h"
#include "menu_photo.h"

#define FLAGS 0x060AA803u
#define PHOTO 0x060B2800u               /* photoctrl's work block (r13 of the module) */
#define CAMW  0x060AA9B0u               /* the camera control work: +0 function, +8 / +16 eye x / z, +22 yaw word, +24 .. */
#define CARB_DUMMY 0x060B2600u          /* the "car" block the road search of the camera works on (gbr of get_block) */

/* ---- the PHOTOTBL.DAT base: the CD file at 0x00248000, otherwise the cartridge image at 0x02F5F000 ---- */
static u32 tbl_base(void) { return (R8(FLAGS) & 0x10) ? 0x00248000u : 0x02F5F000u; }

/* ---- _bpe_expand (0x06015870): byte-pair expansion of one block to VDP2 / CRAM, 16-bit units (+ r6) ---- */
static u32 bpe_expand(u32 r4, u32 r5, u32 r6)           /* 0x06015870 */
{
    const u32 r8 = 0x060A7E00, r9 = 0x060A7F00;
    u8 stk[512];
    u32 r5_start = r5, r14 = 0, r11, r1, r7, r13, r2, r3;
    int i;
    r11 = R32(r4);
    r4 += 4;
    r11 += 1;
    for (;;) {
        r1 = R8(r4); r4++;
        r11--;
        if (r11 == 0) break;
        for (i = 0; i < 256; i++) W8(r8 + i, i);
        r7 = 0;
        r13 = 0x100;
        for (;;) {                                      /* the pair table: runs of literal entries */
            if (r1 > 127) { r7 += r1; r7 += (u32)-127; r1 = 0; }
            if (r7 == r13) break;
            for (r2 = 0; !((s32)r2 > (s32)r1); r2++, r7++) {
                u32 r12 = R8(r4); r4++;
                r11--;
                W8(r8 + r7, r12);
                if (r12 != r7) { r12 = R8(r4); r4++; r11--; W8(r9 + r7, r12); }
            }
            if (r13 == r7) break;
            r1 = R8(r4); r4++;
            r11--;
        }
        r3 = (R8(r4) << 8) | R8(r4 + 1);
        r4 += 2;
        r11 -= 2;
        r3 &= 0xFFFF;
        r2 = 0;
        for (;;) {                                      /* the packed bytes of the block */
            if (r2 != 0) { r2--; r7 = stk[r2]; }
            else {
                if (r3 == 0) break;
                r3--;
                r7 = R8(r4); r4++;
                r11--;
            }
            r7 &= 0xFF;
            r13 = R8(r8 + r7);
            if (r13 == r7) {
                if (r5 & 1) {
                    r14 |= r13;
                    r5--;
                    r14 += r6;
                    W16(r5, r14);
                    r5 += 2;
                } else {
                    r14 = r13;
                    r5++;
                    r14 <<= 8;
                }
            } else {
                stk[r2] = R8(r9 + r7);
                stk[r2 + 1] = R8(r8 + r7);
                r2 += 2;
            }
        }
    }
    if (r5 & 1) {
        r5--;
        r14 += r6;
        W16(r5, r14);
        r5 += 2;
    }
    return r5 - r5_start;
}

/* bpeset (0x06015978): r4 = four table indices (one per byte, 0 = none); each is a 12-byte entry (source, destination, add) of the table at 0x00299784 */
static void bpeset(u32 r4)
{
    int k;
    for (k = 0; k < 4; k++, r4 >>= 8) {
        u32 i = r4 & 0xFF, e;
        if (i == 0) continue;
        e = 0x00299784u + i * 12;
        bpe_expand(R32(e), R32(e + 4), R32(e + 8));
    }
}

/* ---- the staff roll (module ending: set_staff_cg 0x0601F32A, staff_act_init 0x0601F3F0, staff_act 0x0601F404, 0x0601F42A / F460 / F484) ----
 * STAFF.MAP -> 0x002BE000 (a table of page pointers, the glyph cell lists at 0x002BE450), STAFF.CG -> VDP2 VRAM; the palette words go to the colour RAM shadow.  The page counter lives in
 * 0x060D01C0 (+0 page, +4 frames left, +8 frames per page); the text is drawn into the 16-bit cell plane 0x25E3C000 (64 cells per row; the first 28 rows are cleared per page). */
#define STAFF 0x060D01C0u

static void set_staff_cg(void)                          /* 0x0601F32A */
{
    if (R8(FLAGS) & 0x10) sat_cd_request(0x0601F378);  /* STAFF.MAP, STAFF.CG */
    else {
        menu_unported(0x0601F334);                      /* the cartridge image path (not on the disc) */
        return;
    }
    rcopy(0x002BE864, 0x060B8200, 128);                 /* 0x0601D694: long copies */
    rcopy(0x002BEA64, 0x060B8400, 32);
}

static u32 staff_glyph(u32 r4, u32 r5, u32 r6)          /* 0x0601F484: one glyph (the cell list r4 of the table at 0x002BE450) to the plane at r5, cell code offset r6; returns twice its width */
{
    s32 w, h, c;
    u32 q;
    w = R16(r4); h = R16(r4 + 2);
    r4 += 4;
    for (; h > 0; h--) {
        q = r5;
        for (c = w; c > 0; c--) { W16(q, R16(r4) + r6); r4 += 2; q += 2; }
        r5 += 0x80;
    }
    return (u32)w << 1;
}

static u32 staff_line(u32 r4, u32 r5)                   /* 0x0601F460: a line of text: u16 offset, u16 cell code offset, then the character bytes (ended by 0); returns the address behind it */
{
    u32 r8, r9, r6;
    r5 += R16(r4); r4 += 2;
    r6 = R16(r4); r4 += 2;
    r8 = r4;
    r9 = r5;
    for (;;) {
        u32 ch = R8(r8);
        r8++;
        if (ch == 0) break;
        ch = (ch - 32);
        r9 += staff_glyph(R32(0x002BE450 + (ch << 2)), r9, r6);
    }
    return r8;
}

static void staff_page(void)                            /* 0x0601F42A: clear the plane (0x380 longs below 0x25E3CE00) and draw the lines of the page [STAFF] */
{
    u32 r1 = 0x25E3CE00, r4;
    int n;
    for (n = 0x380; n > 0; n--) { r1 -= 4; W32(r1, 0); }
    r4 = R32(0x002BE000 + (R32(STAFF) << 2));
    while (RS16(r4) != -1) {
        r4 = staff_line(r4, 0x25E3C000);
        if (r4 & 1) r4++;
    }
}

static void staff_act_init(u32 frames)                  /* 0x0601F3F0: 11 pages over `frames` passes */
{
    s32 per = (s32)(((s64)(s32)0x1745D174 * (s64)(s32)frames) >> 32);
    W32(STAFF + 8, (u32)per);
    W32(STAFF + 4, (u32)per);
    W32(STAFF, 0);
    staff_page();
}

static void staff_act(void)                             /* 0x0601F404 */
{
    s32 left = RS32(STAFF + 4) - 1;
    W32(STAFF + 4, (u32)left);
    if (!(left > 0)) {
        W32(STAFF + 4, R32(STAFF + 8));
        W32(STAFF, R32(STAFF) + 1);
        staff_page();
    }
}

/* ---- the course data and textures (module init: crs_adv_dataset 0x060047C4, set_game_texture 0x0601AB6C with set_car_texture / set_course_model / 0x0601AD14) ---- */
void crs_adv_dataset(void)                       /* 0x060047C4: AD<course>.DAT (sky bitmap, road block grid and polygon lists) */
{
    sat_load_name_at(R32(0x060047D4 + (u32)RS16(0x060AA882)));
}

static void set_car_texture(void)                       /* 0x0601ABA2: the object textures (OBJECT.MAD) */
{
    u32 old = R32(0x060D008C);
    W32(0x060D008C, 0x4F424A20);
    if (old == 0x4F424A20) return;
    if (!(R8(FLAGS) & 0x10)) { menu_unported(0x0601ABB6); return; }
    sat_cd_request(0x0601ABFC);
    W32(0x060D0048, 0x30002100);
    setup_texture(0x0601AF8C, 0xFDEE0000);
}

static void set_course_model(void)                      /* 0x0601A8F8 */
{
    u32 r2 = R32(0x0601A970 + (u32)RS16(0x060AA882));
    if (r2 == R32(0x060D0090)) return;
    W32(0x060D0090, r2);
    if (!(R8(FLAGS) & 0x10)) { menu_unported(0x0601A912); return; }
    sat_cd_request(R32(0x0601A998 + (u32)RS16(0x060AA882)));
}

static void load_animal2(void)                          /* 0x0601AD14 (the desert only) */
{
    if (RS16(0x060AA87E) != 0) return;
    if (!(R8(FLAGS) & 0x10)) { menu_unported(0x0601AD24); return; }
    sat_cd_request(0x0601AD68);                         /* ANIMAL2.MAD */
    W32(0x060D0048, 0xF8002EC0);
    setup_texture(0x0601AFFC, 0xFDE68000);
}

void set_game_texture(void)                      /* 0x0601AB6C */
{
    if (RS16(0x060AA860) == 10) { menu_unported(0x0601AB8E); return; }      /* the 2P course models */
    set_car_texture();
    set_course_model();
    load_animal2();
}

/* ---- the scenery objects (module event: 0x0601BC94 loads and sets up; the per-frame dispatcher 0x0601BCB8 is the host's, see the header) ---- */
static void event_load(void)                            /* 0x0601BF80 */
{
    if (R8(FLAGS) & 0x10) sat_cd_request(0x0601BFD0);  /* EVENTTBL.MAD: the object tables (0x00200000), the animation data (0x002B0000) */
    else menu_unported(0x0601BF88);
}

static void event_setup(u32 tbl, u32 groups, u32 pool)  /* 0x0601BF18: every record of the 128 groups becomes a 64-byte slot, the group table gets (offset, count) */
{
    u32 r8 = tbl, r10 = groups, r11 = 0, r13 = 1;
    int blk;
    for (blk = 0; blk < 128; blk++) {
        u32 p = R32(r8), n;
        r8 += 4;
        n = R32(p);
        p += 4;
        W16(r10, r11); r10 += 2;
        W16(r10, n); r10 += 2;
        while (n) {
            u32 s = pool + r11, fn;
            p += 2;                                     /* the running id */
            fn = R32(0x0601BFEC + ((u32)RS16(p) << 2));
            p += 2;
            W32(s, fn);
            W16(s + 12, r13 & 1);
            r13++;
            W32(s + 16, R32(p)); p += 4;
            W32(s + 36, R32(p)); p += 4;
            W32(s + 20, R32(p)); p += 4;
            W32(s + 24, R32(p)); p += 4;
            W32(s + 28, R32(p)); p += 4;
            r11 += 0x40;
            n--;
        }
    }
}

static void event_clear(void)                           /* 0x0601DC14 */
{
    u32 a;
    int k;
    for (k = 0; k < 0x80; k++) W32(0x060EFE00 + 4 * k, 0);
    for (a = 0x060D2000, k = 0; k < 32; k++, a += 16) W32(a, 0);
    W32(0x060EFE00, 0);
    if (RS16(0x060AA87E) != 0) return;
    if (RS16(0x060AA860) == 0) { W32(0x060EFE00, 0x0601DC6C); W32(0x060EFE00 + 112, 0x0601E2F4); }
    else if (RS16(0x060AA860) == 2) { W32(0x060EFE00, 0x0601DC6C); W32(0x060EFE00 + 112, 0x0601E308); }
}

void event_init(u32 course)                      /* 0x0601BC94 (r4 = the course word: only handed on) */
{
    (void)course;
    event_load();
    event_setup(R32(0x00200000u + (u32)RS16(0x060AA882)), 0x060D0C00, 0x060E4000);
    event_clear();
}

/* ---- the 3D pipeline's per-frame bookkeeping the memory sees (sprite module / mkpoly) ---- */
static void m_mkpoly_init(void)                         /* 0x06018418: the master work area tables (0x060D1400), the shade code patch and the car display tables (0x060D01E0) */
{
    int i;
    for (i = 0; i < 48; i++) W32(0x060D1400 + 4 * i, R32(0x06018440 + 4 * i));
    for (i = 0; i < 3; i++) W32(0x060D01E0 + 4 * i, R32(0x060101DC + 4 * i));         /* dispcar_init 0x060101B8 */
}

static void set_slave_exec_init(void)                   /* the slave job 0x0600E78C: the slave's work area tables, the light */
{
    s_mkpoly_init();
}

/* ---- the camera control (photoctrl: photoctrlinit 0x0600ECF0, the shot walk 0x0600EE30 / 0x0600EE00, the interpolation photoExec 0x0600ED08) ---- */
static void photo_shot(void)                            /* 0x0600EE30: [PHOTO + 8] = the shot's entry (duration, key record index), [PHOTO + 12] = its key record */
{
    u32 r12 = tbl_base();
    u32 r0 = (u32)RS16(0x060AA882) * 2 + r12;
    u32 r1 = R32(r0) + r12, r2 = R32(r0 + 4) + r12;
    r2 += (u32)RS16(PHOTO + 4) << 2;
    W32(PHOTO + 8, r2);
    r1 += (u32)RS16(r2 + 2) << 2;
    W32(PHOTO + 12, r1);
}

static void photoctrlinit(void)                         /* 0x0600ECF0 */
{
    W32(PHOTO, 0);
    W16(PHOTO + 4, 0);
    W16(PHOTO + 6, 0);
    W32(0x060AA9AC, PHOTO);
    photo_shot();
}

static void photo_frm_ctrl(void)                        /* 0x0600EE00: one pass of the shot clock */
{
    s32 r7 = RS32(PHOTO) + 1;
    W32(PHOTO, (u32)r7);
    W16(PHOTO + 6, RS16(PHOTO + 6) + 1);
    if (RS16(R32(PHOTO + 8)) > r7) { photo_shot(); return; }       /* the shot goes on */
    W32(0x060B9044, R32(0x060AA9F8));                   /* clr_hscr_rr 0x06015FBC */
    {
        s32 r0 = RS16(PHOTO + 4) + 1;
        if (r0 > 4) { W8(0x060AA807, 8); return; }      /* the fifth shot is over: fade out */
        W16(PHOTO + 4, r0);
        W32(PHOTO + 24, 0);
    }
    photo_shot();
}

static void photo_exec(void)                            /* 0x0600ED08: the camera of the shot: a key record (position x y z, pitch, yaw) or the interpolation of two (type 0), fed to the matrix and [CAMW] */
{
    u32 r12 = tbl_base();
    u32 r9 = R32(r12 + 32) + r12, r10 = r9, r8 = R32(PHOTO + 12);
    s32 yaw;
    if (R8(r8) == 2) {                                  /* the key record's type byte indexes a table of two jumps: 0 = a fixed key (below), 2 = the interpolation */
        s32 r7;
        u32 r5 = PHOTO + 28;
        int k;
        r9 += (u32)R8(r8 + 1) << 4;
        W32(PHOTO + 16, r9);
        r10 += (u32)R8(r8 + 2) << 4;
        W32(PHOTO + 20, r10);
        r7 = RS32(PHOTO + 24) + 0x83;
        if (!(0x10000 >= r7)) r7 = 0x10000;
        W32(PHOTO + 24, (u32)r7);
        for (k = 0; k < 3; k++, r9 += 4, r10 += 4, r5 += 4) {
            s32 a = RS32(r9), b = RS32(r10);
            s64 prod = (s64)(s32)((u32)b - (u32)a) * (s64)r7;
            W32(r5, (u32)(a + (s32)(u32)(prod >> 16)));
        }
        for (k = 0; k < 2; k++, r9 += 2, r10 += 2, r5 += 4) {
            s32 a = RS16(r9), b = RS16(r10);
            s64 prod = (s64)(s32)((u32)b - (u32)a) * (s64)r7;
            W32(r5, (u32)(a + (s32)(u32)(prod >> 16)));
        }
    } else {                                            /* type 0: a fixed key record */
        if (R8(r8) != 0) menu_unported(0x0600ED22);
        r9 += (u32)R8(r8 + 1) << 4;
        W32(PHOTO + 16, r9);
        W32(PHOTO + 20, 0xFFFFFFFFu);
        W32(PHOTO + 28, R32(r9));
        W32(PHOTO + 32, R32(r9 + 4));
        W32(PHOTO + 36, R32(r9 + 8));
        W32(PHOTO + 40, (u32)RS16(r9 + 12));
        W32(PHOTO + 44, (u32)RS16(r9 + 14));
    }
    yaw = RS32(PHOTO + 44);
    W16(CAMW + 22, (u32)(-yaw));                        /* rotate_x(-pitch), rotate_y(-yaw) on the matrix; the word is the yaw the scroll control uses */
    W32(CAMW + 8, R32(PHOTO + 28));
    W32(CAMW + 16, R32(PHOTO + 36));                    /* translate2(-x, -y, -z) */
}

/* ---- the road block search of the camera position (gctrl / car module: get_block 0x06007EAC with 0x06007F3C, calc_dispang2 0x06007D68, atan 0x0601B7C0, get_poly_map 0x060081B8) ---- */
static u32 atan_ang(s32 r4, s32 r5)                     /* 0x0601B7C0: the angle (65536 = one turn) of the vector (r4, r5) from the table of 0x060A55A0 */
{
    u32 idx = 0, r2, quo;
    s32 t;
    if ((r4 | r5) == 0) return 0;
    if (r4 < 0) { r4 = -r4; idx = 8; }
    if (r5 < 0) { r5 = -r5; idx += 4; }
    if (!(r4 >= r5)) { t = r5; r5 = r4; r4 = t; idx += 2; }
    quo = (u32)(((s64)r5 << 16) / (s64)r4);             /* the on-chip divider: (r5 << 16) / r4 */
    r2 = (u32)(s32)RS16(0x060A55A0 + ((quo >> 6) & 0xFFFFFFFEu));
    switch (idx) {
    case 0: return r2;
    case 2: return 0x4000 - r2;
    case 4: return -r2;
    case 6: return r2 - 0x4000;
    case 8: return 0x8000 - r2;
    case 10: return 0x4000 + r2;
    case 12: return r2 - 0x8000;
    default: return 0xFFFFC000u - r2;
    }
}

static u32 find_record(u32 p, s32 x, s32 z, u32 base, u32 size)        /* 0x06007F3C: the list at *p (count, indices): the first record whose four corner edges all have the point on the same side */
{
    u32 list = R32(p);
    s32 cnt = RS8(list);
    u32 r0 = (u32)RS8(list + 1) & 0xFF;
    list += 2;
    for (;;) {
        u32 rec = base + (r0 & 0xFF) * size;
        s32 px[4], pz[4], c[4];
        int k, neg = 0, allneg = 1;
        for (k = 0; k < 4; k++) { px[k] = RS32(rec + 4 + 8 * k) - x; pz[k] = RS32(rec + 8 + 8 * k) - z; }
        for (k = 0; k < 4; k++) {
            int n = (k + 1) & 3;
            s64 sum = (s64)px[k] * (s64)pz[n] + (s64)(-px[n]) * (s64)pz[k];
            c[k] = (s32)(sum >> 32);
            if (c[k] < 0) neg = 1; else allneg = 0;
        }
        if (!neg) return rec;
        if (allneg) menu_unported(0x06007FD8);          /* trapa #9: a point outside the polygon with the wrong winding */
        cnt--;
        r0 = (u32)RS8(list) & 0xFF;
        list++;
        if (cnt == 0) { menu_unported(0x06007FF4); return 0; }          /* trapa #9: the point is in none of the listed polygons */
    }
}

void get_block(u32 CARB)                                /* 0x06007EAC (gbr = the car block CARB: the camera's dummy car 0x060B2600 here, the title demo's cars in menu_adv.c) */
{
    s32 x = RS32(CARB + 24), z = RS32(CARB + 32);
    u32 r2 = ((u32)x + 0x04000000u) >> 16, r1, r6, rec, id, r0;
    r2 >>= 4;
    r2 >>= 1;
    r1 = (0x04000000u - (u32)z) >> 16;
    r1 &= 0xFFFFFFE0u;
    r1 <<= 1;
    r1 += r2;
    r1 <<= 2;
    r6 = 0x00244000u + r1;
    rec = find_record(r6, x, z, 0x00290000u, 36);
    r0 = (u32)RS8(rec + 0);
    if (RS16(0x060AA860) == 10) r0 = (u32)RS8(rec + 2);
    r0 |= (u32)RS8(rec + 1) << 4;
    W8(CARB + 241, r0);
    W8(CARB + 183, R8(CARB + 240));
    id = (u32)RS8(rec + 3);
    W8(CARB + 240, id);
    rec = find_record(0x00296000u + (id << 2), x, z, 0x00293000u, 44);
    W16(CARB + 98, R16(CARB + 96));
    W32(CARB + 184, R32(rec + 36));
    W32(CARB + 188, R32(rec + 40));
    W8(CARB + 196, R8(rec + 0));
    W8(CARB + 247, R8(rec + 1));
    W16(CARB + 96, (u32)RS8(rec + 3) & 0xFF);
}

void calc_dispang2(u32 CARB)                            /* 0x06007D68 (and calc_dispang 0x06007D2C: the same, gbr = the car block) */
{
    s32 r0 = (s32)atan_ang(RS32(CARB + 184), RS32(CARB + 188));
    r0 = (s16)(r0 - 0x4000);
    r0 += RS16(0x060AA9C6);
    r0 += RS16(0x060AA9CC);
    r0 = -(s32)(s16)r0;
    r0 <<= 4;
    r0 = (s32)(((u32)r0 << 16) | ((u32)r0 >> 16));
    W8(CARB + 199, (u32)r0 & 15);
}

void get_poly_map(u32 CARB)                             /* 0x060081B8: the blocks around the camera's block: polygon list pointers [0x060AA104 ..], block numbers [0x060AA140 ..], count [0x060AA000] / [0x060AA002] */
{
    u32 r1 = (u32)((s32)RS8(CARB + 240) << 2);
    u32 r9 = 0x060AA104, r13 = 0x060AA140, r5, r0, r7, r10;
    s32 r6 = 0;
    r5 = R32(0x0600821C + ((u32)((s32)RS8(CARB + 199)) << 2));
    r0 = (u32)RS8(r5); r5++;
    {
        u32 t = (u32)RS8(CARB + 241);
        if (r0 != 0) t = (t >> 2) >> 2;
        r7 = t & 15;
    }
    r0 = (u32)RS8(r5); r5++;
    r10 = 0;
    for (;;) {
        u32 r3;
        r0 += r1;
        r0 &= 0x1FC;
        r3 = R32(0x06048000u + r0);
        r0 >>= 2;
        r10 += R32(r3) & 0xFFFF;
        if ((s32)r10 > 0x5DC) break;
        W32(r9, r3);
        W8(r13, r0);
        r0 = (u32)RS8(r5); r5++;
        r6++;
        if ((s32)r0 == -1) break;
        r13++;
        r7--;
        r9 += 4;
        if (r7 == 0) break;
    }
    W16(0x060AA002, (u32)r6);
    W16(0x060AA000, (u32)r6);
}

/* the camera control function [CAMW] = 0x0600EBC8: unit matrix, photo_exec, the road search at the eye position (the "car" block gets x and z), the matrix copy for the slave */
static void photo_camera(void)                          /* 0x0600EBC8 */
{
    photo_exec();
    W32(CARB_DUMMY + 24, R32(CAMW + 8));
    W32(CARB_DUMMY + 32, R32(CAMW + 16));
    get_block(CARB_DUMMY);
    calc_dispang2(CARB_DUMMY);
    get_poly_map(CARB_DUMMY);
}

/* ---- the scroll control [0x060AA9F0] = normal_endmove 0x06006BD4 -> 0x06015FD4 / set_grpara 0x06015F6C ---- */
void normal_endmove(void)                        /* 0x06006BD4 */
{
    u32 r14 = 0x060AA9F0, k;
    for (k = 0; k < 7; k++) W32(r14 + 4 + 4 * k, R32(0x060AAAE4 + 4 * k));      /* rcopy 0x06005674 */
    W32(0x060B9044, R32(0x060AA9F8));
    r14 = 0x060AAAE0;
    {                                                   /* set_grpara 0x06015F6C: the road-follow scroll values from the camera yaw */
        s32 mir = RS8(0x060AA82E), r0, r1, r5, r6;
        u32 yaw = (u32)RS16(CAMW + 22) & 0xFFFF;
        if (mir != 0) yaw = (u32)(-(s32)yaw);
        r1 = (s32)(0xFFFFFFC0u & yaw);
        r1 = (s32)((u32)r1 << 10);
        r1 += RS32(r14 + 24);
        r1 &= 0x03FF0000;
        r0 = RS16(CAMW + 24);
        if (mir != 0) r0 = -r0;
        W32(r14 + 8, (u32)r1);
        if (r0 == 0) { r6 = 0x10000; r5 = 0; }
        else {
            u32 idx;
            r0 >>= 1;
            r5 = -r0;
            idx = ((u32)r5 >> 2) & 0x3FFC;
            r5 = (s32)((u32)(s32)RS16(0x060A15A0 + idx) << 2);
            r6 = (s32)((u32)(s32)RS16(0x060A15A0 + idx + 2) << 2);
            r0 = -r5;
        }
        W32(r14 + 12, (u32)r6);
        W32(r14 + 16, (u32)r0);
        W32(r14 + 20, (u32)r5);
    }
}

PhotoView photo_view;

static void photo_view_fill(void)                       /* what the pipeline would draw: the eye of the shot and the blocks get_poly_map chose (the host draws them) */
{
    int k, n = RS16(0x060AA000);
    photo_view.tick = sat_ticks ? sat_ticks : 1;
    photo_view.course = RS16(0x060AA87E) & 3;
    for (k = 0; k < 3; k++) photo_view.pos[k] = RS32(PHOTO + 28 + 4 * k);
    photo_view.pitch = RS32(PHOTO + 40);
    photo_view.yaw = RS32(PHOTO + 44);
    if (n > 16) n = 16;
    if (n < 0) n = 0;
    photo_view.nparts = n;
    photo_view.have_mat = 0;
    for (k = 0; k < n; k++) photo_view.parts[k] = R8(0x060AA140 + (u32)k);
}

/* ---- one scene frame (0x0600EAF8): the camera, the slave jobs (nothing: no car slots), the object dispatch, the course, the scroll control ---- */
static void photo_frame(void)                           /* 0x0600EAF8 */
{
    /* the car control slots (slave 0x06005F18, master 0x06005EF8) are empty: allclr_car_ctrl_work cleared them */
    photo_camera();                                     /* camera_ctrl 0x06005E20: [CAMW] = 0x0600EBC8 */
    s_mkpoly_frame_init();                              /* slave job 0x0601864A, then the master's 0x06018634 */
    m_mkpoly_frame_init();
    /* the car display slots (0x06005F70 / 0x06005F50) and p2sync_ctrl 0x060067A4 (slot +8 of 0x060B0600 is not 4) do nothing */
    photo_view_fill();
    W8(0x060D0114, RS8(0x060B00C5) == 0);               /* 0x0601BCB8 -> 0x0601BD3E: the object dispatcher's side flag; the objects' state functions are the host's (see the header) */
    /* 0x0601D410: the spray list [0x060D0140] is empty; draw_course 0x060187C0 (pipeline) */
    normal_endmove();                                   /* rscroll_ctrl 0x06005E54 */
    W16(0x060D0040, 0);                                 /* wait_slave_end 0x06017BAC leaves 0 in r0: the store of the epilogue (the word at 0x060D0034 + 12) */
}

void photo_stats(void)                           /* 0x0601D756: the pipeline's counters of the master / slave halves */
{
    u32 m = 0x060D1400, s = 0x060D1C00, r1 = 0x060D0034;
    u32 a = R16(m + 170), b = R16(m + 134), c = R16(m + 168);
    W16(r1 + 4, a);
    W16(r1 + 6, b);
    W16(r1 + 8, R16(s + 170));
    W16(r1 + 0, R16(s + 170) + a);
    W16(r1 + 10, R16(s + 134));
    W16(r1 + 2, R16(s + 134) + b);
    W16(r1 + 12, R16(s + 168) + c);
}

/* ---- fade_color_init (0x0600585C): the fade presets, 10 words each at 0x0600588C + 20 * n ---- */
void fade_color_init(u32 n)
{
    u32 t = 0x0600588C + n * 20;
    fade_inout_init(RS16(t), RS16(t + 2), RS16(t + 4), RS16(t + 6), RS16(t + 8), RS16(t + 10), RS16(t + 12), RS16(t + 14), RS16(t + 16), RS16(t + 18));
}

/* ---- the module ---- */
void photo_scene_vram(void)                      /* 0x0600EC38 */
{
    rfillff(0x25E38000, 0, 0x1000);
    rfillff(0x25E3A000, 0, 0x800);                      /* vram_scrmap_b_clr */
    bpeset(22);                                         /* set_tasc_cg 0x06004B74 */
    rfillff(0x25E00040, 0, 16);
    rfillff(0x25E18040, 0, 16);
    rfillff(0x25E20040, 0, 16);
    W16(0x060B8002, 0);                                 /* set_tasc_cl 0x06004BC0 */
    W16(0x060B800A, 0x7FFF);
    init_nscr_vol();
    /* init_gscroll 0x06015B80 */
    W16(0x25F80034, 0x8203);
    W16(0x25F80038, 0xC00C);
    W16(0x25F800F0, 0x202); W16(0x25F800F2, 0x202); W16(0x25F800F4, 0x202); W16(0x25F800F6, 0x202);
    W16(0x25F800F8, 0x604); W16(0x25F800FA, 0x606); W16(0x25F800FC, 2);
    {
        u32 r13 = 0x25F800C0;
        if (R8(0x060AA810) == 2) menu_unported(0x06015BB8);         /* init_replay_win */
        else {
            u32 r1 = 0x06015D90 + ((u32)RS16(0x060AA860) << 2);
            W16(r13 + 0, R16(r1)); W16(r13 + 2, R16(r1 + 2)); W16(r13 + 4, R16(r1 + 4)); W16(r13 + 6, R16(r1 + 6));
            W16(r13 + 8, 0x100); W16(r13 + 10, 72); W16(r13 + 12, 0x1B0); W16(r13 + 14, 88);
        }
    }
    if (RS16(0x060AA860) == 10) {
        menu_unported(0x06015C32);                      /* the 2P screen set-up */
    } else {
        u32 r1 = (u32)RS16(0x060AA882), k;
        W16(0x25F800D2, 136);
        for (k = 0; k < 24; k++) W32(0x060B9000 + 4 * k, R32(0x0029819C + 4 * k));
        {
            u32 r2 = R32(0x06015DC0 + r1), r4 = R32(0x06015DD0 + r1);
            W32(0x060B9044, r2);
            W32(0x060B9048, r4);
            W32(0x060AA9F8, r2);
            W32(0x060AAA08, r2);
            W32(0x060AAAE8, r2);
            W32(0x060AAAF8, r2);
        }
        W32(0x25FE0040, 0x060B9000);
        W32(0x25FE0044, 0x25E7FF00);
        W32(0x25FE0048, 96);
        W32(0x25FE004C, 0x101);
        W32(0x25FE0054, 7);
        W32(0x25FE0050, 0x100);
        W32(0x25FE0050, 0x101);
    }
    W16(0x25F8000E, (R16(0x25F8000E) & 0x3300) | 0xE0);
    /* the SCU DMA that copies the colour RAM shadow */
    W32(0x25FE0020, 0x060B8000);
    W32(0x25FE0024, 0x25F00000);
    W32(0x25FE0028, 0);
    W32(0x25FE002C, 0x101);
    W32(0x25FE0034, 7);
    W32(0x25FE0030, 0x100);
    W32(0x25FE0030, 0x101);
    W16(0x060C0000, 0x8000);
    W16(0x25C00000, 0x8000);
    init_vdp1sys();
}

static void photo_actor_clear(void)                     /* 0x0600E8A0 */
{
    u32 a;
    W32(0x060AA168, 0);
    W16(0x060AA004, 0);
    W32(0x060AA178, 0);
    W32(0x060AA180, 0);
    W8(0x060AA301, 0);
    W8(0x060AA302, 0);
    for (a = 0x060B0000; a < 0x060B0500; a += 4) W32(a, 0);         /* allclr_car_ctrl_work 0x06005EC4 */
    for (a = 0x060B0600; a < 0x060B2600; a += 4) W32(a, 0);
    for (a = 0x060B2600; a < 0x060B2B00; a += 4) W32(a, 0);
    for (a = CAMW + 4; a < CAMW + 64; a += 4) W32(a, 0);            /* clr_camera_ctrl_work 0x06005E44 (function 0x0600EBC8) */
    W32(CAMW, 0x0600EBC8);
    for (a = 0x060AA9F0 + 4; a < 0x060AA9F0 + 32; a += 4) W32(a, 0);        /* clr_rscroll_ctrl_work 0x06005E74 (function normal_endmove) */
    W32(0x060AA9F0, 0x06006BD4);
    W8(0x060AA9F0 + 0x500, 0);
    W8(0x060AA990 + 245, 0);                            /* gbr (the pad block) + 245 */
}

void photo_init(void)                                   /* 0x0600E6A4 */
{
    tv_off();
    snd_call(0x06016F24, 0, 0, 0);
    snd_call(0x06017580, 0, 0, 0);
    pad_kind_init();
    init_gspr();
    if (R8(FLAGS) & 0x10) sat_cd_request(0x0600E770);   /* PHOTOTBL.DAT -> 0x00248000 */
    set_staff_cg();
    staff_act_init(0x5BE);                              /* 1470 passes: the five shots */
    /* 0x0600E7B8 */
    W8(0x060AA808, 1);
    W8(0x060AA80C, 1);
    W16(0x060AA8E0, 0);
    W32(0x060AAA5C, 0x002CF800);
    W32(0x060AAA60, 0x002DF000);
    {
        u32 c = (u32)RS16(0x060AA87E);
        W16(0x060AA880, c * 2);
        W16(0x060AA882, c * 4);
        W16(0x060AA884, c * 8);
    }
    crs_adv_dataset();
    /* the original's crs_adv_dataset keeps its saved pr on the stack (it jumps to the loader), so the module's tail call to set_game_texture pops that one: it returns into the
     * tail call a second time (and loads the animals of the desert twice) */
    set_game_texture();
    set_game_texture();
    photo_actor_clear();
    photo_scene_vram();
    photoctrlinit();
    swclear();
    W8(0x060AA807, 0);
    W8(FLAGS, R8(FLAGS) & 0xBF);
    W16(0x25D00004, 2);
    W32(0x060A001C, 0x0600E8F4);
    W8(0x060AA806, R8(0x060AA806) & 0xFE);
    W8(0x060A0001, 2);
    W8(0x060A0013, 2);
    photo_camera();                                     /* 0x0600E7A8: camera_ctrl, then the first fade preset */
    fade_color_init(0);
    m_mkpoly_init();
    set_slave_exec_init();
    W16(0x25F80020, 24);
    W16(0x060AA8E6, 24);
    event_init((u32)RS16(0x060AA87E));
    snd_call(0x060173AC, 4, 80, 0);
    tv_on();
}

/* ---- the state handlers ---- */
static void photo_run(void)                             /* 0x0600E974 */
{
    W32(0x060AA168, R32(0x060AA168) + 1);
    W16(0x060AA004, R16(0x060AA004) + 1);
    photo_frm_ctrl();
    photo_frame();
    photo_stats();
    if (!(R8(FLAGS) & 0x20)) {
        u32 pr = (u32)RS16(0x060AA994);
        W16(0x060AA00A, pr);                            /* pl1p_or_switch 0x060085EC */
        if (pr & 0x0800) W8(0x060AA807, 8);             /* START: leave */
    }
    W16(0x060AA994, 0);
}

static void photo_fade_start(void)                      /* 0x0600EA20 */
{
    snd_call(0x06016F3C, 0, 0, 64);
    W16(0x060AA994, 0);
    W8(0x060AA807, 10);
    fade_color_init(1);
}

static void photo_fade_wait(void)                       /* 0x0600EA40 */
{
    W16(0x060AA994, 0);
    if (!(R8(FLAGS) & 0x20)) W8(FLAGS, R8(FLAGS) | 2);
}

void photo_main(void)                                   /* 0x0600E8F4 */
{
    for (;;) {
        W16(0x060AA800, 0x2404);
        switch (R8(0x060AA807)) {
        case 0: photo_run(); break;
        case 8: photo_fade_start(); break;
        case 10: photo_fade_wait(); break;
        default: menu_unported(0x0600E9C8); return;     /* 2 / 4 / 6: the development pause */
        }
        snd_call(0x06017580, 0, 0, 0);
        fade_inout_work();
        staff_act();
        intm_sync(38);
        if (R8(FLAGS) & 2) break;
    }
    snd_call(0x06017080, 0, 0, 0);
    W8(FLAGS, R8(FLAGS) & 0xFD);
    W8(0x060AA80D, R8(0x060AA80D) | 1);
    W32(0x060A001C, 0x0602FFD0);
}

void menu_photo_register(void)
{
    menu_reg(0x0600E6A4, photo_init);
    menu_reg(0x0600E8F4, photo_main);
}
