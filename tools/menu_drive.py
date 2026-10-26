"""M8: scripted sessions on the original menus.  `Drv` runs the harness and lets a script press buttons by condition.
    d = Drv(); d.until(lambda d: d.state() == 14); d.tap(0x0800); d.watch(300)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from menu_env import Menu, MAIN_VECTOR, ROOT  # noqa: E402

START, A, B, C, UP, DOWN, LEFT, RIGHT, X, Y, Z, L, R = 0x0800, 0x0400, 0x0100, 0x0200, 0x1000, 0x2000, 0x4000, 0x8000, 0x0040, 0x0020, 0x0010, 0x0008, 0x0080


IGNORE = 0xFFFFFE00         # pseudo address in PATCHES: (IGNORE, lo (4 bytes big endian) + hi (4 bytes)) = a memory range the C test does not compare in this scenario (a 3D part the port leaves to the host; give the reason in the scenario file)
POKE = 0xFFFFFD00           # pseudo address in PATCHES: (POKE, tick (4 bytes big endian) + address (4) + bytes) = memory the scenario changes at the end of that tick (what an external component the port does not run, e.g. the race engine's car, would have written); the oracle writes it after recording the tick, the C test after comparing it
AXIS = 0xFFFFFF00          # pseudo address in PATCHES: (AXIS, bytes([n])) = the analog axis byte (0x060AA99C) written by pad_read on every tick (C: sat_axis)


class Drv:
    def __init__(self, vector=None):
        self.m = Menu()
        self.m.boot()
        if vector is not None:
            self.m.start(vector)
        self.f = 0

    @property
    def mem(self):
        return self.m.mem

    def patch(self, addr, data):
        """set memory before a scenario starts (PATCHES): bytes at addr; the pseudo address AXIS sets the analog axis byte pad_read presents every tick"""
        if addr == IGNORE or addr == POKE:
            return
        if addr == AXIS:
            self.m.axis = data[0]
            return
        for k, b in enumerate(data):
            self.mem.w8(addr + k, b)

    def vec(self):
        return self.mem.r32(MAIN_VECTOR)

    def state(self):
        return self.mem.r8(0x060AA807)

    def run(self, n=1, pad=0):
        self.m.pad = pad
        for _ in range(n):
            self.m.run()
            self.f += 1

    def tap(self, btn, hold=3, gap=2):
        self.run(hold, btn)
        self.run(gap, 0)

    def until(self, cond, maxf=5000, pad=0):
        for _ in range(maxf):
            if cond(self):
                return True
            self.run(1, pad)
        return False

    def shot(self, name):
        from PIL import Image
        from vdp2_screen import render, state_of
        out = os.path.join(ROOT, "extracted", "ref")
        os.makedirs(out, exist_ok=True)
        p = os.path.join(out, name + ".png")
        Image.fromarray(render(state_of(self.mem))).save(p)
        return p

    def watch(self, n, every=1):
        last = None
        for _ in range(n):
            self.run(1)
            k = (self.vec(), self.state())
            if k != last:
                print(self.f, hex(k[0]), k[1], self.m.log[-1:])
                last = k
