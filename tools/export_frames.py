"""M5: build the static test frames.  For each (course, pass) in FRAMES the original game is run to that pass (oracle pilot), its own pipeline builds the frame, and
  extracted/ref/<name>.png            the reference picture (software VDP1; sky not included, see vdp2.py)
  extracted/psp/frames/<name>.txt     what the PSP renderer needs to draw the same view: course index, the game's 3x4 view matrix, the part list
  extracted/psp/frames/index.txt      frame names, one per line

    python tools/export_frames.py [name ...]
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import refframe  # noqa: E402
import objtrace  # noqa: E402
import conv_objects  # noqa: E402
import vdp1  # noqa: E402
import vdp2  # noqa: E402
import hud  # noqa: E402
from saturn_env import COURSES, CAR0  # noqa: E402
from sh2 import s32  # noqa: E402

FRAMES = [
    ("desert_p330", 0, 330), ("desert_p60", 0, 60), ("desert_p1500", 0, 1500), ("desert_p3000", 0, 3000),
    ("forest_p300", 1, 300), ("forest_p1200", 1, 1200), ("forest_p2500", 1, 2500),
    ("desert_p330_car", 0, 330), ("forest_p300_car", 1, 300), ("lake_p600_car", 3, 600),
    ("mount_p400", 2, 400), ("mount_p1500", 2, 1500),
    ("lake_p600", 3, 600), ("lake_p2000", 3, 2000),
    # arcade mode (game type 0): flags, banners, the start object and the check-point gates exist only there
    ("desert_a120_car", 0, 120, 0), ("desert_a1900_car", 0, 1900, 0), ("forest_a700_car", 1, 700, 0), ("lake_a1000_car", 3, 1000, 0), ("mount_a900_car", 2, 900, 0),
    # the desert helicopter (a one-lap race makes its first lap the last one; `lead` passes of object dispatching before the frame make it fly), both game types
    ("desert_a2023_heli", 0, 2023, 0, {"laps": 1, "lead": 360}), ("desert_p2029_heli", 0, 2029, 2, {"laps": 1, "lead": 360}),
]
PSP = os.path.join(ROOT, "extracted", "psp", "frames")


def main():
    from PIL import Image
    want = sys.argv[1:]
    os.makedirs(PSP, exist_ok=True)
    os.makedirs(refframe.REF, exist_ok=True)
    names = []
    for spec in FRAMES:
        name, course, npass = spec[:3]
        gt = spec[3] if len(spec) > 3 else 2
        opt = spec[4] if len(spec) > 4 else {}
        names.append(name)                   # the *_car frames have the player's car (OBJECT.MAD parts as instances, `inst` lines)
        if want and name not in want:
            continue
        log = []
        r, vram, info = refframe.capture(course, npass, game_type=gt, car=name.endswith("_car"), events=True, setup=lambda rr: objtrace.install(rr, log), hud=True, **opt)
        v = vdp1.Vdp1(vram).run(0)
        st = vdp2.build_state(r)
        bg, _ = vdp2.render_background(st)
        Image.fromarray(v.image()).save(os.path.join(refframe.REF, name + "_vdp1.png"))                      # sprite layer only
        Image.fromarray(vdp2.compose_hud(vdp2.compose(v.image(), v.drawn, bg), hud.render(r))).save(os.path.join(refframe.REF, name + ".png"))   # with the VDP2 sky and the HUD layers
        open(os.path.join(refframe.REF, name + ".vram"), "wb").write(vram)
        with open(os.path.join(PSP, name + ".txt"), "w") as f:
            f.write("course %d\n" % course)
            f.write("view " + " ".join("%.9g" % x for x in info["view"]) + "\n")
            idx = info["part_blocks"]
            f.write("parts %d %s\n" % (len(idx), " ".join(str(i) for i in idx)))
            if name.endswith("_car"):           # the car structure fields dispcar reads: src/car.c rebuilds the model draws from them (tools/cartest.py checks that)
                m = r.mem
                F = lambda o: s32(m.r32(CAR0 + o)) / 65536.0
                f.write("car %d %d %d %d %s\n" % (m.r32(CAR0 + 0x384), m.r8(CAR0 + 0x5C), m.r8(CAR0 + 0x5D), m.r16(CAR0 + 0xB4),
                                                " ".join("%.9g" % F(o) for o in (0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x30, 0x38, 0x3C, 0x40, 0x44, 0x48, 0x4C, 0x50, 0x54, 0x58))))
            ei = r.ev_in                        # inputs of the object code (src/event.c re-runs the dispatcher from them: tools/evvec.py checks that)
            f.write("ev %d %d %d %d %d %d %d %d %d %d %d %d\n" % (ei["game_type"], ei["rng"], ei["c5"], ei["seg"], ei["c8"], ei["ca"], ei["x"], ei["y"], ei["z"], ei["yaw"], ei["nseg"], 0))
            f.write("heli %s %s\n" % (ei["heli"].hex(), ei["dust"].hex()))        # the helicopter object and its dust list before the dispatch (src/event.c ev_set_heli)
            for e in log:                       # recorded model draws in the game's order: part (object.names), clip, kind (1 = spray: always drawn, 2 = helicopter: ported), 3x4 view-space matrix
                f.write("inst %d %d %d %s\n" % (e["idx"], 1 if e["clip"] else 0, 2 if e["slot"] == 760 else (1 if e["kind"] == "splash" else 0), " ".join("%.9g" % x for x in e["mat"])))
            for n, nt in sorted(hud.name_tables(r).items()):        # HUD name tables (NBG2 / NBG3): the non-zero cells as index:value (src/hud.c draws them)
                cells = [(i, int(v)) for i, v in enumerate(nt.reshape(-1)) if v]
                f.write("nt%d %d %s\n" % (n, len(cells), " ".join("%d:%x" % c for c in cells)))
            for c in r.hud_calls:               # the VDP1 HUD sprites: tachometer needle (rpm word of the car) and count-down numerals (index, scale): src/event.c ev_hud rebuilds the draws
                f.write(" ".join(str(x) for x in c) + "\n")
            f.write("scr %d %d %d %d\n" % hud.scroll(r))                 # scroll of NBG2 / NBG3: x2 y2 x3 y3
            roll = st.info["roll"] & 0xFFFF
            f.write("sky %d %d\n" % (st.info["yaw"] & 0xFFFF, roll - 0x10000 if roll & 0x8000 else roll))      # roll is signed (the sky turns by roll / 2)
        print("%-14s %s pass %4d: %3d commands, %d parts, speed %.0f km/h" % (name, COURSES[course], npass, v.ncmd, len(info["part_blocks"]), info["speed"] * 108))
    with open(os.path.join(PSP, "index.txt"), "w") as f:
        f.write("\n".join(names) + "\n")


if __name__ == "__main__":
    main()
