# Sega Rally Championship (Saturn, USA) - 3D polygon pipeline

Reverse-engineering notes for the PSP port. Source of truth: `0RALLY.DAT` (SH-2, loaded at 0x06004000) read as asm/decompiler output plus a private
SH-2 disassembler (the Ghidra output misses some code after `braf` jump tables - e.g. the whole slave job at 0x0601882E, the near-plane clip cases at
0x06019118..0x060194FC, the clip2d inner routines); the disc's model/texture files were parsed with Python to confirm every record layout.
Nothing here is written as code for the port.

Evidence tags: **[V]** verified (I read the code that does it / parsed the real files), **[I]** inferred (consistent with the code, not proven),
**[U]** unknown. Citations are `module::function @address` (DBG names; see section 9 for the labels that are unreliable).

Conventions: 16.16 = signed fixed point, 1.0 = 0x10000. "WK" = the per-CPU polygon-maker work area (section 2.4). Angles are 16-bit turns
(0x10000 = 360 degrees) unless marked "radians".

---------------------------------------------------------------------------------------------------------------------------------

## 0. The ten most important facts

1. Rendering is a software pipeline on the two SH-2s that builds a **VDP1 command table in High WRAM (0x060C0000..0x060D0000)**, sorts it with an
   **8192-bucket depth sort that rewrites CMDLINK fields**, and a V-blank handler DMAs the first 0xD000 bytes to VDP1 VRAM and lets VDP1 draw. [V]
2. Resolution **352x224** (VDP1 system clip 0..351 x 0..223; VDP2 TVMD word 0x0101 -> HRES=352, VRES=224). Screen coordinates are **centre-relative**: a
   local-coordinate command puts the origin at (176,112) (1P) / (176,56) and (176,168) (2P halves). [V]
3. Projection: `sx = x_view * S / z_view`, `sy = -y_view * S / z_view`, S = **224** (1P) or **128** (2P); near plane 1.25 (course) / 0.5 (cars, objects);
   far/key limit 255.0. One hardware divide per vertex (SH-2 DIVU). Same S for x and y. [V]
4. All models are a **128-entry part table + parts** (course) or a flat set of parts (objects). Part = `{u16 nverts, u16 npolys, ->verts(int16 x3), ->polys,
   ->texture table, ->per-vertex normals}`; polygon = `{u16 flags, u16 colour-or-texture-offset, 3|4 x u16 vertex byte offsets, optional int16 x3 normal}`.
   Verified on 1024 course parts (8 files, 0 mismatches) and the object file. [V]
5. Every textured polygon is one VDP1 **distorted-sprite** command (4bpp, 16-colour LUT, index 0 transparent), plain polygons are VDP1 polygon commands.
   Gouraud = a 32-entry grey table at VRAM 0x10000 (flat shading) or a per-polygon 4-word table placed in the command buffer. Course models use only
   textured and flat polygons; no gouraud/mesh/transparency. [V]
6. The slave SH-2 gets jobs through a **mailbox at 0x202A4000** (function pointer + 3 args; kick = word write to 0x21000000). It draws half of the course,
   sorts the far half of the depth range, runs half of the object updates, and in 2P draws player 2. [V]
7. **Retail path = `.MAD` files**: `DESERT.MAD` is a plain scatter-load image holding the model (-> 0x06048000), the already expanded 4bpp texture data
   (-> VDP1 VRAM 0x3B000) and the palettes (-> VRAM 0x15000). `.MDL/.TEX` are the developer-path inputs (`.TEX` is BPE-compressed). The
   `0x02xxxxxx` data symbols are developer-environment addresses that the CD path relocates with per-file constants. [V]
8. Frame pacing: the main loop's `sync(2)` is **not** a 2-frame wait (its V-IN handler zeroes the byte at the very next V-IN). The 30 fps comes from
   each pass needing two V-IN handshakes (verified for `ending_main`: state 36 armed at the top, `intm_sync(38)` at the end) plus the render cost. [V/I]
9. Trig/atan are small tables in `PRGDATA.DAT` (4096 x {sin,cos} int16, 14-bit; 513 x atan int16); sqrt is Newton with the hardware divider. [V]
10. The DBG labels are unreliable in `cache/etcsub/dsp` (0x0601BAF4..0x0601C33C: the code is an object scheduler and the check-point-gate drawer, not cache/DMA/DSP code) and at
    `extract::bpe_expand` 0x06014C8C (not a BPE decoder). Everything in matrix, sprite, mkpoly, clip2d, light, shading, texture, zsort, math, int, vdp matches. [V]

---------------------------------------------------------------------------------------------------------------------------------

## 1. Frame structure

### 1.1 Who drives a frame

* The main loop (copied to RAM at 0x060A0000, `main::mainv_loop` @0x06004158) is: `[0x060AA801] = imm; call *(0x060A001C); sync(imm2)`.
  `sync` (`int::sync` @0x0600520C) stores its argument into **0x060AA800** and spins until a V-IN handler writes 0. [V]
* The immediates are patched at run time: the V-blank handlers write `[0x060A0001]` (the `mov #2,r0` immediate = V-OUT recipe) and `[0x060A0013]` (the
  `mov #2,r4` immediate = V-IN recipe) (`int::vint_in` @0x06004E5A.., `vint_out` @0x060051C2). [V]
* The race renderer is `gctrl::game_ctrl` @0x06005B78 (module `gctrl`). It is entered from the race state handler (not located: no pool reference; the
  only direct callers found are `display::init_game_over` @0x060112D0 and `ending::ending_main` @0x0601F0A0) and **ends with a tail call into
  `zsort::game_zsort` / `game_zsort_2p`**. It dispatches on the word `[0x060AA860]` (index = value/2, table at 0x06005B84): values 0,2 -> 1P path
  @0x06005B90; 4,6,8 -> "two cars, one view" path @0x06005C30; 10 -> split-screen VS path @0x06005CB8. [V]
  (`texture::set_game_texture` @0x0601AB6C also tests `[0x060AA860] == 10` to pick the 2P models.)

### 1.2 1P race frame, in order (`game_ctrl` @0x06005B90..0x06005C2E) [V unless noted]

| # | Step | Where | Runs on |
|---|------|-------|---------|
| 1 | slave frame init, master frame init | `s_mkpoly_frame_init` @0x0601864A, `m_mkpoly_frame_init` @0x06018634: cmd index := 0x200 (master) / 0xC00 (slave), gouraud-slot allocator := 0x1600 / 0x1800, clear per-frame polygon counters (WK+0x84, +0xA8/+0xAA) | both |
| 2 | player-car exec function `[0x060B0000+0]`, then `0x0600FC94` | physics etc. (not my area) | master |
| 3 | `camera_ctrl` @0x06005E20 = `jmp [0x060AA9B0]` | camera handler builds the camera matrix (e.g. `dispcar::FUN_0600FDAC` @0x0600FDAC: `unit_matrix`, `r_rotate_y/x/z`, `translate`, then `copy_matrix` into 0x060D1000) [I for the copy target] | master |
| 4 | player-car **display** function `[0x060B0000+4]` with r4 = 0x060D1000 (the camera matrix = stack base); then for the other cars at 0x060B0600 + n*0x400 (until 0x060B2200): exec `[+0]`, display `[+4]` | each display fn does `push_matrix`, `translate2`, `rotate_*`, then `draw_polygon`-family calls through the per-car slot `[car+0x380]` (GBR = car struct) | master |
| 5 | `plctrl::sync_ctrl` @0x060067D0, `otherctrl::otherCtrl` @0x0600CAD0 | game logic | master |
| 6 | object/event dispatcher `0x0601BCB8(r4=0x060AA140 list, r5=count word 0x060AA002)` + slave half `0x0601BD52` | animals, flags, people, heli, check-points...: each object's `[obj+0]` is called with GBR=obj, r4 = matrix stack base; they call `draw_people_polygon`, `draw_polygon`... | both (interleaved halves) |
| 7 | `0x0601D410` (only if `[0x060B00F5] < 8`) | slave job `0x0601D484` + master walk of the list at 0x060D0140 | both |
| 8 | **`mkpoly::draw_course` @0x060187C0**(r4=0x060D1000, r5=part pointer list 0x060AA104, r6=count word 0x060AA000) | see 1.3 | both |
| 9 | `rscroll_ctrl` @0x06005E54 (VDP2 scroll update via `[0x060AA9F0]`), `wait_slave_end` | | master |
| 10 | `0x0601D756`: sums master+slave polygon counters into 0x060D0034.. | | master |
| 11 | **`zsort::game_zsort` @0x0601B284** (tail call) | builds the system commands and the sorted CMDLINK chain | both |
| 12 | return to the mode handler / main loop; present: `int::sync` (recipe 2) or `intm_sync(38)` | next V-IN DMAs the table to VDP1 | master + V-INT |

