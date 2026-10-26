# Sega Rally Championship (Saturn, USA) - driving model, input and race flow

Source: `0RALLY.DAT` (SH-2, big endian, base 0x06004000) + `PRGDATA.DAT` (data blocks) + `BGDESERT.DAT` (course data sample).
Scope: modules `plctrl player car(dynamics part) steer power carctrl pad gctrl` and the race-flow code (lap/time/start/countdown).
Purpose: understanding for the native PSP port. No port code here.

Evidence tags used on every substantive statement:
- **[V] VERIFIED** - I read the instructions that do it (function + address given). Where noted, I also *executed* the original code (see 0.4).
- **[I] INFERRED** - consistent with the code, not proven.
- **[U] UNKNOWN** - not determined.

## Summary of the most important findings
1. One logic step = 2 V-blanks (30 Hz). `player::execCar` @0x06006C4C is the whole per-car update (23 sub-steps, table in 1.4); GBR == r14 == the car slot (0x060B0000 + ..., 0x400 bytes), there is no separate GBR struct.
2. Units: position in metres, velocity in m/step (108 km/h per 1.0), collision/track coordinates are 16x metres, collision polygon normals have length 256, angles are radians in 16.16. Force-to-velocity is `F/16000`, gravity `force_scale/65536` raw per step (12.6 m/s^2).
3. Model = 4 spring/damper wheels (per-surface stiffness/damping) + two-axle tyre model with friction circle + flywheel engine (81-point torque curve, 4 gears x final drive 3.0, 8800 rpm, 400 rpm/step slew) + simple drag; steering has no speed dependence (0.84 rad max, 0.9 low-pass); force feedback is stubbed out.
4. Executing the original code on a flat floor reproduces sensible numbers (0-100 km/h 1.3 s, top 226.6 km/h, 250 km/h hard cap), which validates the unit chain in this document (3.12).
5. Race flow: countdown is a state machine in the car's second function (3-2-1-GO, 33 steps each); laps are detected by the segment index wrapping; time limit = `(sec<<8)|frame` counter extended by per-course/per-checkpoint/per-lap byte tables (5.4); game over = time up and speed < 5.7 km/h. The DBG addresses of `camera .. laptime` (and `start`) are displaced by +0xBD4 / +0xBE4 (0.3).

## 0. Conventions, method, caveats

### 0.1 Number formats [V]
- Almost everything is signed 16.16 fixed point (int32, 1.0 = 0x10000). Angles are **radians** in 16.16 (pi = 0x3243F, 2*pi = 0x6487F; `power`/`car` code).
- The SH-2 idioms used everywhere, written as C macros in this document:
```c
#define FMUL(a,b)  ((int32_t)(((int64_t)(a)*(b)) >> 16))   // dmuls.l ; sts mach/macl ; xtrct      (16.16 * 16.16)
#define HMUL(a,b)  ((int32_t)(((int64_t)(a)*(b)) >> 32))   // dmuls.l ; sts mach                    (0.32 style multiply, also "plain integer" results)
#define MUL24(a,b) ((int32_t)(((int64_t)(a)*(b)) >> 24))   // dmuls.l ; shll8 mach | macl>>24       (used in the spring code)
#define FDIV(a,b)  ((int32_t)(((int64_t)(a) << 16) / (b))) // SH-2 on-chip divider: DVSR=b, DVDNTH:DVDNTL = a<<16, result at 0xFFFFFF1C
```
  `mac.l` chains accumulate the full 64-bit sum and round only once at the end (`xtrct`); a port that wants bit-exactness should do the same (sum in int64, then >>16).
- Hardware divider `0xFFFFFF00` (VERIFIED users: `power::PowerGet`, `carctrl::carControl`, `math::sqrt`, `car::get_y/get_d`, `FUN_06006dac`): DVSR=0xFF00, DVDNTH=0xFF10, DVDNTL=0xFF14, quotient 0xFF1C, remainder 0xFF18. 64/32 signed, truncation toward zero.
- `math::sqrt` @0x0601B8CC is a Newton iteration on the divider (16.16 in, 16.16 out; negative input returns -1.0, zero returns 0). `math::r_sin` @0x0601B764 (radians -> turns via x0x28BE60DC, then `sincos_tbl` of 4096 entries) returns 16.16 in [-1,1]. `math::atan(r4,r5)` @0x0601B7C0 = atan2(y=r5, x=r4) returning a 16-bit angle (0x10000 = 360 deg); `math::r_atan` converts to radians [V]. `matrix::sincos` @0x06018058 takes **turns** (16.16, 0x10000 = 360 deg).

### 0.2 Units and time step [V/I]
- One logic step per main-loop pass = 2 V-blanks = **30 Hz** (main loop in NOTES.md; `gctrl::game_ctrl` is called once per pass). Constants embed 30 Hz (e.g. 954.93 = 30*60/(2*pi*0.3)).
- Car position/velocity: **metres and metres/step** [I]. Evidence: the speedometer routine @0x06012084 multiplies `speed` (+0x94) by 108.0 (km/h) or 67.0 (mph): 1.0 m/step * 30 steps/s * 3.6 = 108 km/h exactly; the wheel-rotation code uses 3.3333 = 1/0.3 (wheel radius 0.3 m).
- Collision/track coordinates are **16x the car position unit** (1/16 m) [V]: wheel query point = `(pos+vel) << 4 + rotated offset` (FUN_06007718 @0x06007718); course polygons in `BGDESERT.DAT` have coordinates around -6000..+6000 and the Desert start position is x=-369 m (`datgbal::pinit_tbl`). "Track units" below means these 1/16 m units.
- Y is up. Forward is +Z when yaw=0 (forward vector = column 2 of Ry(yaw), FUN_060075f0 @0x060075F0) [V]. A right-turn input produces a *negative* yaw change (see 3.12) [V by execution].

### 0.3 IMPORTANT caveat: DBG symbol addresses are displaced for the modules after `carcoli/othercar` [V]
The physics modules' symbols line up with the code (execCar has a prologue, PowerGet is the engine, getSteer filters the steering, carControl is the tyre model ...). **From `camera` up to and including `laptime`, the DBG addresses are too low by 0x0BD4 in this binary** (real = DBG + 0x0BD4; the same correction was found independently in `collision_world_ai.md` section 0.2; `adv/photo` need +0x044C). Evidence found here: `laptime::lap_time_init` (DBG 0x0601369C) +0xBD4 = 0x06014270 = the race-init routine called from `init_game_work`; `laptime::check_lap` -> 0x0601465C (called by `normal_plmove` after `get_block`); `extend_time_calc` -> 0x0601459C; `lag_time_calc` -> 0x060145D4; `time_cmpgl_save` -> 0x06014CE8; `lap_time_calc` -> 0x06014DDC; `total_time_calc` -> 0x06014E34; `total_time_calc_all` -> 0x06014F58; `time_adjust` -> 0x060150B4; `vstime_cmp` -> 0x06015224; `game_end_chk` -> 0x06015378 (all of these are called from `normal_plmove`/`vs_plmove*`/`goali_plmove*`); `display3::count_down_start` -> 0x0601310C (the countdown driver); `dispcar::dispCar/dispTachoMeter/dispCountDown` -> 0x060101E8 / 0x06010680 / 0x06010710 (the routines `normal_plmt` and the countdown driver call). Un-shifted, `laptime::extend_time_calc` at 0x060139C8 is a fragment writing VDP2 register 0x25F80020 - which is how I first noticed the problem. The `start` module needs +0xBE4: `start_init` = 0x06015408 (loads the course graphics with `variety::cd_cg_set/cd_map_set/cd_color_set`, installs the main vector 0x06015510 = `start_main` and a sub-state pointer in `[0x060AA178]` = 0x0601554C) [V address/behaviour, I naming]. `timer::*` (0x0601F6E0) is correct but is the *menu select-screen countdown* (called from `ac_g_select/ac_c_select/ac_crs_select`), not the race countdown [V].
- In this document, race-flow routines are therefore given by **real address** plus, in brackets, the DBG name obtained with the +0xBD4 correction.
- A Ghidra "function" such as `FUN_06007718` inside `player` (DBG range 0x06006C4C-0x060079E8) is a piece of `execCar`; names I give them below are mine. Ghidra's function labels in the shifted region (e.g. `scroll::set_scr_int` at 0x06014DD8) are likewise wrong.

### 0.4 Method and validation
Static reading of the asm plus a small SH-2 interpreter (Python, scratchpad only) that runs the *original* `0RALLY.DAT` code: `carconfig`, `mycar_init`, `initCarControl`, then `player::execCar` repeatedly, with only `car::get_coli_area` replaced by a flat fake ground polygon and two sound calls stubbed. This confirmed the unit chain (see 3.12). Everything marked "[V by execution]" comes from that. Real course polygons were inspected from `BGDESERT.DAT` (2.6).

---

## 1. Per-frame order of operations

### 1.1 From the main loop to the car [V]
```
main loop (RAM copy @0x060A0000): [0x060AA801]=2; call *(0x060A001C); sync(2)         -> one logic step / 2 frames
 race state function ... -> gctrl::game_ctrl @0x06005B78
   jump table indexed by the word [0x060AA860] (game type, always even):
     0,2      -> 0x06005B90   single player path
     4,6,8    -> 0x06005C30   two-car path (slave runs slot 1, "m_/s_car_move2p_ctrl"), no car-car collision/VS handicap
     10       -> 0x06005CB8   VS battle (adds carcoli::carColiVS @0x0600C628, car::vs_rank_calc @0x060084D0, player::vs_g_ctrl @0x060079E8)
```
Single-player path (0x06005B90) in order [V]: poly pipeline frame init (`mkpoly::s_/m_mkpoly_frame_init` via `slave::set_slave_exec`) ->
`slot0.fn0(r4=0x060D1000)` with GBR=r14=slot0 -> `FUN_0600FC94` (replay/ending hook) -> `camera_ctrl` -> `slot0.fn4` -> for slots 1..8 (0x060B0600 + k*0x400): `fn0`, `fn4` (skipped when null) ->
`plctrl::sync_ctrl` -> `otherctrl::otherCtrl` -> pad/HUD calls -> `mkpoly::draw_course` -> `rscroll_ctrl` -> `slave::wait_slave_end` -> `zsort::game_zsort`.
`gctrl::car_move_ctrl` @0x06005E84 is a slot loop that calls only `fn0` of each slot and skips pointers whose bit 0 is set.

