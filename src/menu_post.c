/* M8: the screens after a race of game type 0 (arcade) / 2 (practice) / 4 / 6 (time attack), ported from the original: the post-goal dispatcher 0x0602FF84 (ceremony, photo, name entry or
 * straight to the next screen), the ranking screen 0x06031BC4 (the table with the new entry, scrolling, continue / quit choice) and the game over / continue screen 0x06032378.
 * The name entry is menu_name.c, the result screen of the time attack menu_result.c.
 *
 * Ranking screen: [0x060A8C61] state (0 set up, 1 count, 2 scroll to the new entry, 3 input, 4 fade to white and leave, 5 / 6 scroll down / up, 7 / 8 fade in when the screen was
 * entered without a name entry); the original's r14 is the scroll offset of the table (kept in rank_r14). */
#include "satlib.h"
#include "menu_lib.h"
#include "satmenu.h"
#include "menu_records.h"
#include "menu_post.h"
#include "menu_ending.h"

#define SND(id) snd_call(0x06016FBC, (u32)(id), 0, 0)

static void post_goal_resume(void);

void post_goal(void)                                    /* 0x0602FF84 */
{
    if (R8(0x060AA810) != 2) {
        s32 type = RS16(0x060AA860);
        if (type == 0) {
            if (RS8(0x060B00F5) == 8 && RS16(0x060AA87E) == 3) {        /* the last stage of the arcade game: the ceremony, then the ending */
                W32(0x060A001C, 0x0601E4E8);
                W32(0x060D0024, 0x0602FFD0);
                return;
            }
        } else if (type == 2) {
            if (RS8(0x060B00B6) == 1) {                                  /* practice won: the photo */
                W32(0x060A001C, 0x0600E6A4);
                return;
            }
        }
    }
    post_goal_resume();
}

void post_goal_ceremony(void)                           /* 0x0602FFD0: the ceremony / ending is over */
{
    init_game_over();                                   /* 0x06011EA4: GMGOVER.DAT + the text box (menu_ending.c) */
    post_goal_resume();
}

static void post_goal_resume(void)                      /* 0x0602FFDA */
{
    s32 type;
    tv_off();
    W8(0x060A8C6F, 1);
    W32(0x060A001C, 0x06031BC4);
    W32(0x060AA9AC, 0x060D0034);
    if (RS8(0x060AA810) != 2) {
        if (RS8(0x060AA82E) == 1) {
            W8(0x060A8C62, 0xFF);
            W8(0x060A8C63, 1);
            goto next;
        }
        type = RS16(0x060AA860);
        if (!(type & 4) && RS8(0x060AA80A) == 2) {      /* the Stratos: no entry in the tables */
            W8(0x060A8C62, 0xFF);
            goto next;
        }
        W8(0x060A8C63, 1);
        if (RS8(0x060B00F5) >= 10) {
            type = RS16(0x060AA860);
            if (type != 4 && type != 6) {
                if (type != 0) goto next;
                if (RS16(0x060AA87E) != 3) goto next;
            }
        }
        name_prepare();
        if (RS8(0x060A8C62) >= 0 || RS8(0x060A8C63) != 1) {
            name_entry();
            return;
        }
    }
next:
    W32(0x060A001C, 0x06032514);
    type = RS16(0x060AA860);
    if (type == 6) W32(0x060A001C, 0x060262A4);
    if (!(RS16(0x060AA860) & 4)) W32(0x060A001C, 0x06031BC4);
    if (RS8(0x060B00F5) == 12) W32(0x060A001C, 0x06015408);
    if (R8(0x060A8C17) == 1) {
        init_gspr2();
        W32(0x060A001C, 0x0602CC74);
    }
    if (RS16(0x060AA860) == 0) W16(0x060AA87E, 0);
}

/* ---- the table as the ranking screen draws it (16 bit cells, NBG3 plane 0x25E3C000; the new entry again on NBG2 0x25E3A000) ---- */
static void name16(u32 r4, u32 r5)                      /* 0x0603120C / 0x06031230: the three letters of the name word as 2 x 2 cells each; empty letters take no room */
{
    int k;
    for (k = 0; k < 3; k++) {
        u32 r2 = (k == 0 ? r5 >> 16 : k == 1 ? (r5 >> 8) : r5) & 0xFF;
        if (r2 == 0) continue;
        r2 = (r2 << 2) + 0x10E;
        W16(r4, r2);
        r4 += 2; r2 += 2;
        W16(r4, r2);
        r4 += 126; r2 += 106;
        W16(r4, r2);
        r4 += 2; r2 += 2;
        W16(r4, r2);
        r4 -= 126;
    }
}

