/* Sega Rally PSP - the HUD name-table generators (M6): `game_display` 0x060123F8 of the original, for the single-player game types 0 and 2.
 * It rewrites, every pass, the name tables of the VDP2 text layers NBG2 / NBG3 (docs/hud.md): speed, gear, tachometer dial, remaining time, lap and total time, the co-driver messages, the wrong-way
 * message (scroll).  The static part (labels) is `init_game_disp`; its result is read from extracted/psp/course/hud_init.dat (tools/oracle.py hudinit).
 * Checked against the oracle (tools/oracle.py run --hud, tools/test_drive.sh -hud): every cell after every pass. */
#ifndef HUDGEN_H
#define HUDGEN_H
#include "drive.h"

#define HUD_NT_BASE  0x25E3A000u                    /* VDP2 VRAM: NBG2 page (0x3A000), NBG3 pages A / B (0x3C000) and C / D (0x3E000), 64 x 64 words each */
#define HUD_NT_WORDS 0x3000
#define HUDE_BASE    0x25E28000u                    /* NBG0's four name table pages (maps 0x0A .. 0x0D, planes A B / C D): the end-of-race screens, 64 x 64 two-word cells each */
#define HUDE_CELLS   0x4000

typedef struct {
    unsigned short w[HUD_NT_WORDS];                 /* the three name table pages as the console's VRAM holds them (host order words, address = HUD_NT_BASE + 2 * index) */
    unsigned short scroll[4];                       /* [0x060AA874 ..]: NBG2 x, y, NBG3 x, y (the V-IN copies them, masked 0x3FF, to the scroll registers) */
    unsigned nt0[HUDE_CELLS];                       /* NBG0 name table cells (palette << 16 | character number), address = HUDE_BASE + 4 * index; written by the end-of-race state machine only */
    unsigned short nscroll[2];                      /* [0x060AA864], [0x060AA868]: NBG0 x, y (integer parts, masked 0x3FF by the V-IN); x starts at 0x200 (plane B, game over), the goal sets it to 0x400 (= 0, plane A) */
    unsigned short bgon;                            /* [0x060AA8E6] (and the register): 0x1C racing (NBG2, NBG3, RBG0), 0x10 / 0x11 / 0x15 at the end (bit 0: NBG0, bit 2: NBG2) */
    unsigned short wctlb;                           /* VDP2 register 0x25F800D2: window control of NBG2 / NBG3: the time attack's blink of the lap row (0x88 / 2 / 0; not compared with the oracle, the renderer may use it) */
    unsigned short win[4];                          /* VDP2 registers 0x25F800C8 ..: the window 1 rectangle (replay_display: one row of the table 0x06013B74 per lap) */
    unsigned short wctla;                           /* VDP2 register 0x25F800D0: window control of NBG0 (0x82 = the blinking window of the arcade goal screen) */
    int replay;                                     /* the set-up of a replay (play mode 2): replay_display / resreplay_display instead of the race HUD */
    int done;                                       /* the state machine reached its last state (26): the original sets bit 1 of [0x060AA803], the next screen follows */
} HudGen;

#define HUD_REPLAY 0x100                                                                 /* ORed to the game type: the display set-up of a replay (play mode 2) */
int  hudgen_load(HudGen *h, const char *init_path, int game_type, int course);        /* the screen after init_game_disp for this game type / course; 0 = ok */
void hudgen_pass(HudGen *h, Drive *d, unsigned frame);                                /* one game_display; `frame` = the free-running pass counter ([0x060AA168]) the blinking uses */
void hudend_pass(HudGen *h, Drive *d);                                                /* the car's control byte >= 8 branch of game_display (0x06012D18): the goal / game-over screens on NBG0 (docs/hud.md 7) */
void hudgen_ta_init(HudGen *h, const Drive *d);                                    /* time attack (game types 4 / 6): what the display set-up draws from the race's values (times, car, transmission, record box) on top of the static screen */
void hudgen_replay_init(HudGen *h, const Drive *d);                                 /* a replay's display set-up: the times of the replayed run in the left rows */
void hudgen_pause_open(HudGen *h, Drive *d);                                      /* the pause menu (src/hudgen.c, docs/hud.md): draw the box, move the cursor (Drive.pause[0] = the item), remove it */
void hudgen_pause_cursor(HudGen *h, Drive *d);
void hudgen_pause_close(HudGen *h);

#endif
