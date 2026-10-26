# Sega Rally Championship (Saturn, USA) - event objects

What the DBG calls `cache / etcsub / dsp / action / event / generator / public / animal / motion / splash / heli` (0x0601BAF4 .. 0x0601E4E8 in `0RALLY.DAT`) is **not** cache / DSP code
(render_pipeline.md section 9): it is the **object scheduler and the scenery objects** - spectators, check-point gates, poles, banners, flags, the start object, zebras, elephants, birds, swans, spray.
Reverse engineered from the disassembly (`tools/sh2dis.py`), ported to C (`src/event.c`) and checked against the game itself (`tools/evvec.py` + `tools/test_event.c`: about 170,000 draws over
4 courses and game types 0 / 2, every state function exercised, 0 differences). Evidence tags as in the other documents: [V] verified by the oracle replay, [I] inferred, [U] unknown.

## 1. Data and slots

* `EVENTTBL.MAD` = block 0 at 0x00200000 (object tables) + block 1 at 0x002B0000 (= `ANIMAL.MOT`, keyframe animation data, 56,080 bytes). Block 0: 8 pointers (4 courses 1P, 4 courses 2P, indexed by
  `[0x060AA882]` = course * 4) -> per course 128 pointers (one per map block, the same block numbers as the course part list) -> `{u32 count; count x 24-byte record}`.
  Record: `u16 w0` (running id), `u16 type`, `i32 L1`, `i32 L2`, `i32 x, y, z` (16.16 m). Object counts per course (1P): Desert 387, Forest 615, Mountain 572, Lake 210. [V]
* Race start (`FUN_0601BC94`: loader through the CD hook, `FUN_0601BF18`: copy, `FUN_0601DC14`: clear): every record becomes a **64-byte slot** at `0x060E4000 + 0x40 * n` (n = running number over all
  blocks in block order; 2P: second pool at 0x060EA000), the group table at `0x060D0C00 + 4 * block` = `{u16 byte offset of the first slot, u16 count}`. Slot layout [V]:

| off | meaning |
|---|---|
| +0x00 | state function (initially the entry of the type, table 0x0601BFEC; each state writes the next one) |
| +0x0C | word: parity (n + 1) & 1: 1 = slave CPU, 0 = master CPU |
| +0x0E / +0x0F | bytes: animation counter (signed) / step |
| +0x10 / +0x12 | words = L1: part / variant index, flip / variant |
| +0x14 / +0x18 / +0x1C | position x, y, z (16.16) |
| +0x24 | heading (65536 = one turn) |
| +0x2C | part pointer / table pointer (swans: bobbing phase) |
| +0x30.. | word / longs of state: timers, velocity (+0x38 vertical, +0x3C forward) |

* The slot, **not the model matrix, is the object**: a state function pushes the camera matrix (r4), translates by the slot position, rotates, tests visibility and calls a draw function.
  GBR = slot, r14 = slot. Everything below is `[V]` unless marked.

## 2. Dispatcher (game_ctrl step 6)

`FUN_0601BCB8(r4 = 0x060AA140 visible block list, r5 = count word 0x060AA002)`: sets `[0x060D0114]` = (`[car+0xC5]` == 0), queues the **slave half** (`0x0601BD52`, own matrix stack 0x060D1800)
and runs the master half itself; both walk the visible blocks in list order and call the state function of every slot of the block whose parity matches (slave: parity 1, master: parity 0).
The master finally calls the special object at 0x060EFE00 (the helicopter, desert only, section 7). One random number generator is shared by both halves, so the real game is not deterministic;
the oracle runs the slave job synchronously first (replay order = slave half, master half). After it `game_ctrl` runs `0x0601D410` (the spray list at 0x060D0140, see 6).

## 3. Helpers

* `0x0601D6CC` = **random number generator**: `r = r * 5 + 0x3537` (state at 0x060D0050, starts at 0), returns r. *It was stubbed as a "sound" call in the M4 oracle traces* (returns its own address);
  `Race(real_rand=True)` runs the real one (event objects need it, the car traces do not use it).
* `0x0601D8D4(r4 = matrix, r5 = radius)`: sphere visibility: depth 0 < z < 250, K = (224 << 32) / z, sx / sy = (x, y) K >> 32, r = (radius >> 1) K >> 32 (all truncated), visible when
  `sx - r <= 176 && sx + r >= -176 && sy - r <= 112 && sy + r >= -112`. (Also used by the car display, `src/mat.h m_visible`.)
