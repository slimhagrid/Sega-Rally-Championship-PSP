"""Lift a code range that has literal pools / jump tables inside (the DBG module boundaries are unreliable there).

    python tools/liftseg.py <entry hex> [<entry hex> ...] [-t <table hex>:<n>] [-lo <hex> -hi <hex>] [--c]

Recursive descent from the entry points (and the n long words of each jump table, -t) finds the reachable code; every maximal run of it is lifted with tools/lift.py and the
unreached bytes in between are listed as data.  Calls (bsr / jsr) are followed only when the target is a constant bsr inside [lo, hi).
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sh2dis, lift, rd
from sh2dis import w, l, dis, s8, s12

def reachable(ents, tabs, lo, hi):
    """the instruction addresses reachable from the entry points (and the entries of the jump tables) inside [lo, hi), delay slots included"""
    ents = list(ents)
    for t, n in tabs:
        for k in range(n):
            ents.append(l(t + 4 * k))
    seen = set()
    work = list(ents)
    while work:
        p = work.pop()
        while lo <= p < hi and p not in seen:
            seen.add(p)
            o = w(p)
            h = o >> 12
            nxt = p + 2
            end = False
            def delay():
                seen.add(p + 2)
            if o == 0x000B or o == 0x002B:                         # rts / rte
                delay(); end = True
            elif h == 10:                                          # bra
                delay(); work.append(p + 4 + s12(o & 0xFFF) * 2); end = True
            elif h == 11:                                          # bsr
                delay(); work.append(p + 4 + s12(o & 0xFFF) * 2); nxt = p + 4
            elif h == 8 and ((o >> 8) & 15) in (9, 11):            # bt / bf
                work.append(p + 4 + s8(o & 255) * 2)
            elif h == 8 and ((o >> 8) & 15) in (13, 15):          # bt/s bf/s
                delay(); work.append(p + 4 + s8(o & 255) * 2); nxt = p + 4
            elif (o & 0xF0FF) == 0x400B:                           # jsr: the target register was loaded from the literal pool a few instructions before
                delay(); nxt = p + 4
                rn = (o >> 8) & 15
                for back in range(2, 14, 2):
                    q = p - back
                    oq = w(q)
                    if (oq & 0xF000) == 0xD000 and ((oq >> 8) & 15) == rn:
                        t = ((q + 4) & ~3) + (oq & 255) * 4
                        v = l(t)
                        if lo <= v < hi and v & 1 == 0:
                            work.append(v)
                        break
                    if (oq & 0xF00F) == 0x6003 and ((oq >> 8) & 15) == rn:    # mov rm,rn
                        rn = (oq >> 4) & 15
            elif (o & 0xF0FF) == 0x402B:                           # jmp
                delay(); end = True
            elif (o & 0xF0FF) in (0x0023, 0x0003):                 # braf / bsrf
                delay(); end = (o & 0xF0FF) == 0x0023; nxt = p + 4
            if end:
                break
            p = nxt
    return seen

def main():
    a = [x for x in sys.argv[1:] if x != "--ptrs"]
    cmode = "--c" in a
    if cmode:
        a.remove("--c")
    lo, hi = 0x06004000, 0x06036000
    ents, tabs = [], []
    i = 0
    while i < len(a):
        if a[i] == "-t":
            t, n = a[i + 1].split(":"); tabs.append((int(t, 16), int(n))); i += 2
        elif a[i] == "-lo":
            lo = int(a[i + 1], 16); i += 2
        elif a[i] == "-hi":
            hi = int(a[i + 1], 16); i += 2
        else:
            ents.append(int(a[i], 16)); i += 1
    seen = reachable(ents, tabs, lo, hi)
    if "--ptrs" in sys.argv:                                   # literal-pool values that point into the range but not at reached code: candidate entries / tables
        cand = {}
        for x in sorted(seen):
            o = w(x)
            if (o & 0xF000) == 0xD000:
                v = l(((x + 4) & ~3) + (o & 255) * 4)
                if lo <= v < hi and v not in seen:
                    cand.setdefault(v, []).append(x)
        for v in sorted(cand):
            print("// ptr %08x (from %s)" % (v, " ".join("%08x" % q for q in cand[v][:3])))
        return
    addrs = sorted(seen)
    segs = []
    for x in addrs:
        if segs and segs[-1][1] == x:
            segs[-1][1] = x + 2
        else:
            segs.append([x, x + 2])
    tg = set()                                                  # branch targets of the whole reached code: labels for jumps that come from other segments
    for x in addrs:
        o = w(x)
        if (o >> 12) == 10:
            tg.add(x + 4 + s12(o & 0xFFF) * 2)
        elif (o >> 12) == 8 and ((o >> 8) & 15) in (9, 11, 13, 15):
            tg.add(x + 4 + s8(o & 255) * 2)
    for s, e in segs:
        lf = lift.Lift((s, e - s, "seg", ""))
        lf.labels |= {t for t in tg if s <= t < e}
        t = lf.run()
        print(lift.to_c(t) if cmode else t)
    print("// code segments: " + " ".join("%08x-%08x" % (s, e) for s, e in segs))

if __name__ == "__main__":
    main()
