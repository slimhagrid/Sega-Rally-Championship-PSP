"""M8: boot the original program headless and run its per-frame state handler (the "main vector" at 0x060A001C), so the menu screens can be observed and ported.

    from menu_env import Menu
    m = Menu()
    m.frame()          # one pass of the main loop: call *(0x060A001C)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from datfile import Scatter  # noqa: E402
from sh2 import CPU, Mem, SimError, s32  # noqa: E402

DISC = os.path.join(ROOT, "extracted", "disc", "files")
SP = 0x060FF000
MAIN_VECTOR = 0x060A001C
POWERON = 0x06004000
MAIN_INIT = 0x0600410C
CD_LOAD = 0x0600F550
VINT_IN = 0x06004CE4
VINT_OUT = 0x06005148


class FrameDone(Exception):
    pass


class MenuMem(Mem):
    def r8(self, a):
        if (a & 0x1FFFFFFF) == 0x00100063:           # SMPC status flag: every command completes at once
            return 0
        v = Mem.r8(self, a)
        if 0x25A00400 <= (a & 0x1FFFFFFF) + 0x25A00000 - 0x05A00000 < 0x25A00800 and (a & 0x1FFFFFFF) >= 0x05A00000:   # sound driver mailbox: the 68000 always acknowledges
            return v & 0x7F
        return v


class Menu:
    def __init__(self, trace=False):
        self.mem = MenuMem()
        self.cpu = CPU(self.mem)
        m = self.mem
        m.load(0x06004000, open(os.path.join(DISC, "0RALLY.DAT"), "rb").read())
        # the Saturn BIOS is not available: every BIOS vector entry (0x06000200 ..) points to an `rts; nop` stub
        m.w16(0x06000100, 0x000B); m.w16(0x06000102, 0x0009)
        for a in range(0x06000200, 0x06000400, 4):
            m.w32(a, 0x06000100)
        m.w16(0x06000104, 0xE000); m.w16(0x06000106, 0x000B); m.w16(0x06000108, 0x0009)       # mov #0,r0; rts; nop  (a BIOS call that succeeds)
        m.w32(0x06000354, 0x06000600)                                                            # BIOS backup-memory (BUP) function table / entry
        m.w32(0x06000358, 0x06000104)
        for k in range(0, 0x100, 4):
            m.w32(0x06000600 + k, 0x06000104)
        self.log = []
        for a in (0x060187C0, 0x0601B284, 0x06018908, 0x06018A10, 0x06019870, 0x06019936, 0x0601978C, 0x060186FC, 0x06010654, 0x060189B8):                                                       # polygon pipeline: draw_course, game_zsort (the 3D parts are not observed here)
            self.cpu.hooks[a] = lambda c: c.ret()
        self.cpu.hooks[0x0600F354] = self._load_name                                             # the file loader: r4 = file name
        self.snd_log = []                                                                        # (tick, function, r4, r5, r6) of every call into the sound driver interface
        for ln in open(os.path.join(ROOT, "extracted", "analysis", "functions.tsv")):            # the sound driver interface (modules sound / gamesd, 0x06015F90 .. 0x060178A4): no sound CPU here, the calls are logged
            p = ln.split("	")
            a = int(p[0], 16)
            if 0x06015F90 <= a < 0x060178A4:
                self.cpu.hooks[a] = self._snd_stub
        self.cpu.hooks[0x06016B74] = self._snd_init
        self.cpu.hooks[CD_LOAD] = self._cd_load
        for a in (0x06005212, 0x0600521E, 0x06005226, 0x06005238):     # the sync loops wait for the V-blank handlers: run them as an interrupt
            self.cpu.hooks[a] = self._vblank
        self.vblanks = 0
        self.tick_cb = None                                                                      # called after every V-IN + V-OUT pair (a "tick"): tick_cb(menu, recipe_in, recipe_out)
        self.pad = 0                                                                             # held buttons of pad 1 (Saturn bits: 0x8000 R, 4000 L, 2000 Dn, 1000 Up, 0800 Start, 0400 A, 0200 C, 0100 B, 0080 R, 0040 X, 0020 Y, 0010 Z, 0008 L)
        self._pad_prev = 0
        self.axis = None                                                                         # analog axis byte written by pad_read (None: not written)
        self.cpu.hooks[0x06005A18] = self._pad_read                                              # pad_read: the harness supplies the pad words
        self.bound = False                                                                       # after boot: the returns of sync / intm_sync end one "pass" (run() returns)
        self.cpu.hooks[0x06005218] = self._boundary                                              # sync: rts
        self.cpu.hooks[0x0600522C] = self._boundary                                              # intm_sync: rts
        import vdp2
        vdp2._scu_dma(self.mem)                                                                  # SCU DMA (colour RAM, VDP2 VRAM copies of the V-IN recipes)
        self.slave = CPU(self.mem)
        self.slave.hooks = self.cpu.hooks
        for a in (0x06017A08, 0x06017A2E, 0x06017B28):
            self.cpu.hooks[a] = lambda c: c.ret()               # slave handshakes (the slave runs jobs on request instead)
        self.cpu.hooks[0x06017B90] = self._set_slave_exec       # slave::set_slave_exec: the job runs at once on the second CPU object
        self.mem.sinit = self._sinit

    def _snd_stub(self, c):
        self.snd_log.append((self.vblanks, c.pc, c.r[4], c.r[5], c.r[6]))
        c.r[0] = 0
        c.ret()

    def _snd_init(self, c):
        """FUN_06016b74 (sound driver start-up): only its stores into the program's own variables are kept"""
        m = self.mem
        self.snd_log.append((self.vblanks, c.pc, 0, 0, 0))
        m.w8(0x060AA82C, 1)
        m.w8(0x060AA80B, 0)
        m.w8(0x060AA80D, 1)
        m.w8(0x060AAAD8, 0)
        c.ret()

    def _run_slave_job(self, job):
        m, s = self.mem, self.slave
        saved = m.master
        m.master = False
        try:
            s.r[15] = 0x060FE800
            s.call(job)
        finally:
            m.master = saved
        m.w32(0x002A4000, 0)

    def _set_slave_exec(self, c):
        self._run_slave_job(c.r[4])
        c.ret()

    def _sinit(self):
        job = self.mem.r32(0x002A4000)
        if job:
            self._run_slave_job(job)

    def _interrupt(self, addr):
        """run an interrupt handler to its `rte` (exception frame: return = the sentinel, saved SR)"""
        c, m = self.cpu, self.mem
        saved = (c.pc, c.pr, c.sr, c.r[:])
        c.r[15] -= 8
        m.w32(c.r[15], c.SENTINEL)
        m.w32(c.r[15] + 4, c.sr)
        c.pc = addr
        c.run(c.SENTINEL, 2_000_000)
        c.pc, c.pr, c.sr = saved[0], saved[1], saved[2]
        c.r[:] = saved[3]

    def _pad_read(self, c):
        m = self.mem
        held = self.pad & 0xFFFF
        edge = held & ~self._pad_prev & 0xFFFF
        self._pad_prev = held
        m.w16(0x060AA990, held)
        m.w16(0x060AA992, edge)
        m.w16(0x060AA994, m.r16(0x060AA994) | edge)
        m.w8(0x060AA9A9, 0x02)                                     # the raw peripheral buffer: byte 1 = the pad's id (non-zero = a pad is connected; the title tests it)
        if self.axis is not None:
            m.w8(0x060AA99C, self.axis)                            # analog pad / wheel axis byte (127 = centre), a constant per scenario (see menu_drive.AXIS)
        c.ret()

    def _boundary(self, c):
        """the return of sync / intm_sync = the end of one pass of the main loop / of a screen's own loop: hand control back to the harness (resume with `run()`)"""
        if self.bound:
            c.ret()
            raise FrameDone()
        c.step_orig()

    def _vblank(self, c):
        """the sync loops wait for the V-blank handlers: take V-IN and V-OUT here"""
        self.vblanks += 1
        rin, rout = self.mem.r8(0x060AA800), self.mem.r8(0x060AA801)
        self._interrupt(VINT_IN)
        self._interrupt(VINT_OUT)
        if self.tick_cb:
            self.tick_cb(self, rin, rout)
        if c.pc in (0x06005212, 0x0600521E, 0x06005226, 0x06005238):
            c.step_orig()                          # the spin loop's own load (the hook replaced it)

    def _prgdata(self, c):
        s = Scatter(os.path.join(DISC, "PRGDATA.DAT"))
        for b in s.blocks:
            self.mem.load(b.dest, s.expanded(b))
        self.log.append("PRGDATA.DAT")
        c.ret()

    def _load_name(self, c):
        m = self.mem
        a = c.r[4]
        name = bytearray()
        while m.r8(a):
            name.append(m.r8(a)); a += 1
        nm = name.decode("ascii", "replace")
        self.log.append(nm)
        path = os.path.join(DISC, nm)
        if os.path.isfile(path):
            s = Scatter(path)
            for b in s.blocks:
                m.load(b.dest, s.expanded(b))
            c.r[0] = 0
        else:
            c.r[0] = 0xFFFFFFFF
        c.ret()

    def _cd_load(self, c):
        """a CD request list (0x0600F550): 24-byte entries ended by a word of -1; type 0 = raw file (+4 dest, name at +10), 2 = scatter-load file (name at +2)"""
        m = self.mem
        req = c.r[4]
        while m.r16(req) != 0xFFFF:
            a = req + 2
            raw_dest = None
            if m.r16(req) == 0 and m.r8(req + 10):
                raw_dest = m.r32(req + 4)
                a = req + 10
            name = bytearray()
            while m.r8(a):
                name.append(m.r8(a)); a += 1
            self.log.append(name.decode("ascii", "replace"))
            path = os.path.join(DISC, name.decode("ascii"))
            if raw_dest is not None and os.path.isfile(path):
                data = open(path, "rb").read()
                if m.r16(req + 2) == 4:                    # flag 4 (loader 0x0600F154): a BPE stream, expanded into the destination (STAFF.CG of the staff roll); the other flags are plain copies
                    from datfile import bpe_expand
                    data = bpe_expand(data)
                m.load(raw_dest, data)
            elif name and os.path.isfile(path):
                s = Scatter(path)
                for b in s.blocks:
                    m.load(b.dest, s.expanded(b))
            req += 24
        c.ret()

    def boot(self, maxsteps=300_000_000):
        """poweron_start .. main_init: runs until the main loop copy at 0x060A0000 is entered"""
        c = self.cpu
        c.r[15] = SP
        c.pr = c.SENTINEL
        c.pc = POWERON
        c.run(0x060A0000, maxsteps)
        self.bound = True

    # --- running screens ---------------------------------------------------------------------------------------------
    def start(self, vector=None):
        """enter a screen handler directly (default: the main vector's current value) as the main loop's `jsr` would: the handler returns into the main loop copy"""
        c = self.cpu
        c.r[15] = SP
        c.pr = 0x060A000C
        c.pc = vector if vector is not None else self.mem.r32(MAIN_VECTOR)
        self.mem.w8(0x060AA801, 2)

    def run(self, maxsteps=100_000_000):
        """run the original (the main loop copy at 0x060A0000 and what it calls) to the next pass boundary: the return of sync / intm_sync"""
        c = self.cpu
        try:
            c.run(c.SENTINEL, maxsteps)
        except FrameDone:
            return True
        raise SimError("program ended at %08x" % c.pc)
