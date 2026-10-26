# Sega Rally Championship (Saturn, USA) - the race HUD

How the original draws its HUD, how the oracle reproduces it (`tools/hud.py`), and how the PSP port draws it (`src/hud.c`, `src/event.c ev_hud`).  Evidence tags: [V] verified (run in the oracle / compared with the reference frames), [I] inferred.

## 1. Two parts

| part | hardware | what |
|---|---|---|
| text, labels, gauges, messages | **VDP2 NBG2 / NBG3**, priority 6 (over the 3D picture) | TOTAL TIME, LAP TIME, the three lap rows, the remaining time (big yellow digits), POSITION n/m, BEST TIME, the tachometer dial, gear, speed (MPH), the co-driver messages |
| tachometer needle, count-down numerals | **VDP1 polygons** through the object pipeline (`draw_polygon`) | `dispTachoMeter` 0x06010680, `dispCountDown` 0x06010710 |

## 2. VDP2 layers [V]

* Registers after `init_game_disp` (0x0601151C, display set-up of game types 0 / 2): BGON 0x1C (NBG2, NBG3, RBG0), CHCTLB 0x1022 (NBG2 / NBG3 / RBG0: 256 colours, 8x8 cells), PNCN2 0x8003, PNCN3 0x8004
  (1-word names, 10-bit character number + flip bits, **supplementary character number** 3 / 4), map numbers MPABN2 / MPCDN2 = 0x1D (all four planes), MPABN3 = 0x1E (A, B), MPCDN3 = 0x1F (C, D), plane size 1 x 1 page, colour RAM mode 1.
* Name table entry: bits 14-12 palette (x 256 colours), bits 11 / 10 flips (never used by the HUD), bits 9-0 + (supplementary << 10) = character number; the cell is at VDP2 VRAM `character * 0x20` (256-colour cells are 64 bytes, 8 rows of 8 dots).
  NBG2: cells from 0x18000, name table page 0x3A000 (map 0x1D x 0x2000); NBG3: cells from 0x20000, name table 0x3C000 (planes A / B) and 0x3E000 (C / D).  Dot 0 is transparent.
* **Scroll** (the V-IN recipe `0x060159DC` copies the shadow longs at 0x060AA864.. to 0x25F80070..; NBG2 x / y = words 0x060AA874 / 76, NBG3 x / y = 0x060AA878 / 7A, masked 0x3FF).  The 1024 x 1024 pixel layer (2 x 2 planes) wraps.
  **NBG3 y = 256 hides the "TURN AROUND" (wrong way) message that `init_game_disp` writes at rows 9 - 12 (12 x 4 cells from MAPDATA.DAT) and brings the co-driver message area (rows 32 - 59) into view**; 0 shows the wrong-way message.
