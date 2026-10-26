# Sega Rally Championship (Saturn, USA) - the VDP2 race background (sky / horizon)

Tags as in `render_pipeline.md`: **[V]** verified (code read and/or run in the SH-2 interpreter, output checked), **[I]** inferred, **[U]** unresolved.
Addresses are real 0RALLY.DAT addresses (base 0x06004000) unless marked "DBG". Tools: `tools/vdp2.py` (reference renderer, runs the game's own set-up code),
`tools/conv_bg.py` (PSP converter, `extracted/psp/course/<name>.rbg`).

## 0. The short version

* The whole background of a 1P race is **one VDP2 layer, RBG0**: a 1024 x 512 pixel, 256-colour **panorama** (360 degrees of camera yaw, sky + clouds + far hills + a
  flat ground colour below the horizon). One panorama per course, stored in `BG<COURSE>.DAT`. [V]
* Per pass the game computes only two things from the camera: **X scroll = base + yaw / 64 pixels** (65536 yaw units = one turn = 1024 px) and a **rotation of the layer
  about the screen centre by roll / 2**. The vertical position is a per-course constant: the camera pitch does **not** move the sky. [V]
* The VDP1 frame buffer (all 3D polygons) is the sprite layer, priority 2 = same as RBG0, and wins ties: the sky shows wherever VDP1 drew nothing. NBG2 / NBG3 (priority 6, HUD text)
  lie above both and are not part of the background. [V]
* PSP: two 512 x 512 T8 textures + a 256-entry CLUT per course, drawn as a horizontally scrolled (wrapping) screen-sized quad behind the 3D scene (section 6). [V]

## 1. VDP2 state during a 1P race [V unless noted]

Produced by running `vdp::init_vdp2sys` @0x06005518 (registers from `datgbal::vdp2init_tbl`, PRGDATA block 0x00298000) and `scroll::init_gscroll` @0x06015B80
(the race's own overrides, game type != 10), then reading 0x25F80000.. :

| reg | value | meaning |
|---|---|---|
| 0x20 BGON | **0x001C** | NBG2, NBG3, **RBG0** on, dot 0 transparent. Written by the display init of game types 0 / 2 (0x0601159C / 0x06011654, table at 0x06011534 by `[0x060AA860]`); the V-IN recipe copies the shadow word `[0x060AA8E6]` to the register every frame. `init_vdp2sys` alone leaves 0x000F (menus use 0x001F via `init_oscroll`). |
| 0x0E RAMCTL | 0x13E0 | colour RAM mode 1 (RGB555, 2048 colours), VRAM split in four banks (A0 A1 B0 B1); rotation-data bank select RDBSB0 = 2 (B0 0x40000 = pattern names), RDBSB1 = 3 (B1 0x60000 = character data) |
| 0x2A CHCTLB | 0x1022 | RBG0: 256 colours (palette), 8x8 cells, not a bitmap; NBG2 / NBG3 256 colours |
| 0x38 PNCR | 0xC00C | RBG0 pattern names: 1 word, 12-bit character number, no flip; supplementary character number 0x0C => **character number += 0x3000** (= VRAM 0x60000 / 0x20) |
| 0x3A PLSZ / 0x3E MPOFR | 0 / 0 | each plane = 1 page (64 x 64 cells = 512 x 512 px); overflow mode "repeat" |
| 0x50..0x5E MPABRA | 0x2021 x 8 | the 16 planes (4 x 4, row-major, low byte first) alternate **page 0x21** (VRAM 0x42000) and **page 0x20** (VRAM 0x40000) => a 2048 x 2048 plane that is the 1024 x 512 panorama tiled 2 x 4 |
| 0xB0 RPMD / 0xB2 RPRCTL / 0xB4 KTCTL | 0 / 0 / 0 | rotation parameter A only, no coefficient table, no per-pixel scaling |
| 0xBC/0xBE RPTA | 0x0003FF80 (x2) | rotation parameter table at VRAM **0x7FF00** (0x60 bytes, DMA2 from HWRAM 0x060B9000 every V-IN: SCU regs 0x25FE0040 / 44 / 48) |
| 0xE0 SPCTL | 0x0020 | sprite type 0, data word may be RGB (VDP1 draws RGB555) |
| 0xF0..0xF6 PRIS, 0xFC PRIR, 0xF8/FA PRIN | 0x0202 x 4, **2**, 0x0604 / 0x0606 | sprite priority 2, RBG0 priority 2, NBG0 4, NBG1 6, NBG2 6, NBG3 6 |
| 0xEC CCCTL, 0xD4 WCTLC | 0, 0 | no colour calculation / transparency, no window on RBG0 or sprites |
| back screen | VRAM 0x7FFFC = 0x0421 | written by `vdp::tv_on` @0x0600553C (`tv_off` writes 0x7BDE); effectively black |

SFPRMD = 0x155 (special priority per character) is set but the pattern names carry no special bit, so RBG0 stays at priority 2. Sprites and RBG0 share priority 2; the
fixed tie-break order is sprite > RBG0 > NBG0 > NBG1 > NBG2 > NBG3 (hardware rule) [I: standard VDP2 behaviour, not tested on hardware].

## 2. Data: `BG<COURSE>.DAT` [V]

Scatter-load file (`datfile.Scatter`); the three VDP2 blocks:

| block | type | dest | expanded size | content |
|---|---|---|---|---|
| 0 | 2 (BPE) | VDP2 VRAM **0x25E60000** (B1) | 0x16A40 DESERT, 0x18EC0 FOREST, 0x19380 MOUNT, 0x1F7C0 LAKE | RBG0 character data: 8x8 cells, 8 bpp, 64 bytes per cell, row-major; cell *n* at 0x60000 + 0x40 * (n/2) (character numbers step by 2) |
| 1 | 2 (BPE), **arg 0x5000** | VRAM **0x25E40000** (B0) | 0x4000 | two 64 x 64 pages of one-word pattern names (page 0 @0x40000 = map 0x20, page 1 @0x42000 = map 0x21). The loader adds 0x5000 to every output word = palette number 5 (bits 14-12) |
| 2 | 0 | HWRAM **0x060B8A00** | 0x200 (MOUNT 0x1E0) | 256 RGB555 colours: **palette 5** of the colour RAM shadow (0x060B8000 + 5 * 0x200) |

(The other blocks of the file - 0x00240000.. / 0x00290000.. - are the collision / block tables, `docs/collision_world_ai.md`.)

Colour RAM: the game keeps a 4 KB shadow at 0x060B8000; SCU DMA1 (0x25FE0020.., 0x1000 bytes, set up in `init::init_gspr` @0x0600460C) copies it to colour RAM 0x25F00000 in the
V-IN recipes (0x06005074.. in `int::vint_in`). Colour index for RBG0 = palette(5) * 256 + dot, i.e. colour RAM byte 0xA00 + 2 * dot; word = RGB555 (red in bits 0-4, green 5-9,
blue 10-14 - the same bit order as the PSP's RGBA5551 minus the alpha bit). Index 0 is transparent (never occurs in the panoramas: 0 dot-0 pixels in all four).
Palette use: DESERT 241 colours, FOREST 249, MOUNT 219, LAKE 227. Nothing writes the shadow or VDP2 VRAM during the race loop in our runs (330-1500 passes) [V for the 1P oracle runs].

Panorama content (`extracted/analysis/preview_<course>_bg.png` via `conv_bg.py --preview`): DESERT blue sky + big clouds + a snow-capped hill, green steppe ground; FOREST dark mountain range with
two rock cliffs, near-black ground; MOUNT cumulus sky over a lake with a small castle island; LAKE red-orange sunset sky, far shore with houses. Left / right edges are seamless.
Rows below the horizon are a flat ground colour (they are hidden behind the road polygons).

## 3. RBG0 geometry - the rotation parameter table [V]

Table A (0x60 bytes, VRAM 0x7FF00) comes from `datgbal::rpara_ginit_tbl` @0x0029819C (PRGDATA) with the per-course constants patched in by `init_gscroll`
(`mova` tables at 0x06015DC0 (Mx) and 0x06015DD0 (My), 4 words per row, indexed by course):

| field | value |
|---|---|
| Xst, Yst, Zst, dXst | 0 |
| dYst, dX | 1.0 (the screen is sampled 1:1) |
| dY | 0 |
| A, E | 1.0 (cos), B, D 0 (+-sin) - rewritten every pass, section 4 |
| C, F | 0 |
| Px, Py, Pz / Cx, Cy, Cz | 176, 112, 0 / 176, 112, 0 (screen centre of 352 x 224) |
| Mx, My (16.16) | DESERT **960**, **184**; FOREST 960, **128**; MOUNT **784**, 176; LAKE 960, 176 (Mx is rewritten every pass, My is constant) |
| kx, ky | 1.0 |

Per-pixel rule used by the VDP2 (and by `vdp2.render_background`), screen pixel (h, v): `Xs = Xst + dXst*v + dX*h`, `Ys = Yst + dYst*v + dY*h`,
`X = kx * (A*(Xs-Px) + B*(Ys-Py) + C*(Zs-Pz)) + A*(Px-Cx)+B*(Py-Cy)+C*(Pz-Cz) + Cx + Mx`, `Y = ky * (D*(Xs-Px)+E*(Ys-Py)+F*(Zs-Pz)) + ... + Cy + My`; the plane is
2048 x 2048, `floor(X), floor(Y)` wrap (overflow = repeat). With this table:

    X = Mx + 176 + A*(h-176) + B*(v-112)        Y = My + 112 + D*(h-176) + E*(v-112)

Field bit formats of table A (32-bit words, 16.16 with the low 6 bits unused; Px.. 14-bit ints): Xst 0x1FFFFFC0, dXst/dX 0x0007FFC0, A..F 0x000FFFC0, Mx/My 0x3FFFFFC0,
kx/ky 0x00FFFFC0 (sign-extended from the top bit of each mask).

## 4. How the camera drives the background [V]

`gctrl::rscroll_ctrl` @0x06005E54 (step 9 of the 1P frame, after `camera_ctrl`, before the Z sort) does `r14 = 0x060AA9F0; jmp [r14]`; init_game_work puts `plctrl::normal_rmove`
@0x06006BBC there (`normal_endmove` @0x06006BD4 / `normal_rmove2p` @0x06006BDC for the goal / 2P). `normal_rmove` loads r13 = 0x060AA9B0 (**camera work**) and jumps to
`scroll::set_grpara2` @0x06015F6E:

* Camera work: `+0x14` pitch, **`+0x16` yaw**, **`+0x18` roll**, all s16 with 65536 = 360 degrees (the camera code stores `-(angle_rad_16.16 * 0x28BE60DB >> 32)`, i.e. `-angle / 2pi * 65536`; the
  gameplay camera writers are 0x0600FD68..0x0600FD86 and 0x0600FFB4..0x0600FFD2; yaw = minus the camera's rotation about the vertical axis, e.g. car yaw -1.571 rad -> 16384).
* `Mx_target = ((yaw & 0xFFC0) << 10  +  base_Mx) & 0x03FF0000`, i.e. **px = base_Mx + (yaw >> 6)**, wrapped at 1024 px (1024 px per turn = 2.84 px per degree; the 3D view uses
  224 px per radian = 3.9 px per degree, so the sky turns about 27 % slower than the world; verified: with the camera turning, sky and polygons move the same way).
  `base_Mx` is the per-course Mx above, stored at `[0x060AAA08]`.
* roll: `angle = roll16 >> 1` (arithmetic), `A = E = cos(angle)`, `B = sin(angle)`, `D = -sin(angle)` (values from the game's 14-bit sine table `datgbal::sincos_tbl`, so they are multiples of
  1/256; 0 roll => identity). The layer is rotated about the screen centre (176, 112) by half the camera roll. The roll was 0 in every sampled DESERT / FOREST frame and up to a few degrees (+-600 units)
  on MOUNT.
* **Mirror mode** (`[0x060AA82E] != 0`, mirrored course): yaw and roll are negated first.
* The targets land in `0x060AA9F8` (Mx), `0x060AA9FC` (A), `0x060AAA00` (B), `0x060AAA04` (D). The V-IN code (`scroll::set_grpara_int` @0x06015FEC, run in recipe 38) then moves the table
  at 0x060B9000 half-way to the target each call (with the 1024-px wrap handled: if |diff| > 512 px it adds one period), truncating to whole pixels, and DMA2 copies the table to VRAM 0x7FF00.
  So the displayed sky lags the camera; `build_state(settle=None)` uses the targets, `settle=n` runs the real smoothing n times.  **Ported** (`camera_sky_init` / `camera_sky_pass`, `src/camera.c`): Mx, A (= E), B, D are
  the table state and every pass moves each half way (Mx around the 1024 px circle, `& 0x03FF0000`) to the targets computed from the camera yaw / roll; `tools/oracle.py run --sky` runs `rscroll_ctrl` + `set_grpara_int` once per pass and logs the table
  and the targets, `test_drive -sky` compares them: 6 traces, ~22,000 passes, 0 differences.  (One smoothing step per race pass is the assumption: recipe 38 of the V-IN runs once per frame end.)
* Pitch is never read in a 1P race, so the sky does not move vertically; My is fixed. The visible rows are `My .. My + 223` of the panorama (the horizon is at panorama row ~300 for DESERT
  = screen row 116, which sits just below the usual vanishing point at row ~100).
* Game types: 0 / 2 (1P) verified. Types 4 / 6 / 8 (two cars one view) use the same `init_gscroll` branch [I]; type 10 (2P split screen) runs the other branch of `init_gscroll`
  (two rotation parameter sets A / B, windows, `rscroll2p_ctrl`) and is **not** covered [U].

## 5. Composition with VDP1 and the screen [V]

VDP1 frame buffer = sprite layer, 352 x 224, RGB555 words (MSB-flagged); pixels the polygon pipeline did not touch stay transparent (the FB is erased to 0 each frame, `EWDR` = 0), so
`final = vdp1_pixel if drawn else rbg0_pixel (index != 0) else back screen`. `vdp2.compose(vdp1_img, drawn_mask, bg)` does this. NBG2 / NBG3 (HUD: time / lap / position text, written by the
display routines into name tables at VRAM ~0x3A000-0x3FFFF, characters blitted from `CGDATA` blocks to 0x25E2xxxx) are drawn over the sprites (decoded by `vdp2.render_hud`, docs/hud.md). The fade in / out at the start /
end of a race uses the VDP2 colour-offset registers (0x114..0x11A, values seen 224..255 = -32..-1 stepping to 0) and is not applied by the reference renderer (frames are rendered fully faded in).

## 6. What the PSP renderer needs

Data (`conv_bg.py`, format documented at the top of the script): per course `<name>.rbg` = header (base_x, base_y, 1/64 px per yaw unit, roll scale 0.5, back colour) + 256-entry RGBA5551 CLUT
(entry 0 transparent) + two 512 x 512 T8 images (panorama x 0..511 and 512..1023), 524,856 bytes.

Per frame (before the 3D scene, no depth write, no depth test; clear colour = back colour):

    yaw16   = (u16)(-camera_yaw_rad / (2*pi) * 65536)          /* same angle the camera code stores at 0x060AA9C6; mirror mode: negate yaw and roll */
    px      = base_x + (yaw16 >> 6)                              /* panorama x of the screen's left edge, wraps at 1024 */
    roll == 0:  u0 = px mod 1024; panorama column u0 goes to screen x = 0. Piece 0 (columns 0..511) is drawn at x = -u0 and at x = 1024 - u0, piece 1 (512..1023) at
                x = 512 - u0 and at x = -512 - u0 (clip to the 352-px screen; at most 3 of the 4 are visible); texture rows base_y .. base_y + 224 (<= 408 < 512: no vertical wrap).
    roll != 0:  rotate the quad's texture coordinates about the screen centre (176, 112) by theta = roll16 * pi / 65536:
                u = px + 176 + cos*(x-176) + sin*(y-112),  v = base_y + 112 - sin*(x-176) + cos*(y-112)   (u wraps at 1024, v at 512; rows reach centre +-208);
                draw each piece as a rotated quad with GU_REPEAT, or sample the 1024-wide panorama as two textures. The original quantises sin / cos to 1/256 (immaterial).
    screen mapping: 352 x 224 -> the PSP's 480 x 272 as for the VDP1 layer (the background is 1 panorama pixel per VDP1 pixel).
    then the VDP1 polygons (they are the sprite layer, so everything the 3D pass draws is in front of the sky).

Use point sampling (the VDP2 does not filter) or bilinear after scaling; palette / T8 with `sceGuClutMode(GU_PSM_5551, 0, 0xFF, 0)` (CLUT index 0 is alpha 0, never used by the data). Only the
rows `base_y .. base_y + 224` (+ a margin for roll) are ever visible: DESERT rows 184..408, FOREST 128..352, MOUNT / LAKE 176..400, so the lower 100+ rows of each piece can be dropped to save VRAM
(they are one flat ground colour).

## 7. Tools and verification

* `python -I tools/vdp2.py --course C --pass N` runs the oracle race to pass N (`refframe.capture`), copies the memory, runs `init_vdp2sys`, `init_gscroll`, `rscroll_ctrl` (the real code, SCU DMAs applied
  by a small helper), decodes RBG0 and writes `extracted/ref/<course>_p<N>_bg.png` (background alone) and `_full.png` (VDP1 over VDP2). `--static --yaw 0x4000 --roll 0` renders without a race.
  Checked visually on all four courses at several passes (including yaw 0..0xFFFF turns): sky / clouds / hills line up with the polygons and the sky scrolls the same way as the 3D world.
* `python -I tools/conv_bg.py --check --preview` converts the four courses and compares the documented display rule (header values only) with `vdp2.render_background`: 0.000 % differing
  pixels for roll 0 at yaw 0 / 0x1234 / 0xC000; 2-8 % at roll != 0 (the original's 1/256 sine quantisation).
* Memory-side facts were checked by reading disassembly produced with a private SH-2 disassembler (V-IN recipe @0x06005074, `set_grpara2`, `set_grpara_int`, `init_gscroll`, the display init) and by
  running the routines: every register value quoted in section 1 is a dump of the emulated 0x25F80000.. page.

## 8. Unresolved

1. NBG2 / NBG3 (HUD) are now decoded by `vdp2.nbg_plane / render_hud` and drawn by `src/hud.c`: see docs/hud.md.  **[U]** BGON 0x1D (NBG0 on) is used by other screens (replay, 2P) not examined.
2. The sky lag is ported (one IIR step per pass, section 4); whether the real console runs recipe 38 exactly once per pass is [I].
3. **[U]** 2P split screen (game type 10), the goal / ceremony / replay screens (other BGON, `normal_endmove`, `rscroll2p_ctrl`, `set_rpara` = the 3D rotating-floor mode used by title / ending code).
4. **[I]** Whether the real display shows RBG0 priority 2 under sprite priority 2 exactly as assumed (standard VDP2 tie rule); no hardware capture was available. Visually the result is the expected
   Sega Rally look.
5. **[U]** Colour-offset fades (start / end of race, `inccoloffset`) are not emulated.
