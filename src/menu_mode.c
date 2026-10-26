/* M8: the mode select screen (cs_md_select 0x06022894 .. 0x06023500, cs_pict_set, the sprite / car calls of cs_poly_set and cs_car_set).
 *
 * Screen vector words: [0x060A001C] = the main vector (slt_mode_main), [0x060A8800] = the screen's own handler (a small state machine, see menu_mode_register).
 * Variables (all halfwords unless noted):
 *   0x060A88A4 cursor (0 arcade, 1 time attack, 2 battle, 3 setting, 4 record, 5 option)   0x060A88A6 previous cursor   0x060A88C0 cursor direction (-1, 0, +1)
 *   0x060A88BE cheat sequence index   0x060A88EA "screen done" flag   0x060A88E4 car visible   0x060A88DE confirmed
 */
#include "satlib.h"
#include "menu_lib.h"
#include "satmenu.h"
#include "menu_pspfix.h"

/* ---- palette cross fade of the highlighted entry (0x0602EF68, FUN_0601DB80 / FUN_0601DB9A) ---- */
static u32 mix555(u32 a, u32 b, s32 t)                  /* 0x0601DB9A: per channel a + (b - a) * t / 65536 */
{
    static const u32 mask[3] = { 0x7C00, 0x03E0, 0x001F };
    u32 out = 0;
    int k;
    for (k = 0; k < 3; k++) {
        s32 x = (s32)(a & mask[k]), y = (s32)(b & mask[k]);
        s32 d = (s32)((u32)(y - x) << 16);
        s32 hi = (s32)(((s64)d * (s64)t) >> 32);
        out |= (u32)(hi + x) & mask[k];
    }
    return out;
}

static void pal_mix(u32 pa, u32 pb, u32 dst, u32 n, s32 t)   /* 0x0601DB80 */
{
    while (n--) {
        W16(dst, mix555((u32)RS16(pa), (u32)RS16(pb), t));
        pa += 2; pb += 2; dst += 2;
    }
}

static void pal_anim(void)                              /* 0x0602EF68 with r14 = 0x060E5980 */
{
    const u32 b = 0x060E5980;
    int guard = 0;
    for (;;) {
        u32 r4 = R32(b + 4), r5 = R32(b + 8);
        if (++guard > 100) menu_unported(0x0602EF68);   /* the fade table (0x0602EFBC) is missing */
        u32 phase = R32(b + 28), step, coef;
        if (phase == 1) { r4 = R32(b + 8); r5 = R32(b + 24); }
        else if (R32(b + 28) == 2) { r4 = R32(b + 24); r5 = R32(b + 4); }
        step = R32(b + 20);
        coef = R16(0x0602EFBC + 2 * step);
        step++;
        if ((s32)coef > 0) {
            W32(b + 20, step);
            pal_mix(r4, r5, R32(b + 12), R32(b + 16), (s32)coef);
            return;
        }
        W32(b + 20, 0);
        phase = R32(b + 28) + 1;
        if (phase == 3) phase = 0;
        W32(b + 28, phase);
    }
}

/* ---- cheat sequences (0x06022D00 / 0x06022D16) ---- */
static void cheat_hold(void)                            /* 0x06022D00: X + Y held: unlock bit 1 */
{
    if (R16(0x060AA990) == 0x60) W8(0x06040062, R8(0x06040062) | 2);
}

static void cheat_sequence(void)                        /* 0x06022D16: X Y Z Y X: unlock bit 2 */
{
    u32 press = R16(0x060AA994);
    u32 i;
    if (!press) return;
    i = R16(0x060A88BE);
    if (press == R16(0x06022D80 + 2 * i)) {
        i++;
        W16(0x060A88BE, i);
        if (i != 5) return;
        W8(0x06040062, R8(0x06040062) | 4);
    }
    W16(0x060A88BE, 0);
}

/* ---- cursor with wrap (0x06022E7A -> 0x06022F16): moves the cursor variable by `dir` inside lo..hi and returns it */
static s32 cursor_step(u32 var, s32 dir, s32 lo, s32 hi)
{
    s32 v = RS16(var) + dir;
    if (lo - 1 >= v) v = hi;
    else if (v >= hi + 1) v = lo;
    W16(var, v);
    return v;
}

static s32 ocur(void) { return RS16(0x060A88A4); }              /* the entry the cursor is on (0 arcade, 1 time attack, 2 battle, 3 setting, 4 record, 5 option) */

