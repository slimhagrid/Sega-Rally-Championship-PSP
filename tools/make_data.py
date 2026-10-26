#!/usr/bin/env python3
"""Convert everything the PSP game needs from YOUR OWN copy of the Sega Rally Championship (Saturn, USA) disc:

    python tools/make_data.py "path/to/Sega Rally Championship (USA).cue"

The cue sheet may be left out when the disc image sits in "Sega Rally Championship (USA)/" next to tools/.  Everything is written below extracted/ (git-ignored):
extracted/disc (the files of the disc), extracted/audio (the CD tracks as WAV), extracted/analysis (symbols), extracted/psp (what tools/build.sh copies into the game folder as data/).

    python tools/make_data.py --list                  the steps and what each makes
    python tools/make_data.py CUE --only menu,cdda    run only these steps (names from --list)
    python tools/make_data.py CUE --skip bgm          leave a step out (the menu songs are the slow one; without them the menus are silent)
    python tools/make_data.py --check                 which steps have their output already

A step that fails stops the run; run it again after fixing the cause (steps that are done are skipped unless --force).  Python 3.9+, numpy, pillow, lameenc, scipy (pip install -r requirements.txt);
the step `functions` needs Ghidra 12.1.x and a JDK 21 (GHIDRA_HOME, JAVA_HOME), the step `bgm` gcc (and zlib) on Linux or inside WSL.
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PSP = os.path.join(ROOT, "extracted", "psp")
COURSES = ("desert", "forest", "lake", "mount")


def course(*names):
    return [os.path.join("course", n) for n in names]


# name, what, command (python script + args, or "bash:script"), the files that show that the step is done (relative to extracted/psp unless they start with extracted/)
STEPS = [
    ("extract", "the files of the disc and the CD tracks as WAV (extracted/disc, extracted/audio)", ["extract_disc.py", "{cue}"], ["extracted/disc/catalog.txt", "extracted/audio/track02.wav"]),
    ("symbols", "RALLY.DBG decoded: the symbol table that names the model parts (extracted/analysis)", ["dbg_parse.py"], ["extracted/analysis/symbols.tsv"]),
    ("functions", "where the functions of the game's program are (extracted/analysis/functions.tsv): Ghidra headless, about 7 minutes; needs GHIDRA_HOME (+ JAVA_HOME)", ["ghidra_import.py"], ["extracted/analysis/functions.tsv"]),
    ("phys", "the car physics and race flow tables (course/phys.dat)", ["conv_phys.py"], course("phys.dat")),
    ("track", "collision / block / slice data of the four courses (course/*.rct)", ["conv_track.py"], course(*(n + ".rct" for n in COURSES))),
    ("parts", "the four courses as lists of parts (course/*.rcp)", ["conv_parts.py"], course(*(n + ".rcp" for n in COURSES))),
    ("sky", "the race background panoramas (course/*.rbg; runs the game's own VDP2 set-up code in the SH-2 interpreter)", ["conv_bg.py"], course(*(n + ".rbg" for n in COURSES))),
    ("objects", "cars, signs, people, animals, flags (course/object.rcp)", ["conv_objects.py"], course("object.rcp", "object.names")),
    ("cars", "the car display tables (course/cars.dat)", ["conv_cars.py"], course("cars.dat")),
    ("events", "the scenery objects of the courses (course/events.dat)", ["conv_events.py"], course("events.dat")),
    ("hud", "the race HUD's characters and palettes (course/hud.dat)", ["conv_hud.py"], course("hud.dat")),
    ("hudinit", "the HUD's start state (course/hud_init.dat, hude_init.dat; runs the original's display set-up in the interpreter)", ["oracle.py", "hudinit"], course("hud_init.dat", "hude_init.dat")),
    ("hudend", "the goal / game over screens' characters (course/hude_*.dat)", ["conv_hudend.py"], course("hude_f.dat", "hude_cr_desert.dat", "hude_pr_mount.dat")),
    ("menu", "every file the menu screens load, in the form the console's memory has it (menu/*.blk, menu/*.raw)", ["conv_menu.py"], [os.path.join("menu", "ROM.blk"), os.path.join("menu", "SELECT.MAD.blk")]),
    ("select", "the 3D parts of the menu screens (course/select.rcp)", ["conv_select.py"], course("select.rcp")),
    ("ending", "the confetti of the ceremony (course/ending.rcp)", ["conv_ending.py"], course("ending.rcp")),
    ("endcar", "the crew of the ceremony's car (course/endcar.rcp)", ["conv_endcar.py"], course("endcar.rcp")),
    ("alpha", "the letters of the name entry (course/alpha.rcp)", ["conv_alpha.py"], course("alpha.rcp")),
    ("pspad", "optional: your own PSP picture for the KEY CONFIG page, art/psp_pad.png (menu/psp_pad.bin)", ["conv_pspad.py"], [os.path.join("menu", "psp_pad.bin")]),
    ("cdda", "the CD tracks as MP3 for the PSP's decoder (cd/*.mp3)", ["conv_cdda.py"], [os.path.join("cd", "track02.mp3"), os.path.join("cd", "track17.mp3")]),
    ("bgm", "the menu songs, rendered with the original sound driver (snd/*.bgm; needs gcc, bash)", ["bash:conv_bgm.sh"], [os.path.join("snd", "bgm.idx"), os.path.join("snd", "end_0.bgm")]),
]


def done(step):
    for f in step[3]:
        p = os.path.join(ROOT, f) if f.startswith("extracted") else os.path.join(PSP, f)
        if not os.path.exists(p):
            return False
    return True


def wsl_path(p):
    out = subprocess.run(["wsl", "wslpath", "-a", p.replace("\\", "/")], stdout=subprocess.PIPE, universal_newlines=True)
    return out.stdout.strip()


def run_step(step, cue):
    name, what, cmd, outs = step
    t0 = time.time()
    if cmd[0].startswith("bash:"):
        script = "tools/" + cmd[0][5:]
        if os.name == "nt":
            line = ["wsl", "--", "bash", "-c", "cd '%s' && bash %s" % (wsl_path(ROOT), script)]
        else:
            line = ["bash", script]
        r = subprocess.run(line, cwd=ROOT)
    else:
        args = [a.replace("{cue}", cue or "") for a in cmd[1:]]
        if cmd[0] == "extract_disc.py" and not cue:
            args = []
        r = subprocess.run([sys.executable, os.path.join(HERE, cmd[0])] + args, cwd=ROOT)
    return r.returncode, time.time() - t0


def main():
    args = sys.argv[1:]
    if "-h" in args or "--help" in args:
        print(__doc__)
        return 0
    only = skip = None
    force = "--force" in args
    if "--only" in args:
        only = args[args.index("--only") + 1].split(",")
    if "--skip" in args:
        skip = args[args.index("--skip") + 1].split(",")
    pos = []
    i = 0
    while i < len(args):
        if args[i] in ("--only", "--skip"):
            i += 2
            continue
        if not args[i].startswith("--"):
            pos.append(args[i])
        i += 1
    cue = os.path.abspath(pos[0]) if pos else ""
    if "--list" in args or "--check" in args:
        for s in STEPS:
            print("%-9s %-5s %s" % (s[0], "done" if done(s) else "-", s[1]))
        return 0
    names = [s[0] for s in STEPS]
    for n in (only or []) + (skip or []):
        if n not in names:
            print("unknown step '%s' (python tools/make_data.py --list)" % n)
            return 2
    for s in STEPS:
        name = s[0]
        if only is not None and name not in only:
            continue
        if skip is not None and name in skip:
            continue
        if name == "pspad" and not os.path.exists(os.path.join(ROOT, "art", "psp_pad.png")):
            print("[pspad] art/psp_pad.png is not there: the KEY CONFIG page keeps the Saturn pad picture (optional)")
            continue
        if done(s) and not force and not (only and name in only):
            print("[%s] done already" % name)
            continue
        print("[%s] %s" % (name, s[1]))
        sys.stdout.flush()
        rc, dt = run_step(s, cue)
        if rc != 0 or not done(s):
            print("[%s] FAILED (exit %d, %.0f s); fix it and run the same command again" % (name, rc, dt))
            return 1
        print("[%s] ok (%.0f s)" % (name, dt))
    print("\nall done. Next: bash tools/build.sh   (builds EBOOT.PBP and stages dist/SegaRallyPSP)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
