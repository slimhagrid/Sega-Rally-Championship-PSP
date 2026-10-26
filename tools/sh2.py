"""M4: a small SH-2 interpreter (big endian, tools only - never part of the PSP build).

It runs routines of the original `0RALLY.DAT` headless so the C port can be checked against the real code (the "oracle").
Covers the complete SH-1/SH-2 integer instruction set (incl. mac.w / mac.l with the S flag, div0s/div0u/div1, tas.b, dmuls/dmulu,
delay slots), the on-chip divider (DVSR/DVDNT/DVDNTH/DVDNTL at 0xFFFFFF00) and a flat memory map:
    High WRAM  0x06000000-0x060FFFFF (mirrored through 0x07FFFFFF)
    Low WRAM   0x00200000-0x002FFFFF
    everything else: sparse 4 KB pages (VDP/SCU/SCSP registers and RAM read back what was written)
Cache-through / cache-purge aliases (bits 31-29 of the address) are ignored.

    cpu = CPU(Mem())
    cpu.call(0x06006C4C, {14: slot, 15: sp})        # runs until the routine returns; hooks can replace routines

Hooks: `cpu.hooks[addr] = fn(cpu)` is called instead of executing the instruction at addr; fn must set `cpu.pc`
(`cpu.ret()` = `rts` without a delay slot).  Write watches: `mem.watch = fn(addr, size, value)` (called for every store).
"""
import struct

M32 = 0xFFFFFFFF
_U32 = struct.Struct(">I")
_S32 = struct.Struct(">i")
_U16 = struct.Struct(">H")
_S16 = struct.Struct(">h")


def s32(x):
    x &= M32
    return x - 0x100000000 if x & 0x80000000 else x


def s16(x):
    x &= 0xFFFF
    return x - 0x10000 if x & 0x8000 else x


def s8(x):
    x &= 0xFF
    return x - 0x100 if x & 0x80 else x


class SimError(RuntimeError):
    pass