Cars are drawn *before* the course (they get the lowest command indices); the order does not matter for correctness because the depth sort re-orders
everything. Objects and cars use the "object" matrix conversion, the course uses the "course" conversion (2.3).

### 1.3 `draw_course` and the master/slave split (`mkpoly::draw_course` @0x060187C0) [V]

```
draw_course(r4 = matrix, r5 = array of part pointers, r6 = count):
    wait_slave_end
    FUN_06018880: budget = 1500 polygons per frame (0x5DC)
        r10 = master_poly_count (WK@0x060D14AA) + slave_poly_count (0x060D1CAA)     ; objects/cars already drawn this frame
        accept parts in list order while r10 + part.npolys <= 1500   (part.npolys = u16 @ part+2)
        master gets the first half (by cumulative polygon count) of the accepted parts, slave gets the rest
    mailbox[4]=matrix, [8]=ptr to first slave part, [12]=slave count
    master: WK.fn94 := set_course_matrix, near := 0x14000 (1.25); set_slave_exec(0x0601882E)
            for each own part: draw_clip_polygon(r4 = matrix, r5 = part)
            restore fn94 := set_object_matrix, near := [0x06018440] (0.5);  wait_slave_end
slave job @0x0601882E: CCR := 0x11 (cache purge), same loop with the slave WK (0x060D1C00)
```
2P (`draw_course_2p` @0x06018750): no split - the caller runs it once per CPU, near = 0x16000 (1.375); the polygon budget is 832 (0x340) per viewport
(`FUN_06018794`), taking this CPU's WK counters into account. Parts beyond the budget are silently dropped, so the part list must be sorted
near-to-far by whoever builds it (that code - the writer of 0x060AA104/0x060AA000 - is **[U]** here).

### 1.4 Master / slave job table [V]

