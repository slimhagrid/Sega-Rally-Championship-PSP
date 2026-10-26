"""The time attack race screen's display set-up with the values the race brings along (src/hudgen.c hudgen_ta_init): the best times, the car, the transmission.

    python tools/hudinit_vec.py [cases per game type and course]            -> extracted/oracle/hudinit.vec

The static part of the set-up is in hud_init.dat / hude_init.dat (made for car 0, automatic, no records); the original's init_game_disp draws these things from the race's variables: the three times
of the right-hand panel (R+0x30 best lap, R+0x24 best time, R+0x70 course record: game type 4; game type 6 only the best lap), the boxes of the car type and of the transmission (NBG0), and
the record box (the flag byte of R+0x70).  Every case runs the original's display set-up for game types 4 and 6 with random values and logs the name tables of NBG2 / NBG3 and the pages of NBG0.

Vector (little endian): "HIN1", u32 ncases, then per case: u32 game_type, u32 course, u32 car (0 / 1 / 2 = Celica / Delta / Stratos), u32 mt, u32 setup_mode, u32 r24, u32 r30, u32 r70,
0x6000 bytes of name tables, 0x10000 bytes of NBG0 pages.
"""
import os
import random
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import oracle  # noqa: E402
import hud as hudmod  # noqa: E402
from saturn_env import Race, CAR0  # noqa: E402


def bcd_time(rnd):
    return (rnd.randrange(0, 10) << 16) | (rnd.randrange(0, 6) << 12) | (rnd.randrange(0, 10) << 8) | (rnd.randrange(0, 10) << 4) | rnd.randrange(0, 10)


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 12
    rnd = random.Random(7)
    cases = []
    for g in (4, 6):
        for course in (0, 3):
            for k in range(n):
                cases.append((g, course, rnd.randrange(3), rnd.randrange(2), rnd.randrange(2), bcd_time(rnd), bcd_time(rnd),
                              bcd_time(rnd) | rnd.choice((0, 0x40000000, 0x80000000, 0xC0000000)) if k % 3 else bcd_time(rnd)))
    path = os.path.join(ROOT, "extracted", "oracle", "hudinit.vec")
    with open(path, "wb") as f:
        f.write(b"HIN1" + struct.pack("<I", len(cases)))
        for g, course, car, mt, setup, r24, r30, r70 in cases:
            r = Race(course, game_type=g, cartype=car, mt=mt, render=True, setup=(1, 2, 1, 2) if setup else None)
            m = r.mem
            m.w32(CAR0 + 0x524, r24)
            m.w32(CAR0 + 0x530, r30)
            m.w32(CAR0 + 0x570, r70)
            oracle.hud_setup(r, hudmod)
            f.write(struct.pack("<IIIIIIII", g, course, car, mt, setup, r24, r30, r70))
            f.write(bytes(m.read(oracle.HUD_NT, 0x6000)))
            f.write(bytes(m.read(oracle.HUDE_NT, 0x10000)))
    print("%d cases -> %s" % (len(cases), path))


if __name__ == "__main__":
    main()
