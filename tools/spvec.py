"""M6 (spray / dust objects): test vectors for src/spray.c - what the game's own car display (the spawner FUN_0601D0E6) and the object list run (0x0601D410) do, pass after pass.

    python tools/spvec.py [course] [game_type] [passes] [--car=N]      -> extracted/oracle/sp_<course>_<type>.vec

The oracle (pilot, chase camera) runs a race with the car display un-stubbed (the polygon draw functions are replaced by recorders).  Per pass the vector holds
    u32 spawned        the car display called the spawner this pass (it does when the car is in the near / middle detail tier)
    u32 rng            the shared random number generator (0x060D0050) before the spawner ran
    u8  car[0x400]     the car slot as the spawner saw it (only when spawned)
    u8  out[0x24]      car +0x3C0 .. +0x3E3 after the spawner (the local velocity / wheel point work fields; only when spawned)
    u8  pool[0x7F8]    the master list after the spawner: header 0x060D0140 (0x18 bytes), the 28 active pointers (0x060D3000), the 28 free pointers (0x060D3100), the 28 objects of 64 bytes (0x060D3200)
    i32 view[12]       the camera matrix of the pass (0x060D1000, 16.16)
    u32 ran            0x0601D410 ran (car byte +0xF5 < 8)
    u32 ndraws; ndraws x { i32 part; i32 r6; i32 pos[3] }     splash draws of the list run, in order (view space position of the matrix, scale)
    u8  pool2[0x7F8]   the list again after the run (only when ran)
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import oracle  # noqa: E402
import conv_objects  # noqa: E402
from saturn_env import Race, VIEW0, CAR0  # noqa: E402
from sh2 import s32  # noqa: E402

SPAWN = 0x0601D0E6
RUN = 0x0601D410
DISP_CAR = 0x060101E8
DRAWS = (0x06018908, 0x06018A10, 0x06019870, 0x06019936, 0x0601978C, 0x060186FC, 0x06010654)
SPLASH = 0x06019936
OUT = os.path.join(ROOT, "extracted", "oracle")


def pool(m):
    return m.read(0x060D0140, 0x18) + m.read(0x060D3000, 0x70) + m.read(0x060D3100, 0x70) + m.read(0x060D3200, 0x700)


def main():
    a = sys.argv[1:]
    opts = [x for x in a if x.startswith("--")]
    a = [x for x in a if not x.startswith("--")]
    course = int(a[0]) if a else 0
    gt = int(a[1]) if len(a) > 1 else 0
    npass = int(a[2]) if len(a) > 2 else 1500
    part_index = {ad: i for i, (ad, _) in enumerate(conv_objects.part_symbols() + conv_objects.animal_symbols())}
    car = [int(x.split("=")[1]) for x in opts if x.startswith("--car=")]
    car = car[0] if car else 0
    r = Race(course, game_type=gt, render=True, rec_buf=0x060FD400, real_rand=True, cartype=car)
    r.init_render()
    r.load_game_textures()
    m = r.mem
    m.w8(0x060AA01B, 2)
    r.cpu.hooks.pop(DISP_CAR, None)
    log = []
    cap = {}

    def rec(c):
        if c.pc == SPLASH:
            log.append((part_index.get(c.r[5], -1), s32(c.r[6]), [s32(m.r32(c.r[4] + o)) for o in (0x0C, 0x1C, 0x2C)]))
        c.ret()
    for ad in DRAWS:
        r.cpu.hooks[ad] = rec

    def spawn_hook(c):
        cap["car"] = bytes(m.read(CAR0, 0x400))
        cap["rng"] = m.r32(0x060D0050)
        c.step_orig()
    r.cpu.hooks[SPAWN] = spawn_hook
    drv = oracle.make_driver("drive", r)
    out = bytearray()
    frames = 0
    for i in range(npass):
        cmd = drv.next(r, i)
        if cmd is None:
            break
        cap.clear()
        r.step(*cmd)
        spawned = "car" in cap
        out += struct.pack("<II", int(spawned), cap.get("rng", 0))
        if spawned:
            out += cap["car"] + m.read(CAR0 + 0x3C0, 0x24) + pool(m)
        out += struct.pack("<12i", *[s32(m.r32(VIEW0 + 4 * k)) for k in range(12)])
        ran = m.r8(CAR0 + 0xF5) < 8
        log.clear()
        if ran:
            r.run_master(RUN)
        out += struct.pack("<II", int(ran), len(log))
        for part, r6, pos in log:
            out += struct.pack("<ii3i", part, r6, *pos)
        if ran:
            out += pool(m)
        frames += 1
    path = os.path.join(OUT, "sp_%d_%d%s.vec" % (course, gt, "_car%d" % car if car else ""))
    with open(path, "wb") as fo:
        fo.write(b"SVEC" + struct.pack("<III", frames, course, gt) + out)
    print("%s: %d passes, %d bytes" % (path, frames, os.path.getsize(path)))


if __name__ == "__main__":
    main()
