"""M8 function-level check of get_camera_angle (0x0601E970, src/menu_ending.c): runs the original on the SH-2 interpreter with random points -> extracted/oracle/fn_ending.txt
(a.x a.y a.z b.x b.y b.z r0 r1 per line, hex); tools/test_ending_fn.sh replays them on the port.  Not run by any screen of the oracle: the title demo (advExec) calls it."""
import os
import sys
import random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from menu_drive import Drv
d = Drv()
m = d.mem; c = d.m.cpu
random.seed(5)
out = []
SP = 0x060FF000
def s32(x): return x - (1 << 32) if x & 0x80000000 else x
for i in range(400):
    k = random.choice([16, 20, 24, 28, 31])
    a = [random.randint(-(1 << k), 1 << k) & 0xFFFFFFFF for _ in range(3)]
    b = [random.randint(-(1 << k), 1 << k) & 0xFFFFFFFF for _ in range(3)]
    if i % 13 == 0: b = list(a)
    if i % 17 == 0: b[0] = a[0]
    if i % 19 == 0: b[2] = a[2]
    for j in range(3):
        m.w32(0x060F0000 + 4 * j, a[j]); m.w32(0x060F0010 + 4 * j, b[j])
    c.call(0x0601E970, {4: 0x060F0000, 5: 0x060F0010, 15: SP})
    out.append(a + b + [c.r[0], c.r[1]])
out_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "extracted", "oracle", "fn_ending.txt")
os.makedirs(os.path.dirname(out_path), exist_ok=True)
open(out_path, "w").write("\n".join(" ".join("%x" % (v & 0xFFFFFFFF) for v in row) for row in out))
print(len(out))
