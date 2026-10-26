"""M8: run a menu screen's handler in the original code and save what the VDP2 shows:  python tools/menu_shot.py <main vector hex> [frames] [name]  -> extracted/ref/menu_<name>.png"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from menu_env import Menu, MAIN_VECTOR, SimError, ROOT  # noqa: E402
from vdp2_screen import render, state_of  # noqa: E402

vec = int(sys.argv[1], 16)
frames = int(sys.argv[2]) if len(sys.argv) > 2 else 1
name = sys.argv[3] if len(sys.argv) > 3 else "%08x" % vec
m = Menu()
m.boot()
m.start(vec)
for i in range(frames):
    try:
        m.run()
    except SimError as e:
        print("frame", i, e)
        break
print("vector now", hex(m.mem.r32(MAIN_VECTOR)), m.log)
from PIL import Image  # noqa: E402
os.makedirs(os.path.join(ROOT, "extracted", "ref"), exist_ok=True)
out = os.path.join(ROOT, "extracted", "ref", "menu_%s.png" % name)
Image.fromarray(render(state_of(m.mem))).save(out)
print(out)
