"""M5: reference frames.  Runs the original race (oracle, pure-pursuit driver) to a given pass, lets the game's own polygon pipeline build the VDP1 command
table for that pass, and renders it with the software VDP1 (`vdp1.py`).  These images are what the PSP renderer is compared with.

    python tools/refframe.py --course 0 --pass 330 [--type 2] [--car] [--out extracted/ref/desert_p330.png]

Also writes `<out>.vram` (the 512 KB VDP1 VRAM image incl. the command table) and `<out>.json` (pass state: car position / angles, camera matrix,
part list) so the PSP side can reproduce the same view.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import oracle  # noqa: E402
from saturn_env import Race, COURSES, fx  # noqa: E402
import vdp1  # noqa: E402
from sh2 import s32  # noqa: E402

REF = os.path.join(ROOT, "extracted", "ref")


DRAW_ENTRIES = (0x06018908, 0x06018A10, 0x06019870, 0x06019936, 0x0601978C)
SLOT_AREA = (0x060E4000, 0xBE00)                # event slots (the helicopter object at 0x060EFE00 and its dust list 0x060D2000 are not part of it)


def snapshot_slots(r):
    m = r.mem
    return (bytes(m.read(*SLOT_AREA)), m.r32(0x060D0050), bytes(m.read(0x060D0100, 3)))


def restore_slots(r, snap):
    m = r.mem
    for k, b in enumerate(snap[0]):
        m.w8(SLOT_AREA[0] + k, b)
    m.w32(0x060D0050, snap[1])
    for k, b in enumerate(snap[2]):
        m.w8(0x060D0100 + k, b)


def lead_dispatch(r):
    """run the object dispatcher for one pass with the draw functions stubbed out: advances the helicopter (and the dust) like the game does every pass"""
    m = r.mem
    saved = {a: r.cpu.hooks.get(a) for a in DRAW_ENTRIES}
    for a in DRAW_ENTRIES:
        r.cpu.hooks[a] = lambda c: c.ret()
    try:
        r.run_master(0x0601BCB8, r4=0x060AA140, r5=m.r16(0x060AA002))
    finally:
        for a, h in saved.items():
            if h is None:
                r.cpu.hooks.pop(a, None)
            else:
                r.cpu.hooks[a] = h


def capture(course, npass, game_type=2, driver="drive", view=2, car=False, setup=None, events=False, laps=None, lead=0, hud=False):
    r = Race(course, game_type=game_type, render=True, rec_buf=0x060FD400, real_rand=events)
    r.init_render()
    r.load_game_textures()
    if events:                                  # bit 4 of [0x060AA803] = files come from the CD: the race start then loads EVENTTBL.MAD through the loader hook
        r.mem.w8(0x060AA803, r.mem.r8(0x060AA803) | 0x10)
    if setup is not None:
        setup(r)                                # e.g. observation hooks (objtrace.py)
    r.mem.w8(0x060AA01B, view)                  # camera sub-view: 0 = A (in the car), 2 = B (chase camera, the car is drawn)
    if laps is not None:
        r.mem.w16(0x060AA006, laps)             # lap count word: 1 = the last lap is the first, so the desert helicopter appears after ~1,800 passes
    if hud:
        import hud as hudmod
        hudmod.setup(r)                         # CGDATA, palettes, static labels; every pass then runs game_display
    drv = oracle.make_driver(driver, r)
    cmd = None
    snap = snapshot_slots(r) if lead else None
    for i in range(npass - 1):
        cmd = drv.next(r, i)
        if cmd is None:
            break
        r.step(*cmd)
        if hud:
            hudmod.display(r)
        if lead and i >= npass - 1 - lead:
            lead_dispatch(r)
    if lead:
        restore_slots(r, snap)
    cmd = drv.next(r, npass - 1) or (True, False, 0.0)
    vram = r.draw_pass(*cmd, car=car, events=events, hud=hud)
    m = r.mem
    info = {
        "course": course, "pass": npass, "pos": [fx(m.r32(0x060B0000 + o)) for o in (0x18, 0x1C, 0x20)],
        "ang": [fx(m.r32(0x060B0000 + o)) for o in (0x0C, 0x10, 0x14)], "speed": fx(m.r32(0x060B0000 + 0x94)),
        "seg": m.r16(0x060B0060), "block": m.r8(0x060B00F0), "view": [fx(m.r32(0x060D1000 + 4 * k)) for k in range(12)],
        "parts": [m.r32(0x060AA104 + 4 * k) for k in range(m.r16(0x060AA000))],
        "part_blocks": [m.r8(0x060AA140 + k) for k in range(m.r16(0x060AA002))],
        "polys": [m.r16(0x060D14AA), m.r16(0x060D1CAA)],
    }
    return r, vram, info


def main():
    a = sys.argv[1:]

    def opt(n, d):
        return type(d)(a[a.index(n) + 1]) if n in a else d

    course, npass, gt = opt("--course", 0), opt("--pass", 330), opt("--type", 2)
    car = "--car" in a
    out = opt("--out", os.path.join(REF, "%s_p%d.png" % (COURSES[course].lower(), npass)))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    r, vram, info = capture(course, npass, gt, car=car)
    v = vdp1.Vdp1(vram).run(0)
    img = v.image()
    from PIL import Image
    Image.fromarray(img).save(out)
    open(out[:-4] + ".vram", "wb").write(vram)
    json.dump(info, open(out[:-4] + ".json", "w"), indent=1)
    print("%s pass %d: %d commands drawn, %d px -> %s" % (COURSES[course], npass, v.ncmd, int(v.drawn.sum()), out))


if __name__ == "__main__":
    main()
