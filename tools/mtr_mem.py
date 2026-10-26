"""dev aid: the oracle's memory at a tick of a trace   python tools/mtr_mem.py <scenario|trace.mtr> <tick> <hexaddr> <len> [<tick2>]   (tick -1 = the snapshot at the start)"""
import os, struct, sys, zlib

def load(path):
    raw = open(path, "rb").read()
    d = zlib.decompress(raw[4:])
    p = 0
    start, npatch = struct.unpack_from("<II", d, p); p += 8
    for _ in range(npatch):
        a, n = struct.unpack_from("<II", d, p); p += 8 + n
    nreg, = struct.unpack_from("<I", d, p); p += 4
    regs = []
    for _ in range(nreg):
        regs.append(list(struct.unpack_from("<II", d, p))); p += 8
    mem = []
    for a, n in regs:
        mem.append(bytearray(d[p:p + n])); p += n
    nt, = struct.unpack_from("<I", d, p); p += 4
    ticks = []
    for t in range(nt):
        pad, rin, rout, vec, nch = struct.unpack_from("<HBBII", d, p); p += 12
        ch = []
        for _ in range(nch):
            a, n = struct.unpack_from("<II", d, p); p += 8
            ch.append((a, d[p:p + n])); p += n
        ticks.append(ch)
    return regs, mem, ticks

def at(regs, mem, ticks, upto):
    m = [bytearray(x) for x in mem]
    for t in range(upto + 1):
        for a, data in ticks[t]:
            for k, (ra, rn) in enumerate(regs):
                if ra <= a and a + len(data) <= ra + rn:
                    m[k][a - ra:a - ra + len(data)] = data
    return m

def get(regs, m, a, n):
    for k, (ra, rn) in enumerate(regs):
        if ra <= a and a + n <= ra + rn:
            return bytes(m[k][a - ra:a - ra + n])

if __name__ == "__main__":
    s = sys.argv[1]
    path = s if s.endswith(".mtr") else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "extracted", "oracle", "menu_%s.mtr" % s)
    regs, mem, ticks = load(path)
    a, n = int(sys.argv[3], 16), int(sys.argv[4], 16)
    for t in [int(sys.argv[2])] + ([int(sys.argv[5])] if len(sys.argv) > 5 else []):
        m = at(regs, mem, ticks, t)
        print("tick", t, get(regs, m, a, n).hex(" "))
