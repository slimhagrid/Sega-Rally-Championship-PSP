# Sega Rally Championship (Saturn, USA) - collision, track map, camera, AI cars

Scope: modules `car` (map queries), `copro`, `carcoli`, `plctrl::calc_pmap_sub`, camera, `othercar` / `othersub` / `otherset` / `otherctrl` / `otherdat`, and the parts of
`gctrl` / `laptime` / `car::vs_rank_calc` that touch position along the course.  Source: `0RALLY.DAT` (SH-2, load 0x06004000), the decompiler/asm dumps in
`extracted/analysis`, and the real data files in `extracted/disc/files` (which I parsed with Python to confirm layouts).  No code for the port is proposed here; this is understanding only.

Status tags: **VERIFIED** = I read the code that does it (and, for data layouts, checked the layout against the real files); **INFERRED** = consistent with the code but not proven;
**UNKNOWN** = not decoded.  Addresses are REAL addresses in the 0RALLY.DAT image (see section 0.2 for the DBG-name problem).

---------------------------------------------------------------------------------------------------------------------------------------------

## 0. Read this first

### 0.1 Units and conventions (VERIFIED unless noted)
* Positions, velocities, distances: signed 16.16 fixed point.  Car struct position = +0x18 x, +0x1c y, +0x20 z; y is up.
* `dmuls.l` + `xtrct` = 16.16 multiply.  `mac.l` runs use the 64-bit MACH:MACL accumulator; sums are normally finished with `xtrct mach,macl`.  Division uses the SH-2 on-chip
  divider (`0xFFFFFF00` = DVSR, `+0x10` DVDNTH, `+0x14` DVDNTL, quotient read back at `+0x1c`): 64-bit dividend / 32-bit divisor.  A port can use int64 for all of this.
* Angles in the car struct (+0x0c rot X, +0x10 rot Y, +0x14 rot Z) are radians in 16.16 (pi = 0x0003243f, pi/2 = 0x0001921f, `copro::regularAngle` wraps at +-pi).
  Camera angle words (16-bit) use 0x10000 = one full turn; conversion radians->turns is `x * 0x28be60db >> 32` (0x28be60db = 10430.378 in 16.16 = 65536/(2*pi)).
* 1 world unit is about 1 metre and a frame is 1/30 s (INFERRED): AI speeds are in km/h and are converted with `* 0x25e / 65536` = 0.00923 (= 1/108 = 1/(3.6*30)) to units/frame
  (othersub::setVelocity @0x0600aeb4, OtherCarCtrl).  The main loop runs 2 VBlanks per pass = 30 fps (NOTES.md).
* Collision polygons use a finer scale: coordinates are `16.16 << 4` (1.0 unit = 2^20, "11.20", range +-2048), plane normals are 8.24, plane constant `d` is 2^22-scaled.

### 0.2 DBG names are WRONG for a large part of this area (important for the Ghidra project)
The shipped `0RALLY.DAT` is not laid out like the debug build `RALLY.DBG` describes.  Modules `car copro carcoli othercar othersub otherset otherctrl gctrl plctrl player` are where
the DBG says (checked by function prologues and by literal pool references).  Further on, they are not:

| DBG says | Real |
|---|---|
| `otherdat` (data tables) at 0x0600ea3c..0x0600ee88 | **0x0600cdb8..0x0600d204** (size 0x44c).  Proof: `initOtherGP` loads `*(0x0600cdb8+course)`, `FUN_0600c78a` indexes 0x0600cdc8 (CarRankTable), `initOtherPR` 0x0600cddc (octpr_inittbl), 0x0600d064 (distStep), 0x0600d0c4 (octgptbl), 0x0600d0dc (octgp_inittbl), 0x0600d1b0 (acc_gp), 0x0600d1c0 (acc_pr), `OtherCarCtrl` 0x0600d200 (crvspdmin).  Every symbol offset inside the module matches. |
| `adv advctrl photo photoctrl` | real = DBG + 0x44c (e.g. the boot main vector `FUN_0600d214` = `adv_init`, DBG 0x0600cdc8) |
| `camera` at 0x0600ee88..0x0600f5e4 | **0x0600fa5c..0x060101b8** (real = DBG + 0xbd4).  `normal_cainit`=0x0600fa5c, `vs_cainit`=0x0600faa8, `normal_ca`=0x0600faf0, `replay_ca`=0x0600fc5c, `replay_cactl`=0x0600fc94, `calc_camera_pmap`=0x0600fd34.  Proof: `init_game_work` (0x06004844) stores 0x0600fa5c / 0x0600faa8 as camera handlers, `normal_cainit` stores 0x0600faf0, `game_ctrl` calls 0x0600fc94. |
| code at 0x0600ee88..0x0600f5e4 labelled `camera` / `normal_ca`... | really the CD/GFS file-loader wrappers (FUN_0600f354 etc.) |
| `laptime` (check_lap 0x06013a88) | real = DBG + 0xbd4: `lap_time_init` = 0x06014270, `check_lap` = 0x0601465c (the routine `normal_plmove` calls right after `get_block`), `lap_time_calc2p` = 0x06014dd8 (Ghidra labels it `scroll::set_scr_int`) |
| `dispcar` labels `dispTachoMeter`, `dispCountDown`... between 0x0600fa5c and 0x060101b8 | camera code (see section 3) |
| `display3::count_down_start1p` as the caller of `execOtherCar`, `setMark`, `get_block2`... | an artefact of Ghidra function boundaries; the real callers are `otherctrl` + `gctrl::game_ctrl` |

Consequence: any Ghidra/DBG name between 0x0600cdb8 and about 0x06015xxx should be treated as unreliable until re-derived.  Sound (`sound::sd_seq_vol_set` 0x0601633c) and the
`slave` module (with the existing 0x34 correction) look right again.  The most plausible explanation is that the release build drops debug-only code and reorders modules.

### 0.3 Correction to NOTES.md (scatter-load `.DAT` layout) - VERIFIED
Each block of a scatter-load file starts on a **2048-byte sector boundary** and the header takes one sector: the loader (`FUN_0600f354`) reads the header with one 0x800-byte read, then
for every entry reads `(size + 0x7ff) >> 11` sectors.  Checks: `BGDESERT.DAT` = header + 26+3+1+8+8+85+4+6+1 sectors = 143 sectors = 292,864 bytes = file size;
`DESERT.MAD` (3 blocks), `PRGDATA.DAT` (5 blocks) also add up exactly.  The "blocks follow back to back" statement in NOTES.md is only true after rounding each block to a sector;
parsing with plain back-to-back offsets silently produces garbage (I fell into this first).  Plain, uncompressed (type 0) blocks can then be read directly.

---------------------------------------------------------------------------------------------------------------------------------------------

## 1. How the game represents the track for physics

### 1.1 Data files and buffers (all VERIFIED against BG{DESERT,FOREST,MOUNT,LAKE}.DAT)
The physics data is **not** in the `.MAD` files; it is in the per-course background dataset `BG<course>.DAT` (loaded by `crs_dataset` with the course word `0x060AA882`), as six
plain (type 0) blocks in Low WRAM.  `AD<course>.DAT` (the attract/title dataset) carries only the last four of them (no collision polygons).

| Low WRAM block | size (desert) | content | read by |
|---|---|---|---|
| 0x00240000 | 0x4000 | **collision-area grid**: 64 x 64 longs, each a pointer (0 = empty) to a list in the 0x248000 block | `get_coli_area` @0x06008008, `get_coli3` |
| 0x00244000 | 0x4000 | **block grid**: 64 x 64 longs, pointer to a byte list in the 0x290000 block (0 = empty) | `get_block` @0x06007eac, `get_block2` @0x06008360 |
| 0x00248000 | 0x2a40e | **collision polygons**, stride 0x34, followed by the lists the 0x240000 grid points to | `FUN_060080aa` (poly locate), `get_y`, `get_d` |
| 0x00290000 | 0x1b94 | **block quads**: 128 records of 0x24 bytes, then the byte lists of the block grid | `get_block` / `FUN_06007f3c` |
| 0x00293000 | 0x2e1b | **slices**: 256 records of 0x2c bytes (finer progress units along the road) | `get_block` second stage |
| 0x00296000 | 0x200 | **block -> slice list table**: 128 longs, pointer to a byte list of slice indices | `get_block` second stage |

Per-course counts (VERIFIED by scanning all lists): desert 3118 collision polygons (1400 type-0 "walls", 1718 floor), forest 4689, mount 3342, lake 3087; every course has exactly
**128 blocks and 256 slices**.  Block ids 0..127 follow the road in driving order and wrap.

