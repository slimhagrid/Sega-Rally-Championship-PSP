# Sega Rally PSP

A native port of **Sega Rally Championship** (Sega, 1995, Sega Saturn, USA release) to the Sony PSP.

It is **not an emulator**: the game's engine is written again in C for the PSP - the driving physics, collisions, camera, the other cars, the scenery objects, the HUD, the
race flow, every menu screen, the intro and the ending - and the game's own data (courses, cars, textures, sound, music) is converted from **your own copy of the disc**.

> ## Read this first - what this repository is, and is not
>
> * **This repository contains no part of the game.** No disc image, no program code, graphics, models, courses, sound, music, text or any other data of Sega Rally
>   Championship are included, and none may ever be added (see `.gitignore`).
> * **You need your own copy of Sega Rally Championship for the Sega Saturn (USA)**, as a disc you own, ripped by yourself to a BIN/CUE image. The tools in `tools/`
>   read that image and write the converted data to your own computer.
> * The converted data and the finished game folder (`EBOOT.PBP` + `data/`) **are derived from Sega's copyrighted work. Do not upload, share or redistribute them.**
>   Only the source code of this repository is free to use, and it is released into the public domain (see `LICENSE`: the Unlicense).
> * This is an unofficial, non-commercial fan project. It is not affiliated with, authorised or endorsed by Sega or Sony. *Sega Rally Championship*, *Sega*,
>   *Toyota*, *Lancia* and all related names, cars and artwork are the property of their respective owners.
> * Nothing here is legal advice. Check what is allowed where you live.

## What you need