/* ---- 3D / sprite calls of the screen ---- */
static void set_car_texture(void)                       /* 0x0601ABA2: the car models + textures (OBJECT.MAD); the texture upload (setup_texture) is the 3D pipeline's */
{
    if (R32(0x060D008C) == 0x4F424A20) return;
    W32(0x060D008C, 0x4F424A20);
    if (R8(0x060AA803) & 0x10) sat_cd_request(0x0601ABFC);
    else menu_unported(0x0601ABA2);
    W32(0x060D0048, 0x30002100);
    setup_texture(0x0601AF8C, 0xFDEE0000u);
}

static void set_select_model(void)                      /* 0x0601ADD8: the menu model pack (SELECT.MAD) */
{
    if (R32(0x060D0090) == 0x53454C45) return;
    W32(0x060D0090, 0x53454C45);
    if (R8(0x060AA803) & 0x10) sat_cd_request(0x0601AE30);
    else menu_unported(0x0601ADD8);
    W32(0x060D0048, 0x76002A00);
    setup_texture(0x0601B00C, 0xFD8B0000u);
}

static void set_car(void)                               /* 0x06026510: the two car models of the menu (turntable state blocks at 0x060E5180 / 0x060E5580) */
{
    W8(0x060E5180 + 92, 0);
    W32(0x060E5180 + 24, 0);
    W32(0x060E5180 + 28, 0);
    W32(0x060E5180 + 32, 0x00078000);
    W32(0x060E5180 + 12, 0xF00);
    W32(0x060E5180 + 16, 0);
    W32(0x060E5180 + 20, 0);
    W8(0x060E5580 + 92, 0);
    W32(0x060E5580 + 24, 0);
    W32(0x060E5580 + 28, 0);
    W32(0x060E5580 + 32, 0x00078000);
    W32(0x060E5580 + 12, 0xE00);
    W32(0x060E5580 + 16, 0x8000);
    W32(0x060E5580 + 20, 0);
}

static void md_pict_set_bright(void);

/* ---- pictures ---- */
static void md_color_chg(void)                          /* 0x060236F4: the palette cross fade block */
{
    W32(0x060E5980, 0);
    W32(0x060E5984, 0x060B8600);
    W32(0x060E5988, 0x060B8000);
    W32(0x060E598C, 0x060B8E00);
    W32(0x060E5990, 0x100);
    W32(0x060E5994, 0);
    W32(0x060E5998, 0x060B8800);
    W32(0x060E599C, 0);
}

static void set_pict_md_slt(void)                       /* 0x0602358C */
{
    u32 t, base;
    if (R8(0x060AA803) & 0x10) {
        sat_load_name_at(0x060236D8);                   /* MD_SLT0.DAT */
        base = 0xFDA44000u;                             /* the table addresses (0x02800000 ..) are those of the data block loaded at 0x00244000 */
    } else {
        menu_unported(0x0602358C);                      /* the BPE-from-memory path is not used when the disc is the source */
        return;
    }
    t = cd_map_set2(0x06023600, base);
    t = cd_color_set(t, base);
    cd_map_set2(t, base);
}

static void md_pict_set_bright(void)                    /* 0x06023716 */
{
    {
    u32 r12 = (u32)RS16(0x060237CC + 2 * RS16(0x060A88A4));
    u32 r11 = (u32)RS16(0x060237CC + 2 * RS16(0x060A88A6));
    u32 base = (R8(0x060AA803) & 0x10) ? 0xFDA44000u : 0;
    u32 t;
    cd_map_set2(0x06023898 + r11, base);
    t = RS16(0x060A88DE) == 0 ? 0x060237D8u : 0x06023838u;
    cd_map_set2(t + r12, base);
    if (RS16(0x060A88A4) == 2) return;
    t = (RS8(0x060AA9A6) == -16) ? 0x060238F8u : 0x06023908u;
    cd_map_set2(t, base);
    }
}

static void set_pict_next_md(void)                      /* 0x06023918: the pictures of the next screens: entries 4 (record) / 5 (option) copy their picture data from the loaded file */
{
    s32 c = ocur();
    u32 r12, base, t;
    if (c < 4 || c == 99) return;
    r12 = (u32)RS16(0x0602399C + 2 * (c - 4));
    if (!(R8(0x060AA803) & 0x10)) { menu_unported(0x06023918); return; }   /* the BPE-from-memory path is not used when the disc is the source */
    base = R32(0x060239A0 + 4 * (c - 4));
    t = cd_map_set(0x060239C8 + r12, base);
    cd_color_set(t, base);
}