Buffer sizes vary per course (coli block: 173,070 / 258,756 / 185,124 / 170,904 bytes for desert / forest / mount / lake).  The first rows of the 0x240000 grid of the desert file
(rows 0..9) contain leftover non-pointer garbage (dev-machine memory dump); rows 10..32 are empty; real cells start at row 33.  They are outside the playable area; a port must
not trust cells it never queries.

### 1.2 Grid addressing (VERIFIED, code + data)
Block grid (`get_block` @0x06007eac): position in raw 16.16 (x = [car+0x18], z = [car+0x20]).
```
col = (x + 0x04000000) >> 21        ; cell = 32.0 units, grid covers x in [-1024,1024)
row = (0x04000000 - z) >> 21        ; row 0 = z +1024, row 63 = z -1024   (code: ((v>>16) & ~0x1f)<<1, then + col, *4)
list = *(0x00244000 + (row*64 + col)*4)
```
Collision grid (`get_coli_area` @0x06008008) is the same cell geometry but takes pre-shifted coordinates (x<<4, z<<4):
`col = ((x<<4) + 0x40000000) >> 25`, `row = (0x40000000 - (z<<4)) >> 25`, `list = *(0x00240000 + (row*64+col)*4)`.
Both give 64x64 cells of 32 units.  Limits: |x|,|z| < 1024 units; `x<<4` wraps at 2048.

### 1.3 Level 1 - block quads (0x290000 + 0x244000)
Block-grid list = `[u8 count][u8 quad index * count]` (VERIFIED: 731 cells, max index 127).  Quad record (0x24 bytes):

| off | field |
|---|---|
| +0 u8 `A` | number of blocks to draw ahead when the car looks "forward" (values 6..11) |
| +1 u8 `B` | same, looking backwards (7 or 8) |
| +2 u8 `A'` | variant of A used when the game mode word `0x060aa860` == 10 (2P battle) |
| +3 u8 | **block id** 0..127 |
| +4.. 4 x (x:s32, z:s32) | quad corners, 16.16, consecutive quads share an edge; quads are ~25 units long and ~44 wide in the desert |

Point-in-quad test = `FUN_06007f3c` @0x06007f3c: for every index in the list, four cross products `(xi-x)*(z(i+1)-z) - (x(i+1)-x)*(zi-z)` (dmuls/mac); the point is inside when the OR of
the four sign bits is clear; if all four are negative the code executes `trapa 9` with the debug tag 'TRIP' (0x54524950); if the list is exhausted, trap with 'NUKP'.  Returns the
record pointer in r6.  Centroid test: the centre of each of the 128 quads of the desert is found by this algorithm in 128/128 cases (VERIFIED).

`get_block` (car @r14 = gbr; reads x,z at +0x18/+0x20):
1. stage 1 (grid 0x244000): `[car+0xf1] = A (or A' if mode==10) | (B << 4)`; `[car+0xb7] = old [car+0xf0]`; `[car+0xf0] = block id`.
2. stage 2: table 0x296000 + blockid*4 -> list of slice indices -> point-in-quad over 0x293000 (stride 0x2c): from the slice record
   `[car+0xc4] = byte0`, `[car+0xf7] = byte1`, `[car+0x62] = old [car+0x60]`, `[car+0x60] (word) = byte3` (slice id 0..255), `[car+0xb8] = long@+0x24`, `[car+0xbc] = long@+0x28`
   (unit road direction in x/z, 16.16).
`get_block2` @0x06008360 is the same two stages for an arbitrary position pointer in r12 (`[r12+0]` = x, `[r12+8]` = z), used by the AI; it stores only the block id (+0xf0), slice (+0x60/+0x62),
direction (+0xb8/+0xbc); it does not touch +0xf1.

Slice record (0x2c bytes): +0 u8 attribute (0 for 244 of 256 slices in the desert; other values 3,7,8,12,13,20 and 129/130/131 = top bit set), +1 u8 section number (0 mostly, 1..6 at fixed
slices, increasing along the lap, e.g. desert at slices 0,45,98,158,215), +2 u8 (0), +3 u8 slice id, +4.. 4 corner pairs (16.16, ~12.5 x 44 units), +0x24/+0x28 unit road direction (x,z).
A block holds 2-3 slices (e.g. block 0 = {0,1,2,255}).  The meaning of byte0 / byte1 is UNKNOWN beyond: `check_lap` stores a positive byte0 in `[car+0xf2]`, and a negative one (129+) triggers a
call sequence (`FUN_06016fbc(1)`, byte 0x060aaa45=29, `FUN_06014bb0`), i.e. it is an event/checkpoint marker (INFERRED).

### 1.4 Level 3 - collision polygons (0x248000 + 0x240000)  (the physics map)
Grid list = `[u16 count][u16 polygon index * count]`; polygon index i -> record at `0x248000 + i*0x34`.  VERIFIED on desert: 263 distinct lists (1..57 entries, mean 19), max index 3117,
and a centroid test of 500 random polygons found the polygon itself through the grid + point-in-polygon code in 500/500 cases.  Lists put wall polygons (type 0) first; for centroids
of floor polygons the first hit was always a floor polygon (1109/1109), for wall centroids a wall (891/891), so wall footprints do not overlap the road.

Record layout (0x34 bytes), VERIFIED by the code in `FUN_060080aa`, `get_y`, `get_d` and by the file:

| off | field |
|---|---|
| +0x00 u32 flags | high 16 bits = **type** (0 = wall/obstacle footprint, nonzero = floor with surface type); bit 0 of the low half = **triangle** (3 vertices instead of 4) |
| +0x04.. | 4 x (x:s32, z:s32), "11.20" (16.16<<4); for a triangle the 4th pair is 0 |
| +0x24, +0x28, +0x2c | plane/wall normal (nx, ny, nz), 8.24 fixed |
| +0x30 | floor: plane constant d (value = d_true * 2^22); wall: length of edge v0->v1 (value = length * 2^17) |

Surface types seen: desert {3,4,5,7} (+0 walls), forest {1,3,7,10}, mount {1,2,8,9}, lake {3,8}; what each type means (sand/asphalt/...) is UNKNOWN (consumers: wheel/tyre code,
`[car+0xb4]` replicated type words in the AI).  The type of the polygon under a wheel is returned in the output struct (+0x10, word).

`FUN_060080aa` @0x060080aa (called through `get_coli_area` @0x06008008, which loads the list pointer into r6 and `bra`s to it): iterate the list, for each polygon do the
3-or-4-edge sign test (inside = all edge functions >= 0, i.e. one winding order), return the **first** polygon in list order containing (x<<4, z<<4) in r6, or r6 = 0.
When nothing is found and r14 points into the car array (0x060b0000..0x060b2600) it executes `trapa 9` with tag 'NUKE' (0x4e554b45), a debug assertion "car off the map".

### 1.5 Ground height, wall distance, normal  (VERIFIED)
* `get_y` @0x0600802c (r4 = x<<4, r5 = z<<4, r6 = polygon, r7 = out vec): `y20 = (d*2^22 - x*nx - z*nz) / ny` via the hardware divider, i.e. the plane `nx*x + ny*y + nz*z = d`;
  result (the quotient, read by the caller at 0xffffff1c) is y in 2^20 scale.  Out vec[0..2] = the polygon normal (raw 8.24).  Example desert poly 3000: d=83,222,022 -> 19.84,
  normal (-0.265, 0.964, -0.013), computed y = -28.0 at its centre (plausible height).
* `get_d` @0x06008060 (same args): `| (x0-x)(z1-z) - (x1-x)(z0-z) | / [poly+0x30]  >> 6` = distance from the point to the edge v0->v1 of a wall polygon (scale 2^17 per unit; the
  edge-function numerator is 2^40/unit^2, the stored edge length 2^17/unit, so the quotient 2^23 is shifted by 6).  Out vec = polygon normal (horizontal for walls).
* `get_coli2` @0x060083b4 (r12 = pointer to a position vector (x at +0, y at +4, z at +8), r13 = output struct): shifts x,z by 4, `get_coli_area`; word at poly+0 (type) -> `out+0x10`;
  type == 0 -> `get_d` (wall): `out+0x0c = distance`; else `get_y`: `out+0x0c = ground_y(2^20) - (y<<4)` (ground minus wheel height: positive when the point is below the ground);
  `out+0..8` = normal.  (r8/r11 are OR-ed in the wall branch as a flag; unused later.)  Callers shift `out+0x0c` right by 4 to get 16.16 (tinyForce, tinyOther).
