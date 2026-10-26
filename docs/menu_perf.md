# Picture time of the menus (October 2026)

The menus draw one picture every second V-blank (30 per second): `vdp_render_frame()` (src/vdpr.c, software VDP1 + VDP2) and the GE upload.  The budget is about 33 ms; a picture that takes
more than one V-blank (16.7 ms) already costs frames in the animations.

## How to measure

* On the PC (relative numbers, the PSP is about 35-50 times slower): `bash tools/menu_prof.sh [scenario...]` draws the pictures of the oracle scenarios (pspfix + renderer) and prints average / worst
  time per scenario.  `PROF_HASH=file` (see tools/test_menu.c, `-DPROF`) writes one hash per picture: two renderers must give identical hashes.
* In PPSSPP (virtual time, close to the PSP): an empty file `data/dbglog` makes the host write `data/menu_frames.txt` ("E tick ms" per picture); `data/menu_perf.txt` has the averages.  A pad script
  runs a transition without a controller: `powershell -File tools/run_ppsspp.ps1 -Dist dist_menu/SegaRallyPSP -Seconds 30 -Pad "120 2000;124 0;...;200 0200;204 0"` (tick, Saturn pad bits in hex).
  The memory stick folder is always `SegaRallyPSP` (the EBOOT's data path is fixed).

## What was slow (found with the above)

1. Every VDP1 command went through `v1_quad`, the general distorted-sprite rasteriser (oversampled four times, doubles).  Nearly all menu sprites are normal sprites (command 0): now `v1_sprite`
   copies the texture 1:1.  (The old path dropped / doubled texel columns: the text sprites are now exact.)
2. During a screen transition the menus change the scroll registers and the rotation parameters of RBG0 every picture; every change rebuilt all five layers completely and the composite.
   Now: a rotation table change no longer invalidates the scroll screens (`rot_gen`), and a layer that only moves (unzoomed scroll screen, RBG0 with the identity matrix) keeps its dots in a
   512 x 256 torus (`LCache`, `update_layer`): only the dots that scroll into the window are built.  The composite goes layer by layer (least important first) in long runs.

PPSSPP, options transition, per picture: 136 ms -> about 20 ms; idle picture 9.8 ms -> 6.6 ms.  All 91 scenarios draw identical pictures with the changes 2 and 3 (compared with the renderer before
the change); only the text sprites differ from the first version.

## Still slow

* A screen that is built from scratch (first picture after a switch) rebuilds every layer: 60-100 ms once.
* A zoomed scroll screen (NBG1 of the mode select while it scrolls) is rebuilt whenever its position changes (`tile_color` per dot).

## Update: why the options page was slow, and the VRAM change tracking

* A change of the VDP2 VRAM invalidates the layer caches only where it matters: `vram_diff` looks at the bytes that changed and ignores the tables the renderer never reads (the line scroll tables, the line colour and back colour tables: the slide of the options pages rewrites a line scroll table every picture); the rotation parameter table only invalidates RBG0 (`rot_gen`); a page inside one layer's name tables (or bitmap) only that layer (`lgen`, `page_owner_mask`); anything else (character patterns, unknown) all layers (`vram_gen`).
* PPSSPP, KEY CONFIG page slide-in, per picture: 87 ms -> 8 ms (idle picture 8 ms).  The 141 oracle scenarios draw identical pictures with and without these changes (tools/test_menu.c -DPROF with PROF_HASH).

## Update: time attack, name entry and fades on the PSP (October 2026)

Reports from a real PSP: the time attack screens and the name entry were slow (the car turned too slowly, the letter wheel crawled), buttons sometimes had to be pressed twice, a fading screen left its 3D car undimmed.
PPSSPP's virtual time (what `data/menu_perf.txt` shows; the PC is 30-50 times faster than that, and the real PSP is slower than PPSSPP's model) was the measure.  Where the time went (time attack car page, per picture,
PPSSPP): renderer 9.6 ms, of which the VRAM change scan 2 - 4 ms (the PSP's `memcmp` is a byte loop: 512 KB per picture), the picture's dots 2.8 ms, colour RAM 0.6, layers 0.6; the 3D car 0.4 ms.

* `vdpr.c`: the VRAM scan compares words and only the 4 KB pages the memory model saw written (`sat_v2_dirty`, set by `W8` / `W16` / `W32`, `sat_memcpy`, the file loaders; reads through `R8` ... `R32` mark nothing; a host that writes the VRAM itself sets
  `sat_v2_touched` = compare everything).  A changed page without a special table (rotation parameters, line / back colour tables) counts at once instead of byte by byte.  The dots of a picture are a table lookup per dot
  (`lut0` / `lutA` / `lutB`: colour RAM with the back screen and the fade offsets applied, rebuilt only when they change); the sprite layer is looked at only inside the span of each line the sprites touched (`v1_x0` / `v1_x1`); the 16 / 256 colour
  sprites and cells have their own loops; `V2R` reads the register array directly.  A picture whose layers, colour RAM, sprites, registers did not change is not composed or uploaded again (`VdpFrame.same`: the photo scene of the staff roll is
  unchanged in 97 % of its pictures).  The sprite layer is not redrawn when the commands and the textures are the same (`sat_v1_touched`).  Time attack car page: 9.6 ms -> 5.0 ms (PPSSPP), on the PC 0.27 -> 0.13 ms; the first picture of a screen 4.8 -> 2.6 ms.
  `tools/menu_skipcheck.sh` proves it: every picture of all 141 recorded menu scenarios is bit-identical to the straightforward renderer, drawn in one part and (`HI=1`) in two parts (`VDPR_REF` + `VDPR_NOSKIP` + `VDPR_NOCACHE`: everything rebuilt, composed dot by dot); `tools/menu_prof.sh` prints the parts
  (`vdpr_t`), `PROF_LOG=file` one line per picture, `PROF_WHY=1` why a picture was composed again; `tools/menu_gprof.sh`; the PSP logs the same parts to `data/menu_perf.txt`.
* `menu_host.c`: the ticks are locked to the display's V-blank count (`pace_wait`).  A tick that took longer than a V-blank no longer costs a whole one: the next ones start at once, and a tick that is a V-blank behind does not draw its picture
  (at most three in a row), so the game keeps 60 ticks a second (the turntable car's speed, the letter wheel, the slides) while the pictures come as fast as they can.  The pad is the OR of every controller sample since the last tick (`read_pad`),
  so a tap inside a slow tick is not lost (the "press twice" report: the car page accepts a one-tick tap the pass it appears, checked on the original); after a race the first read starts from the present state.  The text upload skips unchanged pictures.
* `render.c`: no `floorf` per vertex (the lighting level), no `fminf` / `fmaxf` calls (the PSP's libm).
* The models of a screen (car, course model, letters) are drawn between the parts of the picture: the layers with a priority above the sprite layer's cover them (`vdpr_hi`, as the staff roll does), and the sprite layer's colour offset (the fades)
  dims them: `scene_set_coloff` (flat colours on the CPU, a copy of every texture's palette with the offset).  The arcade car select's cars now fade with the rest.
* The result screen of the time attack does not show the turntable car (its code asks for it, the PSP does not draw it: user report, the car belongs to the car select).

## Update: the name entry's rough frame rate (October 2026)

* PPSSPP, name entry: 47 ticks per second; the 3D letters cost 7.5 ms per picture although `scene_draw` itself took 2.8 ms.  The rest was `m3_replay`: its development check "is data/m3dbg there?" ran `fopen` on the memory stick every picture (a failed open: about 4.5 ms in PPSSPP, more on a real stick).  It is asked once now: 57 ticks per second, 3D 3.0 ms.
* `perf_flush` wrote data/menu_perf.txt every 16 ticks (a file write per quarter second: a hitch in every menu); every 256 ticks now.
* `m_rot_turns` (mat.h) takes sin / cos from a table of the game's 4096 entries (the same floats as `sinf` / `cosf` of the same angle: bit-identical, the race tests are unaffected).
* `data/menu_perf.txt` now also lists the 3D part of the GE upload, the ported screens' code per tick and `scene_draw`'s transform / sort / emit / GE state split (render.c `rs_*`).

## Update: VDP2 windows, the time attack's transmission page, the records page (October 25 2026)

* `vdpr.c` applies the VDP2 windows 0 / 1 of the scroll screens (rectangles; WCTL bit 0 clear = the layer shows outside the window, set = inside; OR / AND of the two; x in half dots) while the picture is composed.  Not done: line windows, the sprite window, colour calculation.  Before, every layer showed everywhere: the time attack's AT / MT box (revealed by a window cut-out in a covering layer) never appeared after the car's first CROSS (the "press twice" report), a locked course peeked in at the edge of the arcade course select, the records list ran down the whole page instead of its five-row window.  `VDPR_NOWIN=1` (only in the -DPROF test tools) draws without windows: `tools/menu_skipcheck.sh` (both modes) is still bit-identical, and the pictures of about 60 scenarios differ from the window-less ones (boot, ta_*, arc_*, rec_*, opt_*, set_*).
* `car_items` (menu_host.c) draws the unlit brake-light part for the time attack / set-up car too: the PSP draws the race model there, and the panel between the tail lights belongs to that part (the original's own SELECT.MDL car has no such part).
* Records: the list scrolls with L / R (Triangle = up) as in the original, and the yellow arrow below the list shows it can; the car picture and the course name sit at the lower right under the list (see the sprite origin below).

## Update: the first real-PSP numbers (October 25 2026)

data/menu_perf.txt of a real PSP, time attack car page, 31 s: 57 ticks a second (the pacing works) but only 333 pictures (10 a second).  Renderer 12.3 ms per picture (PPSSPP 5.8: a real PSP is about 2.1 times slower than PPSSPP's virtual time), of which the final dot lookup 7.4 ms (the PSP has no cache prefetch: 630 KB of comp / out words are line misses) and the composition 1.6 ms, **but the GE upload 71 ms**: the picture's texture sat in RAM, and the GE reads a RAM texture (linear, unswizzled, bilinear) very slowly (PPSSPP: 0.4 ms).
* `menu_host.c`: the picture's texture and the upper-layer texture are copied to VRAM by the GE (`tex_upload`: `sceGuCopyImage` from `frame.pix` / `frame_hi.pix`, no CPU memcpy) and drawn from there (`TEX_VRAM` 0x110000, `TEXHI_VRAM` 0x180000: the same VRAM the race's off-screen view uses, so the textures are copied again after a race).  Expect about 15 - 20 ms per picture on the real PSP (the dot lookup is the next cost: 7.4 ms; ideas: a palette-only change patches the changed colours instead of looking every dot up again).

## Update: the sprites' origin and the depth buckets (October 25 2026)

* The VDP1 commands the screens write with `draw_sprite` are relative to the local origin that `game_zsort` puts at the head of the command list (0x0601B604 with the table at 0x0601B364: local coordinate (176, 112), the middle of the 352 x 224 picture; the system / user clip is the whole picture).  `vdp1_draw` drew them from (0, 0): the Time Attack car page's caption "TOYOTA CELICA GT-FOUR / DELTA ..." (x = -128: the 28 dots that were left of the picture showed as "CA" / "TA LE" above the car), the Records page's two car pictures over the buttons and the course name between the buttons, the scroll arrows (x = -84: never visible) were all in the wrong place.  `vdp1_draw` starts with `loc_x = 176, loc_y = 112` now.
* `game_zsort` also clears the depth buckets once per pass.  The host cleared them every tick, but a pass is two ticks (two V-blanks): the sprites of the pass were missing in the second tick's picture, which one is drawn depends on the tick parity (PPSSPP showed no Time Attack caption at all, the real PSP showed it half cut).  `m_mkpoly_frame_init` (the start of every pass) calls the host's hook (`sat_frame_init_hook`: `zsort_clear` in menu_host.c; the tests leave it empty, the buckets are compared memory there); a screen without it is cleared after 6 ticks.
* `data/menu_perf.txt` is written only with a `data/dbglog` file now (the Time Attack car page is at full speed on the real PSP: user report).


## The title demo on the real PSP (October 25 2026)

* **The picture's left edge**: the first real-PSP photos of the new build showed a strip at the left of the picture (3 dots of the picture's right edge wrapped in, a red dot at the top left).  Cause: `sceGuCopyImage` (the texture upload to VRAM, October 25) rounds the source address down to 16 bytes and `VdpFrame.pix` sat at offset 12 of the struct (behind `w`, `h`, `same`), so the picture reached the texture 3 dots to the right.  `pix` is aligned to 64 bytes now (vdpr.h).  PPSSPP shows it too (the rows of the capture compared with `data/frame.raw`).
* **The demo's picture**: the software rotation of the sky (RBG0, rebuilt every picture while the camera turns) was 11.5 of the 16 ms per picture in PPSSPP; in the 3D scenes (staff roll, ending, title demo: `photo_split`) it is built in 2 x 2 dot blocks now (`vdpr_rbg_half`): layers 4.6 ms, picture 9.8 ms.
* **The CD music**: the title stops the CD (`FUN_0600F7BE` = the pause) before its demo and plays track 13 after it; `cdda_play` did not clear the pause, so the demo was silent.  A new play clears it.
