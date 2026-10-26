"""print fields of one other car in an oracle trace (.opp): python tools/oppdump.py extracted/oracle/<name> <car> <first pass> <last pass> [offset ...]"""
import struct
import sys
name, car, p0, p1 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
offs = [int(x, 16) for x in sys.argv[5:]] or [0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x102, 0x104]
d = open(name + ".opp", "rb").read()
n, sz = struct.unpack("<II", d[:8])
for p in range(p0, min(p1, n - 1) + 1):
    s = d[8 + p * sz + car * 0x200: 8 + p * sz + (car + 1) * 0x200]
    out = []
    for o in offs:
        w = 2 if o in (0x102, 0x104, 0x108, 0x10A, 0xCA, 0xB4) else 4
        v = int.from_bytes(s[o:o + w], "big")
        out.append("%03x=%0*x" % (o, w * 2, v))
    print(p, " ".join(out))
