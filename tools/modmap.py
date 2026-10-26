"""Reading aid for M2: one source module of 0RALLY.DAT as a partition of its code range by DBG symbols.

    python tools/modmap.py car [mkpoly ...]

For every symbol of the module (sorted by address) it prints: address, extent up to the next symbol, name, and the Ghidra functions that start
inside that extent (`F` = file in extracted/analysis/decomp/ and asm/).  Ghidra often splits a routine at the DBG labels, so a symbol's real
code usually runs to the next symbol: read all functions listed on its line, in address order.  Needs extracted/analysis/*.tsv (tools/dbg_parse.py + tools/ghidra_import.ps1).
"""
import bisect
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
A = os.path.join(os.path.dirname(HERE), "extracted", "analysis")


def main():
    mods = {}
    for l in open(os.path.join(A, "modules.tsv")):
        m, s, a, z = l.rstrip("\n").split("\t")
        if s == "P" and int(a, 16) >= 0x06004000 and m not in mods:
            mods[m] = (int(a, 16), int(z, 16))
    funcs = []
    for l in open(os.path.join(A, "functions.tsv")):
        p = l.rstrip("\n").split("\t") + [""]
        funcs.append((int(p[0], 16), int(p[1]), p[2], p[3]))
    funcs.sort()
    fa = [f[0] for f in funcs]
    syms = {}
    for l in open(os.path.join(A, "symbols.tsv")):
        a, k, n, m = l.rstrip("\n").split("\t")
        if k in ("code", "lib"):
            syms.setdefault(m, []).append((int(a, 16), n))
    for m in sys.argv[1:]:
        if m not in mods:
            print("unknown module", m, "- known:", " ".join(mods))
            continue
        base, size = mods[m]
        order = sorted(mods.values())
        nxt = [a for a, _ in order if a > base]
        end = (nxt[0] if nxt else base + size)
        end = max(end, base + size + (0x34 if False else 0))
        print(f"== module {m}: code 0x{base:08x}-0x{end:08x} ({end - base} bytes)")
        ss = sorted(set(syms.get(m, [])))
        for i, (a, n) in enumerate(ss):
            e = ss[i + 1][0] if i + 1 < len(ss) else end
            lo, hi = bisect.bisect_left(fa, a), bisect.bisect_left(fa, e)
            inside = ", ".join(f"{f[2]}@{f[0]:x}({f[1]})" for f in funcs[lo:hi])
            print(f"  {a:08x}-{e:08x} {e - a:5d}  {n:28s} {inside}")
        extra = [f for f in funcs if base <= f[0] < end and not any(a <= f[0] for a, _ in ss[:1])]
        if extra:
            print("  functions before the first symbol:", ", ".join(f"{f[2]}@{f[0]:x}" for f in extra))


if __name__ == "__main__":
    main()
