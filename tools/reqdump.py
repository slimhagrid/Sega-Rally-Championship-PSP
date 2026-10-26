"""dev aid: print the CD request block(s) at the given original addresses (24-byte entries ended by -1): python tools/reqdump.py 0x0601ABFC ..."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sh2dis

def b(a, n):
    return bytes(sum(([sh2dis.w(a + 2 * i) >> 8, sh2dis.w(a + 2 * i) & 255] for i in range(n // 2)), []))

def dump(a):
    out = []
    while sh2dis.w(a) != 0xFFFF:
        e = b(a, 24)
        t = (e[0] << 8) | e[1]
        if t == 0 and e[10]:
            out.append("raw   dest=%08x name=%s  (flags %s)" % (int.from_bytes(e[4:8], "big"), e[10:].split(b"\0")[0].decode(), e[2:4].hex()))
        else:
            out.append("scat  name=%s  (head %s)" % (e[2:].split(b"\0")[0].decode(), e[:2].hex()))
        a += 24
    return out

for s in sys.argv[1:]:
    print(s)
    for ln in dump(int(s, 16)):
        print("   ", ln)