class Mem:
    HW = 0x06000000
    LW = 0x00200000

    def __init__(self):
        self.hw = bytearray(0x100000)
        self.lw = bytearray(0x100000)
        self.pages = {}
        self.on = {}                  # on-chip register words (0xFFFFFE00..)
        self.watch = None
        self.sinit = None
        self.divr = {0x00: 0, 0x04: 0, 0x08: 0, 0x10: 0, 0x14: 0}
        self.master = True            # BCR1.MASTER readback (bit 15 set = slave)

    # --- region lookup -------------------------------------------------------------------------
    def _loc(self, a):
        """-> (buffer, offset) for a plain RAM address"""
        a &= 0x1FFFFFFF
        if 0x06000000 <= a < 0x08000000:
            return self.hw, a & 0xFFFFF
        if 0x00200000 <= a < 0x00300000:
            return self.lw, a & 0xFFFFF
        k = a >> 12
        p = self.pages.get(k)
        if p is None:
            p = self.pages[k] = bytearray(4096)
        return p, a & 0xFFF

    def load(self, addr, data):
        data = bytes(data)
        i = 0
        while i < len(data):
            buf, off = self._loc(addr + i)
            n = min(len(data) - i, len(buf) - off)
            buf[off:off + n] = data[i:i + n]
            i += n

    def read(self, addr, n):
        out = bytearray()
        while n:
            buf, off = self._loc(addr + len(out))
            k = min(n, len(buf) - off)
            out += buf[off:off + k]
            n -= k
        return bytes(out)

    # --- loads ---------------------------------------------------------------------------------
    def r8(self, a):
        a &= M32
        if a >= 0xFFFFFE00:
            return (self.onr(a & ~3) >> (8 * (3 - (a & 3)))) & 0xFF
        b, o = self._loc(a)
        return b[o]

    def r16(self, a):
        a &= M32
        if a >= 0xFFFFFE00:
            return (self.onr(a & ~3) >> (16 if (a & 2) == 0 else 0)) & 0xFFFF
        a &= 0x1FFFFFFF
        if 0x06000000 <= a < 0x08000000:
            o = a & 0xFFFFF
            return (self.hw[o] << 8) | self.hw[o + 1]
        b, o = self._loc(a)
        return (b[o] << 8) | b[o + 1]

    def r32(self, a):
        a &= M32
        if a >= 0xFFFFFE00:
            return self.onr(a & ~3)
        a &= 0x1FFFFFFF
        if 0x06000000 <= a < 0x08000000:
            return _U32.unpack_from(self.hw, a & 0xFFFFF)[0]
        b, o = self._loc(a)
        return _U32.unpack_from(b, o)[0]

    # --- stores --------------------------------------------------------------------------------
    def w8(self, a, v):
        a &= M32
        if self.watch:
            self.watch(a, 1, v & 0xFF)
        if a >= 0xFFFFFE00:
            return
        b, o = self._loc(a)
        b[o] = v & 0xFF

    def w16(self, a, v):
        a &= M32
        if a == 0x21000000 and self.sinit is not None:       # SINIT: wakes the slave SH-2 (job pointer is in the mailbox at 0x202A4000)
            self.sinit()
            return
        if self.watch:
            self.watch(a, 2, v & 0xFFFF)
        if a >= 0xFFFFFE00:
            return
        b, o = self._loc(a)
        b[o] = (v >> 8) & 0xFF
        b[o + 1] = v & 0xFF

    def w32(self, a, v):
        a &= M32
        v &= M32
        if self.watch:
            self.watch(a, 4, v)
        if a >= 0xFFFFFE00:
            self.onw(a & ~3, v)
            return
        b, o = self._loc(a)
        _U32.pack_into(b, o, v)

    # --- on-chip registers ---------------------------------------------------------------------
    def onr(self, a):
        off = a & 0xFF
        if 0xFFFFFF00 <= a < 0xFFFFFF20:          # DIVU (+ mirrors)
            if off in (0x1C, 0x14):
                return self.divr.get(0x14, 0)
            if off == 0x18:
                return self.divr.get(0x10, 0)
            return self.divr.get(off, 0)
        if a == 0xFFFFFFE0:                       # BCR1: bit 15 = MASTER (0 = master)
            return 0x03F0 if self.master else 0x83F0
        return self.on.get(a, 0)

    def onw(self, a, v):
        off = a & 0xFF
        d = self.divr
        if 0xFFFFFF00 <= a < 0xFFFFFF20:
            if off == 0x00:
                d[0x00] = v
            elif off == 0x04:                      # DVDNT write -> 32 / 32
                d[0x04] = v
                self._div(s32(v), s32(d[0x00]))
            elif off == 0x10:
                d[0x10] = v
            elif off in (0x14,):                   # DVDNTL write -> 64 / 32
                d[0x14] = v
                n = (d[0x10] << 32) | v
                if n & (1 << 63):
                    n -= 1 << 64
                self._div(n, s32(d[0x00]))
            else:
                d[off] = v
        elif a in (0xFFFFFF8C, 0xFFFFFF9C):                  # DMAC CHCR0 / CHCR1
            self.on[a] = v
            ch = 0 if a == 0xFFFFFF8C else 0x10
            if (v & 1) and (self.on.get(0xFFFFFFB0, 0) & 1):
                self._dma(ch, v)
        elif a == 0xFFFFFFB0:                                 # DMAOR: DME starts every enabled channel
            self.on[a] = v
            if v & 1:
                for ch, reg in ((0, 0xFFFFFF8C), (0x10, 0xFFFFFF9C)):
                    chcr = self.on.get(reg, 0)
                    if (chcr & 1) and not (chcr & 2):
                        self._dma(ch, chcr)
        else:
            self.on[a] = v

    def _dma(self, ch, chcr):
        """immediate DMAC transfer (the games only start-and-poll): SAR/DAR/TCR at 0xFFFFFF80+ch, CHCR.TS = unit size, SM/DM = address mode"""
        on = self.on
        src = on.get(0xFFFFFF80 + ch, 0)
        dst = on.get(0xFFFFFF84 + ch, 0)
        cnt = on.get(0xFFFFFF88 + ch, 0) & 0xFFFFFF or 0x1000000
        size = (1, 2, 4, 16)[(chcr >> 10) & 3]
        sm = (chcr >> 12) & 3
        dm = (chcr >> 14) & 3
        step = lambda mode, sz: (0, sz, -sz, 0)[mode]
        for _ in range(cnt):
            if size == 16:
                data = [self.r32(src + 4 * k) for k in range(4)]
                for k in range(4):
                    self.w32(dst + 4 * k, data[k])
            elif size == 4:
                self.w32(dst, self.r32(src))
            elif size == 2:
                self.w16(dst, self.r16(src))
            else:
                self.w8(dst, self.r8(src))
            src = (src + step(sm, size)) & M32
            dst = (dst + step(dm, size)) & M32
        on[0xFFFFFF80 + ch] = src
        on[0xFFFFFF84 + ch] = dst
        on[0xFFFFFF88 + ch] = 0
        on[0xFFFFFF8C + ch] = (chcr & ~1) | 2                 # transfer end

    def _div(self, n, dv):
        d = self.divr
        if dv == 0:
            q = 0x7FFFFFFF if n >= 0 else 0x80000000
            r = 0
        else:
            q = abs(n) // abs(dv)
            if (n < 0) != (dv < 0):
                q = -q
            r = n - q * dv
            if not (-(1 << 31) <= q < (1 << 31)):
                q = 0x7FFFFFFF if q > 0 else 0x80000000
        d[0x04] = d[0x14] = q & M32
        d[0x10] = r & M32


