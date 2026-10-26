/* M8: what follows a race (plctrl::game_exit 0x06006A18): the race itself runs in play.c; when its end-of-race screens are done the host puts the car slot (0x060B0000, R block at +0x500)
 * into the console memory and calls race_exit(), which chooses the next screen exactly like the original's game_exit and records the result of the stage.
 *
 * Next vectors (table 0x06006B40): [0] / [4] / [12] 0x0602FF84 (after the goal), [8] 0x06015408 (start_init: the next stage of the arcade chain), [16] 0x0603539C, [20] 0x06032378 (game over). */
#include "satlib.h"
#include "menu_lib.h"
#include "satmenu.h"

void race_exit(void)                                    /* 0x06006A18 */
{
    s32 type = RS16(0x060AA860), r13 = 0;
    static const u32 vec[6] = { 0x0602FF84, 0x0602FF84, 0x06015408, 0x0602FF84, 0x0603539C, 0x06032378 };
    if (type == 0) {                                    /* arcade */
        s32 stage = RS16(0x060AA87E);
        if (RS8(0x060B00F5) == 10) {                    /* game over */
            if (stage == 3) { W8(0x060AA82A, R8(0x060AA82A) | 1); r13 = 12; }
            else r13 = 20;
        } else if (stage == 3) {                        /* the last stage: a rank 1 on a harder level unlocks the special car */
            if (R8(0x060AA808) != 0 && R8(0x060B00B6) == 1) W8(0x06040062, R8(0x06040062) | 4);
            W8(0x060AA82A, R8(0x060AA82A) & 0xFE);
            r13 = 12;
        } else if (stage == 2 && (R8(0x060B00B6) != 1 || (R8(0x060AA810) == 2 && (R8(0x060AA82A) & 1)))) {     /* the third stage: only a rank 1 goes on to the lake */
            W8(0x060AA82A, R8(0x060AA82A) | 1);
            r13 = 12;
        } else {
            if (stage == 2) W8(0x06040062, R8(0x06040062) | 2);      /* the lake is unlocked */
            W16(0x060AA87E, (u32)(stage + 1));
            r13 = 8;
        }
    } else if (type == 2) {                             /* practice */
        r13 = RS8(0x060B00F5) == 10 ? 20 : 4;
    } else if (type == 4 || type == 6) {                /* time attack (0x06006AE4): the engine sounds off, then the post-goal dispatcher */
        r13 = 4;
    } else {
        menu_unported(0x06006A18);                      /* the VS game types */
        return;
    }
    snd_call(0x0601753C, 0, 0, 0);                      /* the engine sounds off */
    W32(0x060A001C, vec[r13 >> 2]);
    race_history();
}

void race_history(void)                                 /* 0x06006B58: the tail of game_exit (set_gover calls it as well, when a race is left through the pause menu) */
{
    if (R8(0x060AA810) != 0) return;
    {                                                   /* 0x06006B58: the result of the stage goes to the history at 0x060AFE00, the R block is backed up (R + 0x80) */
        u32 stage4 = (u32)RS16(0x060AA882), i;
        W8(0x060AFE00 + (stage4 >> 2), R8(0x060B00B6));
        W32(0x060AFE04 + stage4, R32(0x060B0574));
        W32(0x060AFE14, R32(0x060B050C));
        for (i = 0; i < 5; i++) W32(0x060AFE18 + stage4 * 5 + 4 * i, R32(0x060B0510 + 4 * i));
        rcopy(0x060B0500, 0x060B0580, 32);
    }
}

void menu_race_register(void)
{
    menu_reg(0x06006A18, race_exit);
}
