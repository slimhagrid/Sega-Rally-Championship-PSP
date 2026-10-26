"""M5 (objects): test vectors for src/car.c - what the game's own dispCar draws for a given car state, camera matrix and mode.

    python tools/cartest.py [ncases]        -> extracted/oracle/cardisp.vec

The oracle runs a race (pilot) and every few passes calls `dispcar::dispCar` (0x060101E8) with a (partly randomised) car structure and camera: random car model /
damage word / brake light / steering / wheel spin / suspension / ground heights / extra view depth (so every tier from near to beyond 200 m occurs) / lateral offset
(some cars fall off screen) / mode 0-3.  The draw functions (DRAW_POLYGON 0x060186FC, draw_polygon 0x06018908, the statistics stub 0x06010654) are replaced by recorders:
the vector holds the part and the 3x4 view-space matrix of every draw, in order.  tools/test_car.c replays the cases through src/car.c.

Vector file (little endian): u32 magic 'CVEC', u32 ncases, then per case
    u32 mode; f32 view[12]; f32 ang[3], pos[3], steer, spin, susp[4], ground[4]; i32 type, brake, damage;   u32 ninst; ninst x { i32 part; f32 m[12] }
"""
import os
import random
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import oracle  # noqa: E402
import conv_objects  # noqa: E402
from saturn_env import Race, CAR0, VIEW0  # noqa: E402
from sh2 import s32  # noqa: E402

DISP_CAR = 0x060101E8
DRAW_FNS = (0x060186FC, 0x06018908, 0x06010654)
OUT = os.path.join(ROOT, "extracted", "oracle", "cardisp.vec")


def fx(v):
    return s32(v) / 65536.0


def main():
    ncases = int(sys.argv[1]) if len(sys.argv) > 1 else 600
    rng = random.Random(1995)
    part_index = {a: i for i, (a, _) in enumerate(conv_objects.part_symbols())}
    cases = bytearray()
    count = 0
    for course in (0, 1, 3):
        r = Race(course, game_type=2, render=True, rec_buf=0x060FD400)
        r.init_render()
        m = r.mem
        log = []

        def rec(c):
            if c.pc != 0x06010654:
                part = c.r[5]
                log.append((part_index.get(part, -1), [fx(m.r32(c.r[4] + 4 * k)) for k in range(12)]))
            c.ret()
        for a in DRAW_FNS:
            r.cpu.hooks[a] = rec
        r.cpu.hooks.pop(DISP_CAR, None)
        drv = oracle.make_driver("drive", r)
        per_course = ncases // 3
        n = 0
        i = 0
        while n < per_course:
            cmd = drv.next(r, i)
            if cmd is None:
                break
            r.step(*cmd)
            i += 1
            if i % 12 != 0:
                continue
            for variant in range(4):
                saved_car = m.read(CAR0, 0x400)
                saved_view = m.read(VIEW0, 0x30)
                # randomise
                mode = rng.choice((0, 0, 0, 1, 2, 3))
                if variant:
                    m.w8(CAR0 + 0x5C, rng.randrange(0, 11))
                    m.w8(CAR0 + 0x5D, rng.randrange(0, 2))
                    m.w16(CAR0 + 0xB4, rng.choice((0x0000, 0x1111, 0x2222, 0x9999, 0x1234, 0x7777, 0x0F0F, 0x4321)))
                    m.w32(CAR0 + 0x30, int(rng.uniform(-0.5, 0.5) * 65536) & 0xFFFFFFFF)
                    m.w32(CAR0 + 0x38, int(rng.uniform(0, 6.28) * 65536) & 0xFFFFFFFF)
                    for k in range(4):
                        m.w32(CAR0 + 0x3C + 4 * k, int(rng.uniform(-0.3, 0.3) * 65536 * 16) & 0xFFFFFFFF)
                        m.w32(CAR0 + 0x4C + 4 * k, (m.r32(CAR0 + 0x4C + 4 * k) + int(rng.uniform(-0.15, 0.15) * 65536)) & 0xFFFFFFFF)
                    dz = rng.choice((0, 0, 40, 100, 111, 113, 120, 127.5, 129, 150, 199, 201, 230))
                    m.w32(VIEW0 + 0x2C, (m.r32(VIEW0 + 0x2C) + int(dz * 65536)) & 0xFFFFFFFF)
                    if rng.random() < 0.15:
                        m.w32(CAR0 + 0x18, (m.r32(CAR0 + 0x18) + int(rng.uniform(-30, 30) * 65536)) & 0xFFFFFFFF)
                log.clear()
                r.call(DISP_CAR, r4=VIEW0, r5=mode)
                view = [fx(m.r32(VIEW0 + 4 * k)) for k in range(12)]
                f = lambda o: fx(m.r32(CAR0 + o))
                st = [f(0x0C), f(0x10), f(0x14), f(0x18), f(0x1C), f(0x20), f(0x30), f(0x38), f(0x3C), f(0x40), f(0x44), f(0x48), f(0x4C), f(0x50), f(0x54), f(0x58)]
                cases += struct.pack("<I12f16f3iI", mode, *view, *st, m.r8(CAR0 + 0x5C), m.r8(CAR0 + 0x5D), m.r16(CAR0 + 0xB4), len(log))
                for part, mat in log:
                    cases += struct.pack("<i12f", part, *mat)
                m.load(CAR0, saved_car)
                m.load(VIEW0, saved_view)
                n += 1
                count += 1
        print("course %d: %d cases, passes run %d" % (course, n, i))
    with open(OUT, "wb") as fo:
        fo.write(b"CVEC" + struct.pack("<I", count) + cases)
    print("%d cases -> %s (%d bytes)" % (count, OUT, os.path.getsize(OUT)))


if __name__ == "__main__":
    main()
