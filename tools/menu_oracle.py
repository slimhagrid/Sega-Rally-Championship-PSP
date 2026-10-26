"""M8 oracle: run the original program through a pad script and record, after boot and after every V-blank tick, the memory regions the C port is compared with.

    python tools/menu_oracle.py <scenario> [out]        scenarios: see SCENARIOS (python functions driving `Drv`), default out = extracted/oracle/menu_<scenario>.mtr

MTR1 file (little endian, zlib-compressed after the 4-byte magic):
    u32 start_vector (0 = the main loop runs the main vector as the boot left it, else the screen handler entered first),
    u32 npatches, npatches x (u32 addr, u32 len, bytes)   [memory the scenario set before the start: the C test applies the same after its boot],
    u32 nregions, nregions x (u32 addr, u32 size)
    snapshot after boot: the region bytes in order
    u32 nticks, then per tick:  u16 pad, u8 recipe_in, u8 recipe_out, u32 vector (the main vector word), u32 nchunks, nchunks x (u32 addr, u32 len, bytes)
The pad word is the Saturn pad word the harness presented to pad_read for that tick (bits as in menu_env.py).
Optional trailer (scenarios listed in FINAL): u32 magic 'FIN1', u32 vector (the screen outside the package the main loop dispatched), u32 nchunks, chunks as above = the memory
changes between the last tick and that dispatch (the exit stores of the last screen).  The C test compares them when its main loop dispatches the same vector.
"""
import os
import struct
import sys
import zlib

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from menu_drive import Drv, START, A, B, C, UP, DOWN, LEFT, RIGHT, X, Y, Z, L, R  # noqa: E402,F401
from menu_env import ROOT  # noqa: E402

# the compared memory: (address, size).  Stacks and the polygon pipeline's scratch are not part of the menu state.
REGIONS = [
    (0x0603D880, 0x060FE000 - 0x0603D880),          # high work RAM: .data / .bss of the program, the game's work areas, buffers
    (0x00200000, 0x00100000),                       # low work RAM (picture data, tables loaded from the disc)
    (0x25C00000, 0x00080000),                       # VDP1 VRAM (command table, textures)
    (0x25E00000, 0x00080000),                       # VDP2 VRAM
    (0x25F00000, 0x00001000),                       # colour RAM
    (0x25F80000, 0x00000120),                       # VDP2 registers
]


class Rec:
    def __init__(self, drv):
        self.d = drv
        self.m = drv.m
        mem = self.m.mem
        self.bufs = []
        self.cur = []
        for a, n in REGIONS:
            self.bufs.append(np.frombuffer(bytes(mem.read(a, n)), dtype=np.uint8).copy())
        self.snapshot = [b.tobytes() for b in self.bufs]
        self.ticks = []
        self.start = 0
        self.patches = []
        self.m.tick_cb = self.tick

    def tick(self, menu, rin, rout):
        self.ticks.append((menu.pad & 0xFFFF, rin & 0xFF, rout & 0xFF, menu.mem.r32(0x060A001C), self.diff(menu)))
        k = len(self.ticks) - 1
        for t, a, data in getattr(self, "pokes", ()):                 # the POKE pseudo patches (tools/menu_drive.py): written after the tick was recorded
            if t == k:
                for j, v in enumerate(data):
                    menu.mem.w8(a + j, v)

    def finish(self, d, vector, maxpass=5000):
        """run on until the main loop dispatches `vector` (a screen outside the package) and keep the memory changes since the last tick as the trailer of the trace:
        the C test compares them when its main loop dispatches the same vector (the screen's exit stores that no tick shows)"""
        class Reached(Exception):
            pass

        def hook(c):
            self.final = (vector, self.diff(d.m))
            raise Reached()
        d.m.cpu.hooks[vector] = hook
        for _ in range(maxpass):
            try:
                d.run(1)
            except Reached:
                return True
        raise RuntimeError("main vector %08x was not dispatched" % vector)

    def diff(self, menu):
        mem = menu.mem
        chunks = []
        for k, (a, n) in enumerate(REGIONS):
            if a == 0x06000000 or (0x06000000 <= a < 0x08000000):
                now = np.frombuffer(mem.hw, dtype=np.uint8)[a & 0xFFFFF:(a & 0xFFFFF) + n]
            elif 0x00200000 <= a < 0x00300000:
                now = np.frombuffer(mem.lw, dtype=np.uint8)[a & 0xFFFFF:(a & 0xFFFFF) + n]
            else:
                now = np.frombuffer(bytes(mem.read(a, n)), dtype=np.uint8)
            old = self.bufs[k]
            idx = np.flatnonzero(now != old)
            if len(idx):
                # runs of changed bytes, merged when the gap is small
                cuts = np.flatnonzero(np.diff(idx) > 8)
                starts = np.concatenate(([0], cuts + 1))
                ends = np.concatenate((cuts, [len(idx) - 1]))
                for s_, e_ in zip(starts, ends):
                    lo, hi = int(idx[s_]), int(idx[e_]) + 1
                    chunks.append((a + lo, now[lo:hi].tobytes()))
                old[:] = now
        return chunks

    def save(self, path):
        out = bytearray()
        out += struct.pack("<II", self.start, len(self.patches))
        for a, data in self.patches:
            out += struct.pack("<II", a, len(data)) + bytes(data)
        out += struct.pack("<I", len(REGIONS))
        for a, n in REGIONS:
            out += struct.pack("<II", a, n)
        for s in self.snapshot:
            out += s
        out += struct.pack("<I", len(self.ticks))
        for pad, rin, rout, vec, chunks in self.ticks:
            out += struct.pack("<HBBII", pad, rin, rout, vec, len(chunks))
            for a, data in chunks:
                out += struct.pack("<II", a, len(data)) + data
        if getattr(self, "final", None):
            vec, chunks = self.final
            out += struct.pack("<4sII", b"FIN1", vec, len(chunks))
            for a, data in chunks:
                out += struct.pack("<II", a, len(data)) + data
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(b"MTR1" + zlib.compress(bytes(out), 3))
        return len(out)


