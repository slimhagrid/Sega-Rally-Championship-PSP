"""dev aid: who writes a memory range in the original (the program counter and the return address of every store)   python tools/watch_write.py <scenario> <ticks> <hexlo> <hexhi> [maxlines]"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import menu_oracle as mo
from menu_drive import Drv

name = sys.argv[1]
n = int(sys.argv[2])
lo, hi = int(sys.argv[3], 16), int(sys.argv[4], 16)
maxl = int(sys.argv[5]) if len(sys.argv) > 5 else 200
d = Drv()
for a, data in mo.PATCHES.get(name, []):
    d.patch(a, data)
if mo.START_AT.get(name):
    d.m.start(mo.START_AT[name])
for fn in [getattr(mo, "SCENARIOS", {}).get(name)]:
    pass
cpu = d.m.cpu
tick = [0]
lines = [0]


def watch(a, size, v):
    if lo <= a < hi or lo < a + size <= hi:
        if lines[0] < maxl:
            lines[0] += 1
            print("tick %4d  pc %08x pr %08x  [%08x] (%d) = %0*x" % (tick[0], cpu.pc, cpu.pr, a, size, size * 2, v))


d.m.mem.watch = watch
# the scenario's own set-up (unstub etc.) is part of SCENARIOS[name]: run its function with a hooked run()
orig_run = d.run


def run(k=1, pad=0):
    for _ in range(k):
        tick[0] += 1
        orig_run(1, pad)
        if tick[0] >= n:
            raise SystemExit
d.run = run
try:
    mo.SCENARIOS[name](d)
except SystemExit:
    pass
