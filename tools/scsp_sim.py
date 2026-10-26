"""M9: the Saturn sound CPU (68000, driver SDDRVS.DAT) running for real in unicorn, with the SCSP's registers logged.

The driver is the stock Saturn sound driver; the SH-2 side talks to it through sound RAM (command slots at 0x700, engine words at 0x7C0 / 0x7E0, see docs/sound.md).  This rig
loads the driver and the data files of a scenario into a 512 KB sound RAM, lets the 68000 run from its reset vector, delivers the SCSP's two timers (A: polled by the main
loop = one driver tick, B: a level 2 interrupt = the sequencer step) and logs every write to the SCSP registers (0x100000 ..).  No SCSP is emulated here: tools/scsp_render.py
turns the log into samples.

    sim = SoundSim()
    sim.load("SDDRVS"); sim.load("SDDESERT"); sim.load("CELICA1")
    sim.boot(); sim.run_ticks(5)
    sim.command(8, bytes([map_number, 0, 0, 0]))        # the SH-2 loads a sound map (stage / car layout, 0xA000 table)
    sim.play_id(ident, table)                            # what the SH-2's sender (0x06016FD0) does: slot = command 1 + table[id]
    sim.run_ticks(n)
    sim.log                                              # [(tick, "w", offset, value, size)]
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from datfile import Scatter  # noqa: E402
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_READ, UC_HOOK_CODE  # noqa: E402
from unicorn.m68k_const import UC_M68K_REG_A0, UC_M68K_REG_A7, UC_M68K_REG_D0, UC_M68K_REG_PC, UC_M68K_REG_SR, UC_CPU_M68K_M68000  # noqa: E402

ROOT = os.path.dirname(HERE)
FILES = os.path.join(ROOT, "extracted", "disc", "files")
RAM_SIZE = 0x80000
SCSP = 0x100000
SCSP_SIZE = 0x1000
RTE_ADDR = 0x1580                                       # the driver's only RTE (end of the timer B interrupt routine 0x154A)


class SoundSim:
    SLOT = 0x700

    def __init__(self):
        self.uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        self.uc.ctl_set_cpu_model(UC_CPU_M68K_M68000)       # the default model is a ColdFire
        self.uc.mem_map(0, RAM_SIZE)
        self.uc.mem_map(SCSP, SCSP_SIZE)
        self.log = []
        self.tick = 0
        self.insns = 0
        self.reads = {}
        self.acked = False
        self.idle = 0
        self.stop = False
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self._write, begin=SCSP, end=SCSP + SCSP_SIZE - 1)
        self.uc.hook_add(UC_HOOK_MEM_READ, self._read, begin=SCSP, end=SCSP + SCSP_SIZE - 1)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self._ramwrite, begin=0, end=RAM_SIZE - 1)
        self.wh = 0
        self.kyonex_fix = []

    # ---- data ----
    def load(self, name):
        s = Scatter(os.path.join(FILES, name if name.endswith(".DAT") else name + ".DAT"))
        for b in s.blocks:
            self.uc.mem_write(b.dest & 0x7FFFF, s.expanded(b))

    def write_ram(self, addr, data):
        self.uc.mem_write(addr, bytes(data))

    def read_ram(self, addr, n):
        return bytes(self.uc.mem_read(addr, n))

    def _reg16(self, off):
        b = self.read_ram(SCSP + off, 2)
        return (b[0] << 8) | b[1]

    def _set16(self, off, v):
        self.uc.mem_write(SCSP + off, bytes(((v >> 8) & 0xFF, v & 0xFF)))

    def _ramwrite(self, uc, access, addr, size, value, ud):
        if self.wh_on:
            v = value & ((1 << (8 * size)) - 1)
            h = (((self.wh ^ addr) * 16777619 ^ size) * 16777619 ^ v) * 16777619
            self.wh = h & 0xFFFFFFFF

    wh_on = True

    # ---- SCSP registers ----
    def _write(self, uc, access, addr, size, value, ud):
        off = addr - SCSP
        v = value & ((1 << (8 * size)) - 1)
        if off == 0x422 and size == 2:
            # SCIRE: the driver acknowledges interrupts.  bit 6 = timer A (one driver tick: 44100 / 89 Hz after its reload value 0xA7, polled by the main loop),
            # bit 7 = timer B (the interrupt routine 0x154A, level 2: the sequencer step 0x4308, about the same period)
            self._set16(0x420, self._reg16(0x420) & ~v & 0xFFFF)
            if v & 0x40:
                self.acked = True
        if off < 0x400 and (off & 0x1F) == 0 and size in (1, 2, 4):          # a write that includes the control byte of a slot: KYONEX (bit 4) reads back as 0 on the chip
            first = v >> (8 * (size - 1))
            if first & 0x10:
                self.kyonex_fix.append(off)
        self.log.append((self.tick, "w", off, v, size))

    def _read(self, uc, access, addr, size, value, ud):
        off = addr - SCSP
        self.reads[off] = self.reads.get(off, 0) + 1
        if off == 0x420 or off == 0x421:                    # SCIPD: bit 6 = timer A pending; the driver polls it in its main loop.  After the acknowledge two empty polls end the tick.
            if self.acked and not (self._reg16(0x420) & 0xC0):
                self.idle += 1
                if self.idle >= 2:
                    self.stop = True

    def _exec(self, words, at=0x7FF00):
        """run a few real instructions from scratch RAM (the condition codes of unicorn's 68000 are lazy: only real instructions keep them right); PC is restored by the caller"""
        uc = self.uc
        uc.mem_write(at, b"".join(struct.pack(">H", w) for w in words))
        uc.ctl_flush_tb()
        uc.emu_start(at, at + 2 * len(words), count=len(words))

    def _rte(self, uc, addr, size, ud):
        # RTE = MOVE.W (A7)+,SR then RTS (the driver's only RTE, the end of the timer B routine)
        self._exec([0x46DF, 0x4E75])

    # ---- running ----
    def boot(self, ssp=None, pc=None):
        uc = self.uc
        sp, p = struct.unpack(">II", bytes(uc.mem_read(0, 8)))
        uc.mem_write(RTE_ADDR, bytes((0x4E, 0x75)))          # RTE -> RTS (unicorn cannot take interrupts / execute RTE)
        uc.reg_write(UC_M68K_REG_SR, 0x2700)                 # supervisor first: A7 is banked
        uc.reg_write(UC_M68K_REG_A7, sp if ssp is None else ssp)
        uc.reg_write(UC_M68K_REG_PC, p if pc is None else pc)

    def irq(self, level, vector=None):
        """deliver an interrupt of the given level: push PC / SR, vector through the table (autovector 24 + level)"""
        uc = self.uc
        sr = self.get_sr()
        if (sr & 0x700) >= (level << 8) and level != 7:
            return False
        vec = (24 + level) if vector is None else vector
        handler = struct.unpack(">I", bytes(uc.mem_read(vec * 4, 4)))[0]
        pc = uc.reg_read(UC_M68K_REG_PC)
        sp = uc.reg_read(UC_M68K_REG_A7) - 4
        uc.mem_write(sp, struct.pack(">I", pc))
        uc.reg_write(UC_M68K_REG_A7, sp)
        self._exec([0x40E7, 0x027C, 0xF8FF, 0x007C, (level << 8) | 0x2000 if False else (level << 8)])        # push SR (the real flags), mask := level
        uc.reg_write(UC_M68K_REG_PC, handler)
        return True

    def get_sr(self):
        """the status register with its real condition codes: MOVE SR,-(A7)"""
        uc = self.uc
        pc, a7 = uc.reg_read(UC_M68K_REG_PC), uc.reg_read(UC_M68K_REG_A7)
        self._exec([0x40E7])
        sr = struct.unpack(">H", bytes(uc.mem_read(uc.reg_read(UC_M68K_REG_A7), 2)))[0]
        uc.reg_write(UC_M68K_REG_A7, a7)
        uc.reg_write(UC_M68K_REG_PC, pc)
        return sr

    def step(self, n):
        uc = self.uc
        pc = uc.reg_read(UC_M68K_REG_PC)
        uc.emu_start(pc, RTE_ADDR if False else 0xFFFFFFF0, count=n)
        self.insns += n

    def run_ticks(self, n, limit=200000, trace=None):
        """n driver ticks, scheduled like src/snd.c: timer B interrupt (when enabled) first, then timer A pending until the driver has acknowledged it and polled twice more.
        Single-stepped, so the flow is exactly what the C core does.  trace: a list that receives (pc, d0..d7, a0..a7, sr) after every instruction."""
        uc = self.uc
        regs = [UC_M68K_REG_D0 + i for i in range(8)] + [UC_M68K_REG_A0 + i for i in range(8)]

        def one():
            pc = uc.reg_read(UC_M68K_REG_PC)
            self.wh = 0
            if self._movem_a7(pc):
                pass
            else:
                uc.emu_start(pc, 0xFFFFFFF0, count=1)
            self.insns += 1
            while self.kyonex_fix:
                o = self.kyonex_fix.pop()
                b = bytes(uc.mem_read(SCSP + o, 1))[0]
                uc.mem_write(SCSP + o, bytes((b & 0xEF,)))
            if trace is not None:
                trace.append((uc.reg_read(UC_M68K_REG_PC),) + tuple(uc.reg_read(r) for r in regs) + (self.wh,))
            self.wh = 0

        for _ in range(n):
            if (self._reg16(0x41E) & 0x80) and ((uc.reg_read(UC_M68K_REG_SR) >> 8) & 7) < 2:          # timer B: its routine runs as a subroutine from the idle point (the C core does the same in test mode)
                ret = uc.reg_read(UC_M68K_REG_PC)
                handler = struct.unpack(">I", bytes(uc.mem_read(26 * 4, 4)))[0]
                sp = uc.reg_read(UC_M68K_REG_A7) - 4
                uc.mem_write(sp, struct.pack(">I", ret))
                uc.reg_write(UC_M68K_REG_A7, sp)
                uc.reg_write(UC_M68K_REG_PC, handler)
                self._set16(0x420, self._reg16(0x420) | 0x80)
                for _g in range(20000):
                    if uc.reg_read(UC_M68K_REG_PC) == ret:
                        break
                    one()
                self._set16(0x420, self._reg16(0x420) & ~0x80)
            self._set16(0x420, self._reg16(0x420) | 0x40)
            self.acked = False; self.idle = 0; self.stop = False
            g = 0
            while not self.stop and g < 60000:
                one()
                g += 1
            self.tick += 1
        return True

    def _movem_a7(self, pc):
        """MOVEM.L <list>,-(A7) with A7 in the list: the 68000 stores the initial A7, QEMU's model the decremented one (the driver then comes back with a different stack): done by hand"""
        uc = self.uc
        op, mask = struct.unpack(">HH", bytes(uc.mem_read(pc, 4)))
        if op != 0x48E7 or not (mask & 1):
            return False
        regs = [UC_M68K_REG_D0 + i for i in range(8)] + [UC_M68K_REG_A0 + i for i in range(8)]
        vals = [uc.reg_read(r) for r in regs]
        a7 = vals[15]
        addr = a7
        for i in range(16):
            if mask & (1 << i):
                v = vals[8 + 7 - i] if i < 8 else vals[15 - i]
                addr -= 4
                uc.mem_write(addr, struct.pack(">I", v))
                if self.wh_on:
                    h = (((self.wh ^ addr) * 16777619 ^ 4) * 16777619 ^ v) * 16777619
                    self.wh = h & 0xFFFFFFFF
        uc.reg_write(UC_M68K_REG_A7, addr)
        uc.reg_write(UC_M68K_REG_PC, pc + 4)
        return True

    def trace(self, n):
        """single-step n instructions; returns the list of PCs (slow: for reading the driver's flow)"""
        pcs = []
        uc = self.uc
        for _ in range(n):
            pc = uc.reg_read(UC_M68K_REG_PC)
            pcs.append(pc)
            uc.emu_start(pc, 0xFFFFFFF0, count=1)
        return pcs

    # ---- the SH-2 side ----
    def command(self, cmd, params=bytes(4)):
        """what the SH-2's sender (0x06016FD0) writes: byte 0 = command, bytes 2 .. 5 = parameters, into the first free slot (byte 0 == 0)"""
        for k in range(8):
            a = self.SLOT + 16 * k
            if self.read_ram(a, 1)[0] == 0:
                self.write_ram(a + 2, bytes(params[:4]))
                self.write_ram(a, bytes([cmd]))
                return k
        return -1

    def play_id(self, ident, table):
        return self.command(1, table[4 * ident:4 * ident + 4])
