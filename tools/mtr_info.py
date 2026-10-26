"""print the main vector / pad / recipe bytes of oracle trace ticks:  python tools/mtr_info.py <scenario|trace.mtr> <first tick> [<last tick>]   (and a summary of the vector changes when no tick is given)"""
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
        regs.append(struct.unpack_from("<II", d, p)); p += 8
    for a, n in regs:
        p += n
    nt, = struct.unpack_from("<I", d, p); p += 4
    ticks = []
    for t in range(nt):
        pad, rin, rout, vec, nch = struct.unpack_from("<HBBII", d, p); p += 12
        ch = []
        for _ in range(nch):
            a, n = struct.unpack_from("<II", d, p); p += 8
            ch.append((a, d[p:p + n])); p += n
        ticks.append((pad, rin, rout, vec, ch))
    return ticks

if __name__ == "__main__":
    a = sys.argv[1]
    path = a if a.endswith(".mtr") else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "extracted", "oracle", "menu_%s.mtr" % a)
    ticks = load(path)
    if len(sys.argv) > 2:
        lo = int(sys.argv[2]); hi = int(sys.argv[3]) if len(sys.argv) > 3 else lo
        for t in range(lo, hi + 1):
            pad, rin, rout, vec, ch = ticks[t]
            print(t, "pad %04x recipes %02x %02x vec %08x chunks %d" % (pad, rin, rout, vec, len(ch)))
    else:
        last = None
        for t, (pad, rin, rout, vec, ch) in enumerate(ticks):
            if vec != last:
                print(t, "vec %08x" % vec)
                last = vec
        print(len(ticks), "ticks")