# --- scenarios -----------------------------------------------------------------------------------------------------------
def to_title_start(d):
    d.until(lambda d: d.state() == 14 and d.mem.r8(0x060AA803) & 0x40)
    d.tap(START)
    d.run(4)
    d.tap(START)
    d.until(lambda d: d.vec() == 0x06022894)


def sc_mode(d):
    d.run(200)
    d.tap(DOWN); d.run(10); d.tap(DOWN); d.run(10); d.tap(UP); d.run(60)


def sc_boot(d):
    d.run(60)


def sc_title(d):
    d.run(400)


def sc_to_mode(d):
    to_title_start(d)
    d.run(120)


SCENARIOS = {"boot": sc_boot, "title": sc_title, "to_mode": sc_to_mode, "mode": sc_mode}
START_AT = {"mode": 0x06022894}
FINAL = {}                                    # scenario -> main vector of the screen outside the package that ends it (the trace gets the exit stores as a trailer)
PATCHES = {}                                  # scenario -> [(addr, bytes)] set after boot, before the screen starts (the state the earlier screens would have left)

# scenario modules: tools/scn_*.py define SCENARIOS / START_AT / PATCHES (and may use Drv helpers from menu_drive)
for _f in sorted(os.listdir(HERE)):
    if _f.startswith("scn_") and _f.endswith(".py"):
        _ns = {}
        exec(compile(open(os.path.join(HERE, _f)).read(), _f, "exec"), _ns)
        SCENARIOS.update(_ns.get("SCENARIOS", {}))
        START_AT.update(_ns.get("START_AT", {}))
        PATCHES.update(_ns.get("PATCHES", {}))
        FINAL.update(_ns.get("FINAL", {}))


def main():
    name = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "extracted", "oracle", "menu_%s.mtr" % name)
    d = Drv()
    patches = PATCHES.get(name, [])
    for a, data in patches:
        d.patch(a, data)
    if START_AT.get(name):
        d.m.start(START_AT[name])
    rec = Rec(d)
    rec.start = START_AT.get(name, 0)
    rec.patches = list(patches)
    rec.pokes = [(int.from_bytes(p[:4], "big"), int.from_bytes(p[4:8], "big"), p[8:]) for a, p in patches if a == 0xFFFFFD00]
    SCENARIOS[name](d)
    if FINAL.get(name):
        rec.finish(d, FINAL[name])
    n = rec.save(out)
    print("%s: %d ticks, %d bytes raw -> %s" % (name, len(rec.ticks), n, out))


if __name__ == "__main__":
    main()
