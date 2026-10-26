"""M5 (event objects): run the game's object / event dispatcher in the oracle and record every draw it makes.

    python tools/evtrace.py [course] [pass]

`init_events`: what the race start does (FUN_0601BC94: EVENTTBL.MAD through the CD hook, group records at 0x060D0C00, slots at 0x060E4000, FUN_0601DC14).
`run_events`: what game_ctrl step 6 does each frame (FUN_0601BCB8 with the visible block list 0x060AA140 / count 0x060AA002, master half here, slave half through the job hook).
The draw functions are observed: draw_polygon 0x06018908, draw_clip_polygon 0x06018A10, draw_color_polygon 0x0601978C, draw_people_polygon 0x06019870, draw_splash_polygon 0x06019936.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import oracle  # noqa: E402
from saturn_env import Race, VIEW0  # noqa: E402
from sh2 import s32  # noqa: E402

DRAWS = {0x06018908: "poly", 0x06018A10: "clip", 0x0601978C: "color", 0x06019870: "people", 0x06019936: "splash"}
OBJ_LO, OBJ_HI = 0x060F0000, 0x06100000


def init_events(r):
    m = r.mem
    flags = m.r8(0x060AA803)
    m.w8(0x060AA803, flags | 0x10)
    try:
        r.run_master(0x0601BC94)
    finally:
        m.w8(0x060AA803, flags)


def install(r, log):
    m = r.mem

    def hook(c):
        kind = DRAWS[c.pc]
        part = c.r[5]
        e = {"kind": kind, "part": part, "r6": c.r[6], "r7": c.r[7], "cpu": 0 if m.master else 1, "gbr": c.gbr}
        if kind in ("people", "splash"):
            e["obj"] = [s32(m.r32(c.r[4] + o)) / 65536.0 for o in (0x0C, 0x1C, 0x2C)]
        else:
            e["mat"] = [s32(m.r32(c.r[4] + 4 * k)) / 65536.0 for k in range(12)]
        if r.in_frame and OBJ_LO <= part < OBJ_HI:
            log.append(e)
        c.step_orig()
    for a in DRAWS:
        r.cpu.hooks[a] = hook


def run_events(r):
    m = r.mem
    cnt = m.r16(0x060AA002)
    r.run_master(0x0601BCB8, r4=0x060AA140, r5=cnt)


def main():
    import refframe
    a = [x for x in sys.argv[1:] if not x.startswith("-")]
    course = int(a[0]) if a else 0
    passes = [int(x) for x in a[1:]] or [330]
    sym = {}
    for line in open(os.path.join(ROOT, "extracted", "analysis", "symbols.tsv")):
        pp = line.strip().split(chr(9))
        if len(pp) >= 3:
            sym[int(pp[0], 16)] = pp[2]
    for npass in passes:
        log = []
        r, vram, info = refframe.capture(course, npass, car=True, events=True, setup=lambda rr: install(rr, log))
        print("course %d pass %d: visible blocks %s, %d event / car draws" % (course, npass, info["part_blocks"], len(log)))
        for e in log:
            print("   %-6s %-22s %s r6=%08x" % (e["kind"], sym.get(e["part"], hex(e["part"])), e.get("obj") or [round(x, 2) for x in e["mat"][3::4]], e["r6"]))


if __name__ == "__main__":
    main()
