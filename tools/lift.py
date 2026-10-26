"""M8 reading aid (tools only): lift SH-2 code of 0RALLY.DAT into explicit C-like statements.  Not a decompiler: straight-line symbolic execution per basic block with
constant folding of the literal-pool loads, resolved names of functions / known globals, delay slots placed before their branch, T-bit conditions as expressions.
The output is for reading while porting by hand; registers keep their names (r4 .. r9 are argument registers of the original's own conventions).

    python tools/lift.py <function name | address> [end address]     one function (or a raw address range)
    python tools/lift.py -m <module>                                  every function of a module
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rd  # noqa: E402
import sh2dis  # noqa: E402

M32 = 0xFFFFFFFF


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s8(v):
    v &= 0xFF
    return v - 0x100 if v & 0x80 else v


class V:
    """a value: constant (c) or expression (s)"""
    __slots__ = ("c", "s")

    def __init__(self, c=None, s=None):
        self.c = None if c is None else c & M32
        self.s = s if s is not None else ("0x%X" % self.c if self.c is not None else "?")

    def __str__(self):
        if self.c is not None:
            return cstr(self.c)
        return self.s


def cstr(c):
    n = None if C_MODE else rd.sym(c)
    if n:
        return n
    if c >= 0x10000 or c > 0xFF and (c & 0xFFFF0000) in (0x06000000, 0x25000000, 0x25C00000):
        return "0x%08X" % c
    if c >= 0x80000000:
        return "%d" % s32(c)
    return "%d" % c if c < 256 else "0x%X" % c


C_MODE = False


def addr_str(c):
    """a constant address -> name or hex"""
    if C_MODE:
        return "0x%08X" % c
    n = rd.sym(c)
    return n if n else "0x%08X" % c


def const(c):
    return V(c)


def var(n):
    return V(None, n)


def bin_(op, a, b, fn):
    if a.c is not None and b.c is not None:
        return V(fn(a.c, b.c))
    return V(None, "(%s %s %s)" % (a, op, b))


class Lift:
    @property
    def T(self):
        return self._T

    @T.setter
    def T(self, v):
        self._T = v
        self.T_used = False

    def __init__(self, f):
        self.f = f
        self.addr, self.size = f[0], f[1]
        self.end = self.addr + self.size
        self.out = []
        self.pool = set()
        self.labels = set()
        self.reg = [var("r%d" % i) for i in range(16)]
        self.pend = {}           # registers holding a constant that has not been printed yet: flushed before every branch / label so that the C is complete
        self.T_used = False      # a conditional branch has consumed the pending compare result
        self.T = None            # the pending compare result as an expression string
        self.mac = var("macl")
        self.mach = var("mach")
        self.inedges = {}
        self.block_end = None      # end address of a short conditional skip: instructions are printed inside `if (...) {`
        self.dirty = set()
        self.prep()

    # --- pass 1: pool + labels -----------------------------------------------------------------------------------------
    def prep(self):
        a = self.addr
        while a < self.end:
            o = sh2dis.w(a)
            if (o >> 12) == 9:
                self.pool.add(a + 4 + (o & 0xFF) * 2)
            elif (o >> 12) == 13:
                t = ((a + 4) & ~3) + (o & 0xFF) * 4
                self.pool.update((t, t + 2))
            elif (o >> 12) == 12 and ((o >> 8) & 15) == 7:
                t = ((a + 4) & ~3) + (o & 0xFF) * 4
                self.pool.update((t, t + 2))
            a += 2
        a = self.addr
        while a < self.end:
            if a in self.pool:
                a += 2
                continue
            o = sh2dis.w(a)
            h = o >> 12
            tgt = None
            if h == 8 and ((o >> 8) & 15) in (9, 11, 13, 15):
                tgt = a + 4 + s8(o & 0xFF) * 2
            elif h == 10:
                tgt = a + 4 + ((o & 0xFFF) - 0x1000 if o & 0x800 else o & 0xFFF) * 2
            if tgt is not None and self.addr <= tgt < self.end:
                self.labels.add(tgt)
                self.inedges[tgt] = self.inedges.get(tgt, 0) + 1
            a += 2

    # --- helpers -----------------------------------------------------------------------------------------------------------
    def emit(self, a, txt):
        self.out.append("%08x  %s" % (a, txt))

    def touch(self, a, n):
        """register n is about to change: a pending compare result that mentions it is saved first"""
        if self.T is not None and not self.T_used and re.search(r"(?<![0-9])r%d(?![0-9])" % n, self.T) and self.T != "t":
            self.emit(a, "t = %s;" % self.T)
            self.T = "t"

    def setreg(self, a, n, v):
        """assign a value to register n: constants are tracked silently, other values get an assignment statement"""
        self.touch(a, n)
        if self.block_end is not None:
            self.dirty.add(n)
            self.emit(a, "  r%d = %s;" % (n, v))
            self.reg[n] = var("r%d" % n)
            return
        if v.c is not None:
            self.reg[n] = v
            self.pend[n] = v
            return
        self.pend.pop(n, None)
        if v.s == "r%d" % n:
            self.reg[n] = v
            return
        self.emit(a, "r%d = %s;" % (n, v.s))
        self.reg[n] = var("r%d" % n)

    def flush(self, a):
        for n in sorted(self.pend):
            if self.reg[n].c is not None:                  # still a constant
                self.emit(a, "r%d = %s;   // const" % (n, self.reg[n]))
        self.pend = {}

    def reset(self):
        self.reg = [var("r%d" % i) for i in range(16)]
        self.T = None
        self.pend = {}

    def mem(self, kind, addr):
        if addr.c is not None:
            return "%s[%s]" % (kind, addr_str(addr.c))
        return "*(%s*)(%s)" % (kind, addr)

    def cond(self):
        v = self._T if self._T is not None else "T"
        self.T_used = True
        return v

    def neg(self, c):
        if c.startswith("!") and not c.startswith("!("):
            return c[1:]
        m = re.match(r"^\((.*) (==|!=|>=|<=|>|<) (.*)\)$", c)
        if m and "&&" not in c and "||" not in c:
            inv = {"==": "!=", "!=": "==", ">=": "<", "<=": ">", ">": "<=", "<": ">="}[m.group(2)]
            return "(%s %s %s)" % (m.group(1), inv, m.group(3))
        return "!%s" % c

    # --- one instruction ----------------------------------------------------------------------------------------------------
    def step(self, a, delay=False):
        """returns ('branch'|'call'|'ret'|'jmp', info) for control-flow instructions, else None"""
        o = sh2dis.w(a)
        h = o >> 12
        n = (o >> 8) & 15
        m = (o >> 4) & 15
        d = o & 15
        i = o & 0xFF
        R = self.reg
        if o == 0x0009:
            return None
        if o == 0x000B:
            return ("ret", None)
        if o == 0x002B:
            return ("rte", None)
        if o == 0x0008:
            self.T = "0"
            return None
        if o == 0x0018:
            self.T = "1"
            return None
        if o == 0x0019:
            self.emit(a, "div0u;")
            return None
        if o == 0x001B:
            self.emit(a, "sleep;")
            return None
        if o == 0x0028:
            self.emit(a, "clrmac;")
            self.mac = var("macl")
            self.mach = var("mach")
            return None
        if h == 0:
            if d == 2:
                src = {0: "SR", 1: "GBR", 2: "VBR"}.get(m, "?")
                self.setreg(a, n, var(src))
                return None
            if o & 0xF0FF == 0x0003:
                return ("bsrf", n)
            if o & 0xF0FF == 0x0023:
                return ("braf", n)
            if d in (4, 5, 6):
                k = {4: "u8", 5: "u16", 6: "u32"}[d]
                ad = bin_("+", R[0], R[n], lambda x, y: x + y)
                self.emit(a, "%s = %s;" % (self.mem(k, ad), R[m]))
                return None
            if d == 7:
                self.mac = bin_("*", R[m], R[n], lambda x, y: x * y)
                self.emit(a, "macl = %s;  // mul.l" % self.mac)
                return None
            if d == 0xA:
                src = {0: "mach", 1: "macl", 2: "pr"}[m]
                self.setreg(a, n, self.mach if src == "mach" else self.mac if src == "macl" else var("pr"))
                return None
            if d in (0xC, 0xD, 0xE):
                k = {0xC: "s8", 0xD: "s16", 0xE: "s32"}[d]
                ad = bin_("+", R[0], R[m], lambda x, y: x + y)
                self.setreg(a, n, var(self.mem(k, ad)))
                return None
            if d == 0xF:
                self.emit(a, "mac.l @r%d+,@r%d+;" % (m, n))
                return None
            if i == 0x29:
                self.setreg(a, n, var("(%s)" % (self.T or "T")))
                return None
        if h == 1:
            ad = bin_("+", R[n], const(d * 4), lambda x, y: x + y)
            self.emit(a, "%s = %s;" % (self.mem("u32", ad), R[m]))
            return None
        if h == 2:
            if d in (0, 1, 2):
                k = {0: "u8", 1: "u16", 2: "u32"}[d]
                self.emit(a, "%s = %s;" % (self.mem(k, R[n]), R[m]))
                return None
            if d in (4, 5, 6):
                sz = {4: 1, 5: 2, 6: 4}[d]
                k = {4: "u8", 5: "u16", 6: "u32"}[d]
                self.touch(a, n)
                nv = bin_("-", R[n], const(sz), lambda x, y: x - y)
                self.reg[n] = nv if nv.c is not None else var("r%d" % n)
                if nv.c is None:
                    self.emit(a, "r%d -= %d;" % (n, sz))
                self.emit(a, "%s = %s;" % (self.mem(k, self.reg[n]), R[m]))
                return None
            if d == 7:
                self.T = "((%s < 0) != (%s < 0))" % (R[n], R[m])
                self.emit(a, "div0s(%s,%s);" % (R[m], R[n]))
                return None
            if d == 8:
                self.T = "((%s & %s) == 0)" % (R[n], R[m])
                return None
            if d in (9, 0xA, 0xB):
                op = {9: "&", 0xA: "^", 0xB: "|"}[d]
                fn = {9: lambda x, y: x & y, 0xA: lambda x, y: x ^ y, 0xB: lambda x, y: x | y}[d]
                if d == 0xA and m == n:
                    self.setreg(a, n, const(0))
                    return None
                self.setreg(a, n, bin_(op, R[n], R[m], fn))
                return None
            if d == 0xC:
                self.T = "cmpstr(%s,%s)" % (R[n], R[m])
                return None
            if d == 0xD:
                self.setreg(a, n, V(None, "xtrct(%s,%s)" % (R[n], R[m])))
                return None
            if d == 0xE:
                self.mac = V(None, "((u16)%s * (u16)%s)" % (R[n], R[m]))
                self.emit(a, "macl = %s;" % self.mac)
                return None
            if d == 0xF:
                self.mac = V(None, "((s16)%s * (s16)%s)" % (R[n], R[m]))
                self.emit(a, "macl = %s;" % self.mac)
                return None
        if h == 3:
            if d == 0:
                self.T = "(%s == %s)" % (R[n], R[m])
                return None
            if d == 2:
                self.T = "((u32)%s >= (u32)%s)" % (R[n], R[m])
                return None
            if d == 3:
                self.T = "(%s >= %s)" % (R[n], R[m])
                return None
            if d == 6:
                self.T = "((u32)%s > (u32)%s)" % (R[n], R[m])
                return None
            if d == 7:
                self.T = "(%s > %s)" % (R[n], R[m])
                return None
            if d == 8:
                self.setreg(a, n, bin_("-", R[n], R[m], lambda x, y: x - y))
                return None
            if d == 0xC:
                self.setreg(a, n, bin_("+", R[n], R[m], lambda x, y: x + y))
                return None
            if d in (0xA, 0xE, 0xB, 0xF):
                nm = {0xA: "subc", 0xE: "addc", 0xB: "subv", 0xF: "addv"}[d]
                self.emit(a, "r%d = %s(%s, %s);  // T" % (n, nm, R[n], R[m]))
                self.reg[n] = var("r%d" % n)
                return None
            if d == 4:
                self.emit(a, "div1(r%d, r%d);" % (m, n))
                self.reg[n] = var("r%d" % n)
                return None
            if d in (5, 0xD):
                sg = "s" if d == 0xD else "u"
                self.emit(a, "mach:macl = d%smul(%s, %s);" % (sg, R[n], R[m]))
                self.mac = var("macl")
                self.mach = var("mach")
                return None
        if h == 4:
            low = i
            if low == 0x0B:
                return ("jsr", n)
            if low == 0x2B:
                return ("jmp", n)
            if low == 0x00:
                self.T = "(%s < 0)" % R[n]
                self.setreg(a, n, bin_("<<", R[n], const(1), lambda x, y: x << y))
                return None
            if low == 0x01:
                self.T = "(%s & 1)" % R[n]
                self.setreg(a, n, V(None, "((u32)%s >> 1)" % R[n]) if R[n].c is None else const(R[n].c >> 1))
                return None
            if low == 0x20:
                self.T = "(%s < 0)" % R[n]
                self.setreg(a, n, bin_("<<", R[n], const(1), lambda x, y: x << y))
                return None
            if low == 0x21:
                self.T = "(%s & 1)" % R[n]
                self.setreg(a, n, V(None, "(%s >> 1)" % R[n]) if R[n].c is None else const(s32(R[n].c) >> 1))
                return None
            if low in (0x04, 0x05, 0x24, 0x25):
                nm = {0x04: "rotl", 0x05: "rotr", 0x24: "rotcl", 0x25: "rotcr"}[low]
                self.emit(a, "r%d = %s(r%d);  // T" % (n, nm, n))
                self.reg[n] = var("r%d" % n)
                return None
            if low in (0x08, 0x18, 0x28):
                sh = {0x08: 2, 0x18: 8, 0x28: 16}[low]
                self.setreg(a, n, bin_("<<", R[n], const(sh), lambda x, y: x << y))
                return None
            if low in (0x09, 0x19, 0x29):
                sh = {0x09: 2, 0x19: 8, 0x29: 16}[low]
                self.setreg(a, n, V(None, "((u32)%s >> %d)" % (R[n], sh)) if R[n].c is None else const(R[n].c >> sh))
                return None
            if low == 0x10:
                nv = bin_("-", R[n], const(1), lambda x, y: x - y)
                self.setreg(a, n, nv)
                self.T = "(%s == 0)" % self.reg[n]
                return None
            if low == 0x11:
                self.T = "(%s >= 0)" % R[n]
                return None
            if low == 0x15:
                self.T = "(%s > 0)" % R[n]
                return None
            if low == 0x1B:
                self.emit(a, "tas.b(%s);" % self.mem("u8", R[n]))
                self.T = "tas"
                return None
            if low in (0x0E, 0x1E, 0x2E):
                nm = {0x0E: "SR", 0x1E: "GBR", 0x2E: "VBR"}[low]
                self.emit(a, "%s = %s;" % (nm, R[n]))
                return None
            if low in (0x0A, 0x1A, 0x2A):
                nm = {0x0A: "mach", 0x1A: "macl", 0x2A: "pr"}[low]
                if nm == "macl":
                    self.mac = R[n]
                elif nm == "mach":
                    self.mach = R[n]
                self.emit(a, "%s = %s;" % (nm, R[n]))
                return None
            if low in (0x07, 0x17, 0x27, 0x06, 0x16, 0x26):
                nm = {0x07: "SR", 0x17: "GBR", 0x27: "VBR", 0x06: "mach", 0x16: "macl", 0x26: "pr"}[low]
                self.emit(a, "%s = *(u32*)(r%d); r%d += 4;" % (nm, n, n))
                self.reg[n] = var("r%d" % n)
                return None
            if low in (0x03, 0x13, 0x23, 0x02, 0x12, 0x22):
                nm = {0x03: "SR", 0x13: "GBR", 0x23: "VBR", 0x02: "mach", 0x12: "macl", 0x22: "pr"}[low]
                self.emit(a, "r%d -= 4; *(u32*)(r%d) = %s;" % (n, n, nm))
                self.reg[n] = var("r%d" % n)
                return None
            if d == 0xF:
                self.emit(a, "mac.w @r%d+,@r%d+;" % (m, n))
                return None
        if h == 5:
            ad = bin_("+", R[m], const(d * 4), lambda x, y: x + y)
            self.setreg(a, n, var(self.mem("s32", ad)))
            return None
        if h == 6:
            if d in (0, 1, 2):
                k = {0: "s8", 1: "s16", 2: "s32"}[d]
                self.setreg(a, n, var(self.mem(k, R[m])))
                return None
            if d == 3:
                if n != m:
                    self.setreg(a, n, R[m])
                return None
            if d in (4, 5, 6):
                k = {4: "s8", 5: "s16", 6: "s32"}[d]
                sz = {4: 1, 5: 2, 6: 4}[d]
                src = var(self.mem(k, R[m]))
                old_m = R[m]
                self.touch(a, n)
                self.touch(a, m)
                self.emit(a, "%sr%d = %s;" % ("  " if self.block_end is not None else "", n, src))
                self.reg[n] = var("r%d" % n)
                if self.block_end is not None:
                    self.dirty.add(n)
                if m != n:
                    nv = bin_("+", old_m, const(sz), lambda x, y: x + y)
                    if nv.c is not None:
                        self.reg[m] = nv
                    else:
                        self.emit(a, "r%d += %d;" % (m, sz))
                        self.reg[m] = var("r%d" % m)
                return None
            if d == 7:
                self.setreg(a, n, V(None, "~%s" % R[m]) if R[m].c is None else const(~R[m].c))
                return None
            if d == 8:
                self.setreg(a, n, V(None, "swapb(%s)" % R[m]) if R[m].c is None else const((R[m].c & 0xFFFF0000) | ((R[m].c & 0xFF) << 8) | ((R[m].c >> 8) & 0xFF)))
                return None
            if d == 9:
                self.setreg(a, n, V(None, "swapw(%s)" % R[m]) if R[m].c is None else const(((R[m].c & 0xFFFF) << 16) | (R[m].c >> 16)))
                return None
            if d == 0xA:
                self.emit(a, "r%d = negc(%s);" % (n, R[m]))
                self.reg[n] = var("r%d" % n)
                return None
            if d == 0xB:
                self.setreg(a, n, V(None, "-%s" % R[m]) if R[m].c is None else const(-R[m].c))
                return None
            if d == 0xC:
                self.setreg(a, n, V(None, "(u8)%s" % R[m]) if R[m].c is None else const(R[m].c & 0xFF))
                return None
            if d == 0xD:
                self.setreg(a, n, V(None, "(u16)%s" % R[m]) if R[m].c is None else const(R[m].c & 0xFFFF))
                return None
            if d == 0xE:
                self.setreg(a, n, V(None, "(s8)%s" % R[m]) if R[m].c is None else const(s8(R[m].c)))
                return None
            if d == 0xF:
                self.setreg(a, n, V(None, "(s16)%s" % R[m]) if R[m].c is None else const(s16(R[m].c)))
                return None
        if h == 7:
            self.setreg(a, n, bin_("+", R[n], const(s8(i)), lambda x, y: x + y))
            return None
        if h == 8:
            sub = n
            if sub in (0, 1):
                k = "u8" if sub == 0 else "u16"
                ad = bin_("+", R[m], const(d * (1 if sub == 0 else 2)), lambda x, y: x + y)
                self.emit(a, "%s = %s;" % (self.mem(k, ad), R[0]))
                return None
            if sub in (4, 5):
                k = "s8" if sub == 4 else "s16"
                ad = bin_("+", R[m], const(d * (1 if sub == 4 else 2)), lambda x, y: x + y)
                self.setreg(a, 0, var(self.mem(k, ad)))
                return None
            if sub == 8:
                self.T = "(%s == %d)" % (R[0], s8(i))
                return None
            if sub in (9, 11, 13, 15):
                tgt = a + 4 + s8(i) * 2
                return ("bcc", (sub in (9, 13), sub in (13, 15), tgt))
        if h == 9:
            t = a + 4 + i * 2
            v = s16(rd.r16(t))
            self.setreg(a, n, const(v))
            return None
        if h == 10:
            return ("bra", a + 4 + ((o & 0xFFF) - 0x1000 if o & 0x800 else o & 0xFFF) * 2)
        if h == 11:
            return ("bsr", a + 4 + ((o & 0xFFF) - 0x1000 if o & 0x800 else o & 0xFFF) * 2)
        if h == 12:
            sub = n
            if sub in (0, 1, 2):
                k = {0: "u8", 1: "u16", 2: "u32"}[sub]
                off = i * {0: 1, 1: 2, 2: 4}[sub]
                self.emit(a, "%s = %s;  // gbr" % ("*(%s*)(GBR+%d)" % (k, off), R[0]))
                return None
            if sub in (4, 5, 6):
                k = {4: "s8", 5: "s16", 6: "s32"}[sub]
                off = i * {4: 1, 5: 2, 6: 4}[sub]
                self.setreg(a, 0, var("*(%s*)(GBR+%d)" % (k, off)))
                return None
            if sub == 3:
                self.emit(a, "trapa(%d);" % i)
                return None
            if sub == 7:
                t = ((a + 4) & ~3) + i * 4
                self.setreg(a, 0, const(t))
                return None
            if sub == 8:
                self.T = "((%s & 0x%X) == 0)" % (R[0], i)
                return None
            if sub in (9, 10, 11):
                op = {9: "&", 10: "^", 11: "|"}[sub]
                fn = {9: lambda x, y: x & y, 10: lambda x, y: x ^ y, 11: lambda x, y: x | y}[sub]
                self.setreg(a, 0, bin_(op, R[0], const(i), fn))
                return None
            if sub in (12, 13, 14, 15):
                nm = {12: "tst.b", 13: "and.b", 14: "xor.b", 15: "or.b"}[sub]
                if sub == 12:
                    self.T = "((*(u8*)(GBR+r0) & 0x%X) == 0)" % i
                else:
                    self.emit(a, "*(u8*)(GBR+r0) %s= 0x%X;" % ({13: "&", 14: "^", 15: "|"}[sub], i))
                return None
        if h == 13:
            t = ((a + 4) & ~3) + i * 4
            self.setreg(a, n, const(rd.r32(t)))
            return None
        if h == 14:
            self.setreg(a, n, const(s8(i)))
            return None
        self.emit(a, ".word 0x%04x" % o)
        return None

    # --- walk ----------------------------------------------------------------------------------------------------------------
    def callstr(self, tgt_v, via=None):
        args = []
        for r in (4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0, 1, 2, 3):
            v = self.reg[r]
            if r == 0 and str(v) == str(tgt_v):
                continue
            if v.c is not None or v.s != "r%d" % r:
                args.append("r%d=%s" % (r, v))
        t = str(tgt_v)
        return "%s(%s)" % (t, ", ".join(args))

    def run(self):
        a = self.addr
        self.out.append("// %s::%s @ %08x (%d bytes)" % (self.f[3], self.f[2], self.addr, self.size))
        while a < self.end:
            if a in self.pool:
                a += 2
                continue
            if self.block_end is not None and a == self.block_end:
                self.out.append("        }")
                for r_ in self.dirty:
                    self.reg[r_] = var("r%d" % r_)
                self.T = None
                self.block_end = None
                self.dirty = set()
                if self.inedges.get(a, 0) > 1:
                    self.out.append("L%08x:" % a)
                    self.reset()
            elif a in self.labels:
                self.flush(a)
                self.out.append("L%08x:" % a)
                self.reset()
            try:
                r = self.step(a)
            except (KeyError, IndexError, ValueError):
                self.emit(a, ".word 0x%04x" % sh2dis.w(a))
                r = None
            if r is None:
                a += 2
                continue
            kind, info = r
            # delay slot
            ds = a + 2
            if kind in ("bcc",) and not (info[1]):
                # no delay slot
                self.flush(a)
                taken_cond = self.cond()
                bt = info[0]
                c = taken_cond if bt else self.neg(taken_cond)
                tgt = info[2]
                if self.block_end is None and a < tgt <= a + 14 and self.inedges.get(tgt, 0) == 1:
                    self.emit(a, "if %s {" % self.neg(c))
                    self.block_end = tgt
                    self.dirty = set()
                    self.labels.discard(tgt)
                    a += 2
                    continue
                self.emit(a, "if %s goto L%08x;" % (c, tgt))
                a += 2
                continue
            if kind == "ret":
                self.step(ds, True)
                rv = self.reg[0]
                self.emit(a, "return%s;" % ("" if rv.s == "r0" and rv.c is None else " r0=%s" % rv))
                a += 4
                continue
            if kind == "rte":
                self.step(ds, True)
                self.emit(a, "rte;")
                a += 4
                continue
            # delay-slot instruction first
            if kind == "bcc":
                self.flush(a)
                bt = info[0]
                c0 = self.cond()
                if re.search(r"(?<![0-9])r[0-9]+(?![0-9])", c0):
                    self.emit(a, "t0 = %s;" % c0)
                    c0 = "t0"
                self.step(ds, True)
                c = c0 if bt else self.neg(c0)
                self.emit(a, "if %s goto L%08x;   // delayed" % (c, info[2]))
                a += 4
                continue
            if kind == "bra":
                self.step(ds, True)
                self.flush(a)
                self.emit(a, "goto L%08x;" % info)
                a += 4
                continue
            if kind == "bsr":
                self.step(ds, True)
                nm = rd.fname(info) or "FUN_%08x" % info
                self.emit(a, "call %s;" % self.callstr(nm))
                self.after_call()
                a += 4
                continue
            if kind in ("jsr", "bsrf"):
                tv = self.reg[info]
                self.step(ds, True)
                if kind == "bsrf":
                    tv = var("pc+r%d" % info)
                tn = tv
                if tv.c is not None:
                    tn = var(rd.fname(tv.c) or "FUN_%08x" % tv.c)
                self.emit(a, "call %s;" % self.callstr(tn))
                self.after_call()
                a += 4
                continue
            if kind in ("jmp", "braf"):
                tv = self.reg[info]
                self.step(ds, True)
                if tv.c is not None:
                    nm = rd.fname(tv.c) or "FUN_%08x" % tv.c
                    self.emit(a, "tailcall %s;" % self.callstr(var(nm)))
                else:
                    self.emit(a, "jump %s;" % tv)
                a += 4
                continue
            a += 2
        last = [l for l in self.out if re.match(r"^[0-9a-f]{8}  ", l)]
        if last and not re.search(r"  (return|goto L|tailcall|jump|rte)", last[-1]):
            self.out.append("// !!! FALLS THROUGH into the code after 0x%08x (the function table cuts it here)" % self.end)
        return "\n".join(self.out)

    def after_call(self):
        for r in range(0, 10):
            self.reg[r] = var("r%d" % r)
        self.T = None


def lift_func(f):
    return Lift(f).run()


def main():
    args = sys.argv[1:]
    cmode = "--c" in args
    if cmode:
        args.remove("--c")
        global C_MODE
        C_MODE = True
    if not args:
        print(__doc__)
        return
    if args[0] == "-m":
        for f in rd.FUNCS:
            if f[3] == args[1]:
                t = lift_func(f)
                print(to_c(t) if cmode else t)
                print()
        return
    f = rd.find_func(args[0])
    if len(args) > 1:
        f = (int(args[0], 16), int(args[1], 16) - int(args[0], 16), "range", "")
    if not f:
        print("not found")
        return
    t = lift_func(f)
    print(to_c(t) if cmode else t)



# ---------------------------------------------------------------------------------------------------------------------------------------
# --c: turn the lifted statements into compilable C on the Sat memory model (a starting point to restructure by hand, not the final code)
# ---------------------------------------------------------------------------------------------------------------------------------------
def _match_paren(s, i):
    """s[i] == '(': index of the matching ')'"""
    d = 0
    for j in range(i, len(s)):
        if s[j] == "(":
            d += 1
        elif s[j] == ")":
            d -= 1
            if d == 0:
                return j
    return -1


def _strip_outer(e):
    e = e.strip()
    while e.startswith("(") and _match_paren(e, 0) == len(e) - 1:
        e = e[1:-1].strip()
    return e


def to_c(text):
    out = []
    sizes = {"s8": "RS8", "s16": "RS16", "s32": "RS32", "u8": "R8", "u16": "R16", "u32": "R32"}
    wr = {"u8": "W8", "u16": "W16", "u32": "W32", "s8": "W8", "s16": "W16", "s32": "W32"}
    for ln in text.split("\n"):
        m = re.match(r"^([0-9a-f]{8})  (.*)$", ln)
        if not m:
            out.append(ln)
            continue
        addr, st = m.group(1), m.group(2)
        ind = ""
        if st.startswith("  "):
            ind = "    "
            st = st.strip()
        # stores: u8[X] = V;
        mm = re.match(r"^(u8|u16|u32)\[(\w+)\] = (.*);(.*)$", st)
        if mm:
            st = "%s(%s, %s);%s" % (wr[mm.group(1)], mm.group(2), mm.group(3), mm.group(4))
        # stores: *(u8*)(expr) = V;
        mm = re.match(r"^\*\((u8|u16|u32)\*\)\(", st)
        if mm:
            i = st.index("(", 1) if False else len(mm.group(0)) - 1
            j = _match_paren(st, i)
            rest = st[j + 1:]
            mr = re.match(r"^ = (.*?);(.*)$", rest)
            if mr:
                st = "%s(%s, %s);%s" % (wr[mm.group(1)], _strip_outer(st[i:j + 1]), mr.group(1), mr.group(2))
        # loads: s8[X] and *(s16*)(expr)
        def repl_const(mt):
            return "%s(%s)" % (sizes[mt.group(1)], mt.group(2))
        st = re.sub(r"\b(s8|s16|s32)\[(\w+)\]", repl_const, st)
        while True:
            mt = re.search(r"\*\((s8|s16|s32|u8|u16|u32)\*\)\(", st)
            if not mt:
                break
            i = mt.end() - 1
            j = _match_paren(st, i)
            st = st[:mt.start()] + "%s(%s)" % (sizes[mt.group(1)], _strip_outer(st[i:j + 1])) + st[j + 1:]
        # calls
        mm = re.match(r"^call (\w+)\((.*)\);$", st)
        if mm:
            st = "%s();  /* %s */" % (mm.group(1), mm.group(2)) if mm.group(2) else "%s();" % mm.group(1)
        mm = re.match(r"^tailcall (\w+)\((.*)\);$", st)
        if mm:
            st = "%s(); return;  /* %s */" % (mm.group(1), mm.group(2))
        st = st.replace("(u32)", "(u32)")
        out.append("%s/* %s */ %s%s" % ("", addr, ind, st))
    return "\n".join(out)

if __name__ == "__main__":
    main()