static u32 rank_row(u32 r7, u32 r8, s32 r9)             /* 0x06031144: one row (rank number r9 if >= 0, time, name, the two flag glyphs, the car class) of the list at r7; returns the next row */
{
    u32 r4 = r8, r5, r2;
    if (r9 >= 0) putnum_rank(r4, (u32)r9);
    r4 = r8;
    r5 = R32(r7); r7 += 4;
    if (RS16(0x060AA860) == 0 && RS8(0x060AA80C) == 3 && !(r5 & 0x00F00000)) r4 += 2;
    r4 += 26;
    time_disp(r4, r5, putnum_name);
    r4 = r8 + 10;
    r5 = R32(r7); r7 += 4;
    name16(r4, r5);
    r5 >>= 24;
    r2 = 2 + ((r5 & 1) * 12);
    r4 = r8 + 56;
    W16(r4, r2); r4 += 2; r2 += 2;
    W16(r4, r2); r4 += 2; r2 += 2;
    W16(r4, r2); r4 += 124; r2 += 2;
    W16(r4, r2); r4 += 2; r2 += 2;
    W16(r4, r2); r4 += 2; r2 += 2;
    W16(r4, r2);
    r5 = (r5 >> 4) & 3;
    r4 = r8 + 44;
    r2 = 0x101A;
    if (r5 != 0) r2 = 0x1032;
    rec_tile16(r2, r4, 6, 2);
    return r7;
}

static void rank_rows(u32 r7)                           /* 0x060310D8: the rows of the list at r7 (8 / 16 rows), equal times share the rank */
{
    u32 r10 = r7 + 64, r8 = 0x25E3C48E;
    s32 r12 = 1, r14 = 1, r9;
    u32 r13, r11;
    if (RS16(0x060AA860) == 0) r10 += 64;
    W16(0x060AA876, 0xFF80);
    W16(0x060AA87A, 0xFF80);
    do {
        r13 = R32(r7);
        r9 = r12;
        r7 = rank_row(r7, r8, r9);
        r8 += 0x100;
        r14++;
        r11 = R32(r7);
        if (r11 != r13) r12 = r14;
    } while (r7 != r10);
}

/* ---- the setup the ranking screen does when it was not preceded by the name entry (0x06031C88) ---- */
static void rank_reinit(void)
{
    u32 r11, r4;
    init_gspr2();
    init_oscroll();
    if (R8(0x060AA803) & 0x10) {
        sat_cd_request(0x06031CC0);                     /* NAME.DAT */
        r11 = 0x038A7D00;
    } else {
        menu_unported(0x0602D2B8);
        r11 = 0;
    }
    rec_pal_set(0x0603231C, r11);
    W8(0x060AA80D, 1);
    W8(0x060AA80B, 1);
    snd_call(0x060173AC, 1, 0, 0);
    W8(0x060AA80D, 1);
    W16(0x060AAA24, 0);
    snd_call(0x0600F756, 8, 15, 0);
    W32(0x060A8CC8, r11);
    copy_l(0x25E18040, 0x25E20040, 0x16C0);
    box_fill16(0, 0x25E28000, 0x002C001C);
    box_copy16(0x0283EA3C + r11, 0x25E40000, 0x7000);
    box_copy16(0x0283E65C + r11, 0x25E3830C, 0);
    r4 = 0x0283E6DC + r11;
    if (RS16(0x060AA860) == 2) r4 += (u32)((s32)(s16)132 * (s32)(s16)(RS16(0x060AA87E) + 1));
    box_copy16(r4, 0x25E3809C, 0);
    box_copy16(0x0283E634 + r11, 0x25E38C9A, 0x3000);
    box_copy16(0x0283EA24 + r11, 0x25E38CAC, 0x3000);
    box_copy16(0x0283E620 + r11, 0x25E38CB6, 0x6000);
    W16(0x060AA8B0, 0);
    W16(0x060AA8B2, 0);
    W16(0x060AA8B4, 0);
}