/* ---- sprites / car of the screen (cs_poly_set, cs_car_set) ---- */
static void put_modeltype(u32 blk) { W8(blk + 92, R8(0x060AA80A)); }              /* 0x0602672C */
static void put_modeltype2(u32 blk) { W8(blk + 92, R8(0x060AA813)); }             /* 0x06026734 */

static void disp_car(void)                              /* 0x06026574: the turntable car (3D, drawn by the host) */
{
    u32 r0 = R32(0x060E5190) + 128;
    u32 r4 = R32(0x060A882C);
    W32(0x060E5190, r0);
    if (RS16(0x060A88E8) == 1) {
        r4 += (u32)(-RS16(0x060A88CC));
        W32(0x060A882C, r4);
    }
    /* set_poly_offset(r4), dispSelectCar2: pipeline */
    /* set_slave_exec(0x060265BA): the slave runs the second car's turn at once (0x060265C0): the same for the block 0x060E5580 / offset 0x060A883C */
    W32(0x060E5590, R32(0x060E5590) - 128);
    r4 = R32(0x060A883C);
    if (RS16(0x060A88E8) == 1) {
        r4 += (u32)(-RS16(0x060A88CC));
        W32(0x060A883C, r4);
    }
}

static void disp_spr_tm(void)                           /* 0x06023CC0: time attack picture sprite */
{
    s32 r8 = 0;
    u32 r4;
    if (RS16(0x060A88E8) == 1) r8 = -RS16(0x060A88CC);
    r4 = R32(0x060A8858) + (u32)r8;
    W32(0x060A8858, r4);
    draw_sprite(r4, R32(0x06023D38 + 4 * R8(0x060AA80A)), 0x00010000);
}

static void bt_disp_spr2(void)                          /* 0x06023FC4: battle sprites */
{
    s32 r8 = 0;
    u32 r4;
    if (RS16(0x060A88E8) == 1) r8 = -RS16(0x060A88CC);
    r4 = R32(0x060A8838) + (u32)r8;
    W32(0x060A8838, r4);
    draw_sprite(r4, 0x06048460, 0x00010000);
    r4 = R32(0x060A8840) + (u32)r8;
    W32(0x060A8840, r4);
    draw_sprite(r4, 0x06048498, 0x00010000);
    r4 = R32(0x060A8858) + (u32)r8;
    W32(0x060A8858, r4);
    draw_sprite(r4, R32(0x06023D38 + 4 * R8(0x060AA80A)), 0x00010000);
    r4 = R32(0x060A885C) + (u32)r8;
    W32(0x060A885C, r4);
    draw_sprite(r4, R32(0x06023D38 + 4 * R8(0x060AA813)), 0x00010000);
}

/* ---- the screen's own states ([0x060A8800]) ---- */
static void md_wait_fadein(void)                        /* 0x06022D8A */
{
    fade_inout_work();
    if (!(R8(0x060AA803) & 0x20)) {
        W32(0x060A8800, 0x06022DD8);
        pre_fade_inout(0, 255, 51);
    }
}

static void md_arcade_confirm(void)                     /* 0x06022DB4 */
{
    pre_fade_inout(0, 255, 8);
    snd_call(0x06016F3C, 0, 0, 48);
    W32(0x060A8800, 0x0602767E);
}

static void md_input(void)                              /* 0x06022DD8 */
{
    u32 keys;
    s32 dir = 0, cur;
    W16(0x060A88A6, R16(0x060A88A4));
    keys = key_repeat(0);
    W16(0x060A88C0, 0);
    W32(0x060A8810, keys);
    if (keys & 0x1000) { dir = -1; W16(0x060A88C0, dir); }
    if (keys & 0x2000) { dir += 1; W16(0x060A88C0, dir); }
    cur = cursor_step(0x060A88A4, RS16(0x060A88C0), 0, 5);
    if (cur == 2) {                                     /* battle needs a second pad: skipped otherwise */
        if (menu_no_battle || RS8(0x060AA9A6) == -16 || RS8(0x060AA9A9) == 0) {
            cur += RS16(0x060A88C0);
            W16(0x060A88A4, cur);
        }
    }
    W16(0x060A887A, cur);
    if (R32(0x060A8810) & 0xE00) {
        W16(0x060A88DE, 1);
        if (cur == 0) {
            snd_call(0x06016FBC, 74, 0, 0);
            W32(0x060A8800, 0x06022DB4);
        } else {
            W32(0x060A8800, 0x06022F38);
        }
    }
    if (RS16(0x060A88C0) != 0) snd_call(0x06016FBC, 72, 0, 0);
    md_pict_set_bright();
}

