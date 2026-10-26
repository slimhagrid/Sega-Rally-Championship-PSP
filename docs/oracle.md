# The oracle (M4): the original driving code, run headless

Purpose: the C port (M6+) is checked against the **original `0RALLY.DAT` machine code** instead of against our reading of it. The code is run in our own SH-2
interpreter on the real course data; everything it produces is written to files the port can be diffed against. Tools only - nothing here ships on the PSP,
and all generated traces live in `extracted/oracle/` (git-ignored, derived from the user's disc).

## Parts
| file | what |
|---|---|
| `tools/sh2.py` | SH-2 interpreter: the complete SH-1/SH-2 integer instruction set (delay slots, `mac.l`/`mac.w` with the S flag, `div0s/div0u/div1`, `dmuls/dmulu`, `tas.b`), on-chip divider (DVSR/DVDNT/DVDNTH/DVDNTL, 64/32 signed, truncating, saturating), DMAC channel 0/1 (immediate transfer when DMAOR.DME is set), High/Low WRAM + sparse pages for everything else (VDP/SCU/SCSP registers just read back). Hooks (`cpu.hooks[addr]`), write watches (`mem.watch`), pc history (`cpu.hist`). About 1.3 M instructions/s. |
| `tools/saturn_env.py` | `Race(course, game_type, cartype, mt, difficulty)`: loads `0RALLY.DAT` @0x06004000, `PRGDATA.DAT` and `BG<course>.DAT` work-RAM blocks, writes the menu settings, runs the game's own `init_game_work` (0x06004844), then `step(accel, brake, steer)` = one 30 Hz pass: slot-0 `fn0` (normal_plinit -> normal_plstart_wait -> normal_plmove ...) followed by `fn4` (the countdown driver, HUD). The pad is the "analog device" layout at 0x060AA990 (B = accelerator, Y+Z = brake, axis byte at 0x060AA99C top byte, 127 = centre, 255 = full right), so `getIO_pad`, `getSteer`, `config_keyset` run unmodified. |
| `tools/oracle.py` | `run` (scenario -> trace), `replay` (re-run a recorded input file and compare), `compare` (diff two traces: oracle vs the C port), `list`. |
| `tools/oracle_fn.py` | function-level golden vectors -> `extracted/oracle/vectors.json`. |
| `tools/oracle_all.sh` | regenerates all of it. |

## What the oracle runs, and what it does not
Runs (the real code, unmodified): the complete race start (grid, countdown 3-2-1-GO with its 25+33*3+... passes, state `+0x0A` 0 -> 1), `execCar` (whole physics),
`get_coli_area` / `get_y` / `get_d` on the real collision tables, `get_block` / `check_lap` / time extension / game-over rule, pad -> input conversion,
`calc_reverse`, `normal_plmt_start` / `normal_plmt` (map lists). Lap counting and goal detection are the game's own.

Stubbed (hooks that `rts`): the sound command queue push (0x06016FBC, 0x0601D6CC) and the three HUD routines that draw into the polygon pipeline
(`dispCountDown` 0x06010710, `dispTachoMeter` 0x06010680, `dispCar` 0x060101E8).
Not run at all: camera, polygon pipeline / VDP, sound driver, opponents (`otherctrl`), the slave SH-2, replay recording (R+0x74/0x78 point at a scratch buffer
at 0x060F0000 so the live-input recorder has somewhere to write), menus. So **there are no other cars**: no car-car collisions, no VS handicap.
Settings used: practice-style `game type 2` (generous start time: 50 s, Lake 85 s), AT, car 0 (Celica), difficulty 0. `--type 0` gives the arcade start time
(Desert 60 s; the other courses only the leftover time of the previous stage, i.e. nearly nothing from a cold start).

## Traces (`extracted/oracle/<course>_<scenario>.{csv,inp,slots}`)
- `.csv`: one row per logic pass, `TRACE_COLS` of `tools/oracle.py` - the inputs of the pass (`accel`, `brake`, `axis`), the state function pointer `fn0`, and the car
  fields *after* the pass as raw signed integers (position `px py pz`, rotation `rx ry rz`, velocity `vx vy vz`, angular velocity `wx wy wz`, `steer`, `speed`, `rpm`,
  `gear`, `contact` (+0xB2), `mask` (+0xF6), the four wheels' surface types `surf0..3`, `state` (+0x0A), `mode` (+0xF5), `seg` (+0x60), `lap` (+0xC8), `timer`
  (R+0x34 = `(seconds<<8)|frames`)). 16.16 fields: divide by 65536.
- `.inp`: header line `# course=0 type=2 mt=0 car=0 diff=0`, then `accel brake axis` per pass **from the very first pass** (the grid / countdown passes are part of the
  stream). `oracle.py replay x.inp` reproduces `x.csv` exactly (determinism check, 0 differing rows).
- `.slots` (`run --slots`): the whole car slot (0x600 bytes: car struct + the lap / time block at +0x500) after every pass - u32 passes, u32 bytes, data. Lets the C port find
  the *first diverging field* instead of just the first diverging row.
- Scenarios: `drive` (pure pursuit on the AI guide line, a few hundred wall touches included), `accel`, `brake`, `slalom`, `coast` (scripted, Desert).

## Golden vectors (`vectors.json`)
`math` (sin, cos, r_sin, r_cos, sqrt, atan, r_atan: inputs, raw r0), `collision` per course (query point in 11.20 units -> polygon index, flags, floor `y` or wall `d`), `block`
per course (`get_block` on the guide line and 2 m beside it: segment, previous segment, checkpoint byte, block id, selector, flag, direction vector).
The original only gets queried where the game does (non-empty grid cell); empty cells make the real routine walk garbage.

## Quirks the port has to keep (found while building the harness)
- `mac.l @Rm+,@Rn+` with Rm == Rn (used by the collision scan, 0x060080D6 etc.) reads the *next* word for the second operand: sequential post-increment.
- The wall response (`FUN_06007718` tail, 0x06007818..) adds `HMUL(sum of four wall pushes, 0x418937)` to the velocity whenever any wheel probe returned a wall polygon
  (`+0x100` != 0); a missing/uninitialised record buffer makes `make_gswdata` scribble on address 0 - not a game behaviour, an environment artefact fixed in `Race`.
- The game relies on `0x060AA803` bit 2 = analog pad; bit 4 skips a dev-environment DMA from 0x023F0000 into Low WRAM (harmless).


## Addendum (M5 events): the random number generator

`0x0601D6CC` (listed in `SOUND_STUBS` since M4) is **not** a sound call: it is the game's RNG (`r = r * 5 + 0x3537`, state at 0x060D0050, returns r). The stub returns immediately with r0 = its own address,
so every "random" value in the M4 traces is the constant 0x0601D6CC (the traces and `vectors.json` stay valid: in 1,500 Desert passes the generator is called 12 times (race start only) and the player's car structure is bit-identical with the stub and with the real generator).
`Race(..., real_rand=True)` leaves it un-hooked; `tools/evvec.py` and `refframe.capture(events=True)` use it. Event objects need the real generator (docs/events.md).

## Addendum (M5 HUD / helicopter)

* **Display set-up** (`tools/hud.py`): the oracle never ran the game's display init.  `hud.setup` loads CGDATAUS.DAT, runs `init_vdp2sys`, the palette copy (`0x06004754` with the table 0x06004274) and `init_game_disp`; `hud.display` runs `game_display` after every pass
  (`refframe.capture(hud=True)`).  Car state is byte identical with and without it except the HUD's own message bytes +0xF2 / +0xF3 (900 passes).  The CD hook (`saturn_env._cd_load`) also handles the display init's request record (`{u16 0, u16 3, u32 destination, name}`: MAPDATA.DAT, a plain file).
* `draw_pass(hud=True)` un-stubs `dispTachoMeter` / `dispCountDown` for the frame and records their inputs (`Race.hud_calls`); `refframe.capture(laps=, lead=)`: a one-lap race (the desert helicopter appears on the last lap, slice 0xD6, after ~1,800 passes) and `lead` passes of
  object dispatching before the frame (slot memory, RNG and flags restored afterwards) so that the helicopter flies at the frame.  `tools/evvec.py --laps=1` records the helicopter's draws pass after pass (`ev_0_0_heli.vec`, `ev_0_2_heli.vec`).
* `tools/export_math_vec.py` flattens the math / collision vectors for `tools/test_gmath.c` (sqrt, atan, `get_coli_area` / `get_y`).



## Addendum (M6 race clock and HUD traces)
`oracle.py run --rec` also writes `<name>.rec` (the record buffers 0x060AA400.. at the end), `--hud` writes `<name>.hud` (the initial name tables + scroll words of the race screen, then per pass the changed words after `game_display`; the oracle sets the frame counter `[0x060AA168]` to the pass number);
`--hud` implies the render set-up (the display init needs the CD hook to load MAPDATA.DAT: dial face, gear box).  `oracle.py hudinit` writes `extracted/psp/course/hud_init.dat` (game type 0 / 2 x course).  Compare with `tools/test_drive.sh <trace> -hud [-map]` / `tools/test_drive_all.sh`; `tools/cov_drive.sh` lists the lines of `src/drive.c` no trace executes.

## Addendum (end-of-race HUD traces)
`oracle.py run ... --hudend` = `--hud` + 420 passes after the goal / game over (instead of 60) + `<name>.hude`: u32 passes, the initial 0x8000 + 11 words, then per pass the changed words of NBG0's four name table pages (VRAM 0x25E28000, 0x10000 bytes) and 11 register words (NBG0 scroll x / y shadows, BGON shadow and register,
WCTLA, state long, timer long, `[0x060AA803]`, `[0x060AAA45]`).  `--vmax / --curve_k / --steer_k` set the pilot, scenario `idle` never touches the throttle (the clock runs out).  `tools/oracle_hudend.sh` makes the 15 traces; `oracle.py hudsynth` writes `hudsynth.bin` (synthetic end-of-race cases);
`oracle.py hudinit` also writes `hude_init.dat` (the NBG0 cells the display set-up draws).  Checks: `test_drive.sh <trace> -map -hud -hude`, `test_hudend.sh`.  `tools/hud.py setup` leaves NBG0 x at 0x200 (what the screens before the race leave, docs/hud.md 7).
`tools/end_frame.py <trace> <pass>` renders the reference picture of a pass with the end screen (`hud.render(r, chars)`: NBG0 from `vdp2.nbg0_plane`); the car is not drawn there (the goal presentation's slave jobs do not run headless).
**Python on Windows writes CRLF when a script rewrites a repository file in text mode: open files with `newline=''`.**

## Addendum (M6 traces)

* `tools/oracle_m6.sh` makes the extra traces (arcade type, cars 1 / 2, manual gearbox, difficulty, tuned setups, mirror, camera with `--render`): options `--tag --setup a,b,c,d --mirror --view 0|2 --toggle passes --camera --render`; `.inp` lines are `accel brake axis shift view`, `.cam` = camera work area + view matrices + mode bytes + part list per pass.
* `tools/test_drive.sh <trace> -mask lo-hi ... [-cam] [-map]` runs the C port over a trace (docs/drive.md 3).
* **Sky** (`oracle.py run --render --sky`): `init_vdp2sys` + `init_gscroll` once, then per pass `rscroll_ctrl` and one `set_grpara_int`; `<name>.sky` = the table's A, B, D, E, Mx and the targets (9 longs per pass); `test_drive.sh <trace> -sky`.
* **Spray** (`tools/spvec.py course type passes [--car=N]` -> `extracted/oracle/sp_*.vec`; `tools/test_spray.sh`): the car display runs for real (draw functions recorded), the spawner's inputs / outputs and the whole object list after the spawner and after `0x0601D410` are logged.
* **Region byte**: `Race.__init__` sets bit 0 of `[0x0604000D]` like the boot code (mph, USA label layout); traces made before 20 Oct 2026 (HUD ones) were regenerated.
* Play mode: SELECT switches between the whole 480 x 272 screen (default) and the TV's 4:3 shape.
