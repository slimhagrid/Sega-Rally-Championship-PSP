"""M5 (objects): record every model draw the game makes for one frame.

The polygon maker's draw functions are observed at their entry (observe only, the real draw still runs): draw_polygon 0x06018908 (cars, flags, animals ...; the slave job of
DRAW_POLYGON ends here too), draw_clip_polygon 0x06018A10 (gates, poles), draw_color_polygon 0x0601978C, draw_people_polygon 0x06019870 (spectators, swans: a screen aligned quad at the
view-space position of the object, mirrored by the scale r6 = +-1) and draw_splash_polygon 0x06019936 (spray: the quad is squashed vertically by r6 / 65536).  Every draw becomes an
*instance*: object part (number in object.rcp: OBJECT.MDL parts first, then the ANIMAL2 ones), clip flag, kind and a 3x4 view-space matrix in the units of tools/conv_objects.py
(billboards: diag(scale, 1, 1) / diag(1, scale, 1) with the view position as translation).  Only draws made while Race.in_frame is set are kept.

    python tools/objtrace.py [course] [pass]
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import conv_objects  # noqa: E402
import refframe  # noqa: E402
from sh2 import s32  # noqa: E402

DRAWS = {0x06018908: "poly", 0x06018A10: "clip", 0x0601978C: "color", 0x06019870: "people", 0x06019936: "splash"}
OBJ_LO, OBJ_HI = 0x060F0000, 0x06100000
ANIMAL_LO, ANIMAL_HI = 0x06098000, 0x060A0000


def part_index():
    return {a: i for i, (a, _) in enumerate(conv_objects.part_symbols() + conv_objects.animal_symbols())}


def symbols():
    """address -> name of the object / animal parts"""
    return {a: n for a, n in conv_objects.part_symbols() + conv_objects.animal_symbols()}


def install(r, log):
    """observe hooks on the draw functions; appends dicts {kind, part (address), idx, clip, mat, slot, r6} to log"""
    m = r.mem
    index = part_index()

    def hook(c):
        kind = DRAWS[c.pc]
        part = c.r[5]
        if r.in_frame and (OBJ_LO <= part < OBJ_HI or ANIMAL_LO <= part < ANIMAL_HI):
            if kind in ("people", "splash"):
                x, y, z = (s32(m.r32(c.r[4] + o)) / 65536.0 for o in (0x0C, 0x1C, 0x2C))
                sx, sy = (float(s32(c.r[6])), 1.0) if kind == "people" else (1.0, s32(c.r[6]) / 65536.0)
                mat = [sx, 0.0, 0.0, x, 0.0, sy, 0.0, y, 0.0, 0.0, 1.0, z]
            else:
                mat = [s32(m.r32(c.r[4] + 4 * k)) / 65536.0 for k in range(12)]
            log.append({"kind": kind, "part": part, "idx": index.get(part, -1), "clip": kind == "clip", "mat": mat, "r6": c.r[6], "r7": c.r[7],
                        "cpu": 0 if m.master else 1, "slot": (c.gbr - 0x060E4000) // 0x40})
        c.step_orig()
    for a in DRAWS:
        r.cpu.hooks[a] = hook


def capture(course, npass, car=True, events=False):
    log = []
    r, vram, info = refframe.capture(course, npass, car=car, events=events, setup=lambda rr: install(rr, log))
    return r, vram, info, log


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("-")]
    course = int(a[0]) if a else 0
    npass = int(a[1]) if len(a) > 1 else 330
    r, vram, info, log = capture(course, npass, events="--events" in sys.argv)
    sym = symbols()
    print("%d object draws" % len(log))
    for e in log:
        print("%-7s %-24s cpu=%d  t=(%.3f %.3f %.3f)" % (e["kind"], sym.get(e["part"], "%08x" % e["part"]), e["cpu"], e["mat"][3], e["mat"][7], e["mat"][11]))


if __name__ == "__main__":
    main()
