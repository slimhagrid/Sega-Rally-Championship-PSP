"""dev aid: registers of the original at given code addresses (first N hits)   python tools/pc_regs.py <scenario> <ticks> <hex addr>[,<hex addr>..] [-n hits]
Observer hooks (step_orig): the instruction runs normally."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import menu_oracle as mo
from menu_drive import Drv

name, ticks = sys.argv[1], int(sys.argv[2])
addrs = [int(x, 16) for x in sys.argv[3].split(",")]
nmax = int(sys.argv[sys.argv.index("-n") + 1]) if "-n" in sys.argv else 3
d = Drv()
for a, data in mo.PATCHES.get(name, []):
    d.patch(a, data)
if mo.START_AT.get(name):
    d.m.start(mo.START_AT[name])
cpu = d.m.cpu
cnt = {}
tick = [0]
def mk(a):
    def h(c):
        cnt[a] = cnt.get(a, 0) + 1
        if cnt[a] <= nmax:
            print("pass %d pc=%08x " % (tick[0], a) + " ".join("r%d=%08x" % (i, c.r[i]) for i in range(15)) + " pr=%08x gbr=%08x T=%d mach=%08x macl=%08x" % (c.pr, c.gbr, c.T, c.mach, c.macl))
        c.step_orig()
    return h
for a in addrs:
    if a in cpu.hooks:                                  # a function the harness stubs: observing it would run the real code instead of the stub (a different run than the oracle's)
        print("note: %08x is hooked (stubbed) in the oracle; not observed" % a)
        continue
    cpu.hooks[a] = mk(a)
for i in range(ticks):
    tick[0] = i
    d.run(1, 0)