* `0x0601CE8C(r5 = table, r6 = mode)` **keyframe animator**: table = `{u16 n frames, u16 nparts, u32 data, u32 part[nparts]}`, data = n x nparts x 20 bytes `{i32 tx, ty, tz; i16 rz, ry, rx (turns); pad}`.
  Draws frame |counter| (every part: translate, rotate z, y, x, `draw_polygon`), then counter += step; at n: mode 0 -> 0, mode 1 -> 1 - n (turn around), mode 2 -> n - 1. Returns |counter| (mode 1 at the turn: 1 - n).
* `0x0601CD40` mover: position += R_y(heading) * (0, +0x38, +0x3C). `0x0601CABE` scare: bird takes off when |dx| + |dz| <= 64 m from the car.
* Matrix entry points `0x06017FC2 / 0x06018012 / 0x06017F76` = rotate y / z / x with the angle already in turns (the `r_` forms convert radians first); their sense is opposite to the formulas in render_pipeline.md 2.1.
* Draw functions: `draw_polygon` 0x06018908 (object scale 1/1024, near 0.5, no clipping), `draw_clip_polygon` 0x06018A10 (same scale, near plane clipped), `draw_people_polygon` 0x06019870: screen aligned
  quad at the view-space position of the matrix, vertices (dx, dy) / 1024 m, x mirrored by r6 = +-1; `draw_splash_polygon` 0x06019936: x as people, y scaled by r6 / 65536.

## 4. Types (table 0x0601BFEC)

