"""dev aid: which original functions does a scenario call (observer hooks on every function entry of functions.tsv)   python tools/photo_calls.py <scenario> [ticks]"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import menu_oracle as mo
from menu_drive import Drv
from menu_env import ROOT

name = sys.argv[1]
n = int(sys.argv[2]) if len(sys.argv) > 2 else 300
d = Drv()
for a, data in mo.PATCHES.get(name, []):
    d.patch(a, data)
if mo.START_AT.get(name):
    d.m.start(mo.START_AT[name])
cpu = d.m.cpu
names = {}
for ln in open(os.path.join(ROOT, "extracted", "analysis", "functions.tsv")):
    p = ln.rstrip("\n").split("\t")
    names[int(p[0], 16)] = p[2]
seen = {}
order = []
tick = [0]
for a in list(names):
    if a in cpu.hooks:
        continue
    def mk(a):
        def h(c):
            if a not in seen:
                seen[a] = [tick[0], 0]
                order.append(a)
            seen[a][1] += 1
            c.step_orig()
        return h
    cpu.hooks[a] = mk(a)
for a in (0x0600E7B8, 0x0600E8A0, 0x0600EC38, 0x0600E7A8, 0x0600E78C, 0x0600EAF8, 0x0600EBC8, 0x0600EE00, 0x0600ED08, 0x0600E974, 0x0600E9C8, 0x0600E9E0, 0x0600EA08, 0x0600EA20, 0x0600EA40, 0x0600ECF0):
    if a in cpu.hooks:
        continue
    names.setdefault(a, "photo_%08x" % a)
    def mk2(a):
        def h(c):
            if a not in seen:
                seen[a] = [tick[0], 0]
                order.append(a)
            seen[a][1] += 1
            c.step_orig()
        return h
    cpu.hooks[a] = mk2(a)
for i in range(n):
    tick[0] = i
    d.run(1, 0)
for a in order:
    print("%08x %-28s first pass %4d  calls %d" % (a, names.get(a, "?"), seen[a][0], seen[a][1]))
