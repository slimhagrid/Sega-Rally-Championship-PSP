"""M9: expected results of one 68000 instruction in unicorn for every opcode word and several pseudo-random states (checks src/m68k.c with tools/test_m68k.c).

    python tools/m68k_fuzz.py out.bin [states_per_opcode] [first_opcode last_opcode]

The initial state is rebuilt from a seed by the same generator in test_m68k.c (a 32-bit LCG): d0-d7 random, a0-a6 inside 0x2000 .. 0x2FFF, a7 inside 0x3000 .. 0x33FF, the status register
in supervisor mode with random flags and interrupt mask, RAM 0x2000 .. 0x3FFF random, the code at 0x400 (opcode + four extension words of mixed kinds: addresses into the RAM window,
small numbers, small negative numbers, zero).  Unicorn maps only 0 .. 0x3FFF: cases that fault or raise an exception are written as 'skipped'.
Record: u32 op, u32 seed, u32 flag (1 = result follows), 8 x d, 8 x a (the active a7), u32 pc, u32 sr, u32 fnv(ram 0x2000 .. 0x3FFF).
"""
import struct
import sys

from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UcError
from unicorn.m68k_const import (UC_M68K_REG_D0, UC_M68K_REG_A0, UC_M68K_REG_A7, UC_M68K_REG_PC, UC_M68K_REG_SR, UC_CPU_M68K_M68000)

D_REGS = [UC_M68K_REG_D0 + i for i in range(8)]
A_REGS = [UC_M68K_REG_A0 + i for i in range(8)]


class Lcg:
    def __init__(self, seed):
        self.x = seed & 0xFFFFFFFF

    def next(self):
        self.x = (self.x * 1664525 + 1013904223) & 0xFFFFFFFF
        return self.x


def make_state(seed, op):
    g = Lcg(seed)
    d = [g.next() for _ in range(8)]
    a = [0x2000 + ((g.next() % 0x1000) & ~1) for _ in range(7)]
    a.append(0x3000 + ((g.next() % 0x400) & ~1))
    sr = 0x2000 | (g.next() & 0x1F) | ((g.next() & 7) << 8)
    ram = bytes((g.next() >> 24) for _ in range(0x2000))
    ext = []
    for _ in range(4):
        r = g.next()
        k = (r >> 28) & 3
        if k == 0:
            ext.append(0x2000 + ((r & 0x7FE)))
        elif k == 1:
            ext.append(r & 0xFF)
        elif k == 2:
            ext.append(0xFF00 | (r & 0xFE))
        else:
            ext.append(0)
    ext = [w & ~0x100 for w in ext]                         # bit 8 of an index extension word selects the 68020's full format: reserved on the 68000
    return d, a, sr, ram, [op] + ext


def fnv(b):
    h = 0x811C9DC5
    for x in b:
        h = ((h ^ x) * 0x01000193) & 0xFFFFFFFF
    return h


def main():
    out = open(sys.argv[1], "wb")
    per = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    lo = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0
    hi = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0xFFFF
    uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
    uc.ctl_set_cpu_model(UC_CPU_M68K_M68000)
    uc.mem_map(0, 0x4000)
    total = ok = 0
    for op in range(lo, hi + 1):
        for k in range(per):
            seed = (op * 7919 + k * 104729 + 12345) & 0xFFFFFFFF
            d, a, sr, ram, words = make_state(seed, op)
            total += 1
            uc.mem_write(0x2000, ram)
            uc.mem_write(0x400, b"".join(struct.pack(">H", w) for w in words))
            for i in range(8):
                uc.reg_write(D_REGS[i], d[i])
                uc.reg_write(A_REGS[i], a[i])
            uc.reg_write(UC_M68K_REG_SR, sr)
            uc.reg_write(UC_M68K_REG_A7, a[7])
            res = None
            try:
                uc.ctl_flush_tb()                           # a stale translation of the previous opcode at 0x400 would run otherwise
                uc.emu_start(0x400, 0x3FF0, count=1)
                dd = [uc.reg_read(r) for r in D_REGS]
                aa = [uc.reg_read(r) for r in A_REGS]
                pc = uc.reg_read(UC_M68K_REG_PC)
                h = fnv(bytes(uc.mem_read(0x2000, 0x2000)))
                uc.ctl_flush_tb()
                uc.mem_write(0x3F00, bytes((0x40, 0xC7)))           # MOVE SR, D7: unicorn keeps the condition codes lazy, reading SR directly gives stale flags
                uc.emu_start(0x3F00, 0x3FF0, count=1)
                srv = uc.reg_read(D_REGS[7]) & 0xFFFF
                res = (dd, aa, pc, srv, h)
                ok += 1
            except UcError:
                res = None
            except OSError:                                 # unicorn crashes natively on a few states (an access violation inside the engine): start a fresh engine
                res = None
                uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
                uc.ctl_set_cpu_model(UC_CPU_M68K_M68000)
                uc.mem_map(0, 0x4000)
            if res is None:
                out.write(struct.pack(">III", op, seed, 0))
            else:
                dd, aa, pc, srr, h = res
                out.write(struct.pack(">III", op, seed, 1) + struct.pack(">8I", *dd) + struct.pack(">8I", *aa) + struct.pack(">III", pc, srr, h))
    out.close()
    print("%d cases, %d with a result" % (total, ok))


main()