| type | state function | what | notes |
|---|---|---|---|
| 1 | 0x0601C814 | zebra (ANIMAL2 `shima_*`: body 30 frames + tail 60 frames, two animators sharing the counter word) | pauses at the ends, random timers; visible radius 2 |
| 2, 22 | 0x0601C914, 0x0601C684 | deleted at once | |
| 3 | 0x0601C96C | bird on the ground (`bird1_*`): hop (21 frames, speed 0x3851, turns 40 deg or changes state), peck (32 frames), fly away (wings, accelerates) | scare 64 m; not when `[car+0xCA]` != 0; game type 6: deleted |
| 4 | 0x0601C4E4 | **spectator** (people table 0x0601C614: 26 figures, then zebra / elephant sprites): billboard, mirrored when `(w12 ^ [0x060D0114]) & 1` | radius 1; 60 % of all objects |
| 5 | 0x0601CC2C | elephant (`zou_*`: 25 parts, 90 frames), walks 0x5A0 / frame for 1800 frames, then deleted | |
| 6, 7, 8 | 0x0601C564, 0x0601C5F8 | no-op | |
| 9 | 0x0601CB14 | bird in flight (`bird2_*`, scale 0.3, 20 frames), lives 600 frames, flag table 0x0601CC1C selects variants | not game type 6 |
| 10 | 0x0601CCAC | swan: billboard `swan_poly`, mirrored, bobs sin(phase) * 0.1 m, drifts | lake |
| 16 | 0x0601C06C | check-point / finish gate: table 0x0601C190 (16 x {scale x, dx, dy, routine}), parts `checkpoint_s / l`, `finish_l` through `draw_clip_polygon`; flags 0x060D0100..02 (`0x060D0102` hides arcade banners once a gate is drawn); routine 0 also marks the last gate when `[car+0xC8]` == `[0x060AA006]` - 1 | radius 4 |
| 17 | 0x0601C2A0 | pole (`pole_s / m / l` by w10 / 2), `draw_clip_polygon` | radius 4 |
| 18 | 0x0601C2F4 | flag: 32-frame animation (FlagAnimation1 / 2 tables 0x060F5EE0 / 0x060F5F60), random start phase, **advances every executed frame, drawn or not** | arcade (game type 0) only |
| 19 | 0x0601C370 | start object (`startobj_poly` scaled 0.9) + a push-back box on the car at the desert start (`start_push`: the car's +0x64 / +0x6C get the distance to the nearer edge of the box x -369 +- 3 m, z -296 +- 32.5 m; id 2 every 15 passes while it pushes, not on the grid, only in the modes of the table 0x06008C9C) | arcade only |
| 20 | 0x0601C470 | banner (`am3kanban` w10 = 2, `megakanban` w10 = 3), scale 1 + 0.1 * w12 | |
| 21 | 0x0601C530 | spectator variant of type 4 (hidden once a gate was drawn) | arcade only |

## 5. Port and verification

* `tools/conv_events.py` -> `events.dat` (object records, motion tables with part numbers, the motion block, the two flag tables); `tools/conv_objects.py` also converts the ANIMAL2 parts (zebra, elephant) with the
  texture relocation of the game (verified against the oracle's VRAM).
* `src/event.c` (state machines on 64-byte big-endian slots, so the original's overlapping word / long fields translate one to one), `src/mat.h` (matrix operations, sphere test).
* `tools/evvec.py [course] [game type] [passes] --tour` -> `extracted/oracle/ev_<course>_<type>.vec`: the pilot drives, the dispatcher runs after every pass with the draw functions replaced by recorders; `--tour`
  adds frames with a synthetic camera 14 m in front of every object of the rarer types (elephants, swans, banners, gates ...). `tools/test_event.sh` replays all vectors through `src/event.c`
  (inputs per pass: random seed, camera, car fields, block list). Result: every pass identical (slot, kind, part, matrix within 0.02).
* The slave half has its own copy of the camera matrix (0x060D1800); the test harness writes both.

## 5b. Sound (M9)

The objects call the sound library in two places: the helicopter (id 29 at the start of the flight, a pan word every pass of it) and the start object (push-back and id 2).  `Events.snd` collects the calls of a dispatch (`ev_frame` clears it), `EvWorld.push_x / push_z` the push for the car, `EvWorld.view_fx` is the camera matrix as the game holds it (the pan is computed in fixed point).  The vectors "EVE2" of tools/evvec.py carry them (docs/sound.md section 6).

## 6. Open

* The 2P tables (second pool at 0x060EA000), `generator` / `public` areas not reached by any object type.
* The spray of the slave CPU's list (0x060D0160: only game types with a second car display fill it).

## 7. The helicopter (desert only) [V]

`heli` module, 0x0601DC14 .. 0x0601E2F0: one 512-byte object at **0x060EFE00** (state function at +0, GBR = the object, called by the master CPU after the last slot of the visible blocks) and a list of 32 rotor-dust
particles at **0x060D2000** (16 bytes each: size, x, y, z; size 0 = free).  Ported in `src/event.c` (`heli_*`), checked by `tools/evvec.py --laps=1` / `tools/test_event.sh` (`ev_0_0_heli`, `ev_0_2_heli`: 6,200+ dust sprites,
body, both rotors and the shadow over 285 visible passes each, 0 differing passes).

* **Start** (`FUN_0601DC14`, race start): clears both areas; `[0x060AA87E]` (course number) != 0 -> no helicopter; game type (`[0x060AA860]`) 0 -> table 0x0601E2F4, 2 -> table 0x0601E308, other types none.
  Table = {u16 start slice, u16 path nodes (0x3A), long speed (0x2600 / 0x2C08), ..., pointer to the flight path in ANIMAL.MOT (helidata63 / helidata64: 60 nodes of 16 bytes {x, y, z, heading}, 16.16)}.
* **Wait state** (0x0601DC6C): starts when the car's lap word `[0x060B00CA]` == `[0x060AA006]` - 1 (the last lap) and its slice word `[0x060B0060]` == the table's slice (0xD6 / 0xD7); then
  +0x34 (path position, 16.16 nodes) = 0, rotor angles +0x38 / +0x3C = 0, tilt +0x40 / +0x44 = 0, +0x48 = 10.0 (divisor), position = node 0, state = flight, and the sound 29 (ported: section 7 and docs/sound.md 6).
* **Flight state** (0x0601DCE8, every pass): distance to the car `+0x78` = sqrt((dx^2 + dz^2) >> 16) (the math module's `sqrt`); finished (state := 0) when the path position reaches 58 nodes.
  `FUN_0601DFCC` = **cubic Lagrange through four nodes** (n - 1 .. n + 2, the first clamped to node 0) with weights -t(t-1)(t-2)/6, (t+1)(t-1)(t-2)/2, -(t+1)t(t-2)/2, (t+1)t(t-1)/6 (fixed point with the `xtrct` truncation) for x, y, z and
  heading (node angle - pi/2); velocity = target - position (x, z), position := target (y + 2.5); bank / pitch = the velocity rotated by the old heading * 1143 (0x2CAE0000 / 10.0).
  Then: rotor angles += 1.25 rad / `0x0601DFC8` (16.16 radians per pass), path position += speed * (1 - distance * 0.000714) (the farther from the car, the slower: it waits for the player), `get_coli3` under the helicopter (type, height above the ground, normal).
* **Drawing** (all `draw_polygon`, matrices as the original builds them): shadow only below 15 m on a floor polygon: translated to the ground (+0.03), turned by the heading and the ground normal (`atan` + 0x4000 turns about z and x), scaled
  0.8 + 0.04 * height, parts `heli_kage` then `heli2` (rotor shadow, turned by the rotor angle); the helicopter at the object position (radius 4 visibility test): `heli1` body at (0, -2.5, 0) after heading, bank, pitch (r_ rotations: radians 16.16);
  `heli2` main rotor (turned by +0x38); `heli3` tail rotor at (0, -1.15, -6) turned by +0x3C about x.
* **Dust** (`0x0601E146` x 2 per pass, `0x0601E1BC`): below 15 m over surface types 3, 4, 7, 8 (table 0x0601E1B0) a free particle gets size = (15 - height) * 0.095, x / z within +-2 m of the helicopter (two `rand` calls each: the shared RNG),
  y = ground; each pass every particle is drawn as a `sand` splash billboard scaled (2 - |size|), size -= 0.12, freed at -1.5 (or at exactly 0).
* The collision lookup is `src/coli.c` (ports of `get_coli_area` / `get_y` / `get_coli3`, `tools/test_gmath.c`: 5,231 oracle vectors identical) with `src/gmath.h` (`sqrt`, `atan`: 1,228 vectors identical).


## 8. Spray / dust objects (`spray` module, 0x0601D074 .. 0x0601D58C) [V]

Ported in `src/spray.c` (`spray_init`, `spray_spawn`, `spray_frame`), checked against the game by `tools/spvec.py` + `tools/test_spray.c`: the whole list (header, 28 + 28 pointers, 28 objects of 64 bytes) after the spawner and after every list run, the car work fields
+0x3C0 .. +0x3E3 and the splash draws, over 8 runs (4 stages, game types 0 / 2, the three cars; 3,000 passes each): 0 differences (~48,000 splash draws).

* **List** (master: 0x060D0140, slave: 0x060D0160; only the master one is used with one car): +0 active count, +4 free count, +8 four enable bytes (one per wheel: `0x101` = rear wheels, set by the race start `0x0601D074`; 0x01010101 in other modes),
  +12 / +16 / +20 pointers to the active array (0x060D3000), the free stack (0x060D3100), the 28 objects (0x060D3200, 64 bytes each).  The master / slave test reads the bus state control register (address -32 + 2).
* **Spawner** `FUN_0601D0E6`, called by the car display (`dispcar::dispGhost`) in its near and middle detail tiers only (see `car_tier`): needs enable bytes and car +0xEC > 0 (copied to +0x3DC).  The car's velocity (+0x24, +0x2C) is turned back by the yaw into the car frame (+0x3CC lateral smoothed
  by 0.6 per pass, +0x3D0 its absolute value).  The car matrix (position, yaw, pitch, roll) is rebuilt; then for each enabled wheel (word +0xB4, one nibble per wheel = the surface under it) with a wheel value (+0x3C + 4 w) > 0
  (shifted by 4 = r12) and a free object (28 max): the routine word table at 0x0601D328 (by nibble: 0 = none; nibbles 5, 6 = variant A (water), 3, 4, 7, 8 = variant B (sand / dust)) picks the object type.  Wheel offsets: table 0x0601D380 / 0x0601D3B0 / 0x0601D3E0 chosen by the car type byte (+0x5C) through 0x0601D340.
  Both variants translate the car matrix to the wheel (y + r12) and store the world position in obj +4 / +8 / +12.  A (0x0601D1EA): turned by the steering angle (+0x30), velocity = the point of (lateral + side, speed * r12) minus the position, size
  (1 .. 4) * |height difference|, sprite `water_poly`.  B (0x0601D296): rises (obj +20 = 0.3 * |height diff| / 2), size as A, sprite `sand_poly`, 15 frames.  The shared random generator (+0x060D0050) is called once per object.
* **Per pass** (`0x0601D42A`, game_ctrl step 7, only while the car's control byte +0xF5 < 8): from the last active object to the first: draw (translate the camera matrix to the position, radius 1 visibility test, `draw_splash_polygon` with the size) then age.
  A: gravity (+20 -= 0x51E), moves along obj +16 (x and z: the original adds the same word to both), size += +20, dies below 0.  B: +20 -= 0x51F while rising, 0x83 otherwise; dies at size < 0 or after its 15 frames.  Dead objects are replaced by the last active one and pushed on the free stack.
* The object list is part of the 1P frame: `src/play.c` spawns after the car display, runs the list after the event objects.
