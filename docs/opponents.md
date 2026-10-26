# The other cars (M7): `src/opp.c`, `src/oppmath.h`

The rally opponents of the arcade game (game type 0) and the practice rival (game type 2), ported by hand from the original code and checked **byte for byte** against the oracle (`tools/test_opp.sh`).
`docs/collision_world_ai.md` section 4 is the reading of the original; this file says what was built and what the port taught us (it corrects that section in a few places).

## 1. Layout

| file | what |
|---|---|
| `src/oppmath.h` | the `copro` library (`c_outp2`, `c_getr`, `c_vecval`, `c_reg`, `c_turnd` / `c_turnv`, `regularAngle`, `asinf`, the point transforms) and the matrix helpers they call, with the SH-2's rounding (`sqrt_s` = the 64-bit sum of squares -> 16.16 length, big values through `sqrt(hi) << 8 + 127`) |
| `src/opp.c / .h` | `opp_spawn` (normal_otinit: `initOtherGP` / `initOtherPR`), `opp_move` (the slot handlers `[+0]` of the 8 other-car slots: the first-frame handler 0x0600C958, then `execOtherCar`), `opp_other_ctrl` (otherCtrl: rank, LOD requests, draw handlers, neighbours, car-car collisions), `opp_pass` = both, `opp_map` (get_other_map), `opp_visible` |
| `src/coli.c` | `coli_get2` = `get_coli2` 0x060083B4 (type, normal, height / wall depth of the polygon under a point) |
| `src/drive.c` | `drive_calc_dispang` (the start-up routine calls calc_dispang2 on its own); `phys.dat` has 5 more regions (lane widths, the wheel / corner / speed-scale tables, the `otherdat` block 0x0600CC30 .. 0x0600D210, the guide tables up to 0x0029EAA0) |

Every car is the original's **0x400-byte slot as big-endian bytes at the original offsets** (`Opp.car[8][0x400]`, console addresses 0x060B0600 + 0x400 k; the player is 0x060B0000 = `Drive.car`), and the cars point at each other by the console's addresses
(+0x154 car ahead, +0x158 car behind).  The globals the original keeps in work RAM next to the cars are mirrored into the memory they live in (`sync_mem`): car count / rank word / progress word / guide table pointer in the player's slot at +0x2B0 .. +0x2BB, the sorted car list and
the pass counter in the first other car's slot at +0x1A0 .. +0x1E3.

## 2. What was ported

* **Spawn.**  `initOtherGP`: start rank byte `[0x060B00B6]` (15 on the first arcade stage, the previous stage's placing after that), up to 6 cars ahead and 4 behind at the guide-line offsets of `octgptbl` (difficulty x "3 laps" flag), `FUN_0600C78A` (car type from `CarRankTable`, guide position + 0.3 m, LOD 3, 8 equal speeds), the rank word.  `initOtherPR`: the one fast rival of the practice mode (LOD 0 from the start).
* **Move.**  `execOtherCar` (LOD transitions, target speed per lap, the four LOD bodies, the block / lap tracking), `setMark` (position on the guide line, the quadratic B-spline frame), `OtherCarCtrl` (corner speed `150 + Ld/4 - 300 curv`, avoidance), `FUN_0600A374`, `setVelocity`, `getAngle`;
  LOD 3 `far_car` (guide line, no collisions), LOD 2 `near_car` + `tinyOther`, LOD 1 `simpleOther`, LOD 0 `setOtherCar` + `tinyForce` + `otrCarColi` (four wheels with springs / dampers, four wall corners, a rigid body).
* **Race bookkeeping.**  `otherCtrl` (GP and PR versions): the player's progress word, the player's **rank byte** (`[0xB6]`: virtual cars ahead + other cars ahead), LOD requests (position of the car's block in the ten-block list of `get_other_map`), the draw handlers (`normal_otmt` when the block is in the part list),
  the descending-by-block sort and every car's neighbours, `carColiOtr` (car-car, pushes the first car of a pair), `carColiGP` / `hitCheck` (player against each near other car, ellipsoid radii, pushes only the player), `hitCheck2` (practice rival).

## 3. Verification

`oracle.py run --opp --render --slots` (`tools/oracle_m7.sh`) runs the game's own code with the other cars (start-up routine 0x06010FE0, then game_ctrl's per-pass order: player handlers, camera, the slot loop, sync_ctrl, otherCtrl) and logs the first 0x200 bytes of the eight other-car slots + their globals after every pass (`<name>.opp`) and the player's slot (`.slots`).
`tools/test_drive.c` (`opp=1` in the .inp header) replays the same inputs through `drive.c` + `camera.c` + `pmap.c` + `opp.c` and compares all of it after every pass:

| traces | passes | result |
|---|---|---|
| arcade, pilot on the guide line, every stage (desert 5,805, forest 843, mount 1,269, lake 5,165) | ~13,000 | 0 passes differ |
| arcade, slow pilot (the player stays among the other cars) on every stage | ~7,000 | 0 passes differ (28 contacts with the player, wall corners of LOD 0 cars) |
| arcade, player parked on the grid (the other cars lap the stage past it) | ~6,000 | 0 passes differ |
| arcade, parked player + two other cars put side by side next to it every ~200 passes (`--perturb`; `carColiOtr`: 39 contacts) | 2,066 | 0 passes differ |
| practice, every stage (the rival at LOD 0 all the way: wall corners, suspension) | ~12,600 | 0 passes differ (21 contacts with the rival) |

`tools/test_drive.sh` prints the contact counters (player / rival / car-car / wall corners).

## 4. What the port taught us (corrections to collision_world_ai.md)

* `game_ctrl`'s loop over the other cars' slots does **not** test bit 0 of the handler words (only `car_move_ctrl`, the start-up one, does).
* `OtherCarCtrl`: the lane target when the car behind is **near** is the cross product magnitude (r3), not the lane half-width (an original slip: the register holding the signed width is not stored); when it is far the car steers to the side opposite to the car ahead.
* `tinyOther`'s yaw smoothing block keeps the high word of its product in the register that holds the new **roll**: after the first pass in LOD 2 the roll is 0 or -1.
* `initOtherGP` writes a **word** 1 at the rank byte on Lake (rank byte = 0, previous block byte = 1); `initOtherPR` writes the type byte of the slot after the last record too.
* `otherCtrl` (practice) writes the "neighbour" pointers into the first other car's slot from the second pass on (GBR is that car then) and into the player's slot (+0x154 / +0x158, which are elements of the player's scratch matrix) only in the first.
* The `setOtherCar` damping table index `(byte >> 2) & 0x1C0` only works for guide bytes below 0x80 (every real guide point): the first entry, 1.0.
* The rank byte counts **virtual cars**: on the first arcade stage 15 - 6 = 9 cars ahead are never met (only 6 are simulated); only the simulated cars change the rank.
* The guide tables are 303 records (4848 bytes): the lake table ends at 0x0029EA98.

## 5. Not done / open

* The sound calls of the cars, the ghost car of the practice mode (`[0x060AA82D]`, the secret condition of `initOtherPR`), game types 4 / 6 / 8 (time attack opponent: `car_move2p_ctrl`).
* The scenery-less parts: car damage / scratches are not shown (the display reads the damage word as the original does).
* Opponent display uses `car_display` per car (types 0 - 10 of `cars.dat`); the dust / spray of the other cars uses the same spawner (`spray_spawn_car`).