* `get_coli3` @0x060083ec: ground height under the car in [r14+0x18/0x20] (with a NULL-list early exit); returns r1 = car.y - ground.y(16.16), r0 = type word; used by `heli::InitHeli`.
* A "wall" polygon is thus an oriented rectangle (edge v0v1 is its front face); a point inside the footprint is inside the obstacle and `get_d` gives the penetration depth along the normal.
  Wheel probes (`FUN_06007080`) do not expect a wall there: on type 0 they jump to a debug trap tagged 'D2ER' (0x44324552).

### 1.6 "Poly map" = visible-block list for rendering (not collision)  (VERIFIED)
`get_poly_map` @0x060081b8 (gbr = car, for the player; the camera has its own dummy car, section 3) builds the list of course-model objects to draw:
1. `[car+0xc7]` (0..15) = view sector, set by `calc_dispang2` @0x06007d68 as `(-(atan2(dirx,dirz) - 0x4000 + camera yaw word + camera extra word)) >> 12 & 0xf`
   (heading relative to the road direction `[car+0xb8/0xbc]`, in sixteenths of a turn; camera words 0x060aa9c6/0x060aa9cc).
2. Table of 16 pointers at 0x0600821c selects a signed byte list (offsets in blocks, stored as block*4; terminated by -1; first byte = flag):
   sectors 0,1,14,15 (looking along the road): `[0,-1,+1,+2,+3,...,+9]`; 2,13: `[0,-1,1,2,...,6]`; 3,12: `[0,-1,1,-2,2,3]`; 4,11: `[0,-1,1,-2,2,-3]`; 5,10: `[0,1,-1,-2,...,-6]`;
   6..9 (looking back, flag=1): `[0,1,-1,-2,...,-9]`.
3. Number of entries used = the low nibble of `[car+0xf1]` (A) if flag = 0, the high nibble (B) if flag = 1; each entry `(blockid*4 + off) & 0x1fc` indexes the model pointer table.
4. Model table: `course_mdl_buf` = **0x06048000** = first block of `<COURSE>.MAD` (VERIFIED: DESERT.MAD block 0 = 128 longs, first object at 0x06048200).  Each object starts with a long whose low
   16 bits are the polygon count (desert total 11,208).  A polygon budget stops the list: 0x5dc = 1500 (1P), 0x300 = 768 or 0x4b0 = 1200 (`get_poly_map1p/2p`, 1200 when word 0x060aa87e != 0).
5. Output: object pointers -> `0x060aa104[]` (2P: 0x060aa198), block indices -> `0x060aa140[]` (2P: 0x060aa1d4), count words -> 0x060aa000 and 0x060aa002 (2P: 0x060aa020/22).
`get_other_map` @0x0600829c: ten block ids `(car block + {0,+1,-1,+2,-2,+3,+4,+5,+6,+7}) & 0x7f` -> bytes at 0x060aa154, word 0x060aa01c = 10.  `otherctrl` uses this list for AI LOD (4.5).
`plctrl::calc_pmap_sub` @0x06006668 = for car 0x060b0000: `calc_dispang2` + `get_poly_map` + `get_other_map` (used by the attract/ending driver `FUN_0601f048`).

### 1.7 Other per-car quantities derived from the map  (VERIFIED)
* `car::calc_reverse` @0x06007cf8: builds the road direction vector (`[+0xb8]`, 0, `[+0xbc]`) in the car struct (+0x190) and calls `calc_rad` @0x06007c7c (signed angle between two vectors via
  `r_atan`) against the car forward vector at +0x16c; stores the angle in `[+0xc0]` and `quadrant = ((angle*K - 0x6000) << 2 >> 16) & 3` in byte `[+0xc5]` (wrong-way detection).
* Lap counting (`check_lap`, real 0x0601465c, owned by the lap/physics reader): delta = slice(+0x60) - previous slice(+0x62); `delta < -100` -> lap `[+0xca]++`, `delta > 100` -> lap `[+0xca]--`
  (the slice id wraps 255 <-> 0); `[+0xc8]` is the highest lap reached; reaching the lap count word 0x060aa006 sets handler `goali_plmove` and `[+0xf5] = 8`.
* `car::vs_rank_calc` @0x060084d0 (2P battle): compares lap `[+0xca]`, then slice `[+0x60]`, then the sign of `(p1-p2) . road_dir(+0xb8/+0xbc)` and writes ranks 1/2 into `[+0xb6]` of both cars.

---------------------------------------------------------------------------------------------------------------------------------------------

## 2. Collision pipeline

### 2.1 Where it runs (per frame, `gctrl::game_ctrl` @0x06005b78)  (VERIFIED)
`[0x060aa860]` (mode word, a byte offset into a 6-entry table at 0x06005b84) selects one of three loops: values 0 and 2 -> 0x06005b90 (single car + AI), 4/6/8 -> 0x06005c30 (two car slots, car 1 on
the slave SH-2), 10 -> 0x06005cb8 (2P battle, split screen).  Setters (found by scanning every store to 0x060aa860): 0 = `g_select_init` / ending code (arcade = "GP"), **2 = the game-select code at 0x0601fdd8, right next to the `scrldata::prac_cl` / `d_prac_cl` colour symbols, i.e. practice ("PR") - INFERRED from those symbols**, 4 = `tm_attck_init` (time attack), 6 = the time-attack menu code 0x06024900/0x06024a1c, 10 = `battle_2p_init`; 8 was not found.
The race start-up routine at 0x06010fe0 (jump table 0x06010ff4) shows which modes have AI cars: modes 0 and 2 run `car_move_ctrl`, **`normal_otinit`** (spawns the other cars), `camera_ctrl`, `calc_pmap_sub`, `otherCtrl`;
modes 4/6/8 run `car_move2p_ctrl` twice (no AI spawn: the second car slot is a player-type car run on the slave CPU - the time-attack opponent, INFERRED); mode 10 runs `car_move2p_ctrl` twice and `camera1p/2p_ctrl`.
Mode 0/2 order: slave `s_mkpoly_frame_init` / master `m_mkpoly_frame_init`; player handler `[0x060b0000+0]` (e.g. `normal_plmove`); `replay_cactl` 0x0600fc94; `camera_ctrl`
(handler at `[0x060aa9b0]`, r4 = view matrix 0x060d1000); player `[+4]` (`normal_plmt`: `calc_dispang2`, `get_poly_map`, `get_other_map`); then every other car slot 0x060b0600 + n*0x400 (n = 0..7)
whose handler word is nonzero and has bit 0 clear: `[car+0]` (AI move) then `[car+4]` (draw or not); `plctrl::sync_ctrl`; `otherctrl::otherCtrl` (AI bookkeeping + AI/AI and player/AI collision);
visibility (`FUN_0601bcb8`); `mkpoly::draw_course(0x060d1000, 0x060aa104, count)`; z-sort.  In mode 10 the order is `carColiVS` -> `vs_rank_calc` -> `player::vs_g_ctrl`.
Handler pointers carry a flag in bit 0 (set = skipped by the master loop; the slave loop `s_car_move2p_ctrl` runs handlers whose bit 0 is set, calling `handler-1`).

Car storage: player 1 = 0x060b0000 (size 0x600), then slots of 0x400 bytes from 0x060b0600 (player 2 in battle mode, otherwise AI/ghost cars); camera dummy car = 0x060b2600.
Number of AI cars = word 0x060b02b0.