Protocol: `slave::set_slave_exec` @0x06017B90 spins until the mailbox word at 0x202A4000 is 0, stores the job pointer there and writes a halfword to
0x21000000 (SINIT: wakes the slave's FRT input capture). `slave_start` @0x06017AB0 loops: wait ICF (FTCSR), purge the mailbox cache line (write 0x402A4000),
`jsr` the job, clear the mailbox. `wait_slave_end` @0x06017BAC spins until the word is 0. Jobs begin with `CCR := 0x11` (write 0x11 to 0xFFFFFE92 =
cache enable + purge) because both CPUs share cached HWRAM; shared structures are accessed through the uncached alias (0x2xxxxxxx).
Mailbox words: +0 job, +4/+8/+12 arguments. CPU identity is read from BCR1 (`mov.w @(2,r1)` with r1=0xFFFFFFE0; sign = MASTER bit) [I].

| Job address | Enqueued by | Work |
|---|---|---|
| 0x0601864A `s_mkpoly_frame_init` | game_ctrl, advExec | slave frame init |
| 0x060185EA | `mkpoly_init_2p` @0x060185C0 | slave WK perspective scale := 0x80 |
| 0x0601882E | `draw_course` | draws the slave share of the course parts (1P) |
| 0x06005E08 | game_ctrl (VS 2P) | `draw_course_2p(0x060D1800, 0x060AA198, count 0x060AA020)` = player 2's view |
| 0x06018738 | `mkpoly::DRAW_POLYGON` @0x060186FC | `draw_polygon(r4=matrix copy @0x060D1BC0, r5=part)` ; if the mailbox is busy DRAW_POLYGON just calls `draw_polygon` itself |
| 0x0601DB62 | `FUN_0601DB20` (name_disp/name_moji) | same trick for `draw_color_polygon` |
| 0x0601B328 / 0x0601B458 | `game_zsort` / `game_zsort_2p` | depth-sorts the far half of the buckets |
| 0x06005F18 / 0x06005F70 | game_ctrl (modes 4..10) | `s_car_move2p_ctrl` / `s_car_disp2p_ctrl`: car structs whose function pointer has bit0 set (`fn-1` is called with r4 = slave stack 0x060D1800) |
| 0x0601BD52 / 0x0601BE9A | `FUN_0601BCB8` / `FUN_0601BE3C` | slave half of the object/event list (labelled `move_xyz_position` / `boxwrt` in the DBG, see 9) |
| 0x0601D484 | `FUN_0601D410` | slave half of the list at 0x060D0140 |

The labels in the task (`rotate_xyz_angle`, `boxfill2`, `FUN_06018880`, `FUN_0601B284`, `FUN_0601DB20`): `FUN_06018880` is only the master-side balancer,
not a slave job; `FUN_0601B284` is `game_zsort`; `FUN_0601DB20` is the `draw_color_polygon` dispatcher above; `rotate_xyz_angle`/`boxfill2` are DBG labels
inside the object-list walkers (9).

### 1.5 V-blank handshake and frame pacing [V unless noted]

* Two interrupts per frame, vectors 0x40 (`int::vint_in` @0x06004CE4) and 0x41 (`int::vint_out` @0x06005148). Both mask with IMS = 0xFFFFFF7C.
* **V-IN** reads the byte `[0x060AA800]`; if non-zero it `bsrf`s through a halfword table at 0x06004D18 indexed by the byte (even states 2..50) and the
  handler writes 0 (or a new state - a word store at 0x060AA800 sets `[800]` and `[801]` together).
  Decoded recipes (r8 = 0x060AA800; "DMA0/1/2" = SCU DMA levels 0/1/2, "EN" = write 0x101 to Dn_EN = start; DMA0 = HWRAM 0x060C0000 -> VDP1 VRAM 0x25C00000,
  0xD000 bytes, DMA1 = 0x060B8000 -> VDP2 CRAM, DMA2 = indirect table -> VDP2 registers; all three are programmed in `init::init_gspr` @0x0600460C) [V]:
  * **2**: zero the byte and return. So `sync(2)` = wait for the next V-IN, nothing else.
  * **VDP1 table transfer** (the "abort + DMA0" tail at 0x06004DBC): read EDSR (0x25D00010) CEF bit1; if VDP1 is still drawing write ENDR (0x25D0000C) = 0 to stop it and save COPR
    (0x25D00014) in `[0x060AA87C]`; then DMA0 EN. Recipes **4, 16, 20, 44** end in it; **38 and 50** run DMA2 EN (indirect list `0x060B9000`, VDP2 registers) first and then the same tail
    (38 additionally calls the sound/CD housekeeping 0x06015FEC and `cd_open_check` @0x06005934; 50 first burns a 0x23E0-iteration delay and calls 0x06016044). `intm_sync(38)` is the
    frame end of the game/title/ending/photo/ceremony passes, `intm_sync(4)` of the three car/course/game select screens, `intm_sync(50)` of `re_1x1time_2p`.
  * **8, 10, 26..34** are *conditional*: they test `[0x060AA806] bit0` ("frame ready", set by `adv_main`, `photo`, `display`, `scroll::set_rpara`, `ending_main`...) and only do the DMA0 transfer when it is
    set (clearing it), otherwise they re-arm themselves by storing a new word at 0x060AA800.
  * **36, 40, 46, 48**: VDP2-side updates (scroll positions `0x060159DC` -> 0x25F80070.., DMA2/DMA1, BGON `[0x060AA8E6]` -> 0x25F80020, window regs `0x06015B08` -> 0x25F800C0..) without a VDP1 table
    transfer; the menus' `intm_sync(46)` uses 46.
  * 12/14/18/22/24/30/32/42 mostly chain: a word store sets the next V-IN and V-OUT recipe (e.g. 12 -> word 0x0E02) and falls into one of the bodies above.
* **V-OUT** reads `[0x060AA801]`: recipes 2 = nothing, **4** = FBCR (0x25D00002) := 3, **6** = FBCR := 2, **8** = FBCR := 2 and re-arm V-IN 8, **10** = PTMR
  (0x25D00004) := 1 (start plotting). It also calls `switch::pad_read` @0x06005A18 when `[0x060AA803] bit0` is set and `0x060170C4` every frame.
* VDP1 init (`vdp::init_vdp1sys` @0x060054B0): TVMR=0, FBCR=3, PTMR=2, EWDR=0, EWLR=0, EWRR=0x58DF (erase rect (0,0)-(351,223)), first VRAM word = 0x8000 (draw-end).
* **Pacing**: `ending_main` @0x0601F0A0 sets the word `[0x060AA800] = 0x2404` (V-IN state 36, V-OUT state 4) at the top of every pass, renders, then
  `intm_sync(38)` (wait until the byte is 0 = the state-36 V-IN ran; set 38; wait for the next V-IN). That is **>= 2 V-INs per pass = 30 fps** for
  that mode [V]. For the race pass this pattern is **[I]** (the race handler was not located; the main loop's own `sync(2)` only adds one V-IN).
  If a pass takes longer than the V-INs it waits for, the rate drops further; there is no explicit frame counter ("0x060AA800" is a state byte).
* `ints_sync` @0x06005230 (sets `[0x060AA806] bit0`, waits for the handler to clear it) has no callers [V].

---------------------------------------------------------------------------------------------------------------------------------

## 2. Matrices, camera, projection

### 2.1 Matrix stack (`matrix` module, 0x06017D18..0x060181B8) [V]

* Matrix = **3 rows x 4 longs, row-major, 16.16** (0x30 bytes): `[m00 m01 m02 tx / m10 m11 m12 ty / m20 m21 m22 tz]`; the point transform is `p' = R*p + t`
  (`math::calc_point` @0x0601B97C: out_i = sum_k row_i[k]*v[k] + row_i[3]; `calc_point2` @0x0601B9C4 = rotation only).
* The "stack" is a bare pointer in r4: **push** = copy 0x30 bytes to r4+0x30 and continue with r4+0x30 (`push_matrix` @0x06017D4C/`FUN_06017D4E`, r5 = source);
  **pop** = r4 - 0x30 (`pop_matrix` @0x06017D84; most callers just `add #-48,r4`). Storage: master 0x060D1000..0x060D13F0 (21 levels), slave 0x060D1800.. (the
  CPU is chosen by `initial_matrix` @0x06017D18 / `get_matrixbuf` @0x06018170). `store/restore_matrix_pointer` keep r4 at WK-area+0x2C (0x060D142C / 0x060D1C2C).
  `purge_matrix` @0x06018160 writes the three cache lines (address | 0x40000000) so the other CPU sees fresh data.
* Operations are all **post-multiplications by a local transform** `M' = M * T` (columns of M are modified): `translate(r4=M, r5=ptr xyz)` adds `M3x3*v` to the translation
  (`translate2` takes r5,r6,r7 = x,y,z); `translate_x/y/z`; `scale(r5,r6,r7)` scales the columns; `scale_x/y/z`(r0); `reverse_x/y/z` negates a column (mirror);
  `rotate_x/y/z(r4=M, r0=angle)`; `r_rotate_*` (r0 = radians 16.16, converted with x 0x28BE60DC/2^32 = 1/2pi); `t_rotate_*` (radians via the Taylor `t_sin/t_cos`).
  Rotations are the right-handed ones: Rx: y'=c*y-s*z, z'=s*y+c*z; Ry: x'=c*x+s*z, z'=-s*x+c*z; Rz: x'=c*x-s*y, y'=s*x+c*y.
* `mul_matrix(r4=A, r5=B, r6=dest)` computes **dest = B*A** (A is applied first, B's translation added) [V from the mac loops].
* `unit_matrix` @0x06017D24 writes the identity (1.0 = 0x10000 on the diagonal); `copy_matrix(r4=src, r5=dst)`.
* `calc_camera_matrix` @0x060180BA (r8 = camera struct: +0/+4/+8 position, +0xC/+0xE/+0x10 angles x/y/z as 16-bit): `unit; rotate_z(-az); rotate_x(-ax); rotate_y(-ay);
  translate(-pos)` into 0x060D1000. **No caller found** (no pool reference, no bsr) - the camera handlers build their own matrices with `r_rotate_*`/`translate`
  (e.g. @0x0600FE84..0x0600FED0) [V that it has no static caller; U whether it is dead].

### 2.2 Perspective, near/far, centre, clip rectangle (WK, section 2.4) [V]

* `mkpoly::perspective` @0x06018684 (r4 = scale) writes WK+0x8C of both CPUs and recomputes **WK+0x0C = (scale << 32) / near** with the DIVU (`FUN_060186AA` @0x060186AA).
  `set_clip3d_f` @0x060186A6 (r4 = near z, WK+0). Template values (`m_mkpoly_init` @0x06018418, 0xC0 bytes copied from 0x06018440 / 0x06018500): near 0x8000 (0.5),
  far/key limit WK+4 = 0x00FF0000 (255.0), lateral guard WK+8 = 0x00200000 (32.0), clip2d size limit WK+0x10/+0x14 = 512, **centre WK+0x18/+0x1A = (176,112)**, scale WK+0x8C = **0xE0 = 224**.
  `mkpoly_init_2p` @0x060185C0 changes WK+0x1A to 0x38 (56) and the scale to **0x80 = 128** on both CPUs.
* Vertex projection (`mkpoly::calc_vertices` @0x060194FC, `FUN_06019590`, `FUN_06019634`): with z = row2.v + tz, x = row0.v + tx, y = row1.v + ty (all raw 32-bit),
  `K = (scale << 32) / z` (hardware divide: DVSR=z, DVDNTH=scale, DVDNTL=0; result read at 0xFFFFFF1C), `sx = (x*K)>>32`, `sy = (-y*K)>>32` -> `sx = x*scale/z`.
  Packed as `(sx << 16) | (sy & 0xFFFF)` relative to the screen centre. FOV: 1P tan(half-width)=176/224 (76.6 deg horizontal, 53.1 deg vertical); 2P: viewport 352x112, scale 128.
* Near plane: the polygon-maker works on raw z (16.16 world units). Course near = 1.25 (`draw_course`: `set_clip3d_f(0x14000)`), 2P course 1.375 (0x16000), everything else 0.5.
* Units: object matrices are converted with `<<6` on the rotation part (`set_object_matrix` @0x06018AB8: vertex int16 x coefficient << 6 -> 1 vertex unit = 1/1024 world unit);
  course matrices with `<<11` (`set_course_matrix` @0x06018AE8 -> 1 vertex unit = 1/32 world unit); the translation is copied unshifted. Car body vertices are about +-1000
  (about +-1 unit), course vertices up to +-27536 (about +-860 units) -> **1 world unit is about 1 m** [I].

### 2.3 Per-object matrix conversion and the draw functions [V]

`draw_polygon_init` @0x06018A7C (called by every `draw_*polygon`): picks the CPU's WK (BCR1), stores r4 -> WK+0x2C (matrix), r5 -> WK+0x30 (part), adds the part's vertex/polygon
counts to WK+0xA8/+0xAA (stats), sets WK+0x28 = part.texturetable + 8 (first entry), then calls `[WK+0x94]` (`set_vertice_type` @0x06018670 selects `set_object_matrix` or
`set_course_matrix` from a 2-entry table) to copy the 3x4 matrix into WK+0x200 with the shifts above.

| Function | Vertex transform | Per-polygon path | Used for |
|---|---|---|---|
| `draw_polygon` @0x06018908 | `calc_vertices` + `calc_vertbright` | `FUN_06018B16` (quad) / `FUN_06018B66` (tri): no clipping; polygon dropped if any vertex is behind near | cars/objects (slot `[car+0x380]`) |
| `DRAW_POLYGON` @0x060186FC | queues `draw_polygon` to the slave | | same, load balancing |
| `draw_polygon2` @0x06018960 | `FUN_06019590` (adds the screen offset WK+0xA0/A2) | `FUN_060197F8/36` | menu 3D |
| `draw_offset_polygon` @0x060189B8 | `FUN_06019590` | `FUN_06018B16/66` | cars in the select/ceremony screens (`set_poly_offset` @0x060186E4 sets the (x,y) pixel offset) |
| `draw_clip_polygon` @0x06018A10 | `FUN_06019634` (also stores view-space x,y; flags 1/2/4) | near-plane clip + `clip2d` (see 6.5) | **course**, fade/gate polygons |
| `draw_color_polygon` @0x0601978C | as `draw_polygon` | `or 0x8000` into the colour word, then tri/quad | solid-colour polygons (colour in r6) |
| `draw_people_polygon` @0x06019870 | `FUN_060198CC`: **billboard** | `FUN_06018B16` for one quad | spectators |
| `draw_splash_polygon` @0x06019936 | `FUN_06019994`: billboard | one quad | splash/dust |

Billboards (people/splash): the object struct (r4) already holds the view-space position (+0x0C x, +0x1C y, +0x2C z); the part's 4 vertices are `{i16 dx, i16 dy, pad}`; people:
`x = dx*scale(r6)*64 + obj.x`, `y = (dy<<6) + obj.y`; splash: `x = (dx<<6) + obj.x`, `y = dy*scale + obj.y`; all four share z = obj.z.

### 2.4 WK - the polygon-maker work area (master 0x060D1400, slave 0x060D1C00; GBR/r14 point at it) [V unless noted]

| Off | Size | Meaning | Evidence |
|---|---|---|---|
| +0x00 | l | near plane z (16.16) | `set_clip3d_f`, 0x0601947C |
| +0x04 | l | far limit for the depth key (0x00FF0000) | `FUN_06018B16`, `set_polydata4/3` compare |
| +0x08 | l | lateral overflow guard (32.0) | `FUN_06019634` |
| +0x0C | l | (scale<<32)/near | `FUN_060186AA`, 0x0601947C |
| +0x10,+0x14 | l,l | clip2d max bbox size (512,512) | `clip2d_4` |
| +0x18,+0x1A | w,w | screen centre x,y = clip half-extents (176,112 / 56) | `clip2d`, `FUN_06019634` |
| +0x1C | l | transformed-vertex buffer base (0x060D8000 master, 0x060DC000 slave) | calc_vertices |
| +0x20 | l | depth-bucket storage (uncached) 0x20200000 / 0x20200010 | `entry_zbuf` |
| +0x24 | l | depth-bucket counters 0x060E0000 / 0x060E0001 | `entry_zbuf` |
| +0x28 | l | current texture-entry table base (part+0xC, +8) | `draw_polygon_init` |
| +0x2C,+0x30 | l,l | current matrix pointer, current part pointer | `draw_polygon_init` |
| +0x40..+0x48 | 3l | face / vertex normal (16.16, from int16<<2) | `get_vertbuf_addr3` @0x06018BE0 |
| +0x50..+0x58 | 3l | light vector (default 0,0,16.0) | `init_light` @0x0601A6EC, template +0x58 |
| +0x60,+0x70 | 16 B each | temporary vertices created by the near-plane clip | 0x0601947C |
| +0x80,+0x82 | w,w | current polygon word0 / word1 | draw loops |
| +0x86 | w | number of entries sorted this frame | `entry_zbuf` |
| +0x88 | w,w | **current command index** (8-byte units) / **limit** | all draw loops (template 0x0200/0x0BE0 master, 0x0C00/0x15E0 slave) |
| +0x8C | w | perspective scale (224 / 128) | `perspective` |
| +0x8E | w | remaining polygons in the part | `draw_color_polygon` |
| +0x90 | w | next free per-polygon gouraud slot (0x1600 master / 0x1800 slave) | `Imm` 0x0601903A |
| +0x92 | w | CMDCTRL template: 0x1004 polygon / 0x1005 polyline (`polyline_on/off`) | |
| +0x94 | l | matrix-conversion function pointer | |
| +0x9B,+0x9C | b,l | commands emitted by clip2d for this polygon (units), depth key | |
| +0xA0,+0xA2 | w,w | pixel offset added to projected vertices (`set_poly_offset`) | `FUN_06019590` |
| +0xA8,+0xAA | w,w | per-frame stats: vertices, polygons | `draw_polygon_init` |
| +0x100..+0x1C0 | | clip2d polygon scratch (8-byte vertex entries) | clip2d |
| +0x200..+0x22F | 12 l | converted matrix (row stride 0x10) | |

---------------------------------------------------------------------------------------------------------------------------------

## 3. Model data as the code reads it

### 3.1 Files and memory [V]

* Course models: `DESERT/FOREST/LAKE/MOUNT(.MDL|.TEX|.MAD)` and `*2P` variants (about 20% fewer vertices, e.g. DESERT 17032 -> 13676). Loaded at **0x06048000** (`course_mdl_buf`, 0x58000 reserved). The first 0x200 bytes
  are a **table of 128 absolute pointers** (0x06048200, 0x06048CEC, ...) to *parts*; `alphabet_poly_tbl`, `select_mdl_buf`, `ending_mdl_buf` share the same buffer.
* Object models: `OBJECT.MDL` at **0x060F0000** (`cube_poly`): no table; parts are addressed by symbol (`celica_body_poly` 0x060F0D4C, `celica_kage_poly` 0x060F160C, `people` 0x060F8988,
  `flag1_00` 0x060F69C0, ...), plus 9 *texture tables* (`celica_texttbl` 0x060F09B4, `delta_`, `stratos_`, `enemy_`, `check_`, `people_`, `countdown_`, `animal_`, `heli_`). 2P: `OBJECT2P.MDL` at 0x060F0000 + cars in `animal2_mdl_buf` 0x06098000.
* Retail loading: `.MAD` (block image, 16-byte entries `<dest><size><type 0><0>`, blocks padded to 0x800 starting at 0x800): `DESERT.MAD` = {0x06048000 <- DESERT.MDL (byte-identical, 0x3B4B4),
  0x25C3B000 <- 4bpp texture data 0x408A0, 0x25C15000 <- palettes 0x1C40}; `OBJECT.MAD` = {0x060F0000 <- OBJECT.MDL 0xD294, 0x00200000 <- OBJECT.TEX 0x21268}; `OBJECT2P.MAD` adds 0x06098000 and 0x00230000. [V]
  (`texture::set_course_model` @0x0601A8F8 CD branch, `set_car_texture` @0x0601ABA2; the branch is chosen by bit4 of `[0x060AA803]`, which `cd_file_init` @0x06004AEC sets.)

### 3.2 Part header (0x14 bytes, big endian) [V - parsed 1024 course parts + object parts]

```
+0x00 u16 nverts          ; vertex count (course parts: 42..439)
+0x02 u16 npolys          ; polygon count (also the budget unit in draw_course)
+0x04 u32 ->vertices      ; int16 x,y,z per vertex (6 bytes); immediately follows the header in course parts (header + 0x14)
+0x08 u32 ->polygons      ; polygon records
+0x0C u32 ->texture table ; 0xFFFFFFFF for flat-colour parts (kage, pilon, cube...)
+0x10 u32 ->normals       ; int16 nx,ny,nz per vertex (1.14: 16384 = 1.0), or 0xFFFFFFFF (any negative value) = none
```
Parts are 4-byte aligned; the polygon data of a part ends exactly at the next part's header (0 or 2 bytes of padding in all 1024 course parts). Per-vertex normals exist only on a few
test objects (`cube`: +-9453 = 16384/sqrt3; `ball`). Cars use no per-vertex normals.

### 3.3 Vertex and polygon records [V]

Vertex: `int16 x, y, z` (model units, 3.2 for scale). Polygon (big endian):

```
+0 u16 w0   flags (below)
+2 u16 w1   textured: BYTE OFFSET of the 8-byte entry in the texture table (= entry*8; all 11000 textured DESERT polygons are in range of its 413 entries)
            flat    : CMDCOLR value, RGB555 with bit15 set (0x8000 = black, 0xDAD6 ...)
+4 u16 idx0, idx1, idx2 [, idx3]     ; triangles have 3, quads 4; each = vertex_number * 16 (byte offset into the 16-byte transformed-vertex buffer)
?  i16 nx, ny, nz                    ; only if w0 & 0x0020: face normal, 1.14 (16384 = 1.0)
```
Record size 12 bytes (quad), 10 (triangle), +6 with normal. The index is a *byte offset*: `get_vertbuf_addr4` @0x06018BAC does `idx + WK+0x1C` with no scaling.

**w0 flag bits** (from `entry/set_polydata`, `FUN_06018F34` command-type switch, `FUN_06018DEC/E54` depth key, `set_vertice_data`, `Imm`):

| Bits | Meaning | Evidence |
|---|---|---|
| 0 (0x0001) | triangle (3 indices; the 4th vertex register is a copy of the 3rd) | `draw_polygon` `tst #1` |
| 1-2 (0x0006) | **depth-key mode** for the sort: 0 = mean of vertices, 1 = nearest (min), 2 = farthest (max), 3 = **max + 4.0** (pushes the polygon back; first read as min + 4.0 from the code, corrected by measuring the chain order of real frames: `tools/coursecheck.py --subsets`: the max over all four vertices + 4.0 has the fewest inversions in 3 courses) | `FUN_06018DEC` @0x06018DEC / `FUN_06018E54` |
| 4 (0x0010) | single sided: back-face cull using the sign of the 2D cross product of v0,v1,v2 (`chk_revpoly` @0x06018D84) | |
| 5-8 (0x01E0) | **command type** (index into 16-entry table @0x06018F50): 0 flat colour (PMOD 0x04C0); 0x20 flat + gouraud level from the face normal (PMOD 0x04C4, CMDGRDA = 0x2010+level); 0x40 **textured** (distorted sprite); 0x60 textured + face-normal gouraud; 0x80 flat + per-vertex gouraud (`Imm`); 0xC0 textured + per-vertex gouraud; 0x100 **mesh** (PMOD 0x05C0); other combinations fall back to the nearest of these (table: 5,7,13,15 -> flat; 8,9,12 -> mesh; 10,11,14 -> textured) | `FUN_06018F34` + table, parsed data |
| 9-11 (0x0E00) | triangles only: how the 3 vertices (and brightness values) are copied into the 4 sprite corners: 0 ABCD(C), 1 AABC, 2 ABBC, 3 ABCC, 4 ABCA | `set_vertice_data` @0x06018D24, `Imm` |
| 12 (0x1000), 13 (0x2000) | horizontal / vertical flip (copied to CMDCTRL bits 4/5 for textured polygons); nearly every textured polygon has bit 12 or 13 set (the stored vertex order is mirrored) | `FUN_06018FC8` `and #48` of the high byte |
| 14-15 | unused (never set in the data) | |

Data statistics [V]: course models contain **only type 2 (textured) and type 0 (flat)** (`DESERT`: 11000 + 208 polygons, 161 triangles). `OBJECT.MDL` (177 parts): type 0 718, type 2 1182, type 8 (mesh) 59, type 1
(face-normal gouraud) 26, plus the two normal-carrying test parts.

### 3.4 Transformed vertex buffer (16-byte entries, per CPU; master 0x060D8000, slave 0x060DC000) [V]

```
+0  i16 sx, i16 sy         ; centre-relative screen position (packed in one long)
+4  i32 z | flags          ; z view depth 16.16 with the low 8 bits replaced by flags:
                           ;   bit0 (1) lateral overflow (|x| or |y| > 32.0 while z <= 8.0)  -> polygon rejected (clip path)
                           ;   bit1 (2) behind the near plane
                           ;   bit2 (4) projected outside the screen rectangle
+8  i32 x_view  | i16 brightness       ; clip path stores view x (+8) and y (+12); the lighting path stores a RGB555 grey brightness at +8
+12 i32 y_view
```
`draw_polygon`/`draw_offset_polygon` drop a polygon if any vertex has flag bit1 (no clipping); `draw_clip_polygon` classifies the 4 flag bytes (`FUN_06018BF8` for quads, `FUN_06018C30` for
triangles): any bit0 -> reject; bit1 mask 0 -> draw directly (`set_polydata4/3` @0x06018CAC/0x06018CE8); otherwise one of 14 (quad) / 6 (triangle) hand-written cases (dispatch @0x06018C68 / 0x06018C94, code
@0x06019118..0x060194FC) clips the polygon against the near plane with `FUN_0601947C` (linear interpolation of x,y at z=near, then projection, result in WK+0x60/0x70), choosing a diagonal by
comparing z, and emits one or two quads/triangles. [V for structure; individual cases not traced]

### 3.5 Cars, people, splash, shadow [V unless noted]

* **Car**: a *group* of parts: body (`celica_body_poly`: 106 vertices, 96 polygons, textured + a few flat), 2 or 4 tyre parts (`celica_tire_l/r_poly`: 20 vertices, 8 polygons; the tyre matrix
  adds a rotation), brake-light on/off parts (`br_on/off`), shadow part (`celica_kage_poly`: 10 vertices at y=0, 4 flat black polygons w0=0x1014, colour 0x8000), and **damaged `_d` variants**
  (`celica_body_d_poly`) and a **simplified `_s`** body (29 polygons). LOD (`FUN_060101E8` @0x060101E8 sets the draw slot, `FUN_060101B8` copies the thresholds): view depth thresholds **112 / 128 / 200**
  (loaded to 0x060D01E0 from the table @0x060101DC): z < 112 -> `FUN_0601029E`, 112..128 -> `FUN_060103BE`, 128..200 -> `FUN_0601042C`, > 200 not drawn (the three tiers differ in model/tyre handling; not analysed
  further; the `_s` body is presumably the far tier [I]). The draw slot `[car+0x380]` (the display function runs with GBR = the car struct) is set to `DRAW_POLYGON` (queue to
  slave), `draw_polygon`, or a stats-only stub `FUN_06010654` (mode index at `[car+0x384]`). Tyres/body are positioned by `push_matrix; translate2(offset); rotate_*; draw`.
* **Car display, verified against the oracle (M5)**: `dispCar` (r5 = mode: table @0x060101F8 -> 0 / 2 set the draw slot to `DRAW_POLYGON`, 1 to `draw_polygon`, 3 to the statistics stub) falls into `dispGhost` @0x0601021A:
  push; `translate(car + 0x18)`; `FUN_0601D8D4(r = 3.0)` = screen sphere test (K = (224 << 32) / z16.16; sx, sy = (x, y) K >> 32; r = (radius >> 1) K >> 32; needs 0 < z < 250 and |sx|, |sy| within 176 / 112 + r);
  `r_rotate_y(+0x10)`, `r_rotate_x(+0x0C)`, `r_rotate_z(+0x14)`; z = translation z of the result vs the three thresholds 112 / 128 / 200 (table @0x060101DC copied to 0x060D01E0 by `dispcar_init`);
  `FUN_06010630` picks the descriptor from 14 x 4 pointers at 0x06010874 (+8 for the far pair; +4 for damaged = mode 2 or ((word @+0xB4 - 0x1111) & 0x8888) != 0; car type = byte @+0x5C).
  Descriptor {wheel offsets (4 x x,y,z 16.16), shadow part, tyre part list, body part, brake-off part, brake-on part}. Near tier @0x0601029E: body, brake light (byte @+0x5D), then for each wheel i
  (push; translate2(offset.x, offset.y + susp[i] >> 4, offset.z) with susp = longs @+0x3C..+0x48; sphere test r = 0.25; front pair r_rotate_y(@+0x30); r_rotate_x(-@+0x38); draw tyre i), then the shadow with the
  *camera* matrix again: translate2(car x, mean(+0x4C..+0x58) - 0x51E, car z), rotate_y(@+0x10), rotate_x((g0 + g1 - g2 - g3) x 0x3CED9168 / 2^32), rotate_z((g0 + g2 - g1 - g3) x 0x553F7CEE / 2^32), sphere test r = 3, draw. Middle tier
  @0x060103BE: `_s` body + tyres (translate with suspension, steering on the front pair, no spin / sphere test). Far tier @0x0601042C: `_s` body + tyres at offset.y + mean suspension (sum >> 6), no steering.
  `FUN_0601D0E6` (dust / spray objects) runs after the near and middle tiers. Port: `src/car.c`; test: tools/cartest.py + tools/test_car.c. **r_rotate_x/y/z are the opposite sense of the formulas in 2.1** (post-multiplication by R(-angle)).
* **People**: `people_texttbl` (24 entries), parts `kata1_poly`... each 4 vertices / 1 quad, flag word 0x1040, billboard (2.3).
* **Splash/dust/countdown/signs**: the same 1-quad parts (`sand_poly`, `water_poly`, `go_poly`).
* **Flags** are 32-frame animations of separate 9-vertex parts (`flag1_00..31`, `flag2_00..31`) swapped by the event code.
* **Shadow**: ordinary flat polygons at the ground plane with colour 0x8000; whether the car code later changes the PMOD to a shadow/half-luminance mode is **[U]** (no such write found).
* **Course**: 128 parts per course (vertices 42..439, median 156; polygons 21..302, median 100 - over the four 1P courses), no LOD; 2P files are separate, cheaper models. Which parts are visible is decided before `draw_course` (the list at
  0x060AA104; builder **[U]**).

---------------------------------------------------------------------------------------------------------------------------------

## 4. Textures

### 4.1 Texture tables [V]

A part's `->texture table` is `{u16 count; u16 pad; u16 cg_base; u16 cl_base}` followed by `count` 8-byte entries that are **exactly the VDP1 command words +4..+0xB**:

```
+0 u16 CMDPMOD   +2 u16 CMDCOLR   +4 u16 CMDSRCA   +6 u16 CMDSIZE
```
`FUN_06018FC8` copies the entry at `WK+0x28 + w1` into command +4/+8. CMDSIZE = (width/8)<<8 | height. Widths seen (courses): 8..256 (multiples of 8; 64 most common), heights 8..104; the biggest is 256x64.
PMOD is **0x1488 in every course/object/ending table** (0x0488 in the select screen). Decoded with the VDP1 CMDPMOD bit layout: high-speed-shrink (bit 12), user clipping enabled (bit 10), end-code-disable (bit 7),
**colour mode 001 = 16-colour look-up table** (bits 5-3), colour calculation 000 = replace (no transparency, no gouraud - gouraud is OR-ed in by the command builder). Because SPD (bit 6) is 0, **colour index 0 is transparent**. CMDCOLR = address/8 of a 16-entry (32-byte) RGB555 palette, CMDSRCA = address/8
of the 4bpp bitmap (two pixels per byte, row-major, width/2 bytes per row; verified: consecutive SRCA values differ by w*h/2/8).
Palette entries are 16-bit with bit15 = 1 (0xFFFF, 0xFFBC ...), i.e. VDP1 RGB mode.

DESERT: 413 entries, CG range VRAM 0x3B000..0x7B8A0, palettes 0x15000..0x16C40 (about 226 palettes of 32 bytes).

### 4.2 Upload and relocation (`texture` module, 0x0601A7F4) [V]

* `texture::setup_texture(r4 = list, r5 = relocation)` walks longs `ptr|type` until -1: **type 0** -> `entry_texture` (relocate a texture table), **type 1** -> `set_texture_cg`, **type 2** -> `set_texture_cl`.
* `entry_texture` @0x0601A836: on first use stores the current CG pointer (word `[0x060D0048]`) and CL pointer (word `[0x060D004A]`) in the table header (+4,+6) and adds them to every entry's CMDSRCA (+4) and CMDCOLR (+2);
  if the header is already non-zero it first subtracts the old bases (so a reload can be re-based). Pointers are in **8-byte units**.
* `set_texture_cg/cl` @0x0601A894/0x0601A8C6: the source block is `{u32 size; data}`; destination = 0x25C00000 + pointer*8; copy by the SH-2 on-chip DMAC channel 0 (`FUN_0601D644` writes SAR/DAR/TCR/CHCR at 0xFFFFFF80.. with CHCR 0x5A01, i.e. longword transfers [I]) - the DAR left in the controller gives the new pointer.
* `set_car_texture` @0x0601ABA2 (CD path): load `OBJECT.MAD`, CG pointer := 0x3000 (VRAM 0x18000), CL pointer := 0x2100 (VRAM 0x10800), then `setup_texture(0x0601AF8C, 0xFDEE0000)` - the list holds the
  **developer addresses** (e.g. `celica_cg` 0x02320000) and the constant 0xFDEE0000 converts them to the Low-WRAM load address 0x00200000 (`0x02320000 + 0xFDEE0000 = 0x00200000`). The same trick: animal2 uses
  0xFDE68000, the 2P cars 0xFD3A0000 (-> 0x00230000). The list order is celica, delta, stratos, enemy, check-point, people, animal, heli, countdown (each {texture table, cg, cl}).
* `OBJECT.TEX` layout [V]: blocks `{u32 size; data}` back to back: celica_cg 0x4200, celica_cl 0xBE0, delta_cg 0x2EC0, delta_cl 0xA00, stratos_cg 0x3A20, stratos_cl 0xC80, check_cg 0x5C00, check_cl 0x3E0, people_cg 0x4220,
  people_cl 0x300, countdown_cg 0x3800, countdown_cl 0x60, enemy_cg 0x4C00, enemy_cl 0x1040, animal_cg 0x3E0, animal_cl 0xA0, heli_cg 0x540, heli_cl 0x180 (total CG 0x1D620, CL 0x3C00).
* Course (CD path): the `.MAD` scatter load puts CG and CL straight into VRAM; the texture table has base 0 so no relocation is needed. The developer path BPE-expands `DESERT.TEX`
  (`FUN_06015870` @0x06015870, r4 = `{u32 len; data}`, r5 = VDP1 destination; two streams at the file start: pointers 0x025E0008 / 0x0260C4B0): stream 1 -> 0x25C3B000, stream 2 -> 0x25C15000. `.TEX` is therefore
  **not needed** for the port; use the `.MAD` blocks.
* `set_course_model` caches a "what is loaded" tag in `[0x060D0090]` (a per-course 4-character constant from the table @0x0601A970, e.g. `'DESE'`), `set_car_texture` in `[0x060D008C]` (`'OBJ '`); `set_game_texture` @0x0601AB6C calls car texture + course + animal (1P) or the 2P variants.

### 4.3 VDP1 VRAM budget (512 KB, retail path) [V from code constants; ranges from the file blocks]

| VRAM | Content |
|---|---|
| 0x00000-0x0CFFF | command table (SCU DMA0 copies 0xD000 bytes from HWRAM 0x060C0000). Indices 0x000-0x1FF: system lists; master polygons 0x200-0xBE0; slave 0xC00-0x15E0; per-polygon gouraud slots 0x1600.. / 0x1800.. |
| 0x10000-0x100FF | flat-shading gouraud table: 32 entries x 8 bytes (`shading::set_flatgour_tbl` @0x0601A7D4: entry i = four words i*0x0421) |
| 0x10800-0x143FF | object/car palettes (CL pointer 0x2100) |
| 0x15000-0x16C3F | course palettes |
| 0x18000-0x355FF | object/car/people/check-point/countdown/enemy/animal/heli bitmaps (CG pointer 0x3000, 0x1D620 bytes) |
| 0x3B000-0x7B89F | course bitmaps (DESERT: 0x408A0) |

Framebuffers are separate (VDP1 FB 2 x 256 KB). Texture size limits are those of VDP1 (width multiple of 8 up to 504, height up to 255); no texture in the data exceeds 256x104.

---------------------------------------------------------------------------------------------------------------------------------

## 5. Lighting and shading

* **Light** (`light` module): one directional light vector at WK+0x50 (master 0x060D1450, slave 0x060D1C50). `init_light` @0x0601A6EC (r4 = intensity 16.16) sets `L = (0, 0, r4*16)`;
  the template default is (0,0,16.0). `rotate_light` @0x0601A750 (r5,r6,r7 = angles): `init_light(1.0); initial_matrix; rotate_z(r7); rotate_y(r6); rotate_x(r5)` then **L := R3x3 * L**.
  `calc_light` @0x0601A718 (r4 = matrix): copies L and replaces it with `calc_point2(M, L)` (rotate the light into the frame of M, in place). Callers found: `ceremony_init` (calc_light), the select/ceremony/
  name-entry screens (rotate_light) - **no caller in the race frame (`game_ctrl` and everything it calls directly) was found**, so in the race the light presumably stays the default view-space (0,0,16) [I].
* **Brightness of a normal** (`FUN_060190B4` @0x060190B4, r4 = current matrix): `n' = M3x3 * n` (rows 2,1,0 dotted with the normal at WK+0x40), then `level = integer part of n'.L` (high word of the 64-bit sum, L has
  length 16 so level is in [-16,16]), **clamped to [-16, 15]**.
* **Flat (face normal)**: type 0x20/0x60 polygons: `CMDGRDA = 0x2010 + level` (entry (level+16) of the 32-entry grey table at VRAM 0x10000; entry i = four words i*0x0421, i.e. a uniform grey of i/31) and PMOD |= 4
  (gouraud). On VDP1 gouraud adds a signed offset (grey - 0x10) to each 5-bit channel, so level 0 (grey 0x10 = 0x4210) leaves the texel unchanged. [V code; the "adds" semantics are VDP1 hardware behaviour, not shown by the code]
* **Per-vertex** (`calc_vertbright` @0x06019718): only if the part's +0x10 pointer is valid; for every vertex: read its int16 normal (x4) -> `FUN_060190B4` -> grey word from the 32-entry RGB555 table at 0x0601974C
  (idx -16..15 -> 0x0000, 0x0421, ... 0x7BDE) -> stored at vertex+8. Types 0x80/0xC0 (`Imm_polygon_func` @0x0601903A..): allocate the next slot (WK+0x90) in the command buffer, write the 4 brightness words into
  the 8-byte table in the corner order selected by w0 bits 9-11, set CMDGRDA to the slot, PMOD |= 4.
* `shading::set_flatgour_tbl` is called once from `mkpoly_init` @0x060183D4.
* Used by the data: face-normal gouraud 26 polygons in `OBJECT.MDL`, per-vertex only in the two test parts. Cars/course are essentially pre-lit textures. [V]

---------------------------------------------------------------------------------------------------------------------------------

## 6. Sorting and the command table

### 6.1 Command table memory (HWRAM 0x060C0000..0x060D0000, 64 KB = 2048 commands) [V]

Index unit = 8 bytes (VDP1 CMDLINK unit), a command = 4 units = 32 bytes. `sprite::clr_cmdtblbuf` @0x060181B8 zeroes the region; `init_cmdtbl` @0x060181D8 sets the sequential write pointer 0x060D002C / counter 0x060D0030
(menus). Command builders: `set_sclip_cmd`(0x0009, +0x14 = XC,YC), `set_uclip_cmd`(0x0008), `set_lcoord_cmd`(0x000A, +0xC), `set_drawend_cmd`(0x8000), `set_dummy_skip`/`set_skip_assign`(0x5000, jump-assign skip),
`set_erase_poly` @0x060182BE (polygon, PMOD 0x04C0, colour 0), `set_cmdtbl` (copy 8 longs), `set_game_syscmdtbl` @0x06018310 (sclip 351,223 + uclip + lcoord (176,112)), and `draw_sprite` @0x06018360
(r4 = XY, r5 = sprite object, r6 = z: normal sprite 0x1000, texture entry from `obj+0xC + 8 + word(obj+8)+2`).
Polygons from the polygon maker have CMDCTRL = 0x1004 (polygon) / 0x1002 (distorted sprite) / 0x1005 (polyline) - **JP = 1 "jump assign"** - and get their CMDLINK from the sort.

Race layout written by `game_zsort` (@0x0601B284; helpers `FUN_0601B594`, `FUN_0601B604`):
```
cmd  0 (0x000)  0x0009  system clip  (0,0)-(351,223)  [+0x14 = 0x015F00DF]
cmd  1 (0x020)  0x6000  skip+call -> idx 0x20 (byte 0x100)
cmd  2 (0x040)  0x8000  draw end
idx 0x20 (0x100) 0x0008 user clip (0,0)-(351,223)
idx 0x24 (0x120) 0x000A local coordinate (176,112)
idx 0x28 (0x140) 0x5000 skip+assign  <- head of the sorted chain (link written by the sort)
idx 0x2C (0x160) 0x7000 skip+return  <- tail
idx 0x3C (0x1E0) 0x5000              <- chain head for the master half (merged afterwards)
```
Split screen (`game_zsort_2p`, `FUN_0601B5B8`/`FUN_0601B604`/`FUN_0601B63A`): header = sys clip, two `skip+call`s to the player lists and a third to 0x300; the call order **alternates each frame** (toggle byte 0x060D0088).
Sub-lists: 0x100 = P1 user clip (0,0)-(351,111), origin (176,56); 0x200 = P2 user clip (0,113)-(351,223), origin (176,168); 0x300 = full-screen clip, origin (0,0) and a degenerate black polygon
(0,112)-(351,112) = the divider line (PMOD 0x08C0, colour 0x8000). Chain heads 0x28/0x3C (P1) and 0x48/0x5C (P2).

### 6.2 Entering a polygon (`mkpoly::entry_zbuf` @0x06018EBC) [V]

* key = polygon depth (16.16) per the w0 depth mode; skipped if key > 255.0. **Bucket = key >> 11** (1/32 world unit; max 8160) - computed as `(key>>10) & ~1` = byte offset into the counter array.
* Counter array 0x060E0000 (+1 for the slave): 2 bytes per bucket (master byte, slave byte; counts in steps of 2). Storage 0x00200000.. (accessed uncached at 0x20200000): **32 bytes per bucket**, the master uses
  the first 16 bytes (8 command indices, u16), the slave the second 16. If a CPU's 8 slots are full the entry spills into the next (farther) bucket; if that is full too, `entry_zbuf` pops its caller's return address and returns, i.e. the polygon is dropped and the calling draw routine is abandoned (`FUN_06018EE8`).
* So the sort is a **counting/bucket sort with 8192 buckets x (8+8) entries**; capacity = 8 polygons per CPU per 1/32 unit of depth.
* `sprite::draw_sprite` uses the same function with r4 = z.

### 6.3 The sort (`zsort::zsort` @0x0601B074, `game_zsort` @0x0601B284) [V]

Walk the buckets **from far to near** (storage pointer starts at the end), read two buckets' counters per long, and for every entry link it behind the previous one: skip forward over commands whose CMDCTRL
JP bits (CMDCTRL & 0x7000 via the byte at +0) are 0 (multi-command groups from clip2d) to reach the group's last command, write `CMDLINK(prev) = this`, `prev = this`; the counters are zeroed as they are read
(`mov.l r1,@(4,r10)`), so no separate clear is needed (`clr_zsortcnt` @0x0601B220 clears 0x060E0000..0x060E4000). **Within a bucket the slave's entries come before the master's.**
`game_zsort` splits the work: the **slave sorts the far half** (buckets 4096..8191, z >= 128.0, job 0x0601B328), the **master the near half** (buckets 0..4095, `FUN_0601B4EC` finds its first entry), then
`FUN_0601B4D0` links the slave chain's last command to the master's first one and the master tail to the 0x7000 return command. In 2P the same is done with two chains (`FUN_0601B148` keeps r8 = P1 chain, r9 = P2 chain: the
master CPU's entries belong to P1, the slave's to P2). The 1.25 near plane and 255 far limit mean about 8,000 usable buckets.

### 6.4 Sizes [V]

* Polygon command capacity: master 696 (0x200..0xBE0 / 4) + slave 696 = **1392 commands**; clip2d may produce 1-3 commands per polygon. Imm-gouraud slots 512 (master) / 2048 (slave, but only up to index 0x1A00 is transferred).
* Submission budget (before culling): 1500 polygons (1P course), 832 per viewport (2P).
* DMA to VDP1: first 0xD000 bytes (1664 commands' worth) every presented frame.

### 6.5 clip2d (module `clip2d`, 0x06019A08) [partly V]

`clip2d_4`/`clip2d_3` (quad/triangle) run in `set_polydata4/3` after the command header is built: compute the bounding box of the 4 projected vertices (`FUN_06019BEC`), outcode every vertex against +-(176,112 or 56)
(`FUN_06019BB8`), then: all outside -> drop; all inside -> accept and rewrite PMOD (`and 0xF9 | 0x08` on the high byte = clear CLIP bits, set pre-clip-disable); small enough (bbox <= 512 x 512) -> keep one command
(CLIP stays on, VDP1 clips; for textured polygons `FUN_0601A604`/`FUN_06019D4C` appear to rotate the vertex order and adjust the flip bits around the worst vertex [I]); otherwise a Sutherland-Hodgman style clip against the four edges (`FUN_0601A090/A200`, scratch
polygons at WK+0x100..) and re-emission as 1-3 quads (`FUN_0601A348`, byte WK+0x9B = number of 8-byte units produced). Not needed on PSP (GE clips); how texture coordinates behave when a textured polygon
is split is **[U]**.

---------------------------------------------------------------------------------------------------------------------------------

## 7. Math library (`math` module, 0x0601B6B0..0x0601BAF4) [V]

| Routine | Definition |
|---|---|
| `sincos_tbl` @0x060A15A0 (from `PRGDATA.DAT` block 0x060A1000, offset 0x5A0) | 4096 entries x {int16 sin, int16 cos}, value = round(sin/cos(2*pi*i/4096) * 16384) (max error 1) |
| `math::sin(r4)` @0x0601B76C / `cos` @0x0601B784 | index = `(angle >> 4) & 0xFFF`: `((r4>>2) & 0x3FFC)` byte offset into the 4-byte entries; result `<< 2` = **16.16** (so 0x10000 = 1.0); `r_sin/r_cos` take radians (x 0x28BE60DC/2^32) |
| `matrix::sincos` @0x06018058 | r0 = angle -> r5 = sin, r6 = cos (both 16.16), r0 = -sin |
| `t_sin/t_cos` @0x0601B6B4/0x0601B6B0 | radians 16.16; range reduction by +-2*pi (0x6487F), then a Taylor series of cos(|x| - pi/2) with coefficients 1/24, -1/720, 1/40320, -1/3628800, 1/479001600 (x 2^32), sign restored; `t_cos(x) = t_sin(x + 0x19220)` |
| `atan_tbl` @0x060A55A0 | 513 x int16: round(atan(i/512) * 65536 / (2*pi)) (max error 1) |
| `math::atan(r4, r5)` @0x0601B7C0 | = **atan2(y = r5, x = r4)** in 16-bit angle (range about -0x8000..0x8000; 0x4000 = 90 deg); octant by signs and |r4|<|r5|, `q = (small << 16) / large` via DIVU, table index `(q >> 6) & ~1` bytes; (0,0) -> 0 |
| `r_atan` @0x0601B794 | same, result in radians (x 0x6487F) |
| `distance_2d(x1,y1,x2,y2)` @0x0601B850 | `dx/cos(atan(dx,dy))`, special cases dx==0 / dy==0; result in input units |
| `distance_3d(ptrA, ptrB)` @0x0601B8A4 | `sqrt(dx^2+dy^2+dz^2)` with 16.16 products (overflows beyond |d| ~ 181) |
| `sqrt(r4)` @0x0601B8CC | 16.16 in/out, Newton iteration x = (x + r4/x)/2 from x = max(r4, 1.0) until it stops decreasing, DIVU; r4 < 0 -> -1.0; 0 -> 0 |
| `inner_product` @0x0601B910 | `(a.b) >> 16` (3 longs) |
| `outer_product(a,b,out)` @0x0601B924 | standard cross product a x b (16.16) |
| `calc_point`, `calc_point2` | see 2.1 |
| `bspline(t, out*)` @0x0601BA04 | uniform cubic B-spline basis: out[0] = (1-t)^3/6, out[1] = t^3/2 - t^2 + 2/3, out[2] = -t^3/2 + t^2/2 + t/2 + 1/6, out[3] = t^3/6 (t in 16.16) |
| `get_lookat_angle(from*, to*)` @0x0601BA6C | r1 = `atan(dx, dz) - 0x4000` (yaw), r0 = `-atan(horizontal_dist/2, dy/2)` (pitch); uses half-scaled coordinates to avoid overflow |

Sin/cos tables and atan table are data: put them (or equivalent integer code) into the port to keep gameplay bit-identical.

---------------------------------------------------------------------------------------------------------------------------------

## 8. What a PSP port needs

### 8.1 Saturn features actually used [V]
Textured quads as distorted sprites (4bpp CLUT, index 0 transparent, H/V flip, high-speed-shrink flag); flat polygons; gouraud (flat level via 32-entry grey table, per-polygon 4-word tables); **mesh** (59 polygons in
the object file); polyline mode (debug switch); system clip, user clip (2P viewports), local coordinate (centre origin), pre-clipping disable. **Not found**: half-transparency or shadow colour-calculation modes in any
texture table or polygon type; VDP1 frame-buffer erase is plain (0 over 352x224).

### 8.2 Differences to GE triangles
* VDP1 maps a quad by interpolating along its edges - bilinear in the quad's (u,v), **affine, not perspective-correct**; a diagonal split into two GE triangles gives a different warp for non-parallelogram quads.
  Faithful look: draw each quad as two triangles with affine interpolation (through-mode vertices, or subdivide); cleaner look: perspective-correct 3D triangles (small deviation; the models are cut into small parts).
* Degenerate quads (triangles with corner mapping 1..4) are quads with a repeated vertex: emit one triangle.
* Painter's order: the original has no Z buffer; ties are resolved by the key mode (avg/min/max/+4.0) and by slave-before-master within a 1/32-unit bucket. With a Z buffer, polygons that rely on
  "max + 4.0" (decals, shadows, road markings) need a depth bias or a painter pass.
* Gouraud: VDP1 (hardware behaviour, not shown by this code) *adds* a signed offset (c-16 per 5-bit channel) to the texel; GE modulates. A level in [-16,15] maps to a signed offset; use vertex colour + 2x modulate or an additive pass.
* Mesh = checkerboard every other pixel: use a stipple/alpha test or 50% alpha.
* Palettes: RGB555 with bit15 = 1 -> RGBA5551; set alpha 0 for index 0 (T4 + CLUT works directly: 16 colours, 4bpp).
* Coordinates: centre-relative, y down, 352x224 (aspect 1.571 for a 4:3 TV picture = non-square pixels, x stretched by about 1.21); scale S=224 gives equal x/y focal length in the 352x224 pixel grid.

### 8.3 Reimplement (keep behaviour)
`matrix` ops (floats are fine, but rotations are right-handed 3x4 with local post-multiplication), `calc_vertices`/projection (S=224/128, near 1.25/0.5, far 255), the sort-mode semantics, back-face
rule (`chk_revpoly`: cull when 2*area >= 0, flipped by `rewrt_chk_revpoly(r4!=0)`), `calc_light/FUN_060190B4` brightness, `set_vertice_data` corner mapping, `draw_course` budget + list order, car LOD (112/128/200),
the math library and tables (section 7), billboard rules, texture-table relocation (or bake it in the converter).

### 8.4 Replace with the GE / drop
`sprite` command-table builders, `entry_zbuf`/`zsort`/`game_zsort*`, `clip2d`, near-plane clip cases (GE clips), `vint_in/out`/`vdp`/`int::sync*`, SCU DMAs, slave mailbox + all `s_*`/`m_*` pairs (run single threaded
or use ME), `purge_matrix`/CCR writes, `DRAW_POLYGON`/`FUN_0601DB20` queues, `.TEX` BPE (`FUN_06015870`), SH-2 DMAC copies (`FUN_0601D644`), the "cache"/"dsp" labelled code (9).

### 8.5 Converter inputs (next milestone)
Use `*.MAD` (plain, uncompressed): course = {MDL 3.2-3.4, 4bpp bitmaps, palettes}; object = {OBJECT.MDL, OBJECT.TEX blocks, 4.2}. The texture-table entries give everything needed (size, palette, offset).

---------------------------------------------------------------------------------------------------------------------------------

## 9. Corrections to earlier notes / unreliable DBG labels

* `NOTES.md` says `sync(n)` counts n down (2 frames). The V-IN state-2 handler (`0x06004D4C`) zeroes the byte at the first V-IN; 30 fps comes from the two-handshake structure (1.5).
* `PRGDATA.DAT` blocks are **0x800-aligned**, starting at 0x800 (the sum-of-sizes check in the notes does not apply; header 0x60 bytes). The `sincos_tbl`/`atan_tbl` live in block 2 (0x060A1000).
* `0x02xxxxxx` data symbols (`desert_mdl`, `object_tex`, `celica_cg`...) are **developer-environment (A-Bus RAM) addresses** used verbatim by the non-CD branch; the CD branch adds per-file constants
  (0xFDEE0000, 0xFDE68000, 0xFD3A0000...) to reach Low WRAM 0x00200000/0x00230000. They are not "block base << 4" in the cases checked.
* (Scenery objects - spectators, gates, flags, animals - are documented in docs/events.md.) DBG labels in `cache` (0x0601BAF4), `etcsub` (0x0601BB80), `dsp` (0x0601C170) do **not** describe the code: 0x0601BAF4.. is an **object-slot scheduler** (slots of 0x40/0x80 bytes at 0x060E4000/0x060EA000,
  exec hook at +0, next/prev links; `clear`, `init`, `run` loops), 0x0601BB88 wraps two SH-2 DMA calls (`FUN_0601D7F4/0601D874`), 0x0601BC04..0x0601BC9C are slot allocate/link/unlink, 0x0601BCB8/0x0601BD52/0x0601BE3C/0x0601BE9A
  walk object groups (record = {u16 offset,u16 count} at 0x060D0C00/0x060D0E00; the master takes even slots, the slave odd ones, chosen by `word(slot+0xC)`), `DRAW_COLOR_POLYGON` @0x0601C07C and the "dsp" area at
  0x0601C190..0x0601C33C draw the **check-point / finish gates** (`checkpoint_l/s_poly`, `finish_l_poly`, `pole_s_poly` via `draw_clip_polygon`), the `init_fade`/`fade` area loads the event table (it calls the CD loader with an `EVENTTBL.MAD` descriptor) [I]. `cache` has no cache code
  (the CCR writes are `mov #17,r0; mov.b r0,@0xFFFFFE92` inside jobs). `extract::bpe_expand` @0x06014C8C is a different routine from the BPE expander at 0x06015870.
  The modules from `texture` and earlier (matrix, sprite, mkpoly, clip2d, light, shading, texture, zsort, math, int, vdp) match their code.

---------------------------------------------------------------------------------------------------------------------------------

## 10. Open questions

1. Which function sets the race main vector and builds the part list at 0x060AA104 / count 0x060AA000 (and 0x060AA198/0x060AA020 for P2)? Does it sort near-to-far (the 1500-polygon budget assumes so)?
2. Race pass pacing: is the race handler's recipe identical to `ending_main` (36 + 38)? What do V-IN recipes 4, 6, 8, 10, 12-34, 40-48 do in sequence (frame-buffer change vs plot start vs erase)? Is a lost frame detected (COPR saved in 0x060AA87C is never read here)?
3. Do cars get a shadow/half-luminance PMOD at draw time? (no code found; shadow part is flat black). Is any transparency used in menus (SELECT/ALPHA tables use PMOD 0x0488)?
4. `calc_camera_matrix` has no static caller - dead code, or reached through a table? Which camera handler writes 0x060D1000?
5. Texture coordinates when `clip2d` splits a textured polygon (0x0601A348 cases) - re-mapped or full texture per piece?
6. The two test parts with per-vertex normals (`cube`, `ball`) have an 0x1C-byte header (vertices at +0x1C, normals right after the vertices); the 8 bytes between +0x14 and +0x1C are zero (padding?). All other parts have the 0x14-byte header.
7. Overlap of the depth-bucket storage (Low WRAM 0x00200000..0x0023FFFF) with the load areas of `EVENTTBL.MAD`/`OBJECT.MAD` (0x00200000): only safe if those are consumed at load time (likely, not proven).
8. Who writes 0x060D0140 list and 0x060D0C00/0E00 group records (object/event tables), and what is the exact per-object transform contract (r4 = camera matrix, GBR = object)?
9. Exact semantics of WK+0x84 and WK+0x9B/0x9C use in `set_polydata4` when the near-plane clip produces two polygons (the second draw reuses the stored key).