static void md_other_confirm(void)                      /* 0x06022F38: the confirmed entry stays, the other labels are cleared */
{
    int i;
    u32 cur;
    snd_call(0x06016FBC, 74, 0, 0);
    snd_call(0x06016FBC, 71, 0, 0);
    cur = R16(0x060A88A4);
    for (i = 0; i < 6; i++) {
        u32 dst = R32(0x06022FA0 + 4 * i);
        if ((u32)i != cur) box_fill32(0, dst, 0x00210002);
    }
    if (RS16(0x060A88A4) != 5) box_fill32(0, 0x25E2951C, 0x00210002);
    if (ocur() == 1) W32(0x060E5180 + 28, R32(0x060E5180 + 28) + 0xFFFFE000u);
    W32(0x060A8800, 0x06022FB8);
}

static void md_slide_a(void)                            /* 0x06022FB8 */
{
    u32 c4;
    W16(0x060A88E4, 1);
    W16(0x060AA870, 0xFF20);
    W16(0x060A8886, 0xFF20);
    W16(0x060AA876, 0xFF20);
    W16(0x060A888A, 0xFF20);
    W16(0x060AA87A, 0xFF20);
    W16(0x060A888E, 0xFF20);
    W32(0x060A8800, 0x0602302C);
    c4 = (u32)ocur() << 2;
    W32(0x060A882C, R32(0x06023010 + c4));
    W32(0x060A8858, R32(0x06023020 + c4));
    W32(0x060A885C, 0x0008011C);
}

static void tm_set_model(void)                          /* 0x06023424: the car of the time attack picture */
{
    u32 v;
    if (RS16(0x0604001A) == 1) v = R8(0x0604006A + ((u32)RS16(0x0604001E) << 3));
    else v = R16(0x0604001C);
    W8(0x060AA80A, v);
    put_modeltype(0x060E5180);
}

static void bt_set_models(void)                         /* 0x06023458: the two cars of the battle picture */
{
    u32 v;
    if (RS8(0x0604002D) == 1) v = R8(0x0604006A + ((u32)RS8(0x06040030) << 3));
    else v = R8(0x0604002F);
    W8(0x060AA80A, v);
    put_modeltype(0x060E5180);
    if (RS8(0x0604002E) == 1) v = R8(0x0604006A + ((u32)RS8(0x06040032) << 3));
    else v = R8(0x06040031);
    W8(0x060AA813, v);
    put_modeltype2(0x060E5580);
}

static void md_slide_b(void)                            /* 0x0602302C */
{
    s32 c;
    if (ocur() == 1) tm_set_model();
    else if (ocur() == 2) bt_set_models();
    c = ocur() - 1;
    if ((u32)c < 5) {
        menu_call(R32(0x06023078 + 4 * c));
        set_pict_next_md();
    }
    W32(0x060A8800, 0x0602308C);
    W16(0x060AA8E6, 31);
}

static void md_slide_c(void)                            /* 0x0602308C */
{
    s32 c = ocur() - 1;
    W16(0x060A88C0, 240 - RS16(0x060230AE + 2 * c));
    W32(0x060A8800, 0x060230B8);
}

static void md_slide_pos(s32 r4, s32 r6, s32 r8)        /* the stores that end the slide states */
{
    W16(0x060A8892, r4);
    W16(0x060B9048, r4);
    W16(0x060A8882, r6);
    W16(0x060AA868, r6);
    W16(0x060A888A, r8);
    W16(0x060AA876, r8);
    W16(0x060AA870, r8);
    W16(0x060AA87A, r8);
    W16(0x060A88D0, R16(0x060A88CC));
}

static void md_slide_d(void)                            /* 0x060230B8 */
{
    s32 r4, r8, r6, r2, c4, step;
    W16(0x060A88E8, 1);
    r4 = RS16(0x060A8892);
    r8 = RS16(0x060A888A);
    if (r4 <= 8) step = 2;
    else {
        step = 4;
        if (r4 > 20) W32(0x060A8800, 0x060231F0);
    }
    W16(0x060A88CC, step);
    r4 += RS16(0x060A88D0);
    r8 += RS16(0x060A88D0);
    c4 = RS16(0x060A88C4) - 1;
    W16(0x060A88C4, c4);
    r6 = RS16(0x060A8882) + c4;
    r2 = RS16(0x060A88C0) + (c4 - RS16(0x060A88D0));
    W16(0x060A88C0, r2);
    md_slide_pos(r4, r6, r8);
    (void)r2;
}

