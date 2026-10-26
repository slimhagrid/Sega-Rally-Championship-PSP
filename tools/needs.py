"""M8 tool: which original functions does a ported screen still need?

    python tools/needs.py <function name|address> ...        closure over direct calls + the handler addresses stored into the screen / main vectors
    python tools/needs.py -p                                 the ported list

`tools/ported.txt` (committed) lists the original addresses already ported (one per line: `0x06022894 cs_md_select.c slt_mode_init`); stubs (hardware / sound / 3D that the oracle
replaces too) are listed in the same file with the file name `-`.  The output lists the functions still to port, with size and module, in call order.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lift  # noqa: E402
import rd  # noqa: E402

PORTED = os.path.join(HERE, "ported.txt")


def ported():
    """stubs (tools/ported.txt, hand kept) + every function in src/*.c whose definition line carries a comment with its original address: `void foo(void)   /* 0x06022894 ... */`"""
    d = {}
    if os.path.isfile(PORTED):
        for ln in open(PORTED):
            ln = ln.split("#")[0].split()
            if len(ln) >= 2:
                d[int(ln[0], 16)] = ln[1]
    src = os.path.join(os.path.dirname(HERE), "src")
    pat = re.compile(r"^(?:static\s+)?[A-Za-z_][\w\s\*]*?\b(\w+)\s*\([^;{]*\)\s*(?:\{[^}]*\})?\s*/\*\s*(?:FUN_[0-9A-Fa-f]+\s*)?(0x[0-9A-Fa-f]{8})")
    for fn in sorted(os.listdir(src)):
        if fn.endswith(".c"):
            for ln in open(os.path.join(src, fn), errors="replace"):
                m = pat.match(ln)
                if m:
                    d.setdefault(int(m.group(2), 16), fn)
    return d


def targets(f):
    """direct call targets and stored handler addresses of one function"""
    lift.C_MODE = True
    try:
        txt = lift.lift_func(f)
    finally:
        lift.C_MODE = False
    out = set()
    for m in re.finditer(r"\bcall (\w+)\(|\btailcall (\w+)\(", txt):
        n = m.group(1) or m.group(2)
        if n in rd.BYNAME:
            out.add(rd.BYNAME[n][0])
        else:
            mm = re.match(r"FUN_([0-9a-f]{8})$", n)
            if mm:
                out.add(int(mm.group(1), 16))
    for m in re.finditer(r"u32\[0x060A(?:8800|001C)\] = (\w+);|W32\(0x060A(?:8800|001C), (0x[0-9A-Fa-f]+|\w+)\)", txt):
        v = m.group(1) or m.group(2)
        try:
            a = int(v, 16) if v.startswith("0x") else rd.BYNAME[v][0]
        except (ValueError, KeyError):
            continue
        out.add(a)
    return out


def main():
    args = sys.argv[1:]
    if args and args[0] == "-p":
        for a, n in sorted(ported().items()):
            print("%08x %s" % (a, n))
        return
    have = ported()
    seen = set()
    order = []
    st = []
    for a in args:
        f = rd.find_func(a)
        if f:
            st.append(f[0])
    while st:
        a = st.pop(0)
        if a in seen:
            continue
        seen.add(a)
        if have.get(a) == "-":
            continue                                    # stubbed: not explored
        f = rd.BYADDR.get(a) or rd.func_at(a)
        if not f:
            if a not in have:
                order.append((a, 0, "?", "?"))
            continue
        if a not in have:
            order.append(f)
        for t in sorted(targets(f)):
            if t not in seen:
                st.append(t)
    for f in order:
        print("%08x %5d %-24s %s" % (f[0], f[1], f[2], f[3]))
    print("total %d functions, %d bytes" % (len(order), sum(f[1] for f in order)))


if __name__ == "__main__":
    main()
