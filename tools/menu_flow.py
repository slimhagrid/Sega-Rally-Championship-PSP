"""M8: drive the original program through its menus with a pad script and save a picture whenever the screen (main vector) changes.
   python tools/menu_flow.py "30:0 3:0x0800 60:0 ..."   (frames:held buttons)   -> extracted/ref/flow_<n>_<vector>.png"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from menu_env import Menu, MAIN_VECTOR, SimError, ROOT  # noqa: E402
from vdp2_screen import render, state_of  # noqa: E402
from PIL import Image  # noqa: E402

script = [(int(a.split(":")[0]), int(a.split(":")[1], 0)) for a in sys.argv[1].split()]
tag = sys.argv[2] if len(sys.argv) > 2 else "flow"
m = Menu()
m.boot()
out = os.path.join(ROOT, "extracted", "ref")
os.makedirs(out, exist_ok=True)
last = None
n = 0


def shot(why):
    global n
    p = os.path.join(out, "%s_%02d_%s.png" % (tag, n, why))
    Image.fromarray(render(state_of(m.mem))).save(p)
    print("shot", p, flush=True)
    n += 1


frame = 0
for frames, pad in script:
    m.pad = pad
    for _ in range(frames):
        try:
            m.run()
        except SimError as e:
            print("error at frame", frame, e)
            raise SystemExit
        frame += 1
        v = m.mem.r32(MAIN_VECTOR)
        if v != last:
            print("frame", frame, "vector", hex(v), "type", hex(m.mem.r16(0x060AA860)), m.log[-2:], flush=True)
            last = v
    shot("f%d_%x" % (frame, last))