### 1.2 Slots, function pointers, master/slave [V]
- Car slots: slot 0 = player 1 at **0x060B0000**; slot 1 at **0x060B0600**; then 0x060B0A00, 0E00, 1200, 1600, 1A00, 1E00, 2200 (stride 0x400) (`gctrl::car_move_ctrl`). Slot 0 and 1 also own a lap/time block at `+0x500` (slot 1's block, 0x060B0B00, overlaps the next slot - only slots 0/1 use it [I]).
- `slot+0x00` = per-step "move" function, `slot+0x04` = second per-step function ("plmt": presentation - tacho/HUD `FUN_06010680`, car model draw `FUN_060101E8`, `car::calc_dispang2`, `get_poly_map`, `get_other_map`; no physics) [V/I].
- **Bit 0 of the function pointer is a flag** [V]: `car_move_ctrl`/`m_car_move2p_ctrl` run a pointer only when bit0 = 0; `s_car_move2p_ctrl` (FUN_06005F2C @0x06005F2C) runs on the *slave SH-2* only pointers with bit0 = 1 (calls ptr-1, r4=0x060D1800). State switches keep the flag: `mov.l @(0,r14),r0; shlr r0; (load fn); movt r1; or r1,r0; store` (seen in normal_plinit, goali_plmove, ...). `plctrl::vs_ctrl_plset` @0x06006014 gives slot 1 pointers 0x060061DD/0x060066BD (bit0=1) so **player 2 runs its physics on the slave CPU** in two-player modes [V]; `PowerGet` picks its scratch matrix by the sign of the halfword at 0xFFFFFFE2 (BCR1 low half, the MASTER bit [I]): 0x060D1000+0x30 (master) or 0x060D1800+0x30 (slave) [V].
- **GBR == r14 == the current slot** in every car routine [V]: `car_move_ctrl`, `game_ctrl`, `m_/s_car_move2p_ctrl` all do `jsr @r0 ; _ldc r14,gbr`. There is *no separate GBR struct*; `@(disp,gbr)` accesses (r0-only encoding) and `@(disp,r14)` hit the same structure (e.g. `FUN_06007954` @0x06007954 squares `gbr+0x68` and `[r14+0x64]/[r14+0x6C]` as one vector). "Globals" live at fixed 0x060AAxxx addresses (2.5).

### 1.3 Car state functions (slot+0x00) [V]
| fn | address | does | next |
|---|---|---|---|
| `plctrl::normal_ctrl_plset` | 0x06005FF4 | slot0.fn0=normal_plinit, fn4=normal_plmt_start (or LAB_06006710 if mode byte 0x060AA810==2) | |
| `vs_ctrl_plset` / `myline_ctrl_plset` | 0x06006014 / 0x0600600C | VS: slot0 & slot1(bit0=1) init fns; ghost ("myline"): slot1 init | |
| `normal_plinit` | 0x06006030 | `mycar_init`, `initCarControl`, one `normal_plstart_wait`, camera/map init | normal_plstart_wait |
| `normal_plstart_wait` | 0x06006228 | grid: `make_gswdata`, `view_changer1p`, **`execCar`**, `sd_seq_vol_set`, `get_block`, `FUN_0601465C`(lap check), `calc_reverse`. car state `+0x0A`=0 | set to `normal_plmove` by the countdown routine (5.2) |
| `normal_plmove` | 0x060062F4 | racing (below) | goali_plmove (goal) / gover_plmove (time up) |
| `vs_plmove1p/2p` | 0x06006360/0x060063A8 | same for VS (`FUN_060145D4(1 or 2)` stop-watch instead of countdown timer) | |
| `goali_plmove` / `goali_plmove1p/2p` | 0x0600640C / 0x06006490 / 0x060064E0 | goal: `+0x0A`=2, records result, switches fn to `goal_plmove`, teleports car to the goal pose (`player::goal_car_move2p` @0x06007AB2 with `goal_posi*_tbl`) | goal_plmove |
| `goal_plmove` | 0x06006470 | `execCar` only (car coasts, +0x0A=2) | stays |
| `gover_plmove` | 0x060063F0 | no `execCar`; `get_block` + `calc_reverse` only | stays |

`normal_plmove` order [V]: `car::make_gswdata`(input word) -> `car::view_changer1p` -> **`player::execCar`** -> `sound::sd_seq_vol_set` -> `car::get_block`(course segment, direction) -> `FUN_0601465C`(lap/checkpoint/goal) -> unless mode(+8)==2: `FUN_06014DDC`, `FUN_06014E34`, **`FUN_0601459C`**(time-limit countdown) -> `car::calc_reverse`(wrong-way) -> `jmp LAB_06015380`.
`LAB_06015380` (tail) [V]: if `+0x0A`==2 (time up) and speed `+0x94` <= 0x0D74 (0.0526 = 5.7 km/h): `+0xF5`=10, fn0 := `gover_plmove`, `[0x060AA300]` := byte `+0xB6`. => **game over = time up AND slowed below 5.7 km/h**.

### 1.4 `player::execCar` @0x06006C4C - exact order of one step [V]
| # | action | routine (address) |
|---|---|---|
| 1 | read controls | `pad::getIO_pad` @0x0600975C (writes +0xA0 accel, +0xA4 brake, +0xA8 steer; MT gear shift) |
| 2 | steering angle | `+0x30 = (+0xF5==8) ? 0 : steer::getSteer()` @0x06008774 |
| 3 | apply collision push-out vector (+0x64..0x6C) | FUN_06007954 @0x06007954 |
| 4 | predicted position/angles | `+0x118 = pos+vel`; `+0x124/128/12C = ang + angvel>>4` (inline @0x06006C6A-0x06006CB0) |
| 5 | wheel probing + wall response | FUN_06007718 @0x06007718 (-> `get_coli_area`, `get_y`, `get_d`, FUN_060078A4) |
| 6 | body corner points (predicted -> +0x190, current -> +0x240) | FUN_0600760C @0x0600760C x2 |
| 7 | re-probe flagged corners; corner velocities `+0x240 = +0x190 - +0x240` | FUN_06007080 @0x06007080 |
| 8 | body slip angle (+0xD4/+0xD8), wheel direction vectors (+0x160 steered, +0x16C body) | FUN_0600755C @0x0600755C |
| 9 | per-wheel suspension + wheel slip | FUN_06007304 @0x06007304 (-> FUN_060073C8 @0x060073C8, FUN_060074EC @0x060074EC) |
| 10 | axle averages `+0x104/+0x108` (slip), `+0x10C/+0x110` (wheel speed) | inline @0x06006CDE-0x06006D0E |
| 11 | spring forces -> body-frame torques (+0x290..0x298), total spring force (+0x29C..0x2A4) | FUN_0600716C @0x0600716C |
| 12 | speed `+0x94 = +0xEC = norm(vel)` | `car::vecval` @0x06007BAC |
| 13 | tyres + engine + brakes -> forces/torques | **`carctrl::carControl`** @0x06009124 (calls `PowerGet`, `PowerUse`, `r_sin`, `sqrt`) |
| 14 | torques -> angular velocity (+0x70/74/78), clamp | FUN_06007020 @0x06007020 |
| 15 | yaw damping + in-air righting | FUN_06006FA4 @0x06006FA4 |
| 16 | wall un-stick yaw assist | FUN_06006F60 @0x06006F60 |
| 17 | forces -> velocity, angvel -> angles | FUN_06006EE4 @0x06006EE4 |
| 18 | if no wheel on ground (`+0xB2`==0): `vel.y -= 0x7C` | inline @0x06006D3C |
| 19 | if car state `+0x0A`==0: `vel.x = vel.z = 0` | inline @0x06006D48 |
| 20 | `pos += vel` | inline @0x06006D54 |
| 21 | drag | FUN_06006E24 @0x06006E24 |
| 22 | speed cap (250 km/h) | FUN_06006DAC @0x06006DAC |
| 23 | wrap Euler angles to [-pi,pi] | FUN_06006DF4 @0x06006DF4 |

AI cars do **not** use `execCar`/`carControl`: the only references to `execCar` are the plctrl state functions above (pool search over the whole image) [V]; AI uses `othercar::execOtherCar` (not covered here).

---

## 2. Data structures

### 2.1 Car slot (r14 == GBR), size 0x400 (slot 0/1 extend to +0x5xx) [V unless noted]
| off | type | meaning | tag / evidence |
|---|---|---|---|
| +0x00 | u32 | move fn pointer (bit0 = slave-owned flag) | V car_move_ctrl |
| +0x04 | u32 | second fn pointer (presentation) | V |
| +0x08 | u16 | play mode = byte [0x060AA810]: 0 live+record, 1/2 playback of recorded input, 3 live (no record), 4 = P2 live | V `make_gswdata` table @0x06008578 |
| +0x0A | u16 | run state: 0 grid/countdown (horizontal vel forced 0, engine in neutral), 1 racing, 2 finished/time up (throttle 0, brake 0.5, extra drag) | V execCar, PowerGet, FUN_06008D64, FUN_0601459C |
| +0x0C/0x10/0x14 | s32 rad | Euler angles pitch(x) / yaw(y) / roll(z), wrapped to +-pi | V FUN_06006DF4 |
| +0x18/0x1C/0x20 | s32 m | position x, y(up), z | V |
| +0x24/0x28/0x2C | s32 m/step | velocity | V |
| +0x30 | s32 rad | steering angle = -(filtered 0.84*input) | V getSteer |
| +0x34 | s32 | wheel spin step = speed*3.3333 (rad/step) | V PowerGet @0x060089C2 |
| +0x38 | s32 | accumulated wheel rotation (+= +0x34) | V |
| +0x3C..+0x48 | s32[4] | per-wheel clamped compression `h` (<= 3.2 track units); initial 3.2 | V FUN_060073C8 |
| +0x4C..+0x58 | s32[4] | ground height under wheel (m) = corner y + h/16 | V |
| +0x5C | u8 | car id (0,1; 2 maps to 10; VS: 11,12,13) | V `init_game_work` @0x06004844, FUN_06004948 |
| +0x5D | u8 | brake light (brake input != 0) | V FUN_06008D64 |
| +0x5F | u8 | copy of [0x060AA811] (setup mode byte): 1 -> custom tuning in carconfig; ==2 selects alternative constants (918 vs 735 force scale, 0.2 drive split, drift flag 0.15) | V uses; meaning of value 2 U |
| +0x60 / +0x62 | u16 | course segment index (from `get_block`) / previous one | V `car::get_block` @0x06007EAC |
| +0x64/0x68/0x6C | s32[3] | push-out vector written by car-car collision (`otherset::otrCarColi`...) | V |
| +0x70/0x74/0x78 | s32 | angular velocity pitch/yaw/roll; per step angle += value>>4 | V FUN_06006EE4 |
| +0x7C, +0x80 | s32 | wall-response constants 1000.0 and 2292.0 (set by mycar_init). +0x84 (2292.0), +0x88 (1200.0) unused here | V/U |
| +0x8C | s32 | engine rpm (16.16), idle start 800.0 | V |
| +0x90 | ptr | current surface dyn block (2.3) | V carControl |
| +0x94 | s32 | speed = norm(vel) (also copy in +0xEC) | V |
| +0x98 | s32 | wheel-spin/drift ratio 0..1 (engine slip, or 0.01/0.15 when drifting) | V |
| +0x9C | s32 | filtered front/rear load bias | V |
| +0xA0/0xA4/0xA8 | s32 | accel 0..1, brake 0..1, steering -1..1 (16.16) | V getIO_pad |
| +0xAC | s32 | = -initial yaw (mycar_init) | V set, U use |
| +0xB0 | u16 | current gear (0 neutral, 1..4) | V |
| +0xB2 / +0xB3 | u8 | wheels-on-ground bitmask (bit i = wheel i) / previous | V FUN_06007304 |
| +0xB4 | u16 | surface type of wheels as nibbles (wheel0 lowest) | V |
| +0xB6 | u8 | byte carried to/from [0x060AA300] across states; selects the leading car in vs_g_ctrl | V use, U meaning |
| +0xB7,+0xF0,+0xF1 | u8 | coarse block ids from `get_block` | V |
| +0xB8/+0xBC | s32 | course direction vector (x,z) of the car's segment | V get_block, FUN_06006F60 |
| +0xC0 | s32 | relative heading in turns (car vs course dir) | V calc_reverse |
| +0xC4 | u8 | segment checkpoint byte (<0: time-extension checkpoint, id = &0x7F; >0: marker -> +0xF2) | V FUN_0601465C |
| +0xC5 | u8 | heading quadrant vs course (2 = along course, 0 = wrong way) | I |
| +0xC7 | u8 | display angle nibble | V calc_dispang2 |
| +0xC8/+0xCA/+0xCC | u16 | lap bookkeeping: "next lap number", wrap counter, furthest segment this lap | V FUN_0601465C |
| +0xD4 / +0xD8 | s32 | body slip measure atan(lat^2/long^2) with sign, and its per-step delta | V FUN_0600755C |
| +0xDC..+0xE8 | s32[4] | previous-step loads (copy of +0x3C..) | V |
| +0xEC | s32 | speed copy; also error code slot ('NUKE'/'TRIP'... written before `trapa #9`) | V |
| +0xF4 | u8 | transmission: 0 = automatic, !=0 = manual | V PowerGet/getIO_analog |
| +0xF5 | u8 | control state: 0 init, 4 grid (pre-start), 6 racing, 8 goal (steering forced 0), 10/12 game-over | V execCar, countdown, set_gover |
| +0xF6 | u8 | mask of wheels that hit "wall" polygons this step | V |
| +0xF7 | u8 | sector mark 1..6 from the segment record (byte1); nonzero -> split-time routine FUN_06014C08 | V/I |
| +0xF8 / +0xFA | u16 | OR of surface-type bits of the 4 wheels / chosen surface index | V carControl |
| +0x100 | s32 | max wall penetration this step (0 = no wall contact) | V |
| +0x104/+0x108 | s32 | front/rear axle wheel-slip sine (sum, halved if both wheels grounded) | V execCar @0x06006CDE |
| +0x10C/+0x110 | s32 | front/rear axle wheel speed (same averaging) | V |
| +0x114 | u32 | input word (4.1/4.2): bits 31..24 analog steering byte (axis 0..255), 23..16 group1 flags, 15..8 view buttons | V config_keyset |
| +0x118..+0x120 | s32[3] | predicted position | V |
| +0x124/0x128/0x12C | s32[3] | predicted pitch/yaw/roll | V |
| +0x130..+0x15F | 3x4 s32 | scratch rotation matrix (3 rows of 4 longs; col 3 = translation) | V |
| +0x160/+0x16C | s32[3] | steered-wheel direction / body forward vector (xz used); later overwritten per frame (before FUN_0600755C they hold wheel offsets) | V |
| +0x190..+0x1BF | 4x s32[3] | wheel positions x16 (during FUN_06007718), then predicted body corner points | V |
| +0x1C0.. (stride 0x14) | wheel record W[i] | +0..8 ground/wall normal (length 256), +0xC penetration h (track units, 16.16), +0x10 u16 surface type | V |
| +0x210 (stride 0xC) | s32[3] x4 | per-wheel spring force vector (world) / wall push vector in the wall path | V |
| +0x240 (stride 0xC) | s32[3] x4 | corner point velocities (m/step) | V |
| +0x270..+0x27C / +0x280..+0x28C | s32[4] each | per-wheel sin(slip angle) / per-wheel speed | V FUN_060074EC |
| +0x290/+0x294/+0x298 | s32 | torque accumulators: pitch, yaw, roll | V carControl, FUN_06007020 |
| +0x29C/+0x2A0/+0x2A4 | s32 | force accumulator x,y,z | V FUN_06006EE4 |
| +0x2AC | s32 | force scale F: 918.44 ([0x5F]==2) else 734.75 | V mycar_init @0x060060D8 |
| +0x2C0..+0x2CC | s32[4] | digital steering ramp params (4.1/4.2) | V carconfig |
| +0x2D0 | s32 | analog steering gain | V carconfig |
| +0x328 | s32 | steering low-pass state | V getSteer |
| +0x340 | s32 | auto-shift hold counter (init 60) | V |
| +0x344 | s32 | PowerUse feedback (rpm) | V |
| +0x348 | s32 | target rpm | V |
| +0x34C | s32 | overall ratio (gear x final drive) | V |
| +0x350 | s32 | slip ratio of last step | V |
| +0x354 | s32 | low-rpm full-throttle factor | V |
| +0x358 | ptr | torque curve (AT 0x06008DD4 / MT 0x06008F68) | V PowerInit |
| +0x35C/+0x360 | s32 | shift-delay timers - never set to nonzero in the shipped build (only dead code @0x06008CA8) | V |
| +0x364..+0x36C | s32 | sound-trigger state | V FUN_06008BCC |
| +0x370 | s32 | engine inertia R = 52.4288*(1+1/ratio) | V |
| +0x374 | s32 | torque multiplier 1.0625 (ids 10,13) else 1.0 | V PowerInit |
| +0x378 | s32 | final drive 3.0 (VS handicap changes it) | V |
| +0x388 | ptr | gear ratio table | V |
| +0x500.. | block | lap/time block (2.4) | V |

### 2.2 Surface dyn block (13 longs, 52 bytes; `datgbal::carDyn*` @0x060A14D0 + 0x34*k, copied to 0x060A9400 by `pad::carconfig`) [V layout, I names]
Selection (`carControl` @0x06009124 start): `idx` = position of the highest set bit among bits 1..10 of `+0xF8` (OR over the 4 wheels of `1<<(type-1)`, table `car::w_valtobit_tbl` @0x06008544); the in-code table `0x06009D80` (pointers into the RAM copy at 0x060A9400+; `datgbal::cardyntable` @0x060A1490 is its twin pointing at 0x060A14D0+) maps idx -> block. Surface type t (poly flags high half): t=1,2 Tarmac; 3,4 Gravel; 5,6 Water; 7,8 Special; 9,10 Tarmac; 11 Gravel (types >11 are ignored by the selector). Highest type among the four wheels wins [V]. Slot != 0 uses the second player's block (+0xD0).
| off | Tarmac | Gravel | Water | Special | use |
|---|---|---|---|---|---|
| +0x00 | 1.30 | 1.10 | 0.50 | 1.20 | front grip coefficient (`gripF = dyn0*(16+bias)`) |
| +0x04 | 1.30 | 1.10 | 0.50 | 1.20 | rear grip coefficient |
| +0x08 | 0.25 | 0.25 | 0.25 | 0.23 | front share of drive force (0.2 if +0x5F==2) |
| +0x0C | 292 | 292 | 292 | 292 | yaw damping coefficient #1 (x slip-angle delta) FUN_06006FA4 |
| +0x10 | 18.25 | 18.25 | 18.25 | 22.0 | yaw damping coefficient #2 (x yaw rate) |
| +0x14/+0x18 | 30/30 | 30/30 | 30/30 | 30/30 | spring stiffness front/rear |
| +0x1C/+0x20 | 8/8 | 3/3 | 3/3 | 3/3 | damper front/rear |
| +0x24 | 0.10 | 0.05 | 0.10 | 0.10 | load-bias filter gain when bias rising |
| +0x28 | 0.40 | 0.40 | 0.40 | 0.40 | ... falling |
| +0x2C | 0.99747 | same | same | same | rolling drag factor on ground (vx,vz *= ) |
| +0x30 | 0.28 | 0.28 | 0.28 | 0.35 | wall-contact friction coefficient |
Special car (`[0x060AA80A]==2`): whole table replaced from 0x06009DBC (4 blocks): grip 1.55/1.55 (tarmac), 1.35/1.35 (gravel), 0.5/0.5 (water), 1.45/1.45 (special); +0x08 **0.0625** (almost no front drive: near rear-wheel drive; replaced by 0.2 when +0x5F==2); +0x14/+0x18 = 38/36 (stiffer springs); +0x1C/+0x20 = 11/10 (tarmac), 8/7 (others); +0x24/+0x28 = 0.2125 each; +0x2C 0.9975; +0x30 0.28 (0.35 special) [V values from image]. The special-car table is *not* subject to the custom tuning below (carconfig branches over it) [V].

### 2.3 Tuning tables applied by `pad::carconfig` @0x06009938 when [0x060AA811]==1 (player-adjusted setup; setup bytes at 0x060AA817 + 5*player) [V]
- byte0 -> steering: analog gain `[car+0x2D0]` = {1.1875, 1.625, 2.0, 2.5} (table 0x06009A98); digital ramp params `[car+0x2C0..0x2CC]` rows @0x06009AAC: idx0 (-0.125, 0.0859, -0.15625, -0.375), idx1 (-0.125, 0.1094, ...), idx2 (-0.125, 0.125, ...), idx3 (-0.15625, 0.15625, ...).
- byte1 -> tyres: grip row @0x06009AFC (written to dyn+0x00 and +0x04 of each surface block): idx0 {1.175,1.1625,0.5,1.2625}, idx1 {1.3,1.1313,0.5,1.2312}, idx2 {1.3,1.1,0.5,1.2}, idx3 {1.3625,1.1,0.5,1.2} (order tarmac,gravel,water,special).
- byte2 / byte3 (a, b) -> front / rear suspension setting 0..3: stiffness `{20,25,30,35}[a]` -> dyn+0x14 and `[b]` -> dyn+0x18 (table 0x06009B4C, same value for every surface); damper rows (table 0x06009B9C, columns = tarmac, gravel, water, special): setting 0 `{2,1,1,1}`, 1 `{5,2,2,2}`, 2 `{8,3,3,3}`, 3 `{11,5,5,5}`; front damper (dyn+0x1C) takes row a, rear (dyn+0x20) row b.
- (a,b) also index a 16-byte-row table @0x06009BEC at row `a*5+b`: row[0] -> dyn+0x0C (324/292/356/260), row[1] -> dyn+0x10 (20.25/18.25/22.25/16.25; for the *special* surface block row[2] = 23/22/24/21/20 is used instead), row[3] -> dyn+0x08 when non-zero (0.3125 or 0.2812). Default (a=0,b=0): 324, 20.25, drive split unchanged.
- Order inside carconfig: base blocks copied first, then tyres, springs, dampers, then the (a,b) row [V].
- what the 4 bytes are called in the menus: [U] (written by `cs_tm_attck`, `cs_battle_2p`, `cs_md_select`, record screens).

### 2.4 Lap/time block R = car+0x500 (slots 0/1) [V fields I read, rest U]
- R+0x34 time remaining: `(seconds<<8) | frames`, frames count 29..0 (30 per second) [V FUN_0601459C].
- R+0x24/+0x28/+0x2C and R+0x01/02/03 bytes: VS stop-watch (min, sec, 1/30 s counters) with BCD helper tables `datgbal::time10_tbl/time100_tbl` (0x060A1208/0x060A1248) [V FUN_060145D4].
- R+0x04, +0x08, +0x0C, +0x10..+0x20 (5 longs) lap time and split bookkeeping; R+0x48..+0x5C buffers 0x060AA400/0x060AA520/...; R+0x74/+0x78 replay-input buffer pointers (start / end); R+0x7C..0x7E flag bytes [I].

### 2.5 Global variables used by the driving code [V use, names mine]
| addr | meaning |
|---|---|
| 0x060AA803 | flag byte: bit2/bit3 = pad1/pad2 is an "ID 0x10" analog device (`vdp::pad_kind` @0x060053B8); bit0 pad-init done; bit6 set at countdown "ready" |
| 0x060AA006 | total laps for the race (word) |
| 0x060AA808 | difficulty-like index 0..2 (selects the time-extension table) [I] |
| 0x060AA809 / 0x060AA812 | transmission P1 / P2 (0 = AT, 1 = MT) [I] -> car +0xF4 |
| 0x060AA80A (+9/player) | car model select 0,1,2 (2 = special/hidden car) -> +0x5C |
| 0x060AA80C | lap-count setting (arcade) |
| 0x060AA80F / 0x060AA814 | key-config index P1 / P2 |
| 0x060AA810 | play mode (0 live, 2 replay/ending-style) -> car +0x08 |
| 0x060AA811 (+5/player) | setup mode byte (1 = tuning applied) -> +0x5F |
| 0x060AA817 (+5/player) | setup bytes (steering, tyres, suspension F, suspension R, ...) |
| 0x060AA82E | **mirror mode** flag (`cs_tm_attck::check_mirror`): inverts steering input |
| 0x060AA860 | game type (word, even): 0,2 single; 4,6,8 two-car; 10 VS |
| 0x060AA87E | course index 0..3 = DESERT, FOREST, MOUNT(ain), LAKE (string table @0x06004780) |
| 0x060AA882 | course index * 4 (word) |
| 0x060AA100 / 0x060AA16C / 0x060AA170 / 0x060AA174 | global tuning constants set by `mycar_init`: 734.75 (base force scale), 900.0 (damper scale), 72.82 (U), 954.93 (rpm per m/step) |
| 0x060AA178 | start-sequence state (countdown), +4 timer |
| 0x060AA990.. | pad port data (held bits +0, edge +2, sticky +4, analog byte +0xC for port 1; port 2 at +6) |
| 0x060AA00A / 0x060AA00C | "OR switch" pad words used for shift/view buttons; producer not located [U] |
| 0x060D1000 / 0x060D1800 | polygon-pipeline work areas master / slave (used as scratch matrix by PowerGet) |

### 2.6 Course collision data as the physics sees it [V from BGDESERT.DAT + code]
- Loaded into Low WRAM by `BG<course>.DAT` (type-0 blocks; sizes for DESERT in brackets). **Collision**: `0x00240000` (0x4000) = 64x64 grid of 32-bit pointers (cell = 512 track units; index = `zcell*64 + xcell`, x offset +16384, z flipped; `car::get_coli_area` @0x06008008, which ends with `bra` into `FUN_060080A8` = `mov.l @r6,r6` (dereference)) -> lists `[u16 count][u16 polygon index]*count` stored behind the polygons; `0x00248000` (0x2A40E) = polygon records, **0x34 bytes each**, point-in-quad scan in `FUN_060080AA` (edge cross products).
  **Course position / segments** (`car::get_block` @0x06007EAC): `0x00244000` (0x4000) = second 64x64 pointer grid (same cell formula) -> lists of coarse block ids; `0x00290000` (0x1B94) = coarse block records of 0x24 bytes (4 flag bytes + four (x,z) vertex pairs; byte 3 = block id < 128); `0x00296000` (0x200) = 128 pointers indexed by block id -> byte lists (stored behind the records at 0x00295C00..) of segment indices; `0x00293000` (0x2E1B) = segment records of 0x2C bytes (256 segments): `+0` checkpoint byte (-> car +0xC4), `+1` flag (-> +0xF7), `+3` segment index (-> +0x60), four vertex pairs, `+0x24/+0x28` course direction (x,z) (-> +0xB8/+0xBC).
- Polygon record: `+0x00` flags (high 16 bits = **surface type**, 0 = wall; low 16 unknown), `+0x04..+0x20` four (x,z) vertices (16.16 track units), `+0x24/28/2C` normal with **length 256** (e.g. Desert poly 1400: (-1.6, 255.5, -15.3)), `+0x30` D. Ground polygon: plane `nx*x + ny*y + nz*z = 64*D` -> `car::get_y` @0x0600802C returns y = (64D - nx x - nz z)/ny in track units. Wall polygon (type 0): `car::get_d` @0x06008060 evaluates the 2-D cross product of the first edge (vertices 0->1) with the query point and divides by `+0x30` (= edge length/8; Desert poly 0: edge 73.07 -> D = 9.134), then shifts the 16.16 quotient right by 6: result `d` = (distance of the point from that edge line in track units)/8 as a 16.16 value. The wall quads are thin regions *behind* the wall surface (Desert poly 0 is 73 x 195 units), `d` grows as the wheel moves deeper.
- Desert surface types present: 0 (1400 walls), 3, 4, 5, 7.
- If no polygon is found the scan routine writes 'NUKE' (0x4E554B45) into `+0xEC` and executes `trapa #9` (debug trap) then returns r6=0; the physics then reads the halfword at address 0 as the "type" [V code, I effect]. Normal play assumes a polygon exists under every wheel.

---

## 3. Dynamics model

### 3.1 Overview [V]
Rigid body with 3 position + 3 Euler-angle DOF, 3 linear + 3 angular velocities, four wheel contact points, per-wheel spring/damper normal forces, two-axle "bicycle" tyre model (front steered axle, rear axle) with a friction-circle limit, a flywheel engine with a 4-speed gearbox, simple aero/rolling drag, and penalty-force wall response. All forces are summed in two accumulators and integrated explicitly once per 30 Hz step.

### 3.2 Integration (execCar steps 4,14,17-23) [V]
```c
// FUN_06006EE4 (force -> velocity, angular velocity -> angle)   F = c+0x29C, F0.001 = 0x00418937 (0.001 * 2^32)
c->vel.x += HMUL(F.x, 0x418937) >> 4;                     // = F.x / 16000
c->vel.y += (int32_t)((((int64_t)F.y * 0x418937) + ((int64_t)(int32_t)0xFFF00000 /* -16.0 */ * c->force_scale)) >> 32) >> 4;   // = F.y/16000 - force_scale/65536 (raw units) -> gravity
c->vel.z += HMUL(F.z, 0x418937) >> 4;
c->ang[k] += c->avel[k] >> 4;                              // avel is "rate*16" (k = pitch,yaw,roll = +0x70/+0x74/+0x78)
// FUN_06007020 (torque -> angular velocity)   T = c+0x290 (pitch, yaw, roll)
c->avel.pitch += HMUL(T[0], 0x001C97E7);  // 4.363e-4  (= 1 deg/40)
c->avel.yaw   += HMUL(T[1], 0x001C97E7);  // sic: pitch & yaw use the same constant (the 2nd constant is used for roll)
c->avel.roll  += HMUL(T[2], 0x00369D03);  // 8.333e-4  (= 1/1200)
for each avel: clamp to +-0x33333 (3.2)   // = 0.2 rad/step = 6 rad/s
// afterwards in execCar: gravity when airborne, state-0 lock, position, drag, speed cap
if (c->contact_mask == 0) c->vel.y -= 0x7C;                // extra 0.0019 m/step^2 when no wheel touches
if (c->state == 0) { c->vel.x = 0; c->vel.z = 0; }
c->pos += c->vel;
```
Gravity: `vel.y` (raw 16.16) decreases by `force_scale_raw/65536` = 918 (or 735) raw units per step, i.e. 918.44/65536 = 0.0140 (0.0112) m/step^2 = **12.6 (10.1) m/s^2**; plus 0x7C raw = 0.0019 m/step^2 when no wheel touches. Force-to-velocity: `dv = F_raw/16000` raw. (1/0.001 = mass 1000 units; the 1/16 comes from the x16 scaling of the tyre force vectors.)

```c
// FUN_06006E24 drag (after the position update, i.e. applies to next step's velocity)
if (c->wall_depth(+0x100) > 0) {
    int k = (course==3) ? 0.9375 /*0xF000*/ : dyn[0x30];             // 0.28
    int base = (course==3) ? 0.99 /*0xFD71*/ : 0.99599 /*0xFEF9*/;
    int s = base - FMUL(c->wall_depth, k);  if (s < 0) s = 0;
    c->vel.xyz *= s;                                                  // all three components
} else if (c->contact_mask) { c->vel.x *= dyn[0x2C]; c->vel.z *= dyn[0x2C]; }   // 0.99747 per step
if (c->state == 2) { c->vel.x *= 0xF700 /*0.9648*/; c->vel.z *= 0xF700; }         // goal / time up
// FUN_06006DAC speed cap
if (|vel| > 0x25097 /*2.3148 m/step = 250.0 km/h*/) c->vel = vel * 0x25097 / |vel|;
// FUN_06006DF4 : each Euler angle wrapped to [-pi, pi] by +-2*pi
```
(In the wall-friction line the first branch uses `course==3` = LAKE [V], reading `[0x060AA87E]`.)

Collision push-out (FUN_06007954 @0x06007954) [V]:
```c
y = clamp(push.y, +-0x199A /*0.1*/);   m2 = push.x^2 + y^2 + push.z^2 (one mac chain, >>16);
if (m2 > 0x0B73 /*0.0447*/) {          // unsigned compare, threshold on |push|^2
   vel.x += FMUL(push.x, 0x999A /*0.6*/); vel.z += FMUL(push.z, 0x999A);
   pos.x += FMUL(push.x, 0x6666 /*0.4*/); pos.z += FMUL(push.z, 0x6666); pos.y += y;
   push = 0;
}
```
Small pushes (|push|^2 <= 0.0447) are left in place (accumulate until large enough) [V].

### 3.3 Wheel probing and walls (FUN_06007718 @0x06007718) [V]
```c
M = Ry(pred_ang.y) * Rx(pred_ang.x) * Rz(pred_ang.z);                // matrix at +0x130  (t_rotate_y, then x, then z)
local[4] = {(-14.4,-6.4,+22.4), (+14.4,-6.4,+22.4), (-14.4,-6.4,-21.6), (+14.4,-6.4,-21.6)};   // 0x06007924, track units (x right?, y, z fwd)
for i in 0..3:                                                         // wheel 0,1 = front, 2,3 = rear (x<0 left)
    w = M * local[i];  c->off[i](+0x160+12i) = w >> 4;  c->P16[i](+0x190+12i) = w + (pred_pos << 4);
    poly = get_coli_area(P16.x, P16.z);  W[i].type = poly->type(u16);
    if (W[i].type != 0) {                                              // ground polygon
        W[i].n = poly->normal;  W[i].h = ground_y(P16.x,P16.z) - P16.y;   // track units; >0 means wheel below the surface
        wallvec[i] = 0;
    } else {                                                           // wall polygon  (bit i of +0xF6 set)
        d = get_d(...);  W[i].n = poly->normal;  W[i].h = d;  c->wall_depth(+0x100) = max(+0x100, d);
        // FUN_060078A4:  t = n x off ; u = t x n ; proj = (u . off)/|u|
        q = 17.4866 * 2^32 / (2292.0 + FMUL(1000.0, proj));            // DIV unit; = 500 when proj=0
        wallvec[i](+0x210+12i) = (n * FMUL(q, d)) >> 24;               // n has length 256 -> unit normal * q*d
    }
if (c->wall_depth != 0) {
    c->vel.x += HMUL(sum(wallvec.x), 0x418937);  c->vel.z += HMUL(sum(wallvec.z), 0x418937);   // = sum/16000... applied directly to velocity
    c->pred_pos.xz = vel.xz + pos.xz;
    yawimpulse = sum_i( off[i].x * wallvec[i].z - off[i].z * wallvec[i].x ) ;  c->avel.yaw += HMUL(yawimpulse, 0x01C97E70);  c->pred_ang.y = (avel.yaw>>4)+yaw;
}
```
- Wheel order in the table: (x<0,z>0) (x>0,z>0) (x<0,z<0) (x>0,z<0), i.e. FL, FR, RL, RR if +X is right and +Z forward [I]. Track 28.8 units = 1.8 m, wheelbase 44 units = 2.75 m.
- FUN_06007080 re-probes (with the body corner points) wheels flagged in `+0xF6`; if it again finds a wall polygon it executes the debug trap (`'D2ER'` code) - effectively never in play [V code, I].
- Corner tables @0x06007674: body corner points (-0.635,-0.4,+1.25), (+0.635,-0.4,+1.25), (-0.66,-0.4,-1.25), (+0.66,-0.4,-1.25) [V]; `FUN_0600760C` builds a matrix from (position, angles) and transforms them; results: +0x190 (predicted) and +0x240 (current); `+0x240 := +0x190 - +0x240` = per-corner velocity.

### 3.4 Suspension (FUN_06007304 / FUN_060073C8) [V, verified by execution]
Per wheel `i` (k_st = `dyn[0x14]` front / `dyn[0x18]` rear, k_d = `dyn[0x1C]`/`dyn[0x20]`, F = `+0x2AC`):
```c
c->ground_y[i](+0x4C) = corner_y + (W.h >> 4);
if (W.type == 0 || W.h < 0) {                          // wall polygon or wheel above the ground
    c->load[i] = 0; spring[i] = 0; sin_slip[i] = 0; wheel_speed[i] = 0;
    if (c->prev_load[i ^ 1] > 0) spring[i].y = FMUL(HMUL(W.n.y, 0x0C000000), W.h);   // weak ground-hugging pull = 12.0*h (h<0)
    contact_bit(i) = 0;
} else {
    int K  = MUL24(FMUL(k_st, 100.0), F);
    int Fs = MUL24(W.h, K);                             // main spring uses the full penetration
    int hc = W.h;
    if (W.h > 3.2 /*0x33333*/) { Fs += FMUL(W.h - 3.2, 250.0); hc = 3.2; W.h = 3.2; }      // bump stop
    int cd = HMUL(F, FMUL(k_d, *(int*)0x060AA16C /*900.0*/));       // plain integer
    int dh = c->load[i] - hc;  if (abs(dh) < 0x80) dh = abs(dh);     // quirk: tiny changes use |dh|
    int Fn = Fs - FMUL(cd, dh);  if (Fn < 0) Fn = 0;
    c->load[i] = hc;                                                // +0x3C..
    spring[i] = { MUL24(W.n.x,Fn), MUL24(W.n.y,Fn), MUL24(W.n.z,Fn) };   // world-space vector, n has length 256
    contact_bit(i) = 1;
    // wheel slip (FUN_060074EC) using the steered direction for wheels 0,1 and the body direction for 2,3
    v = corner_velocity[i];  spd = |v|;
    if (spd > 0) { wheel_speed[i] = spd; sin_slip[i] = signed_sqrt(1 - ((v.xz . dir.xz)^2 / |v.xz|^2)) with sign of (dir.x*(-v.z) + dir.z*v.x); }
}
```
Rest height with the correct 256-length normals is h = 1.34 track units = 8.4 cm (checked by execution). The sum of the four spring vectors is stored at +0x29C.. in FUN_0600716C, and spring forces are rotated into the body frame (Rz(-roll) Rx(-pitch) Ry(-yaw)) to produce torques [V]:
```
pitch(+0x290) = 1.35*(Fy0+Fy1-Fy2-Fy3) + 0.2*(Fz0+Fz1+Fz2+Fz3)
yaw  (+0x294) = 1.35*(Fx2+Fx3-Fx0-Fx1) + 0.635*(Fz1-Fz0) + 0.66*(Fz3-Fz2)
roll (+0x298) = -0.2*(Fx0+..+Fx3) + 0.635*(Fy0-Fy1) + 0.66*(Fy2-Fy3)           (all >>24 style, values = arm in metres)
if wall_depth: pitch -= FMUL(FMUL(vbody.z,320.0), wall_depth); roll += FMUL(FMUL(vbody.x,800.0), wall_depth)
```
(F_k = body-frame spring force of wheel k at +0x190+12k.) Also `FUN_0600755C` computes `body slip` `+0xD4 = sign(lat)*atan2(y=lat^2, x=long^2)` clamped +-pi/2 and `+0xD8` = delta [V].

### 3.5 Tyre model (`carctrl::carControl` @0x06009124) [V]
```c
void carControl(Car *c, int32 *Facc /*+0x29C*/, int32 *Tacc /*+0x290*/) {
  select dyn block (2.2);  c->+0xFA = idx
  int Fdrive = PowerGet(c, &c->slip98);                                    // 3.7  (signed drive force, raw)
  int Nf = c->load[0]+c->load[1], Nr = c->load[2]+c->load[3];
  xferF = Nf ? (int)(((int64)(c->load[1]-c->load[0]) * 0x119999) / Nf) : 0;        // 17.6*(N1-N0)/(N0+N1)  (64/32 divide)
  xferR = Nr ? ... with load[3]-load[2] ... : 0;
  if (!Nf || !Nr) Fd_f = Fd_r = 0;
  else { split = (c->mode5f==2) ? 0x3333 /*0.2*/ : dyn[0x08];  Fd_f = FMUL(Fdrive, split);  Fd_r = Fdrive - Fd_f; }
  spdfac = FMUL(c->speed, 0x2666 /*0.15*/) + 1.0;                           // 1 + 0.15 v
  // load bias -> grip capacity of each axle
  if (Nf+Nr == 0) c->bias9c = 0;
  else {
      bias = FDIV((Nf-Nr) << 4, Nf+Nr);                                   // 16*(Nf-Nr)/(Nf+Nr)
      delta = bias - (c->bias9c & 0xFE);                                  // sic: "and #0xFE,r0" masks the long to 8 bits
      gain  = delta > 0 ? dyn[0x24] : dyn[0x28];
      b2 = bias - FMUL(gain, delta);
      ang = HMUL(b2, 0x3243F000);                                         // b2 * pi/16
      w = b2 + FMUL(r_sin(ang), b2 > 0 ? 0x147AE /*1.28*/ : 0);
      c->bias9c = w;
      gripF = FMUL(dyn[0x00], 16.0 + w);   gripR = FMUL(dyn[0x04], 16.0 - w);
  }
  // ---- front axle
  sF = c->slip_f(+0x104); sF = deadzone(sF, 0x28F /*0.01*/); c->slip_f = sF;      // in-place dead zone: |s|<0.01 -> 0 else s - sign*0.01
  capF = HMUL(FMUL(spdfac, FMUL(c->F, gripF >> 4)), 0x3E8000 /*62.5*/) << 4;
  a = min(|FMUL(0x2182A /*2.0944*/, sF)|, 0x19220 /*pi/2*/);
  latF = FMUL(r_sin(a), capF);  if (sF >= 0) latF = -latF;                          // lateral tyre force opposes slip
  mag = sqrt(FMUL(Fd_f,Fd_f) + FMUL(latF,latF));  lim = FMUL(mag, c->slip98 + 1.0);
  if (lim > capF) {                                                            // friction circle exceeded
      rho = FDIV(capF, lim);  g = FMUL(1.2, sqrt(rho)) + FMUL(-0.2, FMUL(rho,rho));
      Fd_f = FMUL(Fd_f, g);  latF = FMUL(latF, FMUL(g, 1.1));
  }
  r = FMUL(abs(sF), c->wheelspd_f(+0x10C));  if (r < 0x51E /*0.02*/) latF = FMUL(latF, r * 50);   // fade at very low speed
  if (Fd_f < 0) xferF = 0;
  // ---- rear axle (same pattern)
  capR = HMUL(FMUL(spdfac, FMUL(c->F >> 2, gripR)), 0x3E8000) << 2;
  sR = c->slip_r(+0x108);  a = min(|FMUL(pi /*0x3243F*/, sR)|, pi/2);          // note: rear uses pi, front uses 2.0944, and NO dead zone on the rear
  latR = FMUL(r_sin(a), capR);  if (sR >= 0) latR = -latR;
  friction circle with g = FMUL(0.4,sqrt(rho)) + FMUL(0.6,rho^2)  (rear)      // different blend from the front
  low-speed fade as front using +0x110;  if (Fd_r < 0) xferR = 0;
  PowerUse(Fd_f + Fd_r, Fdrive);                                               // feedback to engine rpm (3.7)
  if (Fd_f == 0 || Fd_r == 0) c->slip98 = 0;
  // drift flag
  if (c->mode5f == 2) { if (c->slip98 < 0x2666) c->slip98 = (|c->bodyslip(+0xD4)| > 0.4 && c->wheelspd_r > 0.2) ? 0x2666 /*0.15*/ : 0; }
  else if (c->slip98 == 0 && |c->bodyslip| > 0.4 && c->wheelspd_r > 0.2) c->slip98 = 0x28F /*0.01*/;
  // build force vector in the front-wheel frame, x16
  if (Fd_f < 0) { Fd_r += Fd_f; Fd_f = 0; }              // front braking is moved to the rear axle
  vec = ( latF << 4, 0, Fd_f << 4 );  rotate_xz(vec, steer_angle);              // FUN_06009718: x' = x cos a - z sin a ; z' = x sin a + z cos a
  t1 = FMUL(Fd_r << 4, 10.56) >> 4;   a = FMUL(xferR, t1) >> 4;                 // rear drive force x half-track x rear load transfer
  t2 = FMUL(xferF, 10.16) >> 4;       b = FMUL(Fd_f << 4, t2) >> 4;              // same for the front
  latR16 = latR << 4;  yawTq = a - b + (latR16 + (latR16 >> 2)) - (vec.x + (vec.x >> 2));   // 1.25 m lever (half wheelbase) on the lateral forces
  vec.x += latR << 4;  vec.z += Fd_r << 4;
  Tacc[pitch] += FMUL(3.2, vec.z) >> 4;   Tacc[roll] -= FMUL(3.2, vec.x) >> 4;  Tacc[yaw] += yawTq;
  rotate_xz(vec, c->yaw);  Facc += vec;                                         // world force
}
```
- `Facc` also receives the spring/wall sums from earlier steps; the `/16000` conversion happens in FUN_06006EE4 (3.2). Constants 10.16 / 10.56 are 16 x the half track widths 0.635 / 0.66 (the vector is x16) [V].
- Remaining stack temporaries `L[0x28]/L[0x2C]` only feed `KickbackCtrl` (stub) and are irrelevant to physics [V].
- The in-place dead zone and `(+0x9C & 0xFE)` are original quirks worth keeping bit-exact [V].

### 3.6 Steering [V]
- Input mapping in 4.2. `steer::getSteer` @0x06008774: `raw = FMUL(in[+0xA8], 0xD70A /*0.84*/); s = c->+0x328; s += FMUL(raw - s, 0xE666 /*0.9*/); c->+0x328 = s; return -s;` => `+0x30` = -(low-pass(0.84*input)), max |angle| 0.84 rad (48 deg). **No speed dependence** of the steering angle itself [V]; speed dependence arises only through slip angles and the friction circle.
- Digital pad can only reach |input| = 0.6875 / 0.875 / 1.0 / 1.0 (depending on setup byte0) because the ramp term `|s|*[+0x2C0] + [+0x2C4]` goes negative at that point [V].
- Steering angle is used (a) to rotate the front-axle force vector (3.5) and (b) in FUN_0600755C to build the steered-wheel direction `+0x160` used for the front wheels' slip angle [V]. `+0xF5 == 8` (goal) zeroes the angle.
- **Kickback / force feedback**: `steer::InitKickback`, `KickbackCentering`, `KickbackCtrl` are `rts` stubs in this build [V]; `carControl` still computes the values it would pass.

### 3.7 Engine, gearbox, transmission (power module) [V]
Tables (16.16): final drive `+0x378` = **3.0**. Gear tables (`power::gearRatio` @0x06008DA4, selected at `PowerInit` @0x0600879C):
| set | used by | [0] (unused) | neutral(g=0) | gear1 | gear2 | gear3 | gear4 |
|---|---|---|---|---|---|---|---|
| A @0x06008DA4 | cars 0,1 (ids !=10,13) | 3.0 | 1.0 | 3.5 | 2.0 | 1.65 | 1.40 |
| B @0x06008DBC | special car (ids 10,13) | 3.0 | 1.0 | 3.5 | 2.0 | 1.5875 | 1.275 |
Overall ratio = table[g+1]*3.0 (index g+1, so entry [0] is never used: no reverse gear). Torque multiplier 1.0625 for set B.
Torque curves: 81 entries, index = floor(rpm * 0.009121) (=rpm/109.63; 0..8800 rpm), table values are 16.16:
```
AT curve @0x06008DD4 (selected when +0xF4 == 0), idx 0..80:
 22.67 23.10 23.53 23.97 24.41 24.84 25.28 25.72 26.16 | 26.59 27.03 27.46 27.90 28.33 28.75 29.18 29.60 30.03
 30.44 30.86 31.27 31.67 32.07 32.47 32.86 33.24 33.62 | 34.00 34.37 34.73 35.08 35.43 35.77 36.10 36.42 36.74
 37.04 37.34 37.63 37.91 38.17 38.43 38.68 38.92 39.14 | 39.36 39.56 39.75 39.93 40.10 40.25 40.39 40.52 40.63
 40.73 40.81 40.88 40.94 40.97 41.00 41.01 40.99 40.93 | 40.84 40.70 40.51 40.27 39.97 39.61 39.19 38.70 38.14
 37.50 36.78 35.98 35.10 34.12 30.10 21.92 12.33 4.11     (idx 81.. = 0; peak 41.01 at idx 60 ~ 6580 rpm)
MT curve @0x06008F68 (+0xF4 != 0):
 22.67 23.10 23.54 23.98 24.43 24.88 25.34 25.79 26.25 | 26.71 27.17 27.63 28.09 28.55 29.02 29.48 29.94 30.39
 30.85 31.30 31.75 32.20 32.64 33.08 33.52 33.94 34.37 | 34.79 35.20 35.60 36.00 36.39 36.78 37.15 37.52 37.87
 38.22 38.56 38.88 39.20 39.51 39.80 40.08 40.35 40.61 | 40.85 41.08 41.29 41.49 41.68 41.85 42.01 42.14 42.27
 42.37 42.46 42.53 42.58 42.61 42.63 42.62 42.61 42.59 | 42.55 42.48 42.37 42.19 41.94 41.60 41.16 40.61 39.93
 39.10 38.13 36.98 35.65 34.12 29.71 21.48 12.04 4.01     (peak 42.63 at idx 59 ~ 6470 rpm)
```
Exact raw words: read them from 0RALLY.DAT at those addresses (4 bytes each).
```c
int32 TorqueGet(Car*c, int32 rpm, int32 thr) {            // 0x06008B38
  idx = (HMUL(rpm, 0x0253C825) >> 16);   frac = HMUL((rpm>>16) % 100 << 16, 0x028F5C28);   // (rpm_int mod 100)/100  -> mismatch with idx step 109.63 (original quirk)
  T = curve[idx] + FMUL(frac, curve[idx+1]-curve[idx]);
  T = FMUL(T, c->tq_mult); T = FMUL(T, thr); T = FMUL(T, 2.75);
  return HMUL(c->F, T);                                   // = F*T as 16.16 value (executed: 1.58 at 6578 rpm, full throttle, F=918.44; 1.26 for F=734.75)
}
```
```c
int32 PowerGet(Car *c, int32 *slip) {                     // 0x06008810
  thr = c->in_accel;  if (c->state >= 2) thr = 0;         // (+0x35C/+0x360 shift-timer branches are dead)
  gear = c->mt ? c->gear : AutoGear(c, thr);  c->gear = gear;           // FUN_06008CE8 / identity
  g = (c->state != 0) ? c->gear : 0;                                    // neutral on the grid
  c->ratio = FMUL(gearTbl[g+1], c->final_drive);          // +0x34C
  R = 0x346DC6 + FDIV(0x346DC6, c->ratio);                // +0x370 : 52.43*(1+1/ratio)
  dir = Ry(c->yaw + c->steer) * (0,0,1);  vf = dot(c->vel, dir);        // forward speed along the steered wheels
  wrpm = (vf >= 0) ? FMUL(FMUL(c->speed, *(int*)0x060AA174 /*954.93*/), c->ratio) : 0;   // wheel-driven rpm
  if (g == 0) { c->rpm = c->rpm_tgt; *slip = 0; }                       // free-revving
  else {
      t = clamp(c->fb /*+0x344*/ + wrpm, 0, 8800.0);  c->rpm_tgt = t;
      c->rpm += clamp(t - c->rpm, -400.0, +400.0);                      // rpm slew limit
      e = c->rpm;  if (c->mode5f == 2) e = FMUL(e, 1.0 + FMUL(*slip, 0.95));
      *slip = min(abs(FDIV(e - wrpm, e)), 1.0);
  }
  c->wheel_step = FMUL(c->speed, 0x35555 /*3.3333*/);  c->wheel_angle += c->wheel_step;
  if (c->rpm <= 1000.0 || vf <= 0) *slip = 0;
  low = 1.0;  if (thr > 0xCCCC /*0.8*/ && c->rpm < 6000.0) low = 1.0 - HMUL(6000.0 - c->rpm, 0xAEC33);   // = rpm/6000
  c->lowrev = low;  c->slip_prev = *slip;                               // +0x354, +0x350
  T = TorqueGet(c, c->rpm, thr);   sound_hook(T);
  Q = ((int64)(T - 0x147B /*0.08*/) << 32) / R;                         // rpm increment (16.16)
  c->rpm_tgt = clamp(c->rpm + Q, 0, 8800.0);
  F = 0;
  if (g != 0 && c->state <= 1) F = FMUL(FMUL(HMUL(R, c->rpm_tgt - wrpm), c->ratio), 0x35555);   // x 1/wheel radius
  brake = BrakeForce(c);  if (vf < 0) brake = -brake;
  return F - brake;                                                      // signed force along the car
}
void PowerUse(Car*c, int32 applied, int32 requested) {                  // 0x06008AF8
  t = FMUL(c->lowrev, applied) - requested;
  c->fb /*+0x344*/ = (int32)(((int64)FMUL(-t, 0x5851 /*0.345*/) << 32) / FMUL(R, c->ratio));
}
int AutoGear(Car*c, int32 thr) {                          // 0x06008CE8
  if (c->state == 0) return 1;
  c->hold++;  if (c->hold < 15) return c->gear;            // min 15 steps between shifts (+0x340)
  g = c->gear;
  if (c->rpm > 7920.0 && c->slip_prev < 0.1) g++;                              // up
  else if (3300.0 + FMUL(2750.0, thr) > c->rpm) g--;                           // down
  g = clamp(g, 1, 4);  if (g != c->gear) c->hold = 0;  return g;
}
int32 BrakeForce(Car*c) {                                 // FUN_06008D64
  b = c->in_brake;  c->brake_light = (b != 0);  if (c->state >= 2) b = 0.5;
  r = FMUL(HMUL(1280.0, c->F), b);  return r < 0 ? 0 : r;                      // 14.3 units at full brake with F=734.75
}
```
- Top speeds per gear at the redline 8800 rpm: v = 8800/(954.93*ratio): gear1 (10.5) 0.878 m/step = 94.8 km/h, gear2 (6.0) 1.536 = 165.9 km/h, gear3 (4.95) 1.862 = 201.1 km/h, gear4 (4.2) 2.194 = 237 km/h; set B gear4 (3.825) 2.409 = 260 km/h (so the special car can reach the 250 km/h cap) [V arithmetic].
- Transmission: **auto** (`+0xF4==0`): `AutoGear` upshifts above 7920 rpm when not slipping (<0.1), downshifts when rpm < 3300 + 2750*throttle, min hold 15 steps. **Manual** (`+0xF4!=0`): `getIO_analog` @0x06009766 changes `+0xB0` by +1 (button bit 0x20) / -1 (bit 0x10) per step, clamped 0..4 (0 = neutral). The bits are *held-state* bits of `+0x114`, so any edge filtering must come from the producer of the OR-switch word [U]. No reverse gear exists [V]: holding brake at standstill only makes the car jitter and creep backward (executed: vz oscillates +-0.012 m/step, net drift about -0.002 m/step).
- Different cars: ids 0 and 1 share all physics constants [V grep: `+0x5C` is only tested against 10/13 in `PowerInit` and in sound code]. The special car (id 2->10, VS 13) has dyn table 0x06009DBC, gear set B, torque x1.0625 [V].

### 3.8 Braking [V]
Brake force = 1280*F*brake (>= 0) in the same units as the drive force, subtracted (sign flipped when moving backwards) in `PowerGet`. Digital brake input is binary (4.2). Measured by execution: full brake from 196 km/h loses about 18 km/h per 10 steps (about 15 m/s^2), 196 -> 78 km/h in 60 steps over 75 m.

### 3.9 Tyre grip / slip / drift summary [V]
Lateral force on each axle = grip_capacity * sin(k * slip) with a friction-circle clamp against the drive force (3.5). Surface changes the grip coefficient (Tarmac 1.3, Gravel 1.1, Water 0.5, Special 1.2) and damper/spring/yaw damping/drag constants (2.2). Drift state `+0x98` (0.01 or 0.15) is set when |body slip| > 0.4 and rear wheel speed > 0.2 and multiplies the combined force used for the friction-circle test by `(1+slip98)`, i.e. lowers grip while sliding. Engine wheelspin (`PowerGet` writes the same variable) does the same.

### 3.10 In-air and special behaviours [V]
- FUN_06006FA4: when not all 4 wheels are grounded (`+0xB2 != 0xF`): `pitch -= 0.42*pitch*|pitch|`; roll restored with factor 1.2 (0.5 on surface idx 6/7) when roll rate and roll have the same sign. Always (every step): `avel.yaw -= (dyn[0x0C]*bodyslip_delta + dyn[0x10]*avel.yaw) >> 24` (yaw stabiliser).
- FUN_06006F60: if wall_depth != 0 and speed <= 0.0926 (10 km/h): `avel.yaw -= 0.1885 * (cross(forward, course_dir) * 16)` - gently turns a car stuck against a wall toward the course direction.
- Goal/time-up (`+0x0A==2`): throttle 0, brake 0.5, horizontal velocity x0.9648 per step.

### 3.11 VS handicap (`player::vs_g_ctrl` @0x060079E8, only when byte [0x060AA826] bit0 is set) [V]
Progress of a car = `(+0xCA << 8) + (+0x60)` (laps*256 + segment). Let `A` = the *assisted* car = slot 1 if slot0's `+0xB6 == 1`, else slot 0; `B` = the other car; `diff = progress(B) - progress(A)`. Then
```c
B.final_drive = 3.0;                         B.force_scale = *(int*)0x060AA100;        // 734.75
x = min(diff, 0x26) + 0x25;                  // no lower clamp
A.final_drive = 3.0 - x/256;                 A.force_scale = 734.75 + x*4;            // up to +300, i.e. +41 %
```
So only the designated car is helped (the further it is behind, the larger `diff`, the bigger the boost; a big lead *reduces* its force scale below base); this is a handicap/rubber-band for one player, not symmetric [V code, I purpose].

### 3.12 Reference results from executing the original code (flat tarmac, no walls, AT, car id 0) [V by execution]
- Rest: wheels settle at body y = 0.316 m (h = 1.34 track units = 8.4 cm compression) on flat ground.
- Full throttle, automatic: 0-100 km/h in **39 steps (1.3 s)**, 0-200 km/h in 93 steps (3.1 s); upshifts at step 33 (83 km/h, 7869 rpm), 61 (152 km/h), 80 (182 km/h); top speed **226.6 km/h** at 8414 rpm in gear 4 (drag-limited, below the 250 km/h cap). Special car: 0-100 in 1.6 s, 0-200 in 3.5 s, top 246.8 km/h.
- Coasting from 196 km/h: -4.3 km/h in the first 10 steps, then about -6 per 10 steps (rolling drag 0.99747/step plus engine braking).
- Digital right input held at ~200 km/h: input ramps to 0.6875, steering angle -0.577 rad, yaw rate about -0.036 rad/step (-62 deg/s), speed drops from 205 to 133 km/h in 60 steps, slip98 rises to 0.19 (drift); right turn = decreasing yaw.
- Brake: see 3.8; hold-brake-at-rest: no reverse.
(A real course supplies polygons; results above use a single flat type-1 polygon, so only the relative behaviour is meaningful.)

### 3.13 Remaining `car` helper routines (assignment list) [V code, I use]
| routine | addr | what it does / who uses it |
|---|---|---|
| `outer_product_p` | 0x06007B0C | cross product of two 3-vectors (each component via one mac, `>>24`-style rounding); used only by FUN_060078A4 (wall response) |
| `regvect_inner` | 0x06007B6C | returns r0 = norm(A), r1 = (A.B)/norm(A) (B in r13) via sqrt and the divider; only FUN_060078A4 |
| `vecval` / `vecval_2` | 0x06007BAC / 0x06007BC0 | norm(v) of a 3-vector (mac, xtrct, `math::sqrt`) / norm((r4,r5,r6)); `vecval` used by execCar, FUN_06006DAC, FUN_060073C8; `vecval_2` has no caller |
| `regvect` | 0x06007BD8 | normalisation helper (norm(v) via `math::sqrt`, components divided by norm(v) through the divider); no caller in this image [I] |
| `calc_xyzpoint` | 0x06007C3C | out[k] = row_k(matrix, stride 16 bytes) . vec (16.16 each, one rounding per row) |
| `calc_rad` | 0x06007C7C | signed angle between two vectors using `r_atan` on norm(A)^2norm(B)^2 - (A.B)^2 and the dot product, sign from a cross term (exact form not verified); only `calc_reverse` [I] |
| `calc_reverse` | 0x06007CF8 | wrong-way detection: rel. angle between course direction (+0xB8,+0xBC) and the car's forward vector (+0x16C); stores +0xC0 (turns) and +0xC5 (quadrant) |
| `calc_dispang`, `calc_dispang2`, `calc_dispang2p` | 0x06007D2C / 0x06007D68 / 0x06007D2E | display angle nibble +0xC7 from course direction and camera yaw (presentation) |
| `check_dist_ab` | 0x06007DA0 | returns 0 if norm(A+B)^2 > r10 else 1 (A = r8 ptr, B = r9 ptr); no caller in this image |
| `spline` | 0x06007DDC | uniform cubic B-spline blend (basis 1/6, 2/3 ... of parameter t in r8) of 4 control points; used by dispcar (ghost/camera path) at 0x0600FF88 and two pool users (0x0600DDD0, 0x06010060) - not physics |
| `view_changer1p/2p`, `pl1p_or_switch`, `pl2p_or_switch` | 0x06008604 / 0x060085FC / 0x060085EC / 0x060085F4 | camera-view toggle (4.1); the two `*_or_switch` are 2-instruction setters of 0x060AA00A/0x060AA00C called through pointers from menu/attract code |
| `get_poly_map*`, `get_other_map` | 0x060081B8.. | presentation/minimap polygon lookup for the car's position (second car function) |
| `vs_rank_calc` | 0x060084D0 | VS ranking from progress values (VS path only) |

---

## 4. Input handling

### 4.1 Digital pad and key config [V]
`car::make_gswdata` @0x06008568 (called every step before execCar) dispatches on `car+0x08` (play mode): 0 = live P1 input via `config_keyset` **and records** the 16-bit input word into the replay buffer (`R+0x74` pointer, end `R+0x78`); 1,2 = playback: word read from the buffer (physics is re-simulated from recorded inputs, so replays/ghosts are deterministic functions of the 16-bit-per-step input stream); 3 = live without recording; 4 = P2 live (pad 2, config byte 0x060AA814, flag mask 8).
`car::config_keyset` @0x0600862C builds `car+0x114`:
```
row = 0x060086C8 + (cfg & 0x7F)*16 (+0x40 if the pad has ID 0x10, i.e. 0x060AA803 bit set; r0 = 8 "analog" flag; the digital left/right bits are then dropped)
bit 0x01 = accel   (word0 & padword)         bit 0x02 = brake (word1)
bit 0x20 = shift up (word2 & OR-switch word) bit 0x10 = shift down (word3)
bits 0x80/0x40 (digital pad only) = Right/Left from the pad high byte (0x8000/0x4000)
view buttons (words 4,5,6) -> +0x114 bits 15..8 (0x40,0x20,0x10)
byte 3 of +0x114 = analog steering byte
```
Row table (Saturn pad bits: B 0x0100, C 0x0200, A 0x0400, R 0x0080, X 0x0040, Y 0x0020, Z 0x0010, L 0x0008, Up 0x1000, Down 0x2000) - decoded from 0x060086C8 [V values, I button names]:
| row | accel | brake | shift up | shift down | view |
|---|---|---|---|---|---|
| 0 (default) | B | A+C | R | L | X,Y,Z |
| 1 | B | A+C | Down | Up | X,Y,Z |
| 2 | B | A+C | Down | Up | R,L |
| 3 | B | X+Y+Z | C | A | R,L |
| 4 (analog dev) | B+C | Y+Z | Down | Up | A,X |
| 5 (analog dev) | Y+Z | B+C | Down | Up | A,X |
| 6 (analog dev) | B | A+C | Down | Up | X,Y,Z |
| 7 (analog dev) | B | A+C | X | Z | Down,Up |
View toggle: `car::view_changer1p` @0x06008604: any of bits 0x70 -> `[0x060AA01B] = ([..]+2)&2` (camera view toggle) unless play mode 2.

### 4.2 Controls -> analog values: `pad::getIO_pad` @0x0600975C [V]
Output block `c+0xA0`: `[0]` accel, `[4]` brake, `[8]` steering, all 16.16.
```c
word = *(u16*)(c+0x114);                  // bits 15..8 analog byte, 7..0 flags
if (word & 0x08) {                        // analog device
    a = (word >> 8) & 0xFF;  a -= 127;    if (mirror /*[0x060AA82E]*/) a = -a - 1;
    s = FMUL((a * 512), c->analog_gain /*+0x2D0*/);   s = clamp(s, -1.0, +1.0);   // NO dead zone; centre = 127
} else {                                   // digital: right if (word & 0x80), left if (word & 0x40); mirror flips the sign
    dir = word&0xC0 ? (word&0x80 ? +1 : -1) * (mirror ? -1 : 1) : 0;
    if (dir == 0)        s = move toward 0 by [+0x2C8] (negative number: s += sign(s)*... stops at 0)   // -0.15625
    else if (s==0 || same_sign(s,dir)) s += dir * (|s|*[+0x2C0] + [+0x2C4]);   // clamp +-1.0
    else                 s = move toward 0 by [+0x2CC] (-0.375)                // counter-steer is faster
}
// accel (digital buttons even with an analog steering device):
accel += (word & 0x01) ? +0.125 : -1.0   -> clamp [0,1]      // table 0x06009914 / 0x0600991C
brake += (word & 0x06) ? +1.0   : -1.0   -> clamp [0,1]      // table 0x06009924 / 0x0600992C ; config_keyset sets bit 0x02 only
if (c->mt) { if (word & 0x20) gear++ else if (word & 0x10) gear--;  gear = clamp(gear, 0, 4); }   // @0x06009882
```
So accelerator reaches full in 8 steps (0.27 s) and releases instantly; brake is binary. Pad kind: only "analog device (ID byte 0x10)" vs digital matters; `pad::analogcfg` (0x06009934 = 1.375) and `difp_handle_on/off` are unreferenced data [V]. Wheel/force-feedback: none (stubs, 3.6). Steering path continues in `getSteer` (3.6).

### 4.3 Device detection [V]
`vdp::pad_kind` @0x060053B8: byte `[0x060AA9A4]` (pad 1) / `[0x060AA9A6]` (pad 2) == 0x10 -> sets bit 2 / bit 3 of `[0x060AA803]`. `switch::pad_read` @0x06005A18 fills `0x060AA990..` from SMPC peripheral data; analog axis = data byte 4 -> `[0x060AA99C]` (pad 1) [V code, I naming "racing wheel"].

---

## 5. Race flow

Routine names in brackets are the DBG names after the +0xBD4 correction of 0.3 (real addresses are used everywhere else). The per-car running flow is in 1.3.

### 5.1 Race init [V]
`init::init_game_work` @0x06004844 -> `gctrl::allclr_car_ctrl_work`, `plctrl::normal_ctrl_plset` (or `vs_ctrl_plset`), sets slot0: `+0x08`, `+0xF4`, `+0xF5=0`, `+0x5C`, `+0x5F`, calls `FUN_06014270` [`laptime::lap_time_init`] (lap/time init, below), then `pad::carconfig`.
Stage start-up (`start::start_init` real 0x06015408, `start_main` real 0x06015510 = main-vector handler): `start_init` loads the course graphics, runs a fade (`clroffset::pre_fade_inout`), stores a sub-state function pointer in `[0x060AA178]` (0x0601554C) and installs the main vector 0x06015510; `start_main` then calls the function in `[0x060AA178]` every frame. A second entry (0x060154EC) installs the same main vector and jumps to `init::init_game_work_start` @0x060041F0. `init_game_work` zeroes `[0x060AA178]`, which afterwards serves as the countdown state integer (5.2) [V code, I flow].
`plctrl::mycar_init` @0x0600609C: state 0, `+0xF5=4`; start pose from `datgbal::pinit_tbl` (0x002996D0, 16 bytes/course: x, y, z, yaw): Desert (-369, 0.4, -310, 0), Forest (-279.7, 0.4, -216, 0), Mountain (-90, 15.4, -529.1, -1.5708), Lake (202.8, 30.5, -83.6, -0.0873); constants listed in 2.1/2.5; `initCarControl` @0x060090FC -> `PowerInit`, `InitKickback`.
`FUN_06014270` [`lap_time_init`] @0x06014270 (per game type, jump table @0x06014280 -> 0x0601428E / 0x060142F6 / 0x0601434E / 0x06014322 / 0x0601434E / 0x06014386 for types 0/2/4/6/8/10): sets `[0x2A8]` (0x00095999 = 9.35, or 0x00595999 = 89.35 for type 10; purpose [U]) and the total laps word `[0x060AA006]`: type 0 = setting byte [0x060AA80C]; type 2 = 3 laps on course 0, else 2; types 4 and 8 = 3; type 6 = per-course bytes {4,5,4,5} (@0x06014348); clears the lap block R+0x04..; sets `R+0x34 = 29` (0 s 29 frames); for types 0 and 2 it then adds the **initial time = time-extension lookup with lap 0, checkpoint 0** (5.4). For type 0 on a course other than DESERT the code does not reset `R+0x34` (only its frame byte is set to 29 and `R+0x0C = R+0x08`), i.e. the remaining time of the previous stage seems to carry over, then the stage's initial extension is added [V code, I interpretation].

### 5.2 Countdown [V]
Not `timer::*`. The grid sequence is driven from the *second* car function: `normal_plmt_start` (0x06006638) calls `FUN_0601310C` [`display3::count_down_start`] @0x0601310C with r2 = &`[0x060AA178]` (state, values 0,2,...,10; sub-timer `[0x060AA17C]`); it dispatches through the table @0x06013120 (states 0/2/4/6/8/10 -> 0x0601312C / 0x06013158 / 0x06013184 / 0x060131A4 / 0x060131C4 / 0x06013200) and then jumps to the HUD routine 0x06010710:
- state 0: `+0xF5 = 4`, state := 2, timer := 25 steps (104 if byte [0x060AA01A] is nonzero);
- state 2: timer-- ; at 0: `[0x060AA803] |= 0x40`, sound 32, state := 4, timer := 0;
- states 4, 6, 8 show the digits **3, 2, 1** (r5 = 3,2,1): each adds 0x800 to the timer per step and advances when it exceeds 0x10000 (33 steps = 1.1 s), with sounds 33 (state 4->6), 34 (6->8) and, at the end of state 8, 35;
- end of state 8 ("GO"): `[0x060AA180] = 2`, **car state `+0x0A = 1`**, car fn0 := `normal_plmove` (flag bit preserved), `+0xF5 = 6`, state := 10;
- state 10 shows GO for 8 more steps (timer to 0x4000), then selects/starts the stage music (via `FUN_0600F756` with a table indexed by type/course) [I].
During the countdown `execCar` runs with state 0: engine in neutral (free revving), horizontal velocity forced to 0 [V]. Two-player variants (`display3` code @0x0601337x-0x06013450) set `vs_plmove1p/2p` and `+0xF5 = 6` the same way.

### 5.3 Lap, checkpoint, goal [V]
`FUN_0601465C` [`check_lap`] @0x0601465C (called every step from `normal_plmove`/`vs_plmove*`) dispatches on the game type (table @0x06014668): types 0 and 2 -> 0x06014674 (described here, with time extension), 4 and 8 -> 0x06014750 and 6 -> 0x06014810 (same lap/goal logic without extension), 10 (VS) -> 0x06014960. Core: `d = pos(+0x60) - prev(+0x62)` (course segment indices from `get_block`).
- `d > 100`: moved backwards across the line: `+0xCA -= 1`.
- `d < -100` (segment index wrapped = crossed the line forward): `+0xCA += 1`; if `+0xCA == +0xC8 + 1`: if `[0x060AA006] == +0xC8 + 1` -> **goal** (fn := `goali_plmove`, `[0x060AA300] = +0xB6`, `+0xF5 = 8`); else new lap: `+0xC8 = +0xC8 + 1`, `+0xCC = 0`, R+4 = 0, R+8 = R+0xC (lap time bookkeeping), HUD message 29 via `[0x060AAA45]`, sound 1, then **time extension** with checkpoint id 0 (`FUN_06014BB0`).
- otherwise (|d| <= 100): if `+0xC8 == +0xCA` and `+0xCC < +0x60`: `+0xCC = +0x60` (progress high-water mark) and, when `+0x60 != 0` and play mode != 2: if `+0xF7 != 0` call `FUN_06014C08` (split-time record); if `+0xC4 < 0` -> sound 1, HUD message 29, **time extension with checkpoint id `(+0xC4)&0x7F`**; if `+0xC4 > 0` -> `+0xF2 = +0xC4` (marker).
The VS version (0x06014960 and `lap_myline` @0x06014B4C) compares against the lap total and at the end sets fn0/fn4 := 0 for finished VS cars [V code, I details].

### 5.4 Time-limit counter and extension [V]
Counter `R+0x34` = `(seconds << 8) | frames`. `FUN_0601459C` [`extend_time_calc`] @0x0601459C (types <= 2 only; types > 2 return immediately): if `R+0x34 == 28` -> car state `+0x0A = 2` (time up); else `R+0x34 -= 1`, and when the low 7 bits underflow to 127 -> borrow: `(R+0x34 >> 8 << 8) + 29`; state stays 1. (So the displayed time reaches 0 s 28 frames at expiry.) VS/other types use `FUN_060145D4` [`lag_time_calc`] @0x060145D4: up-counting min/sec/frame stop-watch with limit byte `[0x060AA827]`.
**Extension** `FUN_06014BB0` @0x06014BB0 (inside the DBG range of `lap_myline`, real 0x06014B4C-0x06014CE8; r3 = checkpoint id):
```c
t1   = *(u32*)(0x06014BE4 + 4*([0x060AA860]/2));            // 0x00299370 (type 0) or 0x00299418 (type 2)
t2   = *(u32*)(t1 + 4*[0x060AA808]);                        // per-difficulty array of pointers (13 longs; only entries 0..4 are row pointers)
rowp = *(u32*)(t2 + 4*lap);                                 // lap = car +0xC8; 16-byte row (4 courses x 4 slots)
secs = *(u8 *)(rowp + [0x060AA882] /*course*4*/ + (ckpt & 0x7F));
R[0x34] += secs << 8;                                       // seconds field
```
Data (bytes per row: 16 = 4 courses x 4 checkpoint slots; slot 0 = time added at the start (lap 0) and at every lap line, slots 1..3 = the course's mid-lap checkpoints) [V values]:
| type 0 (0x299370), difficulty idx 0 | desert | forest | mountain | lake |
|---|---|---|---|---|
| lap 0 row | 60 25 0 0 | 20 22 25 0 | 15 18 16 23 | 80 0 0 0 |
| lap 1..4 row | 24 36 0 0 | 29 23 30 0 | 13 19 17 25 | 80 0 0 0 |
difficulty 1: lap0 `60 20 0 0 / 20 22 25 0 / 15 17 14 22 / 75 0 0 0`, later `23 35 0 0 / 28 22 28 0 / 13 19 17 23 / 76 0 0 0`; difficulty 2: lap0 `60 20 0 0 / 15 20 25 0 / 15 16 14 21 / 72 0 0 0`, later `21 33 0 0 / 27 21 27 0 / 12 18 16 23 / 73 0 0 0`.
Type 2 (0x299418): idx0 lap0 `50 32 0 0 / 50 20 28 0 / 50 13 13 20 / 85 0 0 0`, later `21 33 0 0 / 28 22 28 0 / 12 18 16 24 / 85 0 0 0`; idx1 lap0 `50 27 0 0 / 50 14 26 0 / 50 10 10 13 / 80 0 0 0`, later `21 33 0 0 / 26 19 26 0 / 12 18 16 23 / 75 0 0 0`; idx2 lap0 `50 20 0 0 / 50 12 26 0 / 45 9 12 15 / 80 0 0 0`, later `18 31 0 0 / 27 20 26 0 / 12 17 15 21 / 68 0 0 0`.
Course-side data (parsed from `BG<course>.DAT`, segment records at 0x00293000; byte0 bit7 = extension checkpoint with id = byte0&0x7F, byte1 = sector/split mark) [V]:
| course (index) | extension checkpoints (segment, id) | sector marks (segment, id) |
|---|---|---|
| DESERT (0) | (98, 1) | (0,1) (45,2) (98,3) (158,4) (215,5) |
| FOREST (1) | (98, 1) (164, 2) | (0,1) (56,2) (98,3) (133,4) (164,5) (216,6) |
| MOUNT (2) | (54, 1) (108, 2) (169, 3) | (0,1) (53,2) (107,3) (168,4) (215,5) |
| LAKE (3) | none | (0,1) (44,2) (85,3) (124,4) (173,5) (224,6) |
This matches the table: slot 0 = start/lap-line time, slots 1..3 = the course's checkpoints (Desert 1, Forest 2, Mountain 3, Lake 0). Rows are selected by `+0xC8` (laps completed so far): row 0 applies to the first lap (its slot 0 is the *initial* time, e.g. Desert 60 s), later rows to subsequent laps. Segments are numbered 0..255 around the lap; the lap line is segment 0/255.
The pointer arrays (0x29937C, 0x2993B0, 0x2993E4 for type 0; 0x299424, 0x299458, 0x29948C for type 2; 52 bytes each) hold row pointers only for laps 0..4 (lap 0 row, then the same row repeated); a lap index above 4 or a difficulty index above 2 would read unrelated data (no guard in the code) [V]. Note: the DBG name `extend_time_calc` belongs to the time-limit counter above (0x0601459C); the per-checkpoint extension adder is `FUN_06014BB0` inside `lap_myline`'s range [V]. The task's "extend_time_calc rules" are therefore: counter format + decrement/expiry (this section) and the table-driven additions (below).

### 5.5 Goal, results, game over [V/I]
- Goal: `goali_plmove*` (@0x0600640C/0x06006490/0x060064E0): `+0x0A = 2`, record via `FUN_06014F58`/`FUN_06014CE8`, fn := `goal_plmove`, second fn := goal presentation (LAB_060068E0/6920/6954), car teleported to `goal_posi*_tbl` pose (9 longs per course: pos, angles, velocity; e.g. Desert (-372.6, 0.246, -287.5), angles (0.018,-0.008,0), velocity (0.007,-0.003,0.504)) by `goal_car_move2p` [V]. The car then coasts under `execCar` (throttle 0, brake 0.5, drag 0.9648).
- Time up: `+0x0A = 2` (5.4) then `LAB_06015380` game-over rule (1.3): speed <= 5.7 km/h -> `+0xF5 = 10`, fn := `gover_plmove`.
- `plctrl::set_gover` @0x06006BA8: `+0xF5 = (0x060AA821==1) ? 10 : 12` then falls into `FUN_06006B58` (stores results at 0x060AFE00: per-course byte `+0xB6`, total time R+0x74, ...), `plctrl::game_exit` @0x06006A18 dispatches on game type (table @0x06006A24) [V code, I semantics]. The main-vector handlers that actually leave the race (`init_game_over`, `game_over`, `result_main`) are in `display`/`res_main` (not read) [U].

---

## 6. What a PSP reimplementation needs

### 6.1 Physics core - functions to port (with sizes)
| function | addr | bytes |
|---|---|---|
| execCar (incl. all FUN_ pieces: 06006DAC 06006DF4 06006E24 06006EE4 06006F60 06006FA4 06007020 06007080 0600716C 06007304 060073C8 060074EC 0600755C 060075F0 0600760C 06007718 060078A4 06007954) | 0x06006C4C | 3484 total |
| carctrl::carControl + FUN_06009718 (rotate_xz) | 0x06009124 / 0x06009718 | 1366 + 52 |
| power: PowerInit, PowerGet, PowerUse, TorqueGet, FUN_06008CE8 (AutoGear), FUN_06008D64 (BrakeForce), FUN_06008BCC (sound only) | 0x0600879C / 0x06008810 / 0x06008AF8 / 0x06008B38 / 0x06008CE8 / 0x06008D64 | 82 / 640 / 58 / 112 / 106 / 54 |
| steer::getSteer (+ stubs) | 0x06008774 | 32 |
| pad: getIO_pad, getIO_analog, carconfig (+FUN_06009A7E for the special car) | 0x0600975C / 0x06009766 / 0x06009938 | 140 / 184 / 276 |
| car: make_gswdata, config_keyset, view_changer1p | 0x06008568 / 0x0600862C / 0x06008604 | 14+ / 100 / 38 |
| car math helpers: outer_product_p, regvect_inner, vecval, calc_xyzpoint, calc_reverse, calc_rad | 0x06007B0C / 0x06007B6C / 0x06007BAC / 0x06007C3C / 0x06007CF8 / 0x06007C7C | 94 / 64 / 20 / 64 / 50 / 122 |
| collision queries used by physics: get_coli_area, get_y, get_d, FUN_060080A8 (deref) + FUN_060080AA (polygon scan), get_block, FUN_06007F3C (segment scan) | 0x06008008 / 0x0600802C / 0x06008060 / 0x060080A8 / 0x060080AA / 0x06007EAC / 0x06007F3C | 34 / 52 / 70 / 2 / 238 / 118 / 198 |
| matrix/math: unit_matrix, t_rotate_x/y/z, r_rotate_y, calc_point, calc_point2, inner_product, sincos, sin/cos, r_sin, atan, r_atan, sqrt | 0x06017D24 / 0x060180F8,06018118,06018138 / 0x06017FBC / 0x0601B97C / 0x0601B9C4 / 0x0601B910 / 0x06018058 / 0x0601B76C / 0x0601B764 / 0x0601B7C0 / 0x0601B794 / 0x0601B8CC | - |
| race flow: mycar_init, normal_plstart_wait, normal_plmove, FUN_0601465C, FUN_0601459C, FUN_06014BB0, FUN_06014270, countdown state machine (~0x06013130), vs_g_ctrl, goali_plmove*, goal_car_move2p | see 1.3/5 | |
| gctrl::game_ctrl / car_move_ctrl (scheduling) | 0x06005B78 / 0x06005E84 | 680 / 62 |

### 6.2 Data the core depends on
- From `0RALLY.DAT` code area: gear tables 0x06008DA4/0x06008DBC; torque curves 0x06008DD4/0x06008F68; key rows 0x060086C8; steering tables 0x06009A98/0x06009AAC; tyre/suspension tables 0x06009AFC/0x06009B4C/0x06009B9C/0x06009BEC; special-car dyn 0x06009DBC; cardyn pointer table 0x06009D80; wheel offset table 0x06007924; corner table 0x06007674; digital accel/brake ramp structs 0x06009914..0x0600992C; time-extension tables (via 0x06014BE4).
- From `PRGDATA.DAT` (block 0x060A1000, 0x49A2 bytes): `carDynTarmac/Gravel/Water/Special` (0x060A14D0..0x060A15A0), `cardyntable` (0x060A1490), `sincos_tbl` (0x060A15A0, 4096 x (sin,cos) int16, scaled x4), `atan_tbl` (0x060A55A0); block 0x00298000: `pinit_tbl` (0x002996D0), `goal_posi*_tbl` (0x002995B0/0x00299640), extension tables (0x00299370/0x00299418 ...).
- From `BG<course>.DAT`: collision blocks (2.6). Course-segment records for `get_block`.
- Mutable globals: 2.5.

### 6.3 Behaviours that must be reproduced bit-exactly or knowingly approximated
1. All arithmetic with 64-bit intermediates and the exact shift/rounding (FMUL / MUL24 / HMUL / mac chains / divider truncation).
2. Polygon normals are length 256 in the data; spring/wall vectors divide by 256 via `>>24`. A float port that normalises normals must rescale.
3. Quirks to keep: `bias9c & 0xFE`; rear no dead zone, front dead zone 0.01; `|dh|` for tiny damper deltas; torque index step 109.63 rpm but fraction from rpm mod 100; front/rear friction-circle blends differ (1.2 sqrt - 0.2 rho^2 vs 0.4 sqrt + 0.6 rho^2); pitch and yaw share the same torque-to-rate constant; wall friction special case on course 3; drift flag sets differ for `+0x5F == 2`.
4. Deterministic replay/ghost: inputs are recorded as the 16-bit word per 30 Hz step and the physics is re-run [V]; any change of numerics breaks stored replays (not needed for a fresh port).
5. Two-player: player 2's physics ran on the slave CPU; the math is independent of the CPU, but the per-slot dyn block (+0xD0) and separate setup bytes must be kept.

### 6.4 OPEN QUESTIONS
1. Meaning of `[0x060AA811]==2` (+0x5F==2) and the names of the 4 setup bytes at 0x060AA817 (steering/tyres/suspension F/R) in the menus [U]; which game type numbers 4/6/8 are [U]; course mapping of `[0x060AA808]` (difficulty) [I].
2. Producer of the OR-switch words 0x060AA00A/0C (shift/view buttons are tested against them, not against the pad word) and therefore the exact edge semantics of manual shifting [U].
3. Not decoded: `FUN_06014C08` (split-time record, inside the DBG range of `lap_myline`), `FUN_06014CE8` [`time_cmpgl_save`], `FUN_06014DDC` [`lap_time_calc`], `FUN_06014E34` [`total_time_calc`], `FUN_06014F58` [`total_time_calc_all`], `FUN_060150B4` [`time_adjust`], `FUN_06015224` [`vstime_cmp`] (split/total time bookkeeping, result recording, VS comparison); lap-block fields R+0x04..0x30, 0x48..0x7E are only partially understood [U]. Their DBG names are usable once the +0xBD4 correction of 0.3 is applied.
4. Exact semantics of polygon flags low half-word, of surface types 1..11 beyond the dyn-table grouping (what is "Special"?), of the unit of `get_d` (distance/8 in track units) and the exact meaning of the low flag bits in the coarse-block records [U/I].
5. `[0x060AA168]` bit0, `[0x060AA170]` (72.8), `+0x84/+0x88`, `+0xAC`, `+0xB6`, `+0xF2/+0xF7` precise roles [U].
6. AI cars (`othercar`, `otherctrl`) use different code; whether they share `dyn` blocks (slot != 0 uses +0xD0 for *players* only) [U].
7. Behaviour on missing polygon (debug trap): the real console behaviour of `trapa #9` (vector 9) is unknown; assume it never triggers [U].
8. Purpose of the global `[0x2A8]` (0x00095999 = 9.35, 0x00595999 = 89.35, 0x00CCCCCC = 204.8) that the race-init code sets and the result code compares against lap times [U].

### 6.5 How this was validated / how to reproduce numbers
Round-trip tool (not part of the repo): an SH-2 interpreter covering the ~100 opcodes used, the on-chip divider, flat fake polygon (type 1, normal (0,256,0), D=0) hooked at `get_coli_area`; call sequence `carconfig -> mycar_init(r13=0x002996D0) -> initCarControl -> execCar` with `[car+0x114]` set to the input word (0x0001 accel, 0x0002 brake, 0x0080 right). All numbers in 3.12 and the "executed" notes were produced that way, so a port can be compared against the same trajectories.