void rank_screen(void)                                  /* 0x06031BC4 */
{
    s32 r14 = 0;
    W32(0x060A001C, 0x06032378);
    W8(0x060A8CA1, 0);
    if (RS16(0x060AA860) == 0) W16(0x060AA87E, 0);
    if (R8(0x060AA821) == 1) W32(0x060A001C, 0x0601F978);
    if (RS8(0x060B00F5) == 12) W32(0x060A001C, 0x06015408);
    if (R8(0x060A8C6F) == 1) rank_reinit();
    rec_vdp2_set_w(0x06032360);
    W32(0x060E4004, 0x060B8600);
    W32(0x060E4008, 0x060B8800);
    W32(0x060E400C, 0x060B8C00);
    W32(0x060E4010, 18);
    W32(0x060E4014, 0);
    W32(0x060E4018, 0x060B8A00);
    W32(0x060E401C, 0);
    W8(0x060A8C61, 0);
    for (;;) {
        snd_call(0x06017580, 0, 0, 0);
        switch (R8(0x060A8C61)) {
        case 0: {                                       /* 0x06031ECC */
            s32 rank, r9;
            u32 r1, r4, r7, r8;
            if (RS16(0x060AA860) > 3) goto s4;
            W16(0x060AA8A0, 0);   W16(0x25F800C0, 0);
            W16(0x060AA8A2, 72);  W16(0x25F800C2, 72);
            W16(0x060AA8A4, 0x2D0); W16(0x25F800C4, 0x2D0);
            W16(0x060AA8A6, 201); W16(0x25F800C6, 201);
            W16(0x060AA8B0, 0);   W16(0x25F800D0, 0);
            W16(0x060AA8B2, 0x303); W16(0x25F800D2, 0x303);
            W32(0x060A8C30, 0);
            if (R8(0x060A8C6F) == 1) rank_rows(record_connect(0).r0);
            else rank_rows(0x060A8CF0);
            W16(0x25F800F8, 0x0406);
            W16(0x25F800FA, 0x0606);
            W16(0x25F800FC, 2);
            rank = RS8(0x060A8C62);
            if (rank >= 0) {                            /* the new entry once more on the plane above, in the colours that blink */
                r9 = rank;
                r1 = R32(0x060A8C70) - 4;
                r4 = R32(r1);
                do {
                    r9--;
                    r1 -= 8;
                } while (r1 >= 0x06000008u && R32(r1) == r4);         /* the original walks on through whatever memory lies below when [0x060A8C62] was never set (a game over before any record: undefined on the console) */
                r7 = 0x060A8CF0 + (u32)(rank << 3);
                r8 = 0x25E3A48E + (u32)(rank << 8);
                r9 += 2;
                rank_row(r7, r8, r9);
            }
            r14 = 0;
            if (RS16(0x060AA860) == 0) {
                rank = RS8(0x060A8C62);
                if (rank > 4) r14 = rank > 11 ? 128 : (rank - 4) << 4;
            }
            W32(0x060A8C2C, (u32)r14);
            W8(0x060A8C61, 1);
            if (R8(0x060A8C6F) == 1) {
                W8(0x060A8C61, 7);
                W16(0x25F80112, 0);
                W32(0x060A8C28, 255);
                W16(0x25F80114, 255);
                W16(0x25F80116, 255);
                W16(0x25F80118, 255);
                W16(0x25F8011A, 0);
                W16(0x25F8011C, 0);
                W16(0x25F8011E, 0);
                W16(0x060AA870, 0xFFF8);
                tv_on();
            } else {
                goto s1;
            }
            break;
        }
        case 1:
        s1: {                                           /* 0x06032044 */
            s32 r0 = RS32(0x060A8C30) + 1;
            if (r0 == 20) { W8(0x060A8C61, 2); r0 = 0x060A8C61; }      /* the original loads the address of the state byte into r0 and stores that */
            W32(0x060A8C30, (u32)r0);
            break;
        }
        case 2: {                                       /* 0x06032064: scroll to the new entry */
            s32 r0 = RS16(0x060AA876) - r14 + 4;
            if (r0 > -36) {
                r0 -= 1;
                if (r0 > -24) {
                    r0 -= 1;
                    if (r0 > -12) {
                        r0 -= 1;
                        if (r0 >= 0) {
                            W8(0x060A8C61, 3);
                            box_copy16(0x0283E620 + R32(0x060A8CC8), 0x25E38CB6, 0x6000);
                        }
                    }
                }
            }
            W16(0x060AA876, (u32)(r0 + r14));
            W16(0x060AA87A, (u32)(r0 + r14));
            break;
        }
        case 3: {                                       /* 0x060320D0: input */
            u32 r13 = (u32)RS16(0x060AA994);
            s32 r9;
            if (r13 & 0xC000) {
                u32 r11 = R32(0x060A8CC8);
                if (R8(0x060A8CA1) != 1) {
                    W8(0x060A8CA1, 1);
                    W32(0x060A001C, 0x06015408);
                    W8(0x060AA810, 2);
                    box_copy16(0x0283EA24 + r11, 0x25E38CAC, 0x6000);
                    box_copy16(0x0283E620 + r11, 0x25E38CB6, 0x3000);
                } else {
                    W8(0x060A8CA1, 0);
                    W32(0x060A001C, 0x06032378);
                    if (RS8(0x060B00F5) == 12) W32(0x060A001C, 0x06015408);
                    W8(0x060AA810, 0);
                    box_copy16(0x0283EA24 + r11, 0x25E38CAC, 0x3000);
                    box_copy16(0x0283E620 + r11, 0x25E38CB6, 0x6000);
                }
            }
            r9 = RS32(0x060A8C2C);
            if (RS16(0x060AA860) <= 0) {
                u32 r3 = (u32)RS16(0x060AA990);
                if (r3 & 0x1048) {
                    if (r9 != 0) {
                        W32(0x060322FC, 0);
                        W8(0x060A8C61, 6);
                        goto s6;
                    }
                } else if (r3 & 0x2090) {
                    if (r9 != 0x80) {
                        W32(0x060322FC, 0);
                        W8(0x060A8C61, 5);
                        goto s5;
                    }
                }
            }
            if (RS16(0x060AA994) & 0xE00) {
                W32(0x060A8C28, 0);
                W8(0x060A8C61, 4);
            }
            break;
        }
        case 5:
        s5: {                                           /* 0x060321B2: the table scrolls up 8 steps */
            s32 r4 = RS32(0x060322FC) + 1, r0;
            W32(0x060322FC, (u32)r4);
            r0 = RS32(0x060A8C2C) + 2;
            W16(0x060AA876, (u32)r0);
            W16(0x060AA87A, (u32)r0);
            W32(0x060A8C2C, (u32)r0);
            if (r4 == 8) W8(0x060A8C61, 3);
            break;
        }
        case 6:
        s6: {                                           /* 0x060321D8 */
            s32 r4 = RS32(0x060322FC) + 1, r0;
            W32(0x060322FC, (u32)r4);
            r0 = RS32(0x060A8C2C) - 2;
            W16(0x060AA876, (u32)r0);
            W16(0x060AA87A, (u32)r0);
            W32(0x060A8C2C, (u32)r0);
            if (r4 == 8) W8(0x060A8C61, 3);
            break;
        }
        case 4:
        s4: {                                           /* 0x060321F2: fade to white, then leave */
            s32 r0;
            W16(0x25F80112, 0);
            r0 = RS32(0x060A8C28) + 20;
            if ((RS16(0x060AA994) & 0xE00) || r0 >= 255) {
                W8(0x060AA803, R8(0x060AA803) | 2);
                tv_off();
                r0 = 0;
            }
            W16(0x25F80114, (u32)r0);
            W16(0x25F80116, (u32)r0);
            W16(0x25F80118, (u32)r0);
            W32(0x060A8C28, (u32)r0);
            break;
        }
        case 7: {                                       /* 0x0603222A: fade in from white */
            s32 r0 = RS32(0x060A8C28) - 20;
            if (r0 < 0) {
                W8(0x060A8C61, 8);
                W16(0x25F80112, 8);
                r14 = RS32(0x060A8C2C);
                r0 = 0;
            }
            W16(0x25F80114, (u32)r0);
            W16(0x25F80116, (u32)r0);
            W16(0x25F80118, (u32)r0);
            W32(0x060A8C28, (u32)r0);
            break;
        }
        case 8: {                                       /* 0x060322D0 */
            s32 r0 = RS32(0x060A8C28) - 22;
            if (r0 == -88) W8(0x060A8C61, 2);
            W16(0x25F8011A, (u32)r0);
            W16(0x25F8011C, (u32)r0);
            W16(0x25F8011E, (u32)r0);
            W32(0x060A8C28, (u32)r0);
            break;
        }
        default:
            menu_unported(0x06031EA8);
        }
        {
            u32 b = 0x060E4000;
            (void)b;
            menu_pal_anim(0x060E4000);
        }
        W16(0x060AA994, 0);
        intm_sync(46);
        if (R8(0x060AA803) & 2) break;
    }
    W8(0x060AA803, R8(0x060AA803) & 0xFD);
}