static void md_slide_e(void)                            /* 0x060231F0 */
{
    s32 r4 = RS16(0x060A8892), r8 = RS16(0x060A888A), c4, r6, r2;
    r4 += RS16(0x060A88D0);
    r8 += RS16(0x060A88D0);
    W16(0x060A88CC, 8);
    c4 = RS16(0x060A88C4) - 1;
    W16(0x060A88C4, c4);
    r6 = RS16(0x060A8882) + c4;
    r2 = RS16(0x060A88C0) + (c4 - 8);
    W16(0x060A88C0, r2);
    if (r2 <= 0) {
        r6 += -r2;
        W16(0x060A88C0, 0);
        W32(0x060A8800, 0x06023266);
    }
    md_slide_pos(r4, r6, r8);
}

static void md_slide_f(void)                            /* 0x06023266 */
{
    s32 r4 = RS16(0x060A8892), r8 = RS16(0x060A888A), c4, r6, r2;
    r4 += RS16(0x060A88D0);
    r8 += RS16(0x060A88D0);
    W16(0x060A88CC, 8);
    c4 = RS16(0x060A88C4);
    c4 += (c4 > 0) ? 32 : 40;
    W16(0x060A88C4, c4);
    r6 = RS16(0x060A8882) + c4;
    r2 = RS16(0x060A88C0) + (c4 - 8);
    W16(0x060A88C0, r2);
    if (r2 > 0) {
        r6 += -r2;
        W16(0x060A88C0, 0);
        W16(0x060A88C4, 0);
        W32(0x060A8800, 0x060232EE);
        r6 += -4;
    }
    md_slide_pos(r4, r6, r8);
}

static void md_slide_g(void)                            /* 0x060232EE */
{
    s32 r0 = RS16(0x060A8892), r2 = RS16(0x060A8882), r4 = RS16(0x060A888A);
    s32 step = 0, last = 0;
    if (r0 < 192) step = 8;
    else if (r0 < 208) step = 4;
    else {
        step = 2;
        if (r0 >= 222) {
            W16(0x060A88E8, 0);
            if (r0 >= 224) {
                W16(0x060A88E8, 0);
                W32(0x060A8800, 0x0602337E);
                last = 1;
            }
        }
    }
    if (last) r0 = 224;
    else {
        W16(0x060A88CC, step);
        step = RS16(0x060A88D0);
        r0 += step;
        r2 += step;
        r4 += step;
    }
    W16(0x060A8892, r0);
    W16(0x060B9048, r0);
    W16(0x060A8882, r2);
    W16(0x060AA868, r2);
    W16(0x060A888A, r4);
    W16(0x060AA876, r4);
    W16(0x060AA870, r4);
    W16(0x060AA87A, r4);
    W16(0x060A88D0, R16(0x060A88CC));
}

static void md_slide_h(void)                            /* 0x0602337E */
{
    u32 p = 0x060233C0 + ((u32)(ocur() - 1) << 2);
    s32 r1, r2;
    W16(0x060A88E8, 0);
    r1 = RS16(p);
    r2 = RS16(p + 2);
    W16(0x060A8880, r1);
    W16(0x060AA864, r1);
    W16(0x060AA8B8, r1);
    W16(0x060A8882, r2);
    W16(0x060AA868, r2);
    W16(0x060AA8BC, r2);
    W32(0x060A8800, 0x060233D4);
    W16(0x060A88B8, 10);
}

static void md_wait_done(void)                          /* 0x060233D4 */
{
    s32 r0 = RS16(0x060A88B8) - 1;
    W16(0x060A88B8, r0);
    if (r0 == 0) W16(0x060A88EA, 1);
}