class CPU:
    SENTINEL = 0xDEAD0000

    def __init__(self, mem):
        self.m = mem
        self.r = [0] * 16
        self.pc = 0
        self.pr = 0
        self.gbr = 0
        self.vbr = 0
        self.mach = 0
        self.macl = 0
        self.T = 0
        self.S = 0
        self.Q = 0
        self.M = 0
        self.I = 0
        self.hooks = {}
        self.steps = 0
        self.ops = {}
        self.hist = None              # set to collections.deque(maxlen=n) to keep the last executed pcs (debugging)

    # --- control -------------------------------------------------------------------------------
    @property
    def sr(self):
        return self.T | (self.S << 1) | (self.I << 4) | (self.Q << 8) | (self.M << 9)

    @sr.setter
    def sr(self, v):
        self.T = v & 1
        self.S = (v >> 1) & 1
        self.I = (v >> 4) & 15
        self.Q = (v >> 8) & 1
        self.M = (v >> 9) & 1

    def ret(self):
        """for hooks: behave like `rts` (no delay slot)"""
        self.pc = self.pr

    def call(self, addr, regs=None, maxsteps=5_000_000):
        """run the routine at addr until it returns (PR = sentinel); returns the number of instructions executed"""
        if regs:
            for k, v in regs.items():
                self.r[k] = v & M32
        self.pr = self.SENTINEL
        self.pc = addr
        return self.run(self.SENTINEL, maxsteps)

    def run(self, stop, maxsteps=5_000_000):
        ops = self.ops
        hooks = self.hooks
        m = self.m
        hw = m.hw
        pc = self.pc
        n = 0
        while pc != stop:
            h = hooks.get(pc)
            if h is not None:
                self.pc = pc
                h(self)
                pc = self.pc
                n += 1
                continue
            if 0x06000000 <= pc < 0x06100000:
                o = pc & 0xFFFFF
                w = (hw[o] << 8) | hw[o + 1]
            else:
                w = m.r16(pc)
            f = ops.get(w)
            if f is None:
                f = ops[w] = self._decode(w, pc)
            if m.watch is not None or self.hist is not None:
                self.pc = pc              # lets write watches see the pc of the store
                if self.hist is not None:
                    self.hist.append(pc)
            pc = f(pc)
            n += 1
            if n > maxsteps:
                self.pc = pc
                raise SimError("step limit (%d) at pc=%08x" % (maxsteps, pc))
        self.pc = pc
        self.steps += n
        return n

    def step_orig(self):
        """for hooks that only observe: execute the real instruction at self.pc (ignoring hooks) and advance self.pc"""
        pc = self.pc
        w = self.m.r16(pc)
        f = self.ops.get(w)
        if f is None:
            f = self.ops[w] = self._decode(w, pc)
        self.pc = f(pc)

    def slot(self, pc):
        """execute the delay slot instruction at pc+2"""
        m = self.m
        w = m.r16(pc + 2)
        f = self.ops.get(w)
        if f is None:
            f = self.ops[w] = self._decode(w, pc + 2)
        f(pc + 2)

    # --- decoder: one closure per distinct opcode word -----------------------------------------
    def _decode(self, w, pc0):
        c = self
        r = self.r
        m = self.m
        n = (w >> 8) & 15
        mm = (w >> 4) & 15
        d = w & 15
        i = w & 255
        top = w >> 12

        def bad():
            raise SimError("unimplemented opcode %04x at %08x" % (w, pc0))

        if top == 0:
            if d == 0xC:
                def f(pc): r[n] = s8(m.r8(r[mm] + r[0])) & M32; return pc + 2
            elif d == 0xD:
                def f(pc): r[n] = s16(m.r16(r[mm] + r[0])) & M32; return pc + 2
            elif d == 0xE:
                def f(pc): r[n] = m.r32(r[mm] + r[0]); return pc + 2
            elif d == 4:
                def f(pc): m.w8(r[n] + r[0], r[mm]); return pc + 2
            elif d == 5:
                def f(pc): m.w16(r[n] + r[0], r[mm]); return pc + 2
            elif d == 6:
                def f(pc): m.w32(r[n] + r[0], r[mm]); return pc + 2
            elif d == 7:
                def f(pc): c.macl = (s32(r[n]) * s32(r[mm])) & M32; return pc + 2
            elif d == 0xF:
                def f(pc):
                    a = s32(m.r32(r[n])); r[n] = (r[n] + 4) & M32        # sequential: with Rm == Rn the 2nd read is the next word
                    b = s32(m.r32(r[mm])); r[mm] = (r[mm] + 4) & M32
                    acc = (c.mach << 32) | c.macl
                    if acc & (1 << 63): acc -= 1 << 64
                    acc += a * b
                    if c.S:
                        acc = max(-(1 << 47), min((1 << 47) - 1, acc))
                    acc &= (1 << 64) - 1
                    c.mach = (acc >> 32) & M32; c.macl = acc & M32
                    return pc + 2
            elif w == 0x0008:
                def f(pc): c.T = 0; return pc + 2
            elif w == 0x0018:
                def f(pc): c.T = 1; return pc + 2
            elif w == 0x0028:
                def f(pc): c.mach = 0; c.macl = 0; return pc + 2
            elif w == 0x0009:
                def f(pc): return pc + 2
            elif w == 0x000B:
                def f(pc):
                    t = c.pr; c.slot(pc); return t
            elif w == 0x002B:           # rte
                def f(pc):
                    t = m.r32(r[15]); sr = m.r32(r[15] + 4); r[15] = (r[15] + 8) & M32
                    c.slot(pc); c.sr = sr; return t
            elif w == 0x001B:
                def f(pc): raise SimError("sleep at %08x" % pc)
            elif i == 0x02:
                def f(pc): r[n] = c.sr; return pc + 2
            elif i == 0x12:
                def f(pc): r[n] = c.gbr; return pc + 2
            elif i == 0x22:
                def f(pc): r[n] = c.vbr; return pc + 2
            elif i == 0x0A:
                def f(pc): r[n] = c.mach; return pc + 2
            elif i == 0x1A:
                def f(pc): r[n] = c.macl; return pc + 2
            elif i == 0x2A:
                def f(pc): r[n] = c.pr; return pc + 2
            elif i == 0x29:
                def f(pc): r[n] = c.T; return pc + 2
            elif i == 0x23:
                def f(pc):
                    t = (pc + 4 + r[n]) & M32; c.slot(pc); return t
            elif i == 0x03:
                def f(pc):
                    t = (pc + 4 + r[n]) & M32; c.pr = (pc + 4) & M32; c.slot(pc); return t
            else:
                f = lambda pc: bad()
        elif top == 1:
            def f(pc): m.w32(r[n] + d * 4, r[mm]); return pc + 2
        elif top == 2:
            if d == 0:
                def f(pc): m.w8(r[n], r[mm]); return pc + 2
            elif d == 1:
                def f(pc): m.w16(r[n], r[mm]); return pc + 2
            elif d == 2:
                def f(pc): m.w32(r[n], r[mm]); return pc + 2
            elif d == 4:
                def f(pc): r[n] = (r[n] - 1) & M32; m.w8(r[n], r[mm]); return pc + 2
            elif d == 5:
                def f(pc): r[n] = (r[n] - 2) & M32; m.w16(r[n], r[mm]); return pc + 2
            elif d == 6:
                def f(pc): v = r[mm]; r[n] = (r[n] - 4) & M32; m.w32(r[n], v); return pc + 2
            elif d == 7:
                def f(pc):
                    c.Q = (r[n] >> 31) & 1; c.M = (r[mm] >> 31) & 1; c.T = c.Q ^ c.M; return pc + 2
            elif d == 8:
                def f(pc): c.T = 1 if (r[n] & r[mm]) == 0 else 0; return pc + 2
            elif d == 9:
                def f(pc): r[n] &= r[mm]; return pc + 2
            elif d == 10:
                def f(pc): r[n] ^= r[mm]; return pc + 2
            elif d == 11:
                def f(pc): r[n] |= r[mm]; return pc + 2
            elif d == 12:
                def f(pc):
                    a = r[n] ^ r[mm]
                    c.T = 1 if (not (a & 0xFF) or not (a & 0xFF00) or not (a & 0xFF0000) or not (a & 0xFF000000)) else 0
                    return pc + 2
            elif d == 13:
                def f(pc): r[n] = (((r[mm] & 0xFFFF) << 16) | (r[n] >> 16)) & M32; return pc + 2
            elif d == 14:
                def f(pc): c.macl = ((r[n] & 0xFFFF) * (r[mm] & 0xFFFF)) & M32; return pc + 2
            elif d == 15:
                def f(pc): c.macl = (s16(r[n]) * s16(r[mm])) & M32; return pc + 2
            else:
                f = lambda pc: bad()
        elif top == 3:
            if d == 0:
                def f(pc): c.T = 1 if r[n] == r[mm] else 0; return pc + 2
            elif d == 2:
                def f(pc): c.T = 1 if r[n] >= r[mm] else 0; return pc + 2
            elif d == 3:
                def f(pc): c.T = 1 if s32(r[n]) >= s32(r[mm]) else 0; return pc + 2
            elif d == 6:
                def f(pc): c.T = 1 if r[n] > r[mm] else 0; return pc + 2
            elif d == 7:
                def f(pc): c.T = 1 if s32(r[n]) > s32(r[mm]) else 0; return pc + 2
            elif d == 8:
                def f(pc): r[n] = (r[n] - r[mm]) & M32; return pc + 2
            elif d == 12:
                def f(pc): r[n] = (r[n] + r[mm]) & M32; return pc + 2
            elif d == 13:
                def f(pc):
                    p = (s32(r[n]) * s32(r[mm])) & ((1 << 64) - 1)
                    c.mach = (p >> 32) & M32; c.macl = p & M32; return pc + 2
            elif d == 5:
                def f(pc):
                    p = r[n] * r[mm]
                    c.mach = (p >> 32) & M32; c.macl = p & M32; return pc + 2
            elif d == 14:
                def f(pc):
                    t = r[n] + r[mm] + c.T
                    c.T = 1 if t > M32 else 0; r[n] = t & M32; return pc + 2
            elif d == 10:
                def f(pc):
                    t = r[n] - r[mm] - c.T
                    c.T = 1 if t < 0 else 0; r[n] = t & M32; return pc + 2
            elif d == 15:
                def f(pc):
                    t = s32(r[n]) + s32(r[mm])
                    c.T = 1 if (t > 0x7FFFFFFF or t < -0x80000000) else 0; r[n] = t & M32; return pc + 2
            elif d == 11:
                def f(pc):
                    t = s32(r[n]) - s32(r[mm])
                    c.T = 1 if (t > 0x7FFFFFFF or t < -0x80000000) else 0; r[n] = t & M32; return pc + 2
            elif d == 4:
                def f(pc):
                    old_q = c.Q
                    c.Q = (r[n] >> 31) & 1
                    r[n] = ((r[n] << 1) | c.T) & M32
                    t0 = r[n]
                    if old_q == 0:
                        if c.M == 0:
                            r[n] = (r[n] - r[mm]) & M32; t1 = r[n] > t0
                            c.Q = (1 if t1 else 0) if c.Q == 0 else (0 if t1 else 1)
                        else:
                            r[n] = (r[n] + r[mm]) & M32; t1 = r[n] < t0
                            c.Q = (0 if t1 else 1) if c.Q == 0 else (1 if t1 else 0)
                    else:
                        if c.M == 0:
                            r[n] = (r[n] + r[mm]) & M32; t1 = r[n] < t0
                            c.Q = (1 if t1 else 0) if c.Q == 0 else (0 if t1 else 1)
                        else:
                            r[n] = (r[n] - r[mm]) & M32; t1 = r[n] > t0
                            c.Q = (0 if t1 else 1) if c.Q == 0 else (1 if t1 else 0)
                    c.T = 1 if c.Q == c.M else 0
                    return pc + 2
            else:
                f = lambda pc: bad()
        elif top == 4:
            if i == 0x00:
                def f(pc): c.T = (r[n] >> 31) & 1; r[n] = (r[n] << 1) & M32; return pc + 2
            elif i == 0x01:
                def f(pc): c.T = r[n] & 1; r[n] >>= 1; return pc + 2
            elif i == 0x04:
                def f(pc): t = (r[n] >> 31) & 1; r[n] = ((r[n] << 1) | t) & M32; c.T = t; return pc + 2
            elif i == 0x05:
                def f(pc): t = r[n] & 1; r[n] = (r[n] >> 1) | (t << 31); c.T = t; return pc + 2
            elif i == 0x08:
                def f(pc): r[n] = (r[n] << 2) & M32; return pc + 2
            elif i == 0x09:
                def f(pc): r[n] >>= 2; return pc + 2
            elif i == 0x18:
                def f(pc): r[n] = (r[n] << 8) & M32; return pc + 2
            elif i == 0x19:
                def f(pc): r[n] >>= 8; return pc + 2
            elif i == 0x28:
                def f(pc): r[n] = (r[n] << 16) & M32; return pc + 2
            elif i == 0x29:
                def f(pc): r[n] >>= 16; return pc + 2
            elif i == 0x20:
                def f(pc): c.T = (r[n] >> 31) & 1; r[n] = (r[n] << 1) & M32; return pc + 2
            elif i == 0x21:
                def f(pc): c.T = r[n] & 1; r[n] = (s32(r[n]) >> 1) & M32; return pc + 2
            elif i == 0x24:
                def f(pc): t = (r[n] >> 31) & 1; r[n] = ((r[n] << 1) | c.T) & M32; c.T = t; return pc + 2
            elif i == 0x25:
                def f(pc): t = r[n] & 1; r[n] = (r[n] >> 1) | (c.T << 31); c.T = t; return pc + 2
            elif i == 0x10:
                def f(pc): r[n] = (r[n] - 1) & M32; c.T = 1 if r[n] == 0 else 0; return pc + 2
            elif i == 0x11:
                def f(pc): c.T = 1 if s32(r[n]) >= 0 else 0; return pc + 2
            elif i == 0x15:
                def f(pc): c.T = 1 if s32(r[n]) > 0 else 0; return pc + 2
            elif i == 0x0B:
                def f(pc):
                    t = r[n]; c.pr = (pc + 4) & M32; c.slot(pc); return t
            elif i == 0x2B:
                def f(pc):
                    t = r[n]; c.slot(pc); return t
            elif i == 0x0E:
                def f(pc): c.sr = r[n]; return pc + 2
            elif i == 0x1E:
                def f(pc): c.gbr = r[n]; return pc + 2
            elif i == 0x2E:
                def f(pc): c.vbr = r[n]; return pc + 2
            elif i == 0x0A:
                def f(pc): c.mach = r[n]; return pc + 2
            elif i == 0x1A:
                def f(pc): c.macl = r[n]; return pc + 2
            elif i == 0x2A:
                def f(pc): c.pr = r[n]; return pc + 2
            elif i == 0x26:
                def f(pc): c.pr = m.r32(r[n]); r[n] = (r[n] + 4) & M32; return pc + 2
            elif i == 0x16:
                def f(pc): c.macl = m.r32(r[n]); r[n] = (r[n] + 4) & M32; return pc + 2
            elif i == 0x06:
                def f(pc): c.mach = m.r32(r[n]); r[n] = (r[n] + 4) & M32; return pc + 2
            elif i == 0x07:
                def f(pc): c.sr = m.r32(r[n]); r[n] = (r[n] + 4) & M32; return pc + 2
            elif i == 0x17:
                def f(pc): c.gbr = m.r32(r[n]); r[n] = (r[n] + 4) & M32; return pc + 2
            elif i == 0x27:
                def f(pc): c.vbr = m.r32(r[n]); r[n] = (r[n] + 4) & M32; return pc + 2
            elif i == 0x22:
                def f(pc): r[n] = (r[n] - 4) & M32; m.w32(r[n], c.pr); return pc + 2
            elif i == 0x12:
                def f(pc): r[n] = (r[n] - 4) & M32; m.w32(r[n], c.macl); return pc + 2
            elif i == 0x02:
                def f(pc): r[n] = (r[n] - 4) & M32; m.w32(r[n], c.mach); return pc + 2
            elif i == 0x03:
                def f(pc): r[n] = (r[n] - 4) & M32; m.w32(r[n], c.sr); return pc + 2
            elif i == 0x13:
                def f(pc): r[n] = (r[n] - 4) & M32; m.w32(r[n], c.gbr); return pc + 2
            elif i == 0x23:
                def f(pc): r[n] = (r[n] - 4) & M32; m.w32(r[n], c.vbr); return pc + 2
            elif i == 0x1B:
                def f(pc):
                    v = m.r8(r[n]); c.T = 1 if v == 0 else 0; m.w8(r[n], v | 0x80); return pc + 2
            elif d == 0xF:
                def f(pc):                  # mac.w @Rm+,@Rn+
                    a = s16(m.r16(r[n])); r[n] = (r[n] + 2) & M32
                    b = s16(m.r16(r[mm])); r[mm] = (r[mm] + 2) & M32
                    if c.S:
                        t = s32(c.macl) + a * b
                        if t > 0x7FFFFFFF:
                            t = 0x7FFFFFFF; c.mach |= 1
                        elif t < -0x80000000:
                            t = -0x80000000; c.mach |= 1
                        c.macl = t & M32
                    else:
                        acc = (c.mach << 32) | c.macl
                        if acc & (1 << 63): acc -= 1 << 64
                        acc = (acc + a * b) & ((1 << 64) - 1)
                        c.mach = (acc >> 32) & M32; c.macl = acc & M32
                    return pc + 2
            else:
                f = lambda pc: bad()
        elif top == 5:
            def f(pc): r[n] = m.r32(r[mm] + d * 4); return pc + 2
        elif top == 6:
            if d == 0:
                def f(pc): r[n] = s8(m.r8(r[mm])) & M32; return pc + 2
            elif d == 1:
                def f(pc): r[n] = s16(m.r16(r[mm])) & M32; return pc + 2
            elif d == 2:
                def f(pc): r[n] = m.r32(r[mm]); return pc + 2
            elif d == 3:
                def f(pc): r[n] = r[mm]; return pc + 2
            elif d == 4:
                def f(pc):
                    v = s8(m.r8(r[mm])) & M32
                    if n != mm: r[mm] = (r[mm] + 1) & M32
                    r[n] = v; return pc + 2
            elif d == 5:
                def f(pc):
                    v = s16(m.r16(r[mm])) & M32
                    if n != mm: r[mm] = (r[mm] + 2) & M32
                    r[n] = v; return pc + 2
            elif d == 6:
                def f(pc):
                    v = m.r32(r[mm])
                    if n != mm: r[mm] = (r[mm] + 4) & M32
                    r[n] = v; return pc + 2
            elif d == 7:
                def f(pc): r[n] = (~r[mm]) & M32; return pc + 2
            elif d == 8:
                def f(pc): v = r[mm]; r[n] = (v & 0xFFFF0000) | ((v & 0xFF) << 8) | ((v >> 8) & 0xFF); return pc + 2
            elif d == 9:
                def f(pc): v = r[mm]; r[n] = ((v << 16) | (v >> 16)) & M32; return pc + 2
            elif d == 10:
                def f(pc):
                    t = 0 - r[mm] - c.T
                    c.T = 1 if t < 0 else 0; r[n] = t & M32; return pc + 2
            elif d == 11:
                def f(pc): r[n] = (-r[mm]) & M32; return pc + 2
            elif d == 12:
                def f(pc): r[n] = r[mm] & 0xFF; return pc + 2
            elif d == 13:
                def f(pc): r[n] = r[mm] & 0xFFFF; return pc + 2
            elif d == 14:
                def f(pc): r[n] = s8(r[mm]) & M32; return pc + 2
            else:
                def f(pc): r[n] = s16(r[mm]) & M32; return pc + 2
        elif top == 7:
            imm = s8(i)
            def f(pc): r[n] = (r[n] + imm) & M32; return pc + 2
        elif top == 8:
            sub = n
            disp8 = s8(i)
            if sub == 0:
                def f(pc): m.w8(r[mm] + d, r[0]); return pc + 2
            elif sub == 1:
                def f(pc): m.w16(r[mm] + d * 2, r[0]); return pc + 2
            elif sub == 4:
                def f(pc): r[0] = s8(m.r8(r[mm] + d)) & M32; return pc + 2
            elif sub == 5:
                def f(pc): r[0] = s16(m.r16(r[mm] + d * 2)) & M32; return pc + 2
            elif sub == 8:
                def f(pc): c.T = 1 if s32(r[0]) == disp8 else 0; return pc + 2
            elif sub == 9:
                def f(pc): return ((pc + 4 + disp8 * 2) & M32) if c.T else pc + 2
            elif sub == 11:
                def f(pc): return pc + 2 if c.T else ((pc + 4 + disp8 * 2) & M32)
            elif sub == 13:
                def f(pc):
                    if c.T:
                        c.slot(pc); return (pc + 4 + disp8 * 2) & M32
                    c.slot(pc); return pc + 4
            elif sub == 15:
                def f(pc):
                    if not c.T:
                        c.slot(pc); return (pc + 4 + disp8 * 2) & M32
                    c.slot(pc); return pc + 4
            else:
                f = lambda pc: bad()
        elif top == 9:
            def f(pc): r[n] = s16(m.r16(pc + 4 + i * 2)) & M32; return pc + 2
        elif top in (0xA, 0xB):
            disp = w & 0xFFF
            if disp & 0x800: disp -= 0x1000
            if top == 0xA:
                def f(pc): c.slot(pc); return (pc + 4 + disp * 2) & M32
            else:
                def f(pc): c.pr = (pc + 4) & M32; c.slot(pc); return (pc + 4 + disp * 2) & M32
        elif top == 0xC:
            if n == 0:
                def f(pc): m.w8(c.gbr + i, r[0]); return pc + 2
            elif n == 1:
                def f(pc): m.w16(c.gbr + i * 2, r[0]); return pc + 2
            elif n == 2:
                def f(pc): m.w32(c.gbr + i * 4, r[0]); return pc + 2
            elif n == 3:
                def f(pc): raise SimError("trapa #%d at %08x" % (i, pc))
            elif n == 4:
                def f(pc): r[0] = s8(m.r8(c.gbr + i)) & M32; return pc + 2
            elif n == 5:
                def f(pc): r[0] = s16(m.r16(c.gbr + i * 2)) & M32; return pc + 2
            elif n == 6:
                def f(pc): r[0] = m.r32(c.gbr + i * 4); return pc + 2
            elif n == 7:
                def f(pc): r[0] = (((pc + 4) & ~3) + i * 4) & M32; return pc + 2
            elif n == 8:
                def f(pc): c.T = 1 if (r[0] & i) == 0 else 0; return pc + 2
            elif n == 9:
                def f(pc): r[0] &= i; return pc + 2
            elif n == 10:
                def f(pc): r[0] ^= i; return pc + 2
            elif n == 11:
                def f(pc): r[0] |= i; return pc + 2
            elif n == 12:
                def f(pc): c.T = 1 if (m.r8(c.gbr + r[0]) & i) == 0 else 0; return pc + 2
            elif n == 13:
                def f(pc): a = c.gbr + r[0]; m.w8(a, m.r8(a) & i); return pc + 2
            elif n == 14:
                def f(pc): a = c.gbr + r[0]; m.w8(a, m.r8(a) ^ i); return pc + 2
            else:
                def f(pc): a = c.gbr + r[0]; m.w8(a, m.r8(a) | i); return pc + 2
        elif top == 0xD:
            def f(pc): r[n] = m.r32(((pc + 4) & ~3) + i * 4); return pc + 2
        elif top == 0xE:
            v = s8(i) & M32
            def f(pc): r[n] = v; return pc + 2
        else:
            f = lambda pc: bad()
        return f