### 2.2 Player car vs world (`player::execCar` @0x06006c4c; details belong to the physics reader)  (call order VERIFIED)
`execCar`: pad -> steer (`getSteer`, skipped in goal state `[+0xf5]==8`) -> **`FUN_06007954`** (apply last frame's collision push) -> predicted pose -> **`FUN_06007718`** (body corner probe)
-> `FUN_0600760c` x2 -> **`FUN_06007080`** (wheel ground probe) -> `FUN_0600755c`, `FUN_06007304` -> **`FUN_0600716c`** (wheel world positions) -> `carControl` -> ...
* `FUN_06007080` (wheel probe): for each of 4 wheels with its bit set in mask `[car+0xf6]`: world position at `car+0x190 + 12*i` -> `get_coli_area(x<<4, z<<4)` -> if floor: `get_y`, result record at
  `car+0x1c0 + 20*i` = {normal x,y,z (8.24), height diff +0x0c (2^20), type word +0x10}; if the polygon is a wall -> trap 'D2ER'.  Afterwards it converts the 4 wheel positions into deltas
  relative to the previous ones (`car+0x240`, 12-byte entries).
* `FUN_06007718` (body probe): builds the car rotation matrix at `car+0x130` (t_rotate_y/x/z of the angles at +0x128/+0x124/+0x12c), transforms 4 body points (local table at 0x06007924, values in 2^20
  scale: (-0.9,-0.4,+1.4), (+0.9,-0.4,+1.4), (-0.9,-0.4,-1.35), (+0.9,-0.4,-1.35)), stores world points at `car+0x190`, and probes each with `get_coli_area`; floor -> `get_y` height, wall -> `get_d` distance,
  keeps the max wall distance in `[car+0x100]` and a bitmask of wall hits in `[car+0xf6]`, then calls `FUN_060078a4`, which builds a reaction vector from the wall normal (`outer_product_p`,
  `regvect_inner`) scaled by `[car+0x7c]`/`[car+0x80]` and the constant 0x117c90 and accumulates it at `car+0x70..0x78` (INFERRED details).
* **Push application** `FUN_06007954` @0x06007954 (VERIFIED): `push = [car+0x64], [+0x68], [+0x6c]` (collision displacement accumulator, written by the car-car code below and by `otrCarColi`);
  `push.y` is clamped to +-0x199a (0.1); if `|push|^2 < 0xb73 (0.044)`: `velocity.x/.z ([+0x24],[+0x2c]) += push * 0x999a (0.6)`, the remaining 0.4 is added to the **position** (`[+0x18],[+0x1c],[+0x20]`), the accumulator is cleared.
  A larger push is ignored (the early `cmp/hs`).  `[car+0xc6]` bit 1 is set by the collision routines to flag "collided this frame".

### 2.3 Car vs car  (`carcoli`, VERIFIED)
All three routines only write a **displacement** into the first car's `+0x64/+0x68/+0x6c`; the physics step applies it as above.

| routine | caller / condition | test | response |
|---|---|---|---|
| `carColiGP` @0x0600c510 | tail call of `otherCtrl` (GP loop); r14 = player 0x060b0000 | for each AI slot (stride 0x400 from 0x060b0600, count word 0x060b02b0): skip if `abs(block_ai - block_player) >= 2` (byte +0xf0), else `hitCheck(r14 = player, r12 = AI)` | pushes **only the player** |
| `hitCheck` @0x0600c368 (+ body `FUN_0600c37e`) | | `d = posA - posB` (3D); if `d.d >= 9` (3.0 units) no contact.  `n = d/|d|`.  radius_B = `| S * Rb * n |`, radius_A = `| S * Ra * (-n) |` where `S = diag(0.9, 0.2, 1.9)` (0xe666, 0x3333, 0x1e666) and `R` = rotation of the car by its own angles (matrix built with r_rotate_z(-rz), r_rotate_x(-rx), r_rotate_y(-ry)); contact if `rA + rB > |d|` | `pen = (rA + rB - |d|)/2`; push vector `pen * n` (rotated back through the transposed matrix) added to A's +0x64..; A's `[+0xc6] |= 2` |
| `carColiVS` @0x0600c628 | `game_ctrl` mode 10; r14 = 0x060b0000, r12 = 0x060b0600 | `hitCheck2` @0x0600c198: same geometry as hitCheck (3.0 threshold, same scale matrix, tracks the max penetration in a local) | after `hitCheck2` the wrapper halves the push in car 1 (`sar` each component) and writes the negated halves to car 2 (+0x64..) -> symmetric |
| `carColiOtr` @0x0600c550 | `otherCtrl` | adjacent pairs in the rank-sorted car list at 0x060b07a0 (`[car+0x102] != 3`, neither is the player, `abs(blockdiff) <= 2`): 3D distance `<` 2.0 (`d.d < 4`) | `pen = (2.0 - |d|)/2` along `d/|d|` added to the **first car of the pair** only |

These tests use ellipsoid-like radii for the player (body ~1.8 wide, 3.8 long, 0.4 tall) but unit spheres between AI cars.

### 2.4 AI car vs world
`otherset::otrCarColi` @0x0600b9b8 (called from `setOtherCar`, LOD 0 only): transforms 4 corner points of the AI car (table at 0x0600bc38: (-0.9,-0.4,1.4), (0.9,-0.4,1.4), (-0.9,-0.4,-1.35), (0.9,-0.4,-1.35))
into world space (matrix from the car angles, `c_point`), and calls `get_coli2` for each corner; for corners with a wall polygon (type 0) and positive penetration it builds torque and push terms from
`outer_product` of the radius and the wall normal and accumulates them into `car+0x64..` / `car+0x74..` (INFERRED, structure only; I did not follow every instruction).  The lower LODs (`tinyOther`,
`simpleOther`) use a single `get_coli2` at the car centre only to snap to the ground (4.4); they have no wall response.

### 2.5 Scenery / objects
No code path in this area tests cars against scenery objects: trees, rocks, signs, banks are **wall polygons of the same map** (type 0, 1400-1830 per course), not separate entities.  The animated
event objects (`EvPilon`, `EvFlag`, `EvHelicopter`, `ExecGenerator`... driven by `EVENTTBL.DAT/.MAD`, modules `event generator public animal heli`) are display-only as far as I read (they use
a distance/visibility test, not the push accumulator) - not examined in depth, UNKNOWN beyond.

### 2.6 Feedback fields summary (names from usage; cross-reference with the physics reader's struct)
| off | meaning |
|---|---|
| +0x64 / +0x68 / +0x6c (s32 x3) | collision displacement accumulator, consumed by `FUN_06007954` / `setOtherCar` |
| +0x70 / +0x74 / +0x78 | angular/force accumulators written by wall reaction (player `FUN_060078a4`, AI `otrCarColi`), added to angles every frame (`>>4`) in `execCar` |
| +0xb4 (word) | AI: floor type replicated into the high/low nibbles |
| +0xc6 (byte) | bit 1 = collided this frame |
| +0xf6 (byte) | player: bit mask of wheels/corners with ground or wall contact |
| +0x100 (long) | player: max wall penetration distance of the corners (also cleared per frame) |
| +0x1c0.. (4 x 20 bytes) | player: per-wheel ground record {normal, height diff, type} |

---------------------------------------------------------------------------------------------------------------------------------------------

## 3. Camera  (real code at 0x0600fa5c..0x060101b8; VERIFIED unless marked)

### 3.1 Objects and entry points
* Camera work area: 1P `0x060aa9b0` (size 0xb4), 2P `0x060aaa64`; `gctrl::camera_ctrl` @0x06005e20 does `r14 = work; jmp [work+0]` with GBR = work (r4 = view-matrix slot 0x060d1000 (1P) or
  0x060d1800 (2P)); `camera1p_ctrl` / `camera2p_ctrl` also load r13 = player car 0x060b0000 / 0x060b0600.
* Work area fields (usage): +0 handler; +8,+0xc,+0x10 world eye position (s32); +0x14,+0x16,+0x18 rotation words X,Y,Z (16-bit turns); +0x1a,+0x1c,+0x1e extra angle words added to the rotation
  (read through GBR); +0x20,+0x24,+0x28 eye offset in camera space; +0x2c,+0x30,+0x34 pivot offset added to the car position; +0x38 counter (long in modes 4/6, word angle in mode 2).
* `init_game_work` installs the handler with `gctrl::clr_camera_ctrl_work`: **normal_cainit 0x0600fa5c** (or **vs_cainit 0x0600faa8** in battle mode, 1P handler 0x0600fbc8 / 2P 0x0600fbf8 per view).
  `normal_cainit`: if `[0x060aa810] == 2` (replay/ending) the handler is `ending::ending_camera_act` (0x0601f4ae), otherwise `normal_ca` (0x0600faf0); it also calls `FUN_0600ff10` (below).

### 3.2 Camera modes (byte `[0x060aa01a]` = 0, 2, 4, 6; 2P battle byte `0x060aa01e`)  
`normal_ca` @0x0600faf0 each frame: `FUN_0600ffe0` (lag filter), `unit_matrix(r4)`, optional `matrix::reverse_x` if `[0x060aa82e] != 0` (mirror option), `bsrf` by mode, then `copy_matrix(r4 -> 0x060d1800)`.

| mode | meaning (from the code) |
|---|---|
| 4 (initial value from `init_game_work`) | **start fly-by**: cubic B-spline path (`car::spline` @0x06007ddc, uniform cubic B-spline basis B0=(1-t)^3/6, B1=t^3/2-t^2+2/3, B2=-t^3/2+t^2/2+t/2+1/6, B3=t^3/6 over 4 control points per axis) over the table at `datgbal::start_spldata` (0x002994c0 / 2P 0x002994f0, 12 longs = 3 axes x 4 points) which is **filled at camera init** by `FUN_0600ff10`: the four points (0,16.35,-52.5), (0,7.16,-32.1), (0,-0.39,2.26), (0,-0.86,8.19) are transformed through the car's initial rotation and position.  t advances by 0x312 (0.012) per frame (84 frames, 2.8 s); at t == 0xe646 (0.9, frame 75) `etc::fade_color_init(3)` starts the fade, at t >= 1.0 the mode becomes 6 with `[+0x38] = 3`. Camera angles = minus the car's angles. |
| 6 | 3-frame timer; at 0 `fade_color_init(0)` and mode = 0 (race begins) |
| 0 | gameplay; sub-view byte `[0x060aa01b]` (0 or 2, toggled by `view_changer1p`/`2p` when `(pad word [car+0x114] >> 8) & 0x70 != 0`, ignored when `[0x060aa810]==2`; battle mode uses 0x060aa01f) selects **view A** or **view B** below |
| 2 | **goal orbit**: set by `goal_plmove`; yaw counter `[+0x38]` += 0xd0 per frame (about 1.1 deg), yaw = counter + 0x7fff; pitch = `(0.1*sin(2*counter) + 0.157)` rad-equivalent; offset translate (0, -0.75, 3.5) (table 0x0600ff04); eye height -0.7 more when car type byte `[+0x5c] != 0`; builds the matrix with `FUN_06010148` (scale (1.0625,1,1)) |

* **View A** (`FUN_0600fd68`): rigidly attached; rotation words = minus car angles (X,Y,Z) * 10430.378; pivot offset (0, +0.75, 0) (table at 0x0600fda0), eye offset 0.
* **View B** (`FUN_0600fdac`): chase view; yaw word = `[car+0xac]` * K where `[car+0xac]` is a low-pass of minus-heading updated by `FUN_0600ffe0` each frame
  (`v <- v - 0.05 * (v + car.rotY)`, wrapped to +-pi, constant 0x0ccccccd = 0.05); pitch word = `(0x11df - car.rotX/2) * K` (0x11df = 0.0698 rad = 4 deg down-tilt minus half of the car pitch);
  roll 0; eye offset in camera space (0, +1.3, -6.0) (table at 0x0600fde4: 0x00014ccd, 0xfffa0000); pivot 0.
* Matrix construction: `FUN_06010070` (r4 = matrix stack top): `push_matrix`; translation = car position + pivot; `rotate_y(-(extra_y + y))`, `rotate_x(-(extra_x + x))`, `rotate_z(-(extra_z + z))`;
  `translate2(eye offset)`; the resulting translation is stored as the eye position `[work+8..0x10]`; pop.  `FUN_060100d8` (called as the tail) then builds the **view matrix**:
  `scale(1.0625, 1, 1)` (0x00011000: horizontal stretch, INFERRED as the pixel-aspect fix), translation = -eye offset, `rotate_z/x/y(+angles)`, `translate2(-(car pos + pivot))`.
* Perspective: `mkpoly::perspective(0xe0)` (224, i.e. the screen height; INFERRED as the projection distance) is applied by `ending::set_game_pers` @0x0601f6d0 when the normal camera is entered.

### 3.3 Visible-block list for the camera
`calc_camera_pmap` @0x0600fd34: copies the camera eye x/z (`[work+8]`, `[work+0x10]`) into the **camera dummy car 0x060b2600** (`+0x18`, `+0x20`), sets GBR to it and runs `get_block`, `calc_dispang2`,
`get_poly_map` -> the render list for the replay camera.  In normal play the render list is built from the player's own block (`normal_plmt`).

### 3.4 Replay / ending camera (`replay_cactl` 0x0600fc94, `replay_ca` 0x0600fc5c, `ending::ending_camera_act` 0x0601f4ae, `CAMERA.DAT`)
* `replay_ca` = view B + `calc_camera_pmap` (chase replay camera).  `replay_cactl` runs only when `[0x060aa810] == 2`: a pad press (word 0x060aa00a & 0x700) toggles `[0x060afe7c]` between the chase
  camera (`normal_ca`) and the cut-scene cameras (`ending_camera_act`), and toggles the view byte.
* **CAMERA.DAT** (2400 bytes, plain file, loaded by `ending::set_replay_camera_data` @0x0601f3ac to `camera_dat_buf` 0x002a2000, or copied from `camera_dat`): header = 8 longs (0x002a2020...) = for each of the
  4 courses a pair {256-byte table "slice -> camera number", camera directory + records}.  Directory entry (4 bytes): `u16 type, u16 offset` (offset from the record region start); **type 0 = chase camera
  (replay_ca)**, **type 1 = fixed eye position (3 x s32) that looks at the car** (`math::get_lookat_angle`), type 2 = fixed position + 3 angle words.  `ending_camera_act` takes the camera number from
  `table[slice of the player (0x060b0060)]` (the course byte is `0x060d0089`) and dispatches on the type (table 0x0601f4dc).  Parsed data (VERIFIED against the file): desert 16 type-1 cameras (+ the type-0 entry), forest 19, mount 19, lake 22; e.g. desert camera 1 at (-360.7, 3.1, -253.8) is used for the
  first 12 slices, camera 3 for the next 27.  In replay mode the pad also changes the perspective value (word 0x060d008a, 0xe0..0x300, default 0x200).
* The replay itself (how the player's recorded inputs/positions are stored and played back) is **not covered here** (UNKNOWN); `plctrl::replay_plmt/replay_rmove` exist and `dispGhost`/`rec_*` modules show that a ghost exists.

---------------------------------------------------------------------------------------------------------------------------------------------

## 4. Other cars: the rally opponents and the PR (time-attack/ghost) car

### 4.1 Overview
Opponents are not simulated with the player's physics.  Each follows a **guide polyline** (300 points per course, in `PRGDATA.DAT`) at a target speed, with 4 levels of detail chosen by block
distance to the player; only the nearest LOD runs a (simplified) suspension and wall model.

### 4.2 The guide data  (VERIFIED)
Tables `datgbal::guide_africa / guide_yosemite / guide_corse / guide_europe` = 0x00299ed8 / 0x0029b1c8 / 0x0029c4b8 / 0x0029d7a8, each 0x12f0 bytes, in `PRGDATA.DAT` block 3 (0x00298000).  Pointer table at
**0x0600cdb8** (4 longs, indexed by the course word `0x060aa882` = course*4; course order from the file-name tables is desert, forest, mount, lake, so africa=desert, yosemite=forest, corse=mount, europe=lake - INFERRED).
Record = 16 bytes `(x, y, z, w)` all 16.16: 303 records per course, records 300..302 repeat records 0..2 (VERIFIED: the code wraps the index at 300, 0x12c).  Path length (xz): desert 3156 units, forest 4195,
mount 3492, lake 3137; mean point spacing 10.5 / 14.0 / 11.6 / 10.5 units.  y is the road height.
`w` top byte: bits 0..2 select the lane half-width `table[0x0600a354] = {4.0, 3.0, 3.0, 3.0, 4.0, 4.0, 4.0, 4.0}` -> `[car+0x15c]` (execOtherCar); the upper nibble (0..7, e.g. 0x10 / 0x20 / ... / 0x50 / 0x71) is not read
by the code I followed (UNKNOWN).

### 4.3 Spawning
`plctrl::normal_otinit` @0x06006214 (called from the race start-up routine 0x06010fe0 in modes 0 and 2 and from `ending_init` 0x0601f048): `[0x060aa860] == 0` (arcade) -> `initOtherGP` @0x0600c664, otherwise (practice, mode 2) `initOtherPR` @0x0600c824.  Globals used: level byte `[0x060aa808]` (0..2, difficulty), flag
`[0x060aa80c] == 3` (called `flag` below), player start rank byte `[0x060b00b6]` (1-based; forced to 1 on course word 0xc = lake), ghost flag byte `0x060aa82d`.

**initOtherGP** (arcade/GP, VERIFIED): `n_ahead = rank-1`.  `step = distStep[row][course]` with `row = level + 3*flag` (table at **0x0600d064**, 6 rows x 4 courses, 16.16 guide-index units):

| row | desert | forest | mount | lake |
|---|---|---|---|---|
| 0 (level 0) | 11.75 | 8.5 | 10.0 | 14.0 |
| 1 | 11.75 | 8.75 | 10.5 | 11.0 |
| 2 | 11.75 | 8.75 | 10.5 | 10.0 |
| 3 (flag) | 22.0 | 16.0 | 13.0 | 28.0 |
| 4 | 23.0 | 17.0 | 14.0 | 25.0 |
| 5 | 25.375 | 20.0 | 18.0 | 22.0 |

Cars ahead of the player get consecutive entries of a list `(speed km/h, guide-index offset)` read from `octgptbl` (0x0600d0c4, 6 set pointers indexed `level*2 + flag`, each set = 4 courses x {ahead list, behind list}
terminated by a negative word); guide index of car k = `(sum of k steps) + offset`.  Lists (level 0, flag 0): desert ahead [(160,+3),(176,-6),(175,-12),(175,-20),(176,-21),(178,-31)], behind [];
forest ahead [(162,3),(164,1),(166,-2),(168,-8),(175,-12),(176,-20)], behind [(160,0)]; mount ahead [(172,0),(175,-4),(175,-12)], behind [(160,0),(160,0)]; lake ahead [], behind [(160,2),(152,2),(144,0),(136,0)].
Higher levels raise the speeds slightly (level 2: desert 178-183, forest up to 181, mount 173-181, lake behind 185/176/170/162).  So at most **7** AI cars per race (GP); they fill slots 0x060b0600 + k*0x400.
Cars behind use negative steps (guide index wraps +300 and the car's lap `[+0xca]` is decremented).  Spawn of one car (`FUN_0600c78a`): car type byte `[+0x5c] = CarRankTable[slot]`
(table at 0x0600cdc8: ff 00 08 02 03 09 05 06 07 02 03 04 09 05 08 09 09 09 00 09; a 0 becomes 1 if the player's type is 0); guide index `[+0x10a]`; position = guide point (x, y+0.3, z);
states `[+0x102] = [+0x104] = 3` (far LOD); the 8 longs at `+0x170..0x18c` = speed(km/h) << 16 (**the same value 8 times**); move handler `[+0] = 0x0600c958` (first-frame init, below); `[+0x190] = 0x0600d1b0` (acc table).
After spawning, `FUN_0600c93a` sets word `0x060b02b6` (player progress) = `block + lap*128`.

**initOtherPR** (practice mode 2 and the ending sequence, VERIFIED structure): zeroes 0x060b07e0; ghost flag r12 = `[0x060aa82d]` when `[0x060aa810] == 2`, else 1 only if `(byte@0x06040062 & 4)` and the pad word `[0x060aa990+0] == 0x0210`;
spawns **one** car per record of `octpr_inittbl` (0x0600cddc, 4 course pointers; each course has exactly one record `{speed-list ptr, x, y, z}`): desert (-375.0, 0.3, -310.0), forest (-275.21, 0.3, -216.0),
mount (-90.0, 15.3, -534.0), lake (197.54, 31.0, -83.24).  Speed words = `list[level]` (+0x18 bytes when the ghost flag is set): desert {242,252,260} / ghost {247,257,265}; forest {252,265,275}/{262,275,285};
mount {248,262,272}/{253,267,277}; lake {200,215,230}/{205,220,235}.  Car type byte = 10 if the ghost flag is set, else 0 (1 if the player type is 0).  Lap `[+0xca] = -1`, `[+0x10a] = 0`, handler `[+0] = 0x0600c958`,
`[+0x190] = 0x0600d1c0 + course*4` (per-course acc_pr row), slot at 0x060b0600.  So in practice mode the player faces one fast rival on the guide line; with the ghost flag it is the type-10 car (INFERRED to be the "ghost" drawn by `dispGhost`, a display routine named in the DBG).  The bit-4 test of byte 0x06040062 and the pad word 0x0210 look like a secret/unlock condition (UNKNOWN).

**First-frame handler 0x0600c958** (VERIFIED): `[car+0x160] = guide pointer`, direction `unit(guide[i+1]-guide[i])` -> `[car+0x11c..0x124]`, `get_coli2` + `getAngle` -> initial rotation, clears the force/velocity fields,
constants `[+0x3c..0x48] = 0.2`, `[+0x7c] = 1000.0`, `[+0x80,0x84,0x88] = {3420, 3420, 900}` (or {18240, 6840, 4800} when `[0x060aa87e] == 2`), `[+0x90] = *(0x060a1490)` (datgbal::cardyntable ptr = suspension/spring parameter table),
`get_block2`, `setMark`, then installs `execOtherCar` (0x06009e8c) as the handler (keeping bit 0) and runs it.

### 4.4 Per-frame AI: `othercar::execOtherCar` @0x06009e8c  (VERIFIED structure)
1. Accel/brake retention coefficients: `r1 = [car+0x190]`; if speed `[+0x128] >= 0x7684` (0.46 units/frame = 50 km/h) `r1 += 8`; `[+0x14c] = r1[0]`, `[+0x150] = r1[1]`.  Tables: acc_gp (0x0600d1b0) = {0xf000, 0xfc00 | 0xe800, 0xfc00}
   (0.9375, 0.984 | 0.906, 0.984); acc_pr rows (0x0600d1c0) {0xfd80, 0xfe80 | 0xf800, 0xfc00} (0.99, 0.994 | 0.969, 0.984), same for all 4 courses.
2. Lane half-width `[+0x15c]` from `guide[idx].w & 7` (above).
3. **LOD transition**: if requested `[+0x104]` != current `[+0x102]` and `[+0x3c] + [+0x40] > 0` (grounded), move one step toward it (a jump table at 0x06009ef0 fixes up fields: to level 0 clears angular fields, 3->2 re-snaps to the ground with `get_coli2` + `getAngle`, to 3 sets wheel states to 0x19999).
4. **Target speed**: `[+0x140] = 0` if the player's state word `[0x060b000a] == 0` (cars wait for the start), else `[car + 0x170 + 4*clamp(lap [+0xca], 0, 7)]` (8 identical entries).
5. By LOD (`[+0x102]`): 0 -> `setMark`, `FUN_0600a374` (slip angle/steer feedback), `OtherCarCtrl`, `setOtherCar` (full model); 1 -> `setMark`, ..., `OtherCarCtrl`, `simpleOther`; 2 -> `setMark`, local path frame +
   `tinyOther` (inline block at 0x0600a00e); 3 -> far car: moves along the guide polyline at the target speed (`speed * 0x25e`) without any collision (inline block at 0x0600a194: advances the guide index when
   the projection passes the next point, interpolates between guide points).
6. After the move: `get_block2` for the car; a block wrap (new block < old and old > 125) increments the lap word `[+0xca]`; `[+0xec] = [+0x128]` (speed copy).

**LOD selection** (`otherCtrl`, 4.5): by the position of the car's block id in the ten-entry list from `get_other_map` (offsets 0,+1,-1,+2,-2,+3,+4,+5,+6,+7 around the player): indexes 0..4 -> LOD 0,
5..7 -> LOD 1, 8..9 -> LOD 2 (table 0x0600cc30: 0,0,0,0,0,1,1,1,2,2,2,...), not in the list -> LOD 3.  The draw handler `[car+4]` = `normal_otmt` (draw via `FUN_060101e8`) if the car's block is in the *poly-map*
render list (0x060aa140) else `normal_otmtn` (nothing).

**Path marker `setMark`** @0x0600a3f0: projection parameter `t = ((car-P[i]) . (P[i+1]-P[i])) / |P[i+1]-P[i]|^2` (xz, DVSR divide), advance `i` (word `[+0x10a]`, wrap at 300) while `t > 1`;
evaluates a uniform **quadratic B-spline** through guide points i, i+1, i+2 with weights `(1-u)^2/2, 1/2+u-u^2, u^2/2` for `u = t + 0.5` (and the derivative weights `u-1, 1-2u, u`),
builds a path-aligned frame (`unit_matrix`, `c_turnv`, `translate2`, `c_turnd`, `c_point_rm_xz`), stores the interpolated height in `[+0x110]` and an (x, 0, z) pair at `+0x164/+0x168/+0x16c`.

**Driver `OtherCarCtrl`** @0x0600ab60 (INFERRED semantics, constants VERIFIED): works in the path frame; curvature measure = sum over two consecutive guide segments (32-byte steps) of
`c_outp2(unit tangent_k, unit tangent_k+1)` (`>>1`, abs) -> `curv`; look-ahead distance `Ld = min(200.0, c_getr(car, guide[i+20]))`; neighbour-avoidance: if the squared xz distance to the car ahead (`[+0x154]`)
is < 30 it computes a lateral target from the sign of the cross product, magnitude `[+0x15c]` (3 or 4 units) and gain 0xff7c/0x2c8b; otherwise the lateral target is the signed lane half-width (`[+0x118]`).
Speed state `[+0x100]`: 0 -> 1 when `curv > 0.1` (0x1999) or `Ld < 190` and `[+0x140] > 0` (then `[+0x144] = [+0x140]`); in state 1 the corner speed is
**`v = min([+0x144], max(80.0, 150.0 + Ld/4 - 300*curv))` km/h** (150.0 = 0x960000, 80.0 = `crvspdmin` @0x0600d200 = 0x00500000), back to 0 when `curv < 0.1`.  Output `[+0x148]` = final target speed (km/h, 16.16); `[+0x118]` = lateral target.

**`setVelocity`** @0x0600aeb4: if `[+0x3c] + [+0x40] > 0` (grounded): target `v = [+0x148]*0x25e` units/frame; `speed' = v - (v - |velocity|) * k` with `k = [+0x14c]` if accelerating, `[+0x150]` if braking
(exponential approach, k in 0.9..0.994, so very sluggish); the heading vector `[+0x11c..]` is blended toward the lateral target by `(1 - dot)`; new velocity x/z -> `[+0x24],[+0x2c]`, speed -> `[+0x128]`.

**`getAngle`** @0x0600adcc (r4 = direction vector*, r5 = up/normal vector*, r6 = out*): computes (pitch, yaw, roll) as radians 16.16 in `out[0], out[1], out[2]` via `outer_product`, `r_atan`; the result goes to `[car+0xc,0x10,0x14]` (INFERRED from callers).

**`tinyOther`** @0x0600a5f4 (LOD 2): position += velocity; one `get_coli2` at the car centre; if on a floor polygon the car's y is placed at `ground + normal*76 (raw 0x4c)`; stores the surface type word
(`[+0xb4]`); rotation angles eased toward `getAngle(...)` by 0.95/0.9/0.9 per frame (0xf333, 0xe666); wheel-state words `[+0x4c..0x58]` from `sin(rot_x)`; applies a speed damping factor derived from the slip `[+0x30]`.
**`simpleOther`** @0x0600a7f4 (LOD 1): `tinyOther` plus the 4-wheel ground probes at reduced cost (uses `get_coli2`, `getAngle`, `setVelocity`; INFERRED, not traced line by line).
**`setOtherCar`** @0x0600b028 (LOD 0): applies the push accumulator to velocity/position, `setVelocity`, `otrCarColi` (wall corners), builds the car matrix, 4 x `tinyForce` @0x0600b874 (per-wheel spring/damper:
compression = `out+0x0c >> 4`, spring/damper constants from `[car+0x90]` (cardyntable) front/rear pairs, scale words 0x060aa100 and 0x060aa16c, clamp at 0.2 (0x33333), force along the polygon normal), derives pitch/roll with `asinf`,
integrates; returns when done.  Wheel offsets used: front (+-0.635, -0.4, +1.25), rear (+-0.66, -0.4, -1.25).

### 4.5 `otherctrl::otherCtrl` @0x0600cad0 (per frame, after all car handlers; VERIFIED)
Dispatched by `[0x060aa860]` through a 2-entry table (mode 0 -> 0x0600cb1c = GP version; mode 2 (practice) -> 0x0600cc40 = single-rival version that tail-calls `hitCheck2` with the player and the rival, pushing only the player; it is only ever called in modes 0/2/ending): increments frame counter 0x060b07e0; (a) update the player's progress word `0x060b02b6` from
the block delta; (b) for each AI car compute `progress = block + lap*128`, count how many are ahead of the player (ties resolved by `FUN_0600ccf8`: dot of the position difference with the road direction) -> the player's **rank** byte
`[0x060b00b6]` (a sanity trap `trapa 9` fires if it changes by more than 2 in one frame); (c) after 2 frames set LOD requests `[car+0x104]` and draw handlers (above); (d) `0x0600cd20`: build the car pointer list at 0x060b07a0
(player first, then AI), bubble-sort **descending by block id byte (+0xf0)**, then for every AI car `[+0x154]` = car in front of it (previous in the list), `[+0x158]` = car behind (next); (e) `carColiOtr`; (f) tail-call `carColiGP`.
(The GP version ends with `carColiOtr` + `carColiGP`; the practice version with `hitCheck2`.)

### 4.6 Difficulty / rubber banding
* Difficulty (`[0x060aa808]`, 0..2, plus flag `[0x060aa80c]==3`) selects: spawn spacing (distStep), per-car target speeds (GP lists: 136-185 km/h, PR rival: 200-285 km/h), nothing else that I found.
* **No rubber banding of the AI speed** was found in 1P modes: target speeds are constant per car (`+0x170..0x18c` hold the same value), the corner formula depends only on the track, and the only dynamic effects are
  "wait until the player starts", overtaking/avoidance, and the speed floor 80 km/h.  (The player-side power code `power::*` was not examined, so a catch-up on the player cannot be excluded.)
* **2P battle handicap** exists: `player::vs_g_ctrl` @0x060079e8 (called from `game_ctrl` mode 10 when bit 0 of `0x060aa826` is set): with `gap = progress_other - progress_leader` (`lap<<8 + slice`), clamped to <= 38,
  `x = (gap + 37) << 8`: car 2's parameter `+0x378 = 3.0`, leader's `+0x378 = 3.0 - x`; car 2's `+0x2ac = [0x060aa100]`, leader's `+0x2ac = [0x060aa100] + (x << 10)` (INFERRED to be power/torque scale words; leader is the car whose rank byte is 1).

### 4.7 GP vs PR summary
| | GP (`initOtherGP`) | PR (`initOtherPR`) |
|---|---|---|
| mode word 0x060aa860 | 0 (arcade) | 2 (practice; also the ending sequence).  Modes 4/6/8/10 spawn no AI cars at all |
| cars | up to 7 around the player's start rank, speeds 136-185, grid spacing distStep | 1 car at a fixed start point, speed 200-285 by level, type byte 10 if "ghost" flag |
| LOD / path / driver | identical (execOtherCar etc.) | identical |

---------------------------------------------------------------------------------------------------------------------------------------------

## 5. `copro` vector/angle library  (VERIFIED, real addresses = DBG addresses)
Matrix = 3 rows of {m0, m1, m2, t} (0x30 bytes, 16.16), applying `v' = M*v + t`.  r4/r5/r6 are pointers unless stated; "regs" = values in r1,r2,r3.

| function | address | signature -> result |
|---|---|---|
| `c_outp2` | 0x0600bc68 | (r4 = a*, r5 = b*) -> r0 = a.z*b.x - a.x*b.z (xz cross product, 16.16) |
| `c_getr` | 0x0600bc7c | (r4 = a*, r5 = b*) -> r0 = distance |a-b| (16.16; large values use an integer sqrt and `<<8 + 0x7f`) |
| `c_vecval` | 0x0600bcbc | (r4 = v*) -> r0 = |v| (same large-value trick) |
| `c_turnd` / `c_turnv` | 0x0600bcf0 / 0x0600bd0c | (r4 = matrix*, r5 = direction*): computes `z/Lxz, x/Lxz, y/L, Lxz/L` (division unit) and rotates the matrix columns by yaw then pitch (`turnd`) or the inverse pitch-then-yaw (`turnv`) = "turn the matrix to look along `dir`" and its inverse (INFERRED) |
| `regularAngle` | 0x0600bf58 | (r4 = angle) -> r4 wrapped into [-pi, pi] (single +-2pi correction) |
| `asinf` | 0x0600bf78 | (r4 = x in 16.16) -> r0 = asin(x) = `r_atan(1.0, x/sqrt(1-x^2))`; |x| > 1 -> 0, |x| = 1 -> +-pi/2 |
| `c_point` (= `c_point_mm`) | 0x0600bff0 | (r4 = M*, r5 = src*, r6 = dst*) dst = M*src + t, result also in r1,r2,r3 |
| `c_point_rm` | 0x0600bfdc | src in r1,r2,r3, dst in memory (r6) |
| `c_point_rr` | 0x0600c038 | src in regs, result in regs |
| `c_point_mr` | 0x0600c04c | src in memory (r5), result in regs |
| `c_point_rr_xz`, `c_point_mr_xz` | 0x0600c08c, 0x0600c0a0 | only the x and z rows are evaluated (result r1 = x', r3 = z') |
| `c_point_rm_xz`, `c_point_mm_xz` | 0x0600c0ce, 0x0600c0e4 | as above, result stored to `dst[0]` and `dst[2]` (r6) |
| `c_reg` | 0x0600c118 | (r4 = v*) normalise: r0,r1,r2 = x/L, y/L, z/L; zero vector -> (0, 0, 1.0) |

Helpers in module `car`: `vecval` 0x06007bac (|v|), `vecval_2` 0x06007bc0 (|(a,b,c)| with registers), `regvect` 0x06007bd8 / `regvect_inner` 0x06007b6c (normalise / dot), `outer_product_p` 0x06007b0c
(cross product, result `>>24` after a 64-bit multiply, for 8.24 inputs), `calc_xyzpoint` 0x06007c3c (3 dot products, no translation), `check_dist_ab` 0x06007da0 (distance^2 between sums of two points against r10), `calc_rad`, `spline`.
`math::` (not mine, used here): `sqrt` 0x0601b8cc (16.16 Newton iteration; negative input returns -1.0), `r_atan` 0x0601b794 (r4 = adjacent, r5 = opposite -> radians), `atan` 0x0601b7c0 (16-bit turns), `sin/cos/r_sin`, `inner_product`, `outer_product`,
`get_lookat_angle` 0x0601ba6c, `distance_3d`.

---------------------------------------------------------------------------------------------------------------------------------------------

## 6. Car struct offsets seen in this area (names from usage only)
Player 1 (0x060b0000), AI slots (0x060b0600 + n*0x400), camera dummy (0x060b2600).  A field used by both marked P/AI.

| off | type | meaning |
|---|---|---|
| +0x00 / +0x04 | ptr | move handler / second handler (draw or map update); bit 0 = skip/slave flag |
| +0x08, +0x0a | word | mode copy; state (0 start, nonzero started, 2 finished) |
| +0x0c +0x10 +0x14 | s32 | rotation X (pitch), Y (yaw), Z (roll), radians 16.16 |
| +0x18 +0x1c +0x20 | s32 | position x, y, z |
| +0x24 +0x28 +0x2c | s32 | velocity per frame x, y, z (AI uses x, z) |
| +0x3c..+0x48 | s32 x4 | wheel load / contact state (AI initial 0.2; "grounded" if sum > 0) |
| +0x5c | byte | car type id |
| +0x60 / +0x62 | word | slice id (0..255) / previous slice |
| +0x64 +0x68 +0x6c | s32 | collision displacement accumulator |
| +0x70 +0x74 +0x78 | s32 | angular/force accumulator |
| +0xac | s32 | P: camera lag yaw (minus heading, low-passed) |
| +0xb6 / +0xb7 | byte | rank (1-based) / previous block id |
| +0xb8 / +0xbc | s32 | road direction x / z (unit, 16.16) |
| +0xc0 | s32 | angle between road direction and car forward |
| +0xc4 / +0xc5 / +0xc6 / +0xc7 | byte | slice attribute / heading quadrant / flags (bit1 = collision) / view sector 0..15 |
| +0xc8 / +0xca | word | highest lap / current lap (signed) |
| +0xf0 / +0xf1 / +0xf2 / +0xf5 / +0xf6 / +0xf7 | byte | block id / map counts (lo = A, hi = B) / event code / state (8 = finished) / contact mask / section |
| +0x100 | s32 or word | P: max wall penetration; AI: speed-state (0/1) |
| +0x102 / +0x104 | word | AI: LOD current / requested; +0x108 flag; +0x10a guide index |
| +0x110 | s32 | AI: interpolated guide height |
| +0x114 | long | P: pad word (edge bits); +0x118..0x124 AI: lateral target / heading vector; +0x128 AI: speed (units/frame) |
| +0x130 | 3x4 matrix | P: car rotation matrix |
| +0x140 +0x144 +0x148 | s32 | AI target speed (base, corner-limited, final) km/h |
| +0x14c / +0x150 | s32 | AI accel / brake retention coefficients |
| +0x154 / +0x158 / +0x15c | ptr/ptr/s32 | AI: car ahead / car behind / lane half-width |
| +0x160 | ptr | AI: guide table |
| +0x164 +0x168 +0x16c | s32 | AI: setMark output pair (x, 0, z) (P: 0x16c is also used as car forward vector in `calc_reverse`) |
| +0x170..+0x18c | s32 x8 | AI: target speed by lap (identical) |
| +0x190 | ptr / 4x12 | AI: pointer to the acc table; P: 4 wheel/corner world positions (same offset, union) |
| +0x1c0 | 4 x 20 | P: per-wheel ground record |

---------------------------------------------------------------------------------------------------------------------------------------------

## 7. What a PSP reimplementation needs

**Data (all plain, loadable directly):** per course `BG<course>.DAT` blocks at 0x240000 / 0x244000 / 0x248000 / 0x290000 / 0x293000 / 0x296000 (pointers inside are absolute Low WRAM addresses - rebase them with
`addr - block_base`); `<COURSE>.MAD` block 0 (model pointer table + objects, 0x06048000, absolute pointers); `PRGDATA.DAT` guide tables (4 x 4848 bytes) and the car-start / spline / goal tables (`pinit_tbl` 0x002996d0, `start_spldata` 0x002994c0,
`goal_posi_tbl` 0x00299520, `rally_level` 0x00299370 / `prac_level` 0x00299418 - the latter two are the arcade level tables read by other code); the otherdat tables inside `0RALLY.DAT` (0x0600cdb8..0x0600d204, section 4.3); `CAMERA.DAT` for the replay.

**Functions to port (real addresses):** map queries `get_block` 0x06007eac / `get_block2` 0x06008360 (+ `FUN_06007f3c`), `get_coli_area` 0x06008008 + `FUN_060080aa`, `get_y` 0x0600802c, `get_d` 0x06008060, `get_coli2` 0x060083b4,
`get_coli3` 0x060083ec, `get_poly_map` 0x060081b8 (+ 1p/2p, `FUN_060082fc`), `get_other_map` 0x0600829c, `calc_dispang2` 0x06007d68, `calc_reverse` 0x06007cf8, `vs_rank_calc` 0x060084d0, `spline` 0x06007ddc; collisions `carColiGP/hitCheck`,
`carColiVS/hitCheck2`, `carColiOtr`, `FUN_06007954`; AI `initOtherGP/PR`, `FUN_0600c78a`, first-frame 0x0600c958, `otherCtrl` (+ 0x0600cd20 sort, 0x0600cccc progress), `execOtherCar`, `setMark`, `OtherCarCtrl`, `setVelocity`, `getAngle`, `tinyOther`,
`simpleOther`, `setOtherCar`, `tinyForce`, `otrCarColi`; camera `normal_cainit/ca`, `vs_cainit`, `replay_ca`, `replay_cactl`, `FUN_0600fd68/fdac/fe84/ff10/ff88/ffe0/06010070/060100d8/06010148`, `ending_camera_act` + `CAMERA.DAT`;
`copro` (all 20, ~1.3 KB).  The SH-2 hardware divide and the 64-bit MAC are the only exotic dependencies (int64 is enough).  A port can build the collision lists with its own grids but must keep the **first-match-in-list** semantics
(walls first) because the floor/wall decision depends on list order.

**Dependencies on code outside my area:** matrix stack (`unit_matrix`, `r_rotate_*`, `rotate_*`, `t_rotate_*`, `translate`, `translate2`, `scale`, `push_matrix`, `copy_matrix`, `reverse_x`), `math::sqrt/atan/r_atan/sin`, `etc::fade_color_init`, `mkpoly::perspective`, `etc::rfillff`;
game flags: `0x060aa810` (game state/mode), `0x060aa860` (mode word), `0x060aa808` (level), `0x060aa80c`, `0x060aa826`, `0x060aa82e` (mirror), `0x060aa87e`, `0x060aa882` (course*4), `0x060aa006` (lap count).

### Open questions / UNKNOWN
1. Meaning of the surface type values (3,4,5,7 / 1,2,8,9,10) and how the wheel code maps them (friction, sound, dust) - not read.
2. Slice record bytes 0 and 1 (attribute 3/7/8/12/13/20/129...; section 1..6) and the guide record `w` high nibble.
3. Game mode word values 6 and 8 (6 is stored by the time-attack menu, 8 never found), the exact meaning of mode 2 (practice is inferred from the `prac_*` symbols next to the setter) and what flag `[0x060aa80c] == 3` means (it changes the grid spacing a lot: 22-28 index units vs 8-14).
4. The full body of `setOtherCar`, `simpleOther` and `OtherCarCtrl` (I documented I/O, structure and the speed formula; the avoidance geometry and the slip/steer feedback in `FUN_0600a374` are only sketched).
5. Whether any catch-up exists on the player side in 1P (power/torque modules not read); VS handicap `vs_g_ctrl` fields `+0x378` / `+0x2ac` are only INFERRED.
6. Replay/ghost recording and playback of the player's own run (only the camera side is covered), the exact role of the PR car (ghost/rival) and the byte at 0x06040062.
7. The real addresses of every other module between 0x0600cdb8 and 0x06015f90 (DBG names are displaced: see 0.2); `game`, `display*`, `start`, `scroll`, `gamesd` must be re-derived before trusting their names.
8. Behaviour of the debug traps (`trapa 9` with tags 'TRIP', 'NUKP', 'NUKE', 'D2ER') in the shipped build - the vector is not examined; a port should treat them as asserts.
9. Event objects (EVENTTBL) vs cars: no interaction found, not proven absent.