* Character data = `CGDATAUS.DAT` (USA; `CGDATA.DAT` / `_AD` / `_VS` / `_F` are other versions / modes), loaded by the menus before the race: scatter blocks with VDP2 VRAM destinations 0x25E18040 ...
  Palettes: the `cl_*` tables of PRGDATA.DAT (0x00296200 ..) are copied to the colour RAM shadow 0x060B8000.. by the table at 0x06004274 (routine 0x06004754, run by the game's init code, game type 10 uses 0x060042F0);
  the V-IN DMA copies the shadow to colour RAM.  The HUD uses palettes 2 (white / black digits), 3 (labels, dial), 4 (red messages).
* Region byte: `[0x0604000D]` **bit 0 is set by the boot code** (0x06004084) in the USA release: it selects `CGDATAUS.DAT` (0x06004390), the 20 x 3 cell wrong-way message and the other label layout of `init_game_disp` (0x060117E8 / 0x0601181A) and **mph** (x 0x430000 = 67) instead of km/h (x 0x6C0000 = 108) in the speed digits (0x06012084).  The oracle skips the boot code and used to leave the bit 0 (km/h, Japanese-layout labels with US characters); `Race.__init__` now sets it, `hud_init.dat` and the HUD traces were regenerated.
* **Per pass `game_display` (0x060123F8)** (jump table by `[0x060AA860]`; game types 0 / 2: 0x06011ECC wrong-way blink, 0x06011EF8, 0x06011F2C, 0x06012084 speed, 0x060120F0, 0x060121BC lap time, 0x0601308C **navigator message** (copies a 4 x 4 block
  of cell numbers from the table at 0x00298DF8 + 16 * (message - 1), message number `[0x060AAA45]`, to the name table at 0x25E3D2A8), 0x06012664 best times ...) rewrites the name tables.  It only touches the HUD fields of the car (+0xF2 / +0xF3: message timer) -
  car state after 900 passes is byte identical with and without it (`[V]`).

## 3. VDP1 sprites [V]

* `dispTachoMeter` (GBR = the car): screen offset word (-124, +72) (`0x060186E4` stores it for the polygon pipeline: it is added to the projected position), identity matrix translated to z = 0.5, rotated about z by the engine word (+0x8C):
  v = max(rpm, 800) * 0xE8BA >> 16; angle (turns) = v * 0x20C49 >> 32 below 4000, else (v - 4000) * 0x83127 >> 32 + 0x2000; scaled 1/16; part `tachometer` (a flat triangle).
* `dispCountDown(r5 = numeral 0 GO / 1 / 2 / 3, r6 = size)`: nothing when r6 = 0, else identity translated to z = 0.5 and scaled (factor[idx] * r6 >> 16) + offset[idx] (table 0x060107B4: 0x3000 / 0x1000 for GO, 0x2000 / 0x2400 for 1 - 3), parts `go`, `one`, `two`, `three`.

## 4. Oracle and reference frames

* `tools/hud.py`: `setup(race)` loads CGDATAUS.DAT, runs `init_vdp2sys`, the palette copy and `init_game_disp`; `display(race)` = one `game_display`; `render(race)` composes NBG2 over NBG3 with their scroll (`tools/vdp2.py nbg_plane / render_hud`).
  The oracle's CD hook now understands the display set-up's request record (`{u16 0, u16 3, u32 destination, name}`: MAPDATA.DAT is a plain file at 0x00200000).
* `refframe.capture(hud=True)` runs `game_display` after every pass and un-stubs the two VDP1 routines for the frame; `tools/export_frames.py` writes the name tables (`nt2` / `nt3` / `nt4`: index:value of the non-zero cells), the scroll (`scr`), the tachometer word (`tach`)
  and the count-down call (`cdown idx size`) into the frame files and composes the reference pictures with the HUD over the VDP1 / VDP2 picture.
* `tools/conv_hud.py` -> `hud.dat`: the 768 cells of 0x18000 .. 0x24000 as a 256 x 192 T8 atlas and eight RGBA5551 palettes (dot 0 transparent).

## 5. PSP renderer

`src/hud.c hud_draw`: for each layer (NBG3 first, NBG2 over it) and each palette used: one CLUT load and one triangle batch of the visible cells (45 x 29 with the sub-cell scroll), cell edges rounded together so the scaled picture has no gaps.
`src/event.c ev_hud` returns the needle / numeral as scene items (the object pipeline, near plane 0.5, screen offset folded into the translation).  Compared in PPSSPP: HUD text, dial, gear, speed, messages and numerals line up with the reference;
the needle is a thinner line (a 1-pixel-wide triangle on the GE, two pixels on the VDP1).
**PPSSPP quirk found on the way:** switching the GE clip planes off after the scene's last draw (a leftover from copying the sky code) made PPSSPP drop the scene's nearest polygons (the road at the bottom of the screen); `hud_draw` leaves them as `scene_draw` set them.

## 6. Name-table generators (M6): `src/hudgen.c`

`hudgen_pass(HudGen *, Drive *, frame)` is `game_display` (0x060123F8) for the single-player game types 0 and 2: the jump table by `[0x060AA860]` leads to the routines below; the screen it starts from is what `init_game_disp` leaves
(`extracted/psp/course/hud_init.dat`, `tools/oracle.py hudinit`: eight snapshots, game type 0 / 2 x course; it contains the dial face and gear box from MAPDATA.DAT, which the oracle only gets with the render set-up's CD hook).
The `HudGen` holds the three 64 x 64 name table pages (VDP2 VRAM 0x25E3A000 .. 0x25E40000) and the four scroll words; `src/play.c` hands them to `hud_draw` every pass.

| routine | what it writes |
|---|---|
| 0x06011ECC wrong way | NBG3 y scroll 0 (message shown) / 0x100 (hidden), blinking with `[0x060AA168] & 31`, message byte `[0x060AAA45]` = 30 |
| 0x06011EF8 | 3 x 3 cells of the dial at 0x25E3AC10: characters 0x30D8 + 18 * (+0xB0) |
| 0x06011F2C gear | 2 x 3 cells at 0x25E3A144 .. (digit from the BCD table 0x060A1000: character 0x2002 + 12 * digit; "1" is the narrow glyph 0x20CC) |
| 0x06012084 speed | three small digits (2 cells high) at 0x25E3AC98: `speed * 0x6C0000 >> 32` mph through the BCD table, characters 0x2090 + 4 * digit |
| 0x060120F0 time left | two big digits at 0x25E3A128 from `R+0x34 >> 8` (yellow 0x21C2, the last digit red 0x223A for 5 s or less) |
| 0x060121BC times | this lap's row (address pair from the table at 0x00298570 + 8 * lap): m ss hh from the R block lap slot; total time at 0x25E3A106 (two minute digits from 10 minutes); Lake in the arcade game: a blinking message instead |
| 0x0601308C navigator | car +0xF2 / +0xF3 (message number, frames left): 4 x 4 cells from the table at 0x00298DF8 + 16 * (message - 1) to 0x25E3D2A8, shown for 45 passes, cleared at the end and when driving backwards |
| 0x06012664 best times | does nothing while R+1 (the compare-mode byte) is 0 |

Verification: `tools/test_drive.sh <trace> -hud` (traces made with `oracle.py run --render --hud`, `tools/oracle_m6.sh`): the oracle runs `hud.display` after every pass and logs the changed words of the 0x3000 name table words and the scroll words; the C port must produce all 0x3004 words
after every pass (and the car slot, which the HUD changes at +0xF2 / +0xF3).  The oracle sets `[0x060AA168]` (the free-running frame counter the blinking uses) to the pass number; the port is called with the same number.

The end of the race (`ctrl >= 8`: goal, game over) runs another routine instead of the ones above: section 7.  Not ported: the demo (car mode 2), the 2P / VS variants, the red digits' beep once a second in the last five seconds (the other sound queue pushes are in `sndev.c`).  The time attack's HUD (game types 4 / 6), the compare with the best run (R+1) and the pause menu are in docs/timeattack.md.

## 7. The end of the race: goal and game over (VDP2 NBG0) [V]

`game_display` branches at 0x06012444 to 0x06012D18 when the car's control byte +0xF5 is >= 8 (8 = goal, 10 = game over; mode word +8 != 2).  That code is a **state machine on `[0x060AA178]`** (even values 0 .. 26, jump table 0x06012D24; the long after it, `[0x060AA17C]`, is the timer;
`Drive.ret_state` / `cd_timer`: the count-down uses the same two variables and leaves them at 0).  r13 = the R block (car + 0x500); its first byte **R+0** is the progress counter of the drawing routines (the slot compare used to mask +0x500..0x503 for this: -1 = everything drawn).
Ported as `hudend_pass` (`src/hudgen.c`); every NBG0 name table word and register after every pass is identical to the oracle on 15 end-of-race traces (1,000 - 6,200 passes, the last 420 after the goal / game over) and 48 synthetic cases.

**The layer.**  The end screens are drawn by NBG0 (BGON bit 0): two-word names (`PNCN0` bit 15 clear: palette in bits 22 - 16, character number in bits 14 - 0, flips in bits 31 / 30), 8x8 cells of 256 colours (`CHCTLA` 0x1010), plane size 1 x 1, 2 x 2 planes of 64 x 64 cells
(1024 x 1024 pixels, wrapping).  The map numbers `MPABN0` = 0x0A0B, `MPCDN0` = 0x0C0D give **plane A = map 0x0B (VRAM 0x2C000), B = 0x0A (0x28000), C = 0x0D (0x34000), D = 0x0C (0x30000)** (the low byte of a map register is the left / first plane), x 0x4000 bytes per page.
The cells say palette 0x10 = palette 1 (colour RAM 0x100 .. 0x1FF: the `cl_*` table of hud.dat); character n is the cell at VDP2 VRAM `0x20 * n`.  Character data: the result files of the stage, `CR<STAGE>.DAT` (arcade) / `PR<STAGE>.DAT` (practice), blocks at VRAM 0x00040 .. 0x162C0
(only the part from 0x77C0 on differs between stages and games; the block at 0x10440 is also in `GMGOVER.DAT`).  `tools/conv_hudend.py` -> `hude_<cr|pr>_<stage>.dat` (256 x 384 T8 atlas of VRAM 0 .. 0x18000).
**The pictures are prepared by `init_game_disp`**: banners and labels are already in the pages when the race starts (`TIME IS UP` / `RETIRED` / `RANK` on plane B, `FINISH!!` / `<STAGE> CLEAR!` / `RANK` / `LAP n` / `TOTAL TIME` on plane A, `GAME OVER` on plane D, rows 12 - 15 and 34 - 56; 730 - 1100 cells per set-up, `extracted/psp/course/hude_init.dat`,
written by `oracle.py hudinit`); the state machine adds the placing, the times and the labels that depend on them.  Scroll and BGON do the rest:

| output | meaning |
|---|---|
| BGON `[0x060AA8E6]` (and the register 0x25F80020) | 0x1C while racing; 0x10 on the first pass after the goal (RBG0 only: the HUD disappears); 0x11 from the second pass (+ NBG0) |
| NBG0 x `[0x060AA864]` (integer word, the V-IN recipe 0x060159DC masks it 0x3FF) | the goal writes 0x400 (= 0: plane A, "FINISH!!"); game over leaves x alone, and the game-over content is on plane B ("TIME IS UP"), so x must be about **0x200** at the race start [I]: a scene set-up at 0x06020DEC writes [0x060AA864] := 0x200 (0x260 when [0x06040018] = 3), the screens before the race are not run by the oracle: `tools/hud.py setup` writes 0x200 |
| NBG0 y `[0x060AA868]` | 0: the banner rows 12 - 15 are in view; **0x100 when the first timer runs out** (90 passes): the rows 32 - 59 with the placing and the times |
| WCTLA 0x25F800D0 | 0x82 / 0: arcade goal only, see below (the window rectangle itself is not known) |
| `[0x060AA803]` bit 0x40 cleared at the first pass; state 26: bit 1 set if bit 5 (a fade, started by 0x0600585C) is clear | the next screen (results, M8) |
| `[0x060AAA45]` := 36 at a goal | message byte |

**States** (value of `[0x060AA178]`, jump table 0x06012D24):

| state | what it does | next |
|---|---|---|
| 0 | first pass: bit 0x40 of [0x060AA803] cleared, BGON := 0x10, timer := 90; goal: x := 0x400, message 36; sound / CD calls (0x06016F10, 0x0600F756: CD track 6 goal, 7 game over, 0x06016FA4) | goal 16, game over 2 |
| 16 (goal) | the placing block (10 x 5 cells, words from the table 0x002987DC + 4 * placing byte [+0xB6]: 1st .. 7th, `+ 0x00100062`) at 0x25E2E550 (practice) or 0x25E2E650 / E650 / E450 / E550 (arcade, stage 0 .. 3); BGON := 0x11 | arcade 18, practice 22 |
| 2 (game over) | the same block at 0x25E2A550; BGON := 0x11 | arcade 4, practice 10 |
| 22 | practice goal, one pass each: the lap times (R+0x10 + 4 * lap, address pairs from 0x00298714 (+ 24 for more than 3 laps)), then the total (R+12, pairs at 0x00298754); timer 90 | 24 |
| 18 | arcade goal: the stage times of the game so far (R+0x38 + 4 * n, tables 0x00298684 / 88 / 8C / 90), then the total (R+12, pairs at 0x002986BC); stage 2 and a placing other than 1st writes WCTLA := 0x82 once; timer 90 | 20 |
| 10 | practice game over: one lap row per pass (lap time at address - 0x4000 + a label, 15 x 2 cells from the byte tables at 0x002985AC), then the "total" label (14 x 2 cells, characters 0x386 ..); timer 90 | 12 |
| 4 | arcade game over: the times of the earlier stages (R+0x38 + 4 * n, pairs from 0x002986D4); timer 90 | 6 |
| 24, 6, 12 | timer 120 (0x78) counts down | 26 |
| 20 | timer 120; WCTLA blinks 0x82 / 0 every 16 passes (stages 0, 1, and 2 with 1st place; stage 3 leaves it, stage 2 otherwise stays 0x82: a window that hides the "qualified" part of the picture [I]) | 26 |
| 22 / 18 / 10 / 4 when the timer reaches 0 | next state + 2, NBG0 y := 0x100, timer := 120, sound push 0x06017548 | |
| 26 | `[0x060AA803]` bit 1 (when no fade is running) = go to the result screen; `HudGen.done` | |
| 8, 14 | nothing | |

A **time** m'ss"hh is drawn by 0x06012C44: BCD bytes +1 .. +3 of the R block slot as 2 x 2-cell digits (character `0x00100002 + 8 * digit`, apostrophe + 0x50, double quote + 0x58), address pairs (upper / lower row) from the tables; from 10 minutes on the first digit moves a cell to the left.
The copy routines are 0x060056B4 (words), 0x060056BC (bytes), 0x06005704 (consecutive characters): a rectangle, 4 bytes per cell, row pitch 0x100.  Reads outside the converted tables (a placing of 0 or above 7 at a game over, `+0xB6` = 0 in the arcade game over) give zeros like the oracle's memory; the block is then blank cells.

Timeline of a practice goal (the first pass after the goal = 0): 0 BGON 0x10, 1 placing block + BGON 0x11, 2 .. 5 the three laps and the total, 91 y := 0x100, 211 state 26.  Not ported: the fade (0x0600585C: colour offset registers, `[0x060AA803]` bit 5), the window rectangle of WCTLA, the next screen.

**Oracle traces**: `tools/oracle.py run --hudend` (= `--hud` + 420 passes after the goal, `<name>.hude`: NBG0 pages as changes + 11 register words per pass), `tools/oracle_hudend.sh` (practice on the four stages, arcade goal on the desert, arcade game over on the other stages, no throttle in arcade / practice, manual gearbox / other cars),
`oracle.py hudsynth` (48 synthetic cases: both game types x stages x goal / game over x placings 0 .. 8 and -1 x laps x random times (BCD or not, 10+ minutes); the pilot is too slow for the later arcade checkpoints).  Checks: `tools/test_drive.sh <name> -map -hud -hude` (the car slot including R+0 is compared too, no mask any more), `tools/test_hudend.sh`.
**Renderer**: no tachometer needle once the control byte is >= 8 (the presentation routine 0x06006754 skips `dispTachoMeter`; `src/play.c` passes no rpm to `ev_hud`); `hud_draw_end` (`src/hud.c`) draws NBG0 over the picture when BGON bit 0 is set (the cells of `HudGen.nt0`, the same CLUT as the HUD, scroll `nscroll`), `hud_draw` skips NBG2 / NBG3 when their BGON bits are clear; reference pictures `tools/end_frame.py` (oracle replay + `vdp2.nbg0_plane`).

## 8. Open

* The end-of-race sound / CD / fade calls, the window rectangle of the arcade goal's blinking part, the next screen (results and name entry: M8).  The initial NBG0 x of a game over (0x200) is inferred from the code (section 7).
* Count-down sequencing is in `src/drive.c` (`count_down_start`); the other screens (replay, pause, 2P) are not examined.
* Fade in / out (colour offset registers), the colour calculation / transparency of the co-driver messages and 2P layouts.
