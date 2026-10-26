# Time Attack and the pause menu (game types 4 and 6; October 2026)

Evidence tags: **[V]** checked against the oracle (the original's code run in `tools/sh2.py`), **[I]** read from the code, **[U]** not done / unknown.

## 1. What the original does

`[0x060AA860]`: **4 = Time Attack, three laps; 6 = Time Attack, free run** (the menu's "3 LAPS / FREE RUN" page, `menu_timeattack.c`).  The player's car is the same car: `execCar` and the camera are the arcade's [V: slot, camera and part list of
types 4 / 6 are identical to the oracle's over 8 traces of 6,000 - 9,000 passes].  What differs is the rules around it:

| | type 0 / 2 | type 4 | type 6 |
|---|---|---|---|
| race start-up (`FUN_06010FE0`, table 0x06010FF4) | other cars (`normal_otinit`, `otherCtrl`) | `car_move2p_ctrl` (the two car slots: the player and the "myline" ghost) | the same |
| `lap_time_init` | time limit, laps 3 / 2 | no time limit, 3 laps (0x0601434E) | free run: `[0x060AA006]` = {4, 5, 4, 5}[course] (0x06014348), flags R+124 / 125 / 126 (0x06014322) |
| `check_lap` | 0x0601465C | 0x06014750 (section marks call `FUN_06014C08` + the compare sound) | 0x06014810 (every crossing of the line starts lap 0 again) |
| `extend_time_calc` (time limit) | counts down | returns | returns |
| `game_display` (HUD) | 0x06012416: gear, time left, lap rows, navigator | 0x06012472: no gear / time left; best-time panel, compare, retime blink | 0x060124D2: free run: lap row + area (section) rows |
| end of the race | `hudend_pass` (NBG0 result pictures) | `result_display` table 0x06012F84 (4 states: jingle, new-record blink, 210 passes, done) | none (never ends by itself) |
| NBG0 characters | CR / PR<stage>.DAT | CGDATA_F.DAT (`hude_f.dat`) | CGDATA_F.DAT |
| `game_exit` | arcade / practice routing | vector 0x0602FF84 (post-goal dispatcher), history | - |

A stored best run (`[0x060AA815] == 4`: the **ghost**, `myline_set` 0x060049B8, slot 1 at 0x060B0600, replay inputs at [0x060AAA60] = 0x002C0000 + stage * 0x3D00 + 104) races beside the player in type 4 [V, section 5].  The player's own
inputs are recorded per pass at R+0x74 (start [0x060AAA5C] = 0x002CF800, end + 0xF7FE) for the name entry's `bup_replay_save` [V, section 5].

The compare with a best run (`R+1`, option `[0x060AA80E]`, reference list R+84 = 0x060AA538 for the option 1, R+88 = 0x060AA658 for 2) is shared by every game type: `init_record_pointers` computes R+1 from the option and the reference list and clears the current run's
lists (`rfillff`), `rec_tail` (0x06014D58) computes R+100 / R+104, `timecmp_display` (0x06012664, types 0 / 2 / 4) and `timecmp_display_f` (0x0601269C, type 6) draw it [V: traces with `--cmp`, all four game types].

## 2. What is ported (src/drive.c, src/hudgen.c)

* `lap_time_init` (types 4 / 6), `check_lap_t4` / `check_lap_t6` (+ `hud_retime` = `re_1x1time_ft` 0x06011CDC), the goal of type 4 (`goali_move`: the new-best-lap flag and the corrected last lap redrawn), `init_record_pointers` (R+1, rec clearing).
* HUD: `timeattack_pass` (types 4 / 6), `free_run_times` (0x06012278), `retime_etc` (0x0601289C: the blink of a new best lap), `timecmp` / `timecmp_f` + `cmp_show` (0x06012718), `blink` (0x060127DC: the window register 0x25F800D2 is `HudGen.wctlb`),
  `laps_number` (0x06011D4C), `ta_end` (0x06012F8C ..), `hudgen_ta_init` (the display set-up's dynamic part: the three times of the panel, the car / transmission boxes, the record box).
* Menus: `race_exit` (types 4 / 6: vector 0x0602FF84), `race_history` (game_exit's tail, also used when a race is left through the pause menu).
* Data: `hud_init.dat` / `hude_init.dat` hold 16 set-ups (game type 0, 2, 4, 6 x course; index = type / 2 * 4 + course), `hude_f.dat` = CGDATA_F.DAT's characters (tools/conv_hudend.py).

## 3. The pause menu (all menu-launched races)

The race loop 0x06011170 is a state machine on `[0x060AA807]`: START (from the count-down on: `[0x060AA803]` bit 6) pauses (the sound driver and the CD music stop, the engine words are 0), the box **EXIT? / NO / YES / RESTART** (8 x 8 cells at 0x25E3A5A4,
`pause_display` 0x06013BA4) appears, UP / DOWN move the cursor (the item row blinks: 8 passes in the highlight palette, 8 normal, `pause_cursor` 0x06013BCC), START chooses.  NO continues (`pause_delete`).  YES / RESTART leave the race: the screen fades out, the next
screen is chosen by game type (table 0x06011200: arcade -> SELECT GAME / the first stage again, practice -> SELECT GAME / the race again, time attack -> the post-goal dispatcher), `set_gover` makes the control byte 10 (YES) / 12 (RESTART) and records the stage's result.
`play.c` runs it (`PlayOut.retire`), `menu_host.c host_retire` does the routing.  Not ported: the combination that hides the box (0x060113F0 ..: the held pad word 0x0070 = X + Y + Z of the Saturn pad; the PSP has no Z button, so it cannot be pressed), the sound driver's pause command (only the engine words and the CD music).

## 5. The ghost and the input recording (src/drive.c: `drive_ghost_*`, `make_gswdata`; src/play.c)

* **Recording** (`make_gswdata`, play mode 0): every pass the 16-bit game part of the input word (axis byte, button byte; the view bits are not recorded) goes to R+0x74, which then advances by 2, until it reaches R+0x78.  The host gives the work RAM
  (`DriveCfg.rec_win`, `PlayCfg.rec_win`): the words land at 0x002CF800 where `bup_replay_save` copies them into the stage's stored block.  A later stage of the arcade chain goes on where the stage before ended (the R block carries the pointer).
  [V: `<name>.irec` of the oracle = the port's buffer, 8 traces.]
* **The ghost** is a second `Drive` (slot 1): `drive_ghost_init` = carconfig's second player (`d->slot1`: its own dynamics block 0xD0 behind player 1's, the tuning bytes [0x060AA81C ..], the car [0x060AA813], the set-up mode [0x060AA816], the transmission
  [0x060AA812]) + `myline_set` (play mode 4 = playback, car, R+0x74 = the stored run).  Per pass: `drive_ghost_move` (0x06006078 = normal_plinit's twin, then 0x06006254 / 0x06006340: playback word, `execCar`, `get_block`, `lap_myline` 0x06014B4C (its lap count; the end of its
  last lap stops it), `calc_reverse`), `drive_ghost_present` (0x06006784: `dispCar` mode 0, alternating with 3 = not drawn, every pass unless [0x06040062] bit 3), `drive_p2sync` (0x060067A4: the count-down's end (g180 = 2) switches the ghost to its moving state, g180 = 6 starts its
  presentation).  It is drawn like the other cars (its dust too, in both modes).  [V: `tools/oracle_ghost.sh`: the whole slot 1 after every pass, four courses with cars 0 / 1 / 2, manual transmission and set-ups, the ghost finishing first.]
* The ghost uses the settings of the stored run, not yours (set-up, car, transmission); a stored run made with other set-ups still follows its line because the inputs are replayed on its own physics.

## 6. Replays (play mode 2, game type 4: the result screen's "replay: yes" and a record's replay)

`[0x060AA810] == 2`.  The result screen's choice YES sets the vector 0x06015408 (the stage start) and the mode; the Records screen's replay (`record_replay_start` 0x0602D066) copies the stored run's settings, times and the pointer [0x060AAA5C] = the stored block + 104
(the words are read in place).  After the stage start screen `host_race` runs the replay: `PlayCfg.mode2`, `DriveCfg.replay`.

* **The car** (`make_gswdata`, mode 2): the next word of the recording (rec_win from rec_start = [0x060AAA5C]: 0x002CF800 for the run just made) is the input; everything else is the live physics [V: the oracle's replay of a recording: the slot after every pass, 4 courses].  `check_lap` / the clock
  skip the lap bookkeeping (mode 2 tests in the lap code), `fn4` is 0x06006710 (count_down_start_replay: no voice, the stage's music at the first pass on the first stage, the sound timeline `replay_sdctrl` 0x06016AD4: `sndev.c`) and then 0x0600680C (replay_plmt: the player's car is always drawn,
  `calc_dispang2` / `get_poly_map` only with the chase camera).
* **The cameras** (`camera.c replay_pass`): `normal_cainit` starts `ending_camera_act` 0x0601F4AE / 0x0601F4C8: the camera of CAMERA.DAT for the player's road slice (the table of the stage at 0x002A2000: type 1 = a fixed eye that looks at the car, type 2 = fixed eye and angles,
  type 0 = the chase camera `replay_ca`), the view matrix of the photo scene (no horizontal stretch), the part list from the road block at the eye (`calc_camera_pmap`: a dummy car, `drive_dummy_init`), the pipeline's perspective 0x200 (`scene_set_pers`; the goal's orbit is the chase camera's: 224).
  The pad's A / B / C (Cross / Circle / Square) toggle cut scenes <-> chase camera, X / Y / Z (Triangle / L / R) change the chase camera's view (`camera_replay_keys` = `replay_cactl`) [the keys: not verified]. [V: camera work area, view matrices, part list after every pass, 4 courses, the goal's orbit.]
* **The HUD** (`hudgen.c`): the display set-up of a replay is one more set of `hud_init.dat` / `hude_init.dat` (index 16 + course; `HUD_REPLAY`), the lap rows show the run's times (`hudgen_replay_init`: R+0x8C = the total, R+0x90 .. the laps), every pass `replay_display` 0x06013A84 (the blinking
  REPLAY sign, the window of the lap row), the dial and the speed; after the goal `resreplay_display` 0x06014138 (90 + 120 passes, then the screen is over). [V: name tables, scroll words, NBG0 pages and registers after every pass.]
* **The way out**: START opens the pause menu as in a race (YES: the post-goal dispatcher 0x0602FF84, which skips the history in a replay and goes to the result screen / the Records screen; RESTART plays the replay again: 0x06010F0C); the end of the replay does the same as YES.

* **A new best total and the course record** (`ta_end` state 2, `retime_etc` 0x0601289C): the new total blinks (draw 10 passes, cleared 8, ...), a course record also draws the record box (`record_sub` 0x060118FC on the second plane) and its own time.  The blink machine runs twice in the pass that
  sets it (state 2, then state 4) [V: `tools/oracle_recbox.sh`: three traces with a stored record (the three flag values 0 / 0x80 / 0x40), the HUD name tables after every pass; the record box itself is outside the compared planes].
  Replays have spray / dust like races (checked by eye); "tyre marks" in older notes were the spray pools (0x060D0140 / 0x060D0160), which are ported.  The call 0x0600F804 of the replay's count-down on the later stages is a CD block command (the track is prepared, nothing audible).

## 4. Checks

```
wsl -d Ubuntu -- bash tools/oracle_ta.sh                       (python: the traces below; or by hand: oracle.py run --course N --type 4|6 --slots --rec --render --hudend --tag _t4end)
wsl -d Ubuntu -- bash tools/test_drive.sh desert_drive_t4end -map -hud -hude      (every trace: tools/test_drive_all.sh)
python tools/oracle.py run --course 0 --type 4 --car 1 --cmp 1 --ref extracted/oracle/desert_drive_t4end.rec --tag _t4cmp --slots --rec --render --hudend     (the compare; test_drive reads <name>.cmp)
bash tools/oracle_replay.sh; bash tools/test_drive.sh desert_drive_rpt -map -hud -hude   (a replay: slot, camera, part list, HUD, the end sequence)
bash tools/oracle_recbox.sh; bash tools/test_drive.sh desert_drive_rb0 -hud   (a new best total / course record: the blink and the record box; _rb0 / _rb1 / forest _rb2 = the three flag values; ORACLE_WATCH=hexaddr:len prints changes of memory bytes)
bash tools/oracle_ghost.sh; bash tools/test_drive.sh desert_drive_gg      (the ghost: slot 1 after every pass + the recorded input words; _gr = a run that records, _gg = a run with that recording as the ghost)
python tools/pause_vec.py; bash tools/test_pause.sh             (the pause menu's cells: 8 cases, 984 steps)
python tools/hudinit_vec.py; bash tools/test_hudinit.sh         (the display set-up with car / transmission / records: 48 cases)
```
Development aids in the PSP build: `data/race_pad.txt` ("pass hexmask" PSP buttons for a race started from the menus), `data/race_trace` (-> `data/race_trace.txt`: the car's position every 15 passes), `data/race_cfg.txt` (what the menus asked for),
`data/menu_race.txt` "replay NAME" (the oracle's inputs; the menus leave the **steering tuning byte** `[0x060AA817]` = the option's default (1) in the car's set-up: `oracle.py run --steer 1` makes a matching trace, otherwise the replay drifts off the road).
