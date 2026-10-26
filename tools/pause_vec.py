"""The pause menu's picture (src/hudgen.c hudgen_pause_*): what the original's pause routines write into the HUD name tables.

    python tools/pause_vec.py [game_type ...]            -> extracted/oracle/pause.vec

The race screen is set up like the HUD traces do (oracle.hud_setup, game types 0, 2, 4, 6 by default); then a script of calls: pause_display (0x06013BA4: clears [0x060AA821 ..] and writes the box),
pause_cursor (0x06013BCC) with the chosen item in [0x060AA821] (the original's up / down keys only change that byte), pause_delete (0x06013BC0).  After every call the three HUD name table pages are
logged.  tools/test_pause.c replays the script through hudgen_pause_*.

Vector (little endian): "PSV1", u32 ncases, then per case: u32 game_type, u32 course, 0x6000 bytes (name tables before the pause), u32 nsteps, then per step { u8 op (0 open, 1 cursor, 2 close), u8 item,
u8 0, u8 0, 0x6000 bytes of name tables after it }.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import oracle  # noqa: E402
import hud as hudmod  # noqa: E402
from saturn_env import Race  # noqa: E402

PAUSE_DISPLAY, PAUSE_DELETE, PAUSE_CURSOR = 0x06013BA4, 0x06013BC0, 0x06013BCC
ITEM = 0x060AA821

# (item the cursor is on, passes): blink phases, every item, moves back and forth
SCRIPT = [(0, 20), (1, 20), (2, 25), (1, 3), (0, 3), (2, 9), (0, 17), (1, 16), (1, 8)]


def nt(r):
    return bytes(r.mem.read(oracle.HUD_NT, 0x6000))


def case(g, course, out):
    r = Race(course, game_type=g, render=True)
    oracle.hud_setup(r, hudmod)
    m = r.mem
    steps = []
    base = nt(r)
    r.call(PAUSE_DISPLAY)
    steps.append((0, m.r8(ITEM), nt(r)))
    for item, n in SCRIPT:
        for _ in range(n):
            m.w8(ITEM, item)
            r.call(PAUSE_CURSOR)
            steps.append((1, item, nt(r)))
    r.call(PAUSE_DELETE)
    steps.append((2, 0, nt(r)))
    out.write(struct.pack("<II", g, course) + base + struct.pack("<I", len(steps)))
    for op, item, snap in steps:
        out.write(bytes([op, item, 0, 0]) + snap)
    return len(steps)


def main():
    types = [int(a) for a in sys.argv[1:]] or [0, 2, 4, 6]
    path = os.path.join(ROOT, "extracted", "oracle", "pause.vec")
    cases = [(g, c) for g in types for c in (0, 3)]
    with open(path, "wb") as f:
        f.write(b"PSV1" + struct.pack("<I", len(cases)))
        for g, c in cases:
            n = case(g, c, f)
            print("type %d course %d: %d steps" % (g, c, n))
    print("->", path)


if __name__ == "__main__":
    main()