/* ---- init / main loop ---- */
void slt_mode_init(void)                                /* 0x06022894 */
{
    int i;
    W8(0x060A0001, 2);
    W8(0x060A0013, 2);
    sync_vout();
    tv_off();
    vram_scrmap_a_clr();
    pad_kind_init();
    init_gspr2();
    init_oscroll();
    delete_nbg0();
    swclear();
    snd_call(0x060173AC, 3, 70, 0);
    W32(0x060A001C, 0x06022B5C);
    set_car_texture();
    set_select_model();
    mkpoly_init();
    set_car();
    W16(0x25F80020, 17);
    W16(0x060AA8E6, 17);
    W16(0x25F800F8, 0x406);
    W16(0x25F800FA, 0x604);
    W16(0x25F800FC, 2);
    W16(0x25F80038, 0);
    W16(0x25F800F0, 0x404);
    W16(0x25F800F2, 0x404);
    W16(0x25F800F4, 0x404);
    W16(0x25F800F6, 0x404);
    for (i = 0; i < 8; i++) W16(0x25F80050 + 2 * i, R16(0x06022B4C + 2 * i));
    W16(0x060B9048, 0);
    W16(0x060A8892, 0);
    W16(0x060AA868, 0);
    W16(0x060A8882, 0);
    W16(0x060AA8B0, 0);
    W16(0x060AA8B2, 0);
    W16(0x060AA8B4, 0);
    W16(0x060AA8B6, 0);
    W16(0x060A88A6, 0);
    W16(0x060A88D4, 0);
    W16(0x060A88E2, 0);
    W16(0x060A88C0, 0);
    W16(0x060A88E4, 0);
    W16(0x060A88CC, 0);
    W16(0x060A88BE, 0);
    W16(0x060A88B6, 0x3E8);
    W8(0x060AA811, 0);
    W16(0x060A88D0, 0);
    W16(0x060A88DE, 0);
    W32(0x060A882C, R32(0x06022B44));
    W32(0x060A883C, R32(0x06022B48));
    W32(0x060A8838, 0xFF7F00C6);
    W32(0x060A8840, 0x002700C6);
    W32(0x060A8858, 0xFF80011C);
    W32(0x060A885C, 0x000A011C);
    W16(0x060A88A4, R16(0x060A887A));
    key_repeat_init();
    md_color_chg();
    set_pict_md_slt();
    md_pict_set_bright();
    pal_anim();
    if (RS16(0x060A88EA) != 2) {
        pre_fade_inout(255, 0, -18);
        W16(0x060A88BC, 0);
    }
    tv_on();
}

void slt_mode_main(void)                                /* 0x06022B5C */
{
    W32(0x060A8800, 0x06022D8A);
    W16(0x060A88EA, 0);
    do {
        m_mkpoly_frame_init();
        s_mkpoly_frame_init();                          /* set_slave_exec(s_mkpoly_frame_init): the slave runs at once */
        if (RS16(0x060A88E4) == 0) pal_anim();
        menu_call(R32(0x060A8800));
        cheat_sequence();
        if (RS16(0x060A88EA) == 1) W8(0x060AA803, R8(0x060AA803) | 2);
        W16(0x060AA994, 0);
        if (RS16(0x060A88E4) != 0 && ocur() < 4) disp_car();
        if (ocur() == 1) disp_spr_tm();
        else if (ocur() == 2) bt_disp_spr2();
        cheat_hold();
        intm_sync(46);
    } while (!(R8(0x060AA803) & 2));
    W8(0x060AA803, R8(0x060AA803) & 0xFD);
    W16(0x060A88DE, 0);
    switch (ocur()) {
    case 1: W32(0x060A001C, 0x0602425C); break;                 /* tm_attck_main */
    case 2: W32(0x060A001C, 0x06028A10); break;                 /* battle_2p_main */
    case 3: W32(0x060A001C, 0x06026A30); break;                 /* setting_main */
    case 4: W32(0x060A001C, 0x0602CC74); break;                 /* record_main */
    case 5: W32(0x060A001C, 0x06032F60); break;                 /* option_init */
    default: W8(0x060AA80D, 1); W32(0x060A001C, 0x0601F978); break;   /* g_select_init */
    }
}

void menu_mode_register(void)
{
    menu_reg(0x06022894, slt_mode_init);
    menu_reg(0x06022B5C, slt_mode_main);
    menu_reg(0x06022D8A, md_wait_fadein);
    menu_reg(0x06022DB4, md_arcade_confirm);
    menu_reg(0x06022DD8, md_input);
    menu_reg(0x06022F38, md_other_confirm);
    menu_reg(0x06022FB8, md_slide_a);
    menu_reg(0x0602302C, md_slide_b);
    menu_reg(0x0602308C, md_slide_c);
    menu_reg(0x060230B8, md_slide_d);
    menu_reg(0x060231F0, md_slide_e);
    menu_reg(0x06023266, md_slide_f);
    menu_reg(0x060232EE, md_slide_g);
    menu_reg(0x0602337E, md_slide_h);
    menu_reg(0x060233D4, md_wait_done);
}
