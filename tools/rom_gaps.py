"""dev aid: which bytes of [lo, hi) are NOT in extracted/psp/menu/ROM.blk (the C port reads zeros there; add the range to EXTRA in tools/conv_menu.py)   python tools/rom_gaps.py <lo> <hi> ..."""
import os, struct, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
d = open(os.path.join(ROOT, "extracted", "psp", "menu", "ROM.blk"), "rb").read()
n, = struct.unpack_from("<I", d, 4)
p = 8
cov = []
for _ in range(n):
    a, sz = struct.unpack_from("<II", d, p)
    p += 8 + sz
    cov.append((a, a + sz))
cov.sort()
args = [int(x, 16) for x in sys.argv[1:]]
for lo, hi in zip(args[0::2], args[1::2]):
    cur = lo
    gaps = []
    for a, b in cov:
        if b <= cur:
            continue
        if a >= hi:
            break
        if a > cur:
            gaps.append((cur, a))
        cur = max(cur, b)
        if cur >= hi:
            break
    if cur < hi:
        gaps.append((cur, hi))
    print("%08x-%08x:" % (lo, hi), " ".join("%08x-%08x" % g for g in gaps) if gaps else "covered")