| | |
|---|---|
| **The game** | Sega Rally Championship (USA) for the Sega Saturn (product MK-81207, V1.006) as a BIN/CUE rip of your own disc: 1 data track (MODE1/2352) and the 16 CD-audio tracks. Other releases are not supported |
| **A PSP** with custom firmware (PRO, ARK-4 ...) that runs homebrew, or **PPSSPP** on PC / Android | tested on a PSP and in PPSSPP 1.20; built for the extra RAM of the PSP-2000 / 3000 / Go (`MEMSIZE 1`), the PSP-1000 has not been tried |
| **A Memory Stick** | about 100 MB free |
| **Linux, or WSL2 (Ubuntu) on Windows** | to run the build (and the one step that needs `gcc`) |
| **Python 3.9+** | `pip install -r requirements.txt` (numpy, pillow, lameenc, scipy) |
| **gcc, zlib** | only for the step that renders the menu songs (`zlib1g-dev` on Ubuntu) and for the host tests |
| **[Ghidra](https://ghidra-sre.org/) 12.1.x and a JDK 21** | one step (about 4 minutes) finds where the functions of the game's program are, so that the converted data carries the program's data tables but not its code; set `GHIDRA_HOME` (and `JAVA_HOME`) |
| **The PSP toolchain** | [pspdev](https://pspdev.github.io/) (`psp-cmake`, `psp-gcc`, `pack-pbp` ...) |

## Step by step

### 1. Rip your disc

Make a BIN/CUE image of your own disc with any ripper that writes the audio tracks (one BIN per track or one BIN for the whole disc). The data track of the USA
disc has 13,328 sectors; there are 16 audio tracks (tracks 2 to 17). Keep the `.cue` and the `.bin` files together.

### 2. Install the tools

```bash
# Python packages
pip install -r requirements.txt

# PSP toolchain: follow https://pspdev.github.io/installation.html, then
export PSPDEV=$HOME/pspdev            # wherever you installed it
export PATH=$PATH:$PSPDEV/bin

# Ghidra (for the step that finds the program's functions)
export GHIDRA_HOME=$HOME/ghidra_12.1.3_PUBLIC
export JAVA_HOME=/path/to/jdk-21        # or have java 21 on the PATH
```

**Windows (the route this was developed and tested on):** install Python 3.9+ for Windows and the packages above, install WSL2 with Ubuntu and the PSP toolchain
(+ `gcc`, `zlib1g-dev`) inside it. Run `python tools/make_data.py ...` from a normal Windows shell (the one step that needs `gcc` is called through `wsl`
automatically) and `bash tools/build.sh` inside WSL (your drives appear as `/mnt/c/...`; from PowerShell:
`wsl -d Ubuntu -- bash /mnt/c/path/to/sega-rally-psp-release/tools/build.sh`). Do the WSL call from PowerShell or `cmd`, not from Git Bash (it rewrites `/mnt/...` paths). Ghidra and the JDK are the Windows ones: in PowerShell
`$env:GHIDRA_HOME = "C:	ools\ghidra_12.1.3_PUBLIC"; $env:JAVA_HOME = "C:	ools\jdk-21"` before `make_data.py`.

**Linux:** the same commands run natively. This route has not been tested by the author yet, please report problems.

### 3. Convert the game data

```bash
python tools/make_data.py "path/to/Sega Rally Championship (USA).cue"
```

This runs, in order: disc extraction (the 185 files, the CD tracks as WAV) -> the symbol table of the game's debug file `RALLY.DBG` -> the function boundaries of the program (Ghidra, headless) -> physics tables -> the four courses
(collision data, parts, textures, sky panoramas) -> cars, signs, people, animals -> scenery objects -> HUD characters and the HUD's start state (the original's own
display code runs in an SH-2 interpreter) -> every file the menu screens load -> the 3D parts of the menus, the ceremony and the name entry -> the CD tracks as MP3 ->
the menu songs, rendered with the original sound driver (it runs the original's 68000 sound code in a C interpreter and records what it plays).
In all, about 10 minutes on a normal PC.
`python tools/make_data.py --list` shows the steps and which are done, `--only NAME,NAME` runs some, `--skip bgm` leaves the songs out for a quick test (the menus are then
silent), `--force` redoes done steps.

Everything lands in `extracted/` (git-ignored). `tools/make_data.py` can be run again at any time: steps that are done are skipped.

`art/psp_pad.png` (640 x 400, magenta = transparent) is the one picture that is part of this repository: the picture of the PSP that the step `pspad` turns into the replacement for the
Saturn pad on the KEY CONFIG page. You can put your own at that path before the step runs.

### 4. Build

```bash
bash tools/build.sh
```

This compiles `EBOOT.PBP` and stages the game folder `dist/SegaRallyPSP/` (`EBOOT.PBP`, `README.txt`, `data/`).
Optional: put your own `ICON0.png` (144x80), `PIC1.png` (480x272), `PIC0.png`, `SND0.AT3` into `pbp/` before building for the XMB icon and background.
No game artwork is included here, and the game's own art must not be committed.

### 5. Put it on the PSP

1. Copy the whole folder `dist/SegaRallyPSP` to the Memory Stick as `ms0:/PSP/GAME/SegaRallyPSP` (so that `EBOOT.PBP` and `data/` are in that folder).
2. Start **Sega Rally PSP** from *Game -> Memory Stick* in the XMB.
3. Records, your car set-ups and the options are saved in its `data/` folder (`BUP0.BIN`, `BUP1.BIN`, `keycfg.txt`).

**PPSSPP:** copy the same folder to `<PPSSPP memstick>/PSP/GAME/SegaRallyPSP`, or open `EBOOT.PBP` with *File -> Load*.

## Controls

Menus: D-pad or analog nub = move, **CROSS = confirm** (the Saturn's C), CIRCLE = back (B), SQUARE = A, TRIANGLE = X, SELECT = Y, L / R, START = start. The pictures that show Saturn
buttons ("Press C button ...") are redrawn with PSP button names.

Racing: **Options > KEY CONFIG** chooses one of three layouts (the D-pad left / right and the analog nub always steer):

| | TYPE A (default) | TYPE B | DIY |
|---|---|---|---|
| Accelerate | R | CROSS | your own |
| Brake | L or SQUARE | SQUARE or CIRCLE | |
| Shift up / down | TRIANGLE / CROSS | R / L | |
| Change view | CIRCLE | TRIANGLE | |

DIY: move to DIY on the KEY CONFIG page and press CROSS; UP / DOWN choose a function, CROSS then the new buttons for it, START to finish (SELECT = start again from type A).
START in a race opens the original's pause menu (EXIT? NO / YES / RESTART). In a replay CROSS, CIRCLE or SQUARE switch between the TV cameras and the chase camera; TRIANGLE, L or R change its view.

## What is in the game

The port plays the whole game of the disc except the 2 Player Battle (a PSP has one pad; the mode select skips that entry, as the original does without a second pad).

* **Arcade**: the Championship (Desert, Forest, Mountain, and the Lake stage when you win on Mountain), SELECT GAME, car and gearbox choice (Celica / Delta, automatic / manual,
  the unlockable Stratos), the 15-car field, checkpoints, the time limit, the goal, name entry, ranking, the ceremony and the ending, the game over screen.
* **Practice**: one stage against the practice rival, the staff roll after a win.
* **Time Attack**: three laps or free run on every stage, the ghost of your best run, input recording, replays with the TV cameras, the pause menu.
* **Car Settings** (the set-up screen with the 3D cars), **Records** (with the saved data), **Options** (sound, KEY CONFIG, pad, the sound tests).
* The **intro** (SEGA logo, title, the demo run on three stages, ranking pages), the helicopter of the Desert's last lap, the start line, spray and dust, the co-driver's voice.
* **Sound**: the original's 68000 sound driver and SCSP are run for the engine, tyre, wall and checkpoint sounds and the voice; the menu songs are rendered offline with that
  driver; the 16 CD tracks are played as MP3 by the PSP's hardware decoder (made for the PSP's small speaker: bass and drums cut a little).

Not built: the **2 Player Battle** (split screen, VS rules, `docs/twoplayer.md` is a survey), the practice mode's secret ghost car, a few small sounds (see `docs/TECHNICAL.md`).

## How it works, in short

* **Converters** (`tools/conv_*.py` and friends, Python) take the disc apart (`extract_disc.py`, `datfile.py`: the game's scatter-load files and its byte-pair compression), read
  the game's formats (the model / texture / VDP1 command formats are in `docs/render_pipeline.md`) and write compact, PSP-friendly packs (`.rcp` parts, `.rct` collision data, `.rbg` sky,
  `.blk` memory images of the files the menus load, `.bgm` songs).
* **The game logic is written again in C** (`src/`), subsystem by subsystem, from the original program read in a disassembler: `drive.c` / `camera.c` / `coli.c` (physics, collisions,
  camera), `opp.c` (the other cars), `event.c` (scenery, helicopter), `car.c`, `render.c` (a painter's-algorithm renderer for the game's polygons on the PSP's GE), `hudgen.c` / `hud.c`,
  `sndev.c` / `snd_lib.c` (the sound calls), `play.c` (the race loop). The menu screens (`menu_*.c`) are exact ports that run on a model of the Saturn's memory (`sat*.c`) with a software VDP1 / VDP2
  renderer (`vdpr.c`) - the original's screen code is ported function by function and compared tick by tick.
* **Sound**: `m68k.c` + `scsp.c` + `snd.c` run the original 68000 driver and a model of the SCSP; `bgm.c` plays songs rendered offline with it; `cdda.c` plays the CD tracks.
* **Verification** (`tools/oracle*.py`, `tools/sh2.py`, `tools/test_*`): the original's own code runs in an SH-2 interpreter on your disc's data (the oracle); each C subsystem is compared with it:
  car state after every frame on ~95,000 frames of full races, the other cars, scenery draw lists, the HUD name tables, 158 menu scenarios (memory identical after every tick), the sound calls.

See `docs/TECHNICAL.md` for the overview and the other documents in `docs/` for the details.

## Documentation for further development

All the working documents of the port are included in `docs/`, as they were written while the game was taken apart and rebuilt, so that anyone who wants to extend, fix or fork the
project does not have to start from the disc: `docs/TECHNICAL.md` is the entry point (pipeline, source map, how it was verified, what is not ported); the rest are the detailed notes.

| Document | What it holds |
|---|---|
| `physics.md`, `collision_world_ai.md`, `render_pipeline.md`, `vdp2_background.md`, `oracle.md` | the analysis of the original's driving model, collision / track data, the other cars' AI, the polygon pipeline and model formats, the sky, and the SH-2 interpreter used as the reference |
| `drive.md`, `opponents.md`, `events.md`, `hud.md` | how each ported subsystem works and how it was checked against the original |
| `menu_port_guide.md`, `menus.md`, `timeattack.md`, `menu_perf.md` | the method for porting a screen exactly and proving it, every screen and its data, Time Attack / ghost / replays, the speed work for a real PSP |
| `sound.md` | the original's 68000 sound driver and library, what is ported, the audio cost work, and the findings from a real PSP |
| `twoplayer.md` | a survey of the 2-player modes: the one part of the game that is **not** ported, with what a port would need |

The notes use evidence tags (**[V]** verified against the original, **[I]** inferred, **[U]** unknown) and name the original's functions and addresses, so that a claim can be checked against
your own copy with a disassembler (the `tools/ghidra/` scripts set up a Ghidra project). 

## Testing (optional)

The host tests need the data of your disc and, for most of them, traces recorded by the oracle first (`tools/oracle_*.sh`, hours of Python) - see `docs/TECHNICAL.md`, "Tests".
`tools/test_drive_all.sh` and `tools/test_menu_fast.sh` are the two big regressions.

## Known differences from the Saturn version

* The 2 Player Battle is missing (see above).
* Pictures are drawn by the PSP's GE: the Saturn's VDP1 maps textures differently on distorted quads, so edges and some thin polygons differ by a pixel; the semi-transparent car windows are
  drawn as real alpha instead of a checkerboard.
* The ending replays your own last stage on the real physics with the rival cars (like the original) at a lower frame rate on the PSP.

## Contributing

Pull requests are welcome for the source code. **Never commit anything derived from the game** (data, dumps, disassembly listings, extracted text, models, sound; whole-screen screenshots are fine in
issues but not in the repository). `.gitignore` already excludes the usual places; please check `git status` before every commit.

## License

The source code in this repository is released into the public domain under the [Unlicense](https://unlicense.org): use it, change it, sell it, no attribution needed. That covers
this code only, never the game or anything made from your copy of it.

## Credits

Sega Rally Championship is a game by Sega (AM3). This port is a fan project that needs, and respects, the original. Built with the [pspdev](https://github.com/pspdev) toolchain;
the analysis used [Ghidra](https://ghidra-sre.org/) (SuperH-2 support), the converters use [NumPy](https://numpy.org/), [Pillow](https://python-pillow.org/), [SciPy](https://scipy.org/) and
[LAME](https://lame.sourceforge.io/) (through `lameenc`). It was tested on a real PSP.