/* ---- the game over / continue screen (0x06032378): a white fade in, 360 ticks or a key, a fade out ---- */
void (*menu_gover_hook)(void);
int menu_gover_early_key;                               /* the PSP host: a key during the fade-in leaves the screen too (the original ignores it for the 13 passes of the fade-in) */

void game_over_screen(void)
{
    if (menu_gover_hook) menu_gover_hook();
    W32(0x060A001C, 0x0601F978);
    tv_off();
    snd_call(0x0600F756, 9, 0, 0);
    init_gspr2();
    rec_pal_set(0x06032508, 0);
    W16(0x25F80110, 127);
    W16(0x25F80112, 1);
    W16(0x25F80114, 255);
    W16(0x25F80116, 255);
    W16(0x25F80118, 255);
    W16(0x25F8011A, 255);
    W16(0x25F8011C, 255);
    W16(0x25F8011E, 255);
    W32(0x060AA8A0, 0);
    W32(0x060AA8A4, 0);
    W32(0x060AA8B0, 0);
    W16(0x060AA864, 0x200);
    W16(0x060AA868, 0x200);
    W32(0x060A8C30, 255);
    tv_on();
    W8(0x060A8C61, 0);
    if (menu_gover_early_key) W16(0x060AA994, 0);        /* (a press of the screen before it, or one during the set-up, does not leave this one) */
    for (;;) {
        switch (R8(0x060A8C61)) {
        case 0: {                                       /* 0x06032464: fade in */
            s32 r0 = RS32(0x060A8C30) - 20;
            if (r0 < 0) { W8(0x060A8C61, 1); r0 = 0; }
            W16(0x25F8011A, (u32)r0);
            W16(0x25F8011C, (u32)r0);
            W16(0x25F8011E, (u32)r0);
            W32(0x060A8C30, (u32)r0);
            if (menu_gover_early_key && (RS16(0x060AA994) & 0xE00) && R8(0x060A8C61) == 0) { W8(0x060A8C61, 2); W32(0x060A8C30, 0); }
            break;
        }
        case 1: {                                       /* 0x06032484 */
            s32 r1 = 0;
            if (!(RS16(0x060AA994) & 0xE00)) {
                r1 = RS32(0x060A8C30) + 1;
                if (r1 != 0x168) { W32(0x060A8C30, (u32)r1); break; }
            }
            W8(0x060A8C61, 2);
            W32(0x060A8C30, 0);
            break;
        }
        case 2: {                                       /* 0x060324B4: fade out */
            s32 r0 = 0;
            if (!(RS16(0x060AA994) & 0xE00)) {
                r0 = RS32(0x060A8C30) + 5;
                if (r0 < 255) goto st;
            }
            W16(0x060AA87E, 0);
            W8(0x060AA80D, 1);
            W8(0x060AA803, R8(0x060AA803) | 2);
            tv_off();
            r0 = 0;
        st:
            W16(0x25F8011A, (u32)r0);
            W16(0x25F8011C, (u32)r0);
            W16(0x25F8011E, (u32)r0);
            W32(0x060A8C30, (u32)r0);
            break;
        }
        default:
            menu_unported(0x06032458);
        }
        W16(0x060AA994, 0);
        intm_sync(46);
        if (R8(0x060AA803) & 2) break;
    }
    W8(0x060AA803, R8(0x060AA803) & 0xFD);
}

void menu_post_register(void)
{
    menu_reg(0x0602FF84, post_goal);
    menu_reg(0x0602FFD0, post_goal_ceremony);
    menu_reg(0x06031BC4, rank_screen);
    menu_reg(0x06032378, game_over_screen);
}
