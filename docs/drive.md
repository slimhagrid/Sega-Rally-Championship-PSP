# The driving core (M6): `src/drive.c`, `src/camera.c`, `src/pmap.c`, `src/play.c`

The player's car, the start sequence, the race flow and the camera, ported by hand from the original SH-2 code and checked **byte for byte** against the oracle
(`tools/test_drive.sh`).  `docs/physics.md` is the reading of the original; this file says what was built and what the port taught us (it corrects `physics.md` in a few places).

## 1. Layout

| file | what |
|---|---|
| `src/fx.h` | the SH-2 arithmetic idioms (`FM` = dmuls + xtrct, `HM` = mach, `M24`, `X16` of a 64-bit accumulator, the on-chip divider `div64` with its saturation), big-endian field access, `unit_matrix` / the `t_rotate_*` family, `t_sin` (the polynomial of `math::t_sin` 0x0601B6B4) |
| `src/drive.c / .h` | `drive_init` (init_game_work for the player), `drive_move` (slot function 0: grid, race, goal, game over), `drive_present` (slot function 4: count-down driver), `drive_pass` = both.  The car is the original's **0x600-byte slot as big-endian bytes at the original offsets**, so a pass can be compared with the oracle's `.slots` dump with `memcmp`. |
| `src/camera.c / .h` | `camera_init` / `camera_pass` = `camera_ctrl` (0x06005E20): fly-by on a B-spline, the 3-pass pause, view A / B, goal orbit; the view matrix (0x060D1000) and the work area (0x060AA9B0) |
| `src/pmap.c / .h` | `pmap_build` = `car::get_poly_map` (0x060081B8): the list of course blocks to draw |
| `src/coli.c` | + `coli_wall_d` (get_d), `coli_find_quad` / `coli_find_slice` (get_block's two grids) |
| `src/play.c / .h` | play mode (section 5) |
| `tools/conv_phys.py` | `extracted/psp/course/phys.dat`: the constant tables (torque curves, gear sets, surface dynamics, tuning rows, sine table, start poses, time-extension tables, goal poses) copied byte for byte with their **original addresses**; `src/drive.c` reads them with `pm_r32(addr)` like the SH-2 code did |

Everything is integer arithmetic with the original's rounding; the only floats are in the renderers.  Build flag `-fwrapv` (32-bit wrap-around as on the SH-2).

## 2. What was ported

`execCar` and everything it calls: `getIO_pad` (analog and digital), `getSteer`, push-out (`FUN_06007954`), wheel probes with wall response (`FUN_06007718`, `FUN_060078A4`), body corners, re-probe, slip angle,
suspension (`FUN_06007304 / 060073C8 / 060074EC`), spring torques (`FUN_0600716C`), `carControl` (tyres, load transfer, friction circle), `PowerGet / PowerUse / TorqueGet / AutoGear / BrakeForce`, torque -> angular velocity,
air damping, wall assist, integration, drag, speed cap, angle wrap; start-up (`carconfig` with the tuning tables, `mycar_init`, `PowerInit`, `initCarControl`, `lap_time_init` for game types 0 and 2);
the state functions (`normal_plinit`, `normal_plstart_wait`, `normal_plmove`, `goali_plmove`, `goal_plmove`, `gover_plmove`), the count-down driver (`count_down_start`), `get_block`, `calc_reverse`, `calc_dispang2`,
`check_lap` (types 0 / 2), the time limit (`extend_time_calc`) with the extension tables, the game-over rule, `goal_car_move`, and the **race clock** (section 2a).

### 2a. The race clock (R block = car + 0x500)

Times are BCD longs `0x00MMSSHH` (minutes, seconds, hundredths); the tables `time10_tbl` / `time100_tbl` (0x060A1208 / 0x060A1248: frame -> hundredths) are in `phys.dat`.

| routine | original | what |
|---|---|---|
| `lap_time_calc` | 0x06014DDC | the stop-watch of the running lap: R+5 minutes, R+6 seconds, R+7 frames (0..29) -> the BCD slot R+0x10 + 4 * lap; 10 minutes = the cap `[car+0x2A8]` = 0x00095999 |
| `total_time_calc` | 0x06014E34 | R+0xC = R+8 (the laps before) + the running lap, digit-pair BCD add with the original's limits (10 / 6 / 6); minutes overflow = 0x00595999; not on Lake in the arcade game |
| `time_adjust` | 0x060150B4 | 0..3 hundredths: the car is that far past the section line when the pass counted it (the line from the slice record 0x293000 + 0x2C * seg, `mac.l` + hardware divider by speed * section constant) |
| `split_record` | 0x06014C08 | a section mark (+0xF7 != 0) was passed: the adjusted lap and total time are pushed to the record buffers (`R+0x4C` / `R+0x48`) |
| `newlap_record` | 0x06015014 | a new lap: the finished lap's slot and R+8 are corrected by `time_adjust`, the best lap R+0x30 (bit 31 = "new best", not in mirror mode), `lap_end_record`; returns the value the original leaves in r3 (the caller's `R+0x30 byte = 0` store only hits the R block when no correction was made - otherwise it goes to the console's ROM) |
| `lap_end_record` | 0x06014CE8 | the lap time and the total time of the finished lap go to the buffers, the per-lap pointers are reset |
| `goal_record` | 0x06014F58 | the goal: the last lap and the total are corrected, best lap, the sum of all lap times in R+0x38 + 4 * course |

The record buffers (`Drive.rec`, 0x060AA400 .. 0x060AA800: lap time buffers, split buffers, the best-run buffers) are written exactly as the original does; the compare with a stored best run (`R+1 != 0`, 0x06014D58 tail, 0x06014EC4) is ported but not exercised by any trace
(the oracle leaves R+1 = 0).  The sound pushes are in `sndev.c` (docs/sound.md); the new-best-lap jingle is not ported.

## 3. Verification (docs/oracle.md)

`tools/oracle.py run --slots` writes the whole car slot after every pass; `tools/test_drive.c` replays the same inputs through the C port and compares all 0x600 bytes after every pass.

| traces | passes | result |
|---|---|---|
| M4 set: Desert accel / brake / slalom / coast, full races Desert / Forest / Mount / Lake (pure pursuit, hundreds of wall touches, goal + 60 passes after) | 28,000 | 0 passes differ |
| arcade game type 0 on every stage (time carried over, game over by time), cars 1 and 2 (the special car: own dynamics table, gear set B, torque x 1.0625), manual transmission with shifting, difficulty 1 / 2, tuned setups (`--setup 3,2,1,2`), mirror mode | 40,000 | 0 passes differ |
| camera (`--render`: view A and view B with toggles, fly-by, pause, goal orbit, mirror) on every stage: work area, both view matrices, mode bytes, **and the part list of `get_poly_map` every pass** | 27,000 | 0 passes differ |
| race clock + HUD (`--rec`, `--hud --render`): R block, record buffers, all 0x3004 name table / scroll words of `game_display` after every pass (arcade + practice, every stage, manual gearbox, cars 1 / 2) | ~60,000 | 0 passes differ |
| the end-of-race screens (`oracle_hudend.sh`, `--hudend`: 420 passes after the goal / game over; practice goal on every stage, arcade goal on the desert, arcade game over on the other stages, no throttle in both game types, manual gearbox, car 2): NBG0 name tables, scroll, BGON, WCTLA, state machine, **and the car slot including R+0..3** (the state machine's progress counter; the mask is gone) + 48 synthetic cases (`oracle.py hudsynth`, `test_hudend.sh`) | ~70,000 | 0 passes differ |

Not compared (masked in the test, see `tools/test_drive_all.sh`): the sound-state bytes (+0x364..0x36F: `FUN_06008BCC`; +0xC6: `game_se_set`).  The whole R block (stop-watches, lap and total times, best lap, record pointers) **is** compared since the race clock was ported,
and `oracle.py run --rec` also dumps the record buffers at the end of the run (`<name>.rec`), which the test compares.

## 4. What the port taught us (corrections to physics.md)

* `rotate_y` turns the opposite way from `rotate_x` / `rotate_z` (it pushes `cos, sin, -sin, cos`, the others `cos, -sin, sin, cos`); the forward vector of yaw 0 is +z, `x = -sin(yaw)`.
* In the wall branch of `FUN_06007718` the wheel record's `h` (+0xC) is the **running maximum** of the wall depth over the wheels probed so far (the delay slot stores r0 after the max update), not the depth of that wheel.
* `carControl`: the front axle negates its lateral force *before* the friction circle, the rear axle *after* (the product `FMUL` is not odd, so the order matters bit for bit); the rear clamp is `0x1921F`, the front `0x19220`.
* `FUN_06009718` (rotate x/z by the steering / heading) uses `0x28BE60DB`; `r_rotate_*` / `r_sin` use `0x28BE60DC`.  One unit in the last place of that constant changes results.
* The camera's `normal_cainit` works in the car's scratch matrix (+0x130), and so does the goal orbit; `calc_dispang2` reads the camera's yaw words (0x060AA9C6 / 0x060AA9CC), so the player's view sector depends on the camera.
* `[0x060AA01A]` is the **camera mode** (4 fly-by, 6 pause, 0 race, 2 goal orbit); it also decides the length of the grid wait (104 passes while the fly-by is on, 25 otherwise).
* The goal presentation (`0x060068E0`) cuts the part list to 5 blocks.
* `count_down_start` hands (numeral, size) to `dispCountDown`: states 4 / 6 / 8 show 3 / 2 / 1 with the running timer as size, state 10 shows GO.
* Off the map (no polygon under a wheel) the original reads address 0 and then traps in debug builds; the port keeps the wheel in the air instead.

## 5. Play mode (`src/play.c`)

`data/play.txt` (lines: `course N`, `type N`, `car N`, `mt N`, `diff N`, `mirror N`, `setup a b c d`, `view 0|2`, `rect 0|1|2`, `replay name`, `hold N`) starts it instead of the frame viewer.
Per 30 Hz pass: pad -> `DriveIn` (analog nub = steering axis with a dead zone, X accelerate, SQUARE brake, TRIANGLE / CIRCLE shift up / down, L view toggle, D-pad as digital steering, START exit), `drive_move`,
`camera_pass`, `drive_present`, `pmap_build`, then the M5 renderers: car (`car_display` from the slot), event objects (`ev_frame`, state persists between passes), HUD sprites (`ev_hud` with the count-down numeral), course parts, sky from the camera's yaw / roll words.
`replay name` plays the recorded inputs of an oracle trace (`extracted/psp/replay/<name>.inp`, copy of `extracted/oracle/<name>.inp`) instead of the pad, `hold N` freezes after pass N: PPSSPP screenshots of pass N are compared with the reference frames
(`tools/run_ppsspp.ps1 -Play "course 0;replay desert_drive.inp;view 2;rect 0;hold 329"`, `tools/compare_frames.py`).

Picture placement (real-PSP test, 20 Oct 2026): the game picture is always drawn at 352 x 224 into an off-screen buffer (VRAM 0x110000, `view_begin` / `view_end` in `src/render.c`) and copied to the screen without distortion: `rect 0` pixel for pixel in the middle (sharpest), `rect 1` (default) scaled to the screen height (428 x 272, nearest), `rect 2` the same with bilinear filtering; SELECT cycles them.  The old odd-sized viewports (363 x 272, stretched 480 x 272) are gone: on the PSP the 363-wide one showed 3D geometry past its right edge.  `debug 1` in `play.txt` prints the old text line.  **`data/perf.txt`** (written by `src/perf.c` during play): per 300 passes the average / maximum of logic, scene build, display-list build and GE wait times, a vblank histogram (2 = 30 Hz) and one line per slow pass with scene size and track position.

## 6. Open

* The sound queue; the end-of-race HUD is ported (docs/hud.md 7), its sound / CD / fade calls are not.  The compare with a stored best run (R+1) is ported (docs/timeattack.md 1: all game types, checked with `oracle.py run --cmp`).
* (The start-object push-back is in src/event.c / play.c now: docs/events.md 4.)  Game types 4 / 6 (time attack) are ported: docs/timeattack.md; game types 8 / 10 (two cars, VS handicap) and the time attack's ghost car are not.  Opponents: docs/opponents.md (play mode runs the original's start-up routine when game type is 0 / 2: `opponents 0` in play.txt turns them off).
