"""M8 reading aid (tools only): print a function's Ghidra decompilation with the literal-pool constants resolved, and/or its annotated disassembly.

    python tools/rd.py g_select_main            # decompiled C, PTR_DAT_xxxx -> the pointer value / symbol, DAT_xxxx -> the constant
    python tools/rd.py 0601fac4 -a              # + disassembly (names of call targets, pool constants)
    python tools/rd.py -m ac_g_select           # every function of a module
    python tools/rd.py -s 0x060AA807            # who refers to an address (literal pool search) -> functions
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
AN = os.path.join(ROOT, "extracted", "analysis")
sys.path.insert(0, HERE)
import sh2dis  # noqa: E402

IMG = sh2dis.D
BASE = sh2dis.BASE


def r8(a):
    o = a - BASE
    return IMG[o] if 0 <= o < len(IMG) else 0


def r16(a):
    o = a - BASE
    return struct.unpack(">H", IMG[o:o + 2])[0] if 0 <= o < len(IMG) - 1 else 0


def r32(a):
    o = a - BASE
    return struct.unpack(">I", IMG[o:o + 4])[0] if 0 <= o < len(IMG) - 3 else 0


FUNCS = []        # (addr, size, name, module)
for ln in open(os.path.join(AN, "functions.tsv")):
    p = ln.rstrip("\n").split("\t")
    if len(p) >= 4:
        FUNCS.append((int(p[0], 16), int(p[1]), p[2], p[3]))
FUNCS.sort()
BYADDR = {f[0]: f for f in FUNCS}
BYNAME = {}
for f in FUNCS:
    BYNAME.setdefault(f[2], f)
DATA = {}
for ln in open(os.path.join(AN, "symbols.tsv")):
    p = ln.rstrip("\n").split("\t")
    if len(p) >= 4 and p[1] == "data":
        DATA.setdefault(int(p[0], 16), p[2])
# our own names for the globals the menus use (tools/menu_names.tsv: addr<TAB>name)
NAMES = os.path.join(HERE, "menu_names.tsv")
if os.path.isfile(NAMES):
    for ln in open(NAMES):
        p = ln.split("#")[0].rstrip("\n").split("\t")
        if len(p) >= 2 and p[0].strip():
            DATA[int(p[0], 16)] = p[1].strip()


def fname(a):
    f = BYADDR.get(a)
    if f:
        return f[2]
    return None


def func_at(a):
    best = None
    for f in FUNCS:
        if f[0] <= a < f[0] + max(f[1], 2):
            best = f
    return best


def sym(v):
    if v < 0x00200000 or (0x00300000 <= v < 0x06000000):
        return None
    n = fname(v)
    if n:
        return n
    if v in DATA:
        return DATA[v]
    # inside a known data object?
    return None


def val_str(v):
    s = sym(v)
    return "%s(0x%08X)" % (s, v) if s else "0x%08X" % v


def resolve_decomp(txt):
    def ptr(m):
        slot = int(m.group(2), 16)
        v = r32(slot)
        return "@" + val_str(v)

    def const(m):
        a = int(m.group(1), 16)
        if a % 4 == 2:
            return "K(0x%04X)" % r16(a)
        w, l_ = r16(a), r32(a)
        return "K(0x%04X|0x%08X)" % (w, l_)

    def fun(m):
        a = int(m.group(1), 16)
        n = fname(a)
        return n if n else "FUN_%08x" % a

    txt = re.sub(r"\bPTR_(\w*?)_?((?:[0-9a-f]{8}))\b(?!_)", lambda m: ptr(re.match(r"(.*?)([0-9a-f]{8})$", m.group(0))) if False else None or "", txt) if False else txt
    # PTR_<anything>_<slot>: the slot address is the last 8 hex digits
    txt = re.sub(r"\bPTR_[A-Za-z0-9_]*?([0-9a-f]{8})\b", lambda m: "@" + val_str(r32(int(m.group(1), 16))), txt)
    txt = re.sub(r"\bDAT_([0-9a-f]{8})\b", const, txt)
    txt = re.sub(r"\bFUN_([0-9a-f]{8})\b", fun, txt)
    txt = re.sub(r"\bLAB_([0-9a-f]{8})\b", lambda m: "label_%s" % m.group(1), txt)
    return txt


def decomp_of(f):
    addr, size, name, mod = f
    cands = [n for n in os.listdir(os.path.join(AN, "decomp")) if n.endswith("__%s.c" % name) or n == "__%s.c" % name or n == "%s.c" % name]
    for n in (["%s__%s.c" % (mod, name)] + cands + ["__FUN_%08x.c" % addr, "FUN_%08x.c" % addr]):
        p = os.path.join(AN, "decomp", n)
        if os.path.isfile(p):
            return open(p).read()
    return None


def disasm(f):
    addr, size, name, mod = f
    out = []
    pool = set()
    a = addr
    end = addr + size
    # first pass: find pool words referenced by pc-relative loads
    while a < end:
        o = sh2dis.w(a)
        if (o >> 12) == 9:
            pool.add(a + 4 + (o & 0xFF) * 2)
        elif (o >> 12) == 13:
            t = ((a + 4) & ~3) + (o & 0xFF) * 4
            pool.add(t)
            pool.add(t + 2)
        a += 2
    labels = set()
    a = addr
    while a < end:
        if a in pool:
            a += 2
            continue
        t = sh2dis.dis(a)
        m = re.match(r"(bt|bf|bt/s|bf/s|bra|bsr) 0x([0-9a-f]+)", t)
        if m and not m.group(1) == "bsr":
            labels.add(int(m.group(2), 16))
        a += 2
    a = addr
    while a < end:
        if a in pool:
            if a % 4 == 0 and (a + 2) in pool:
                out.append("%08x  .long 0x%08x   %s" % (a, r32(a), sym(r32(a)) or ""))
                a += 4
            else:
                out.append("%08x  .word 0x%04x" % (a, r16(a)))
                a += 2
            continue
        try:
            t = sh2dis.dis(a)
        except Exception:
            t = ".word 0x%04x" % sh2dis.w(a)
        m = re.search(r"; =([0-9a-f]{8})$", t)
        if m:
            s = sym(int(m.group(1), 16))
            if s:
                t += "  <%s>" % s
        m = re.match(r"bsr 0x([0-9a-f]+)", t)
        if m:
            s = fname(int(m.group(1), 16))
            if s:
                t += "  <%s>" % s
        lab = "L%08x:" % a if a in labels else ""
        out.append("%08x  %-10s %s" % (a, lab, t))
        a += 2
    return "\n".join(out)


def find_func(key):
    if key in BYNAME:
        return BYNAME[key]
    try:
        a = int(key, 16)
    except ValueError:
        return None
    return BYADDR.get(a) or func_at(a)


def search(addr):
    t = struct.pack(">I", addr)
    i = 0
    res = {}
    while True:
        i = IMG.find(t, i)
        if i < 0:
            break
        slot = BASE + i
        # which function is nearest before the slot (literal pools follow their function)
        f = func_at(slot) or max((x for x in FUNCS if x[0] <= slot), default=None)
        res.setdefault(f[2] if f else "?", []).append(slot)
        i += 1
    return res


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return
    if args[0] == "-s":
        a = int(args[1], 16)
        for n, slots in search(a).items():
            print("%-30s %s" % (n, " ".join("%08x" % s for s in slots)))
        return
    if args[0] == "-m":
        for f in FUNCS:
            if f[3] == args[1]:
                show(f, False)
        return
    f = find_func(args[0])
    if not f:
        print("not found", args[0])
        return
    show(f, "-a" in args)


def show(f, asm):
    print("// %s::%s @ %08x (%d bytes)" % (f[3], f[2], f[0], f[1]))
    d = decomp_of(f)
    if d:
        d = d.split("\n", 1)[1] if d.startswith("//") else d
        print(resolve_decomp(d).strip("\n"))
    if asm or not d:
        print(disasm(f))


if __name__ == "__main__":
    main()
