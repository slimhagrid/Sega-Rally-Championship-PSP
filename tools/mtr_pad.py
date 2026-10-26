"""the pad script of an oracle trace for the PSP host / run_ppsspp.ps1 -Pad:  python tools/mtr_pad.py <scenario|trace.mtr> [last tick]  ->  "tick hexmask;tick hexmask;..." (only where the pad changes)
Play a screen of a scenario in PPSSPP without a controller: the trace starts at the mode select like the PSP build does."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mtr_info import load  # noqa: E402

a = sys.argv[1]
path = a if a.endswith(".mtr") else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "extracted", "oracle", "menu_%s.mtr" % a)
ticks = load(path)
last = int(sys.argv[2]) if len(sys.argv) > 2 else len(ticks)
out = []
prev = 0
for t, tk in enumerate(ticks[:last]):
    if tk[0] != prev:
        out.append("%d %x" % (t + 1, tk[0]))
        prev = tk[0]
print(";".join(out))
