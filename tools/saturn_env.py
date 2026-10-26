"""M4: the "console" the oracle runs the original game code in: real `0RALLY.DAT` + `PRGDATA.DAT` + one course's `BG<course>.DAT` collision
tables in emulated RAM, the race initialised through the game's own `init_game_work`, and the per-pass car functions called the way
`gctrl::game_ctrl` calls them.  Everything the driving code does not need (VDP, sound, polygon pipeline, camera) is hooked out.

    from saturn_env import Race
    r = Race(course=0)            # 0 DESERT 1 FOREST 2 MOUNT 3 LAKE
    r.step(pad_word)              # one 30 Hz logic pass of the player's car
    r.car(0x18)                   # car-slot fields (16.16 -> use fx())

Memory map and routine addresses: docs/physics.md (1.2-1.4, 5.1), docs/collision_world_ai.md.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from datfile import Scatter  # noqa: E402
from sh2 import CPU, Mem, s16, s32  # noqa: E402

DISC = os.path.join(ROOT, "extracted", "disc", "files")
COURSES = ["DESERT", "FOREST", "MOUNT", "LAKE"]

CAR0 = 0x060B0000                    # player 1 slot (GBR == r14 in all car routines)
SP = 0x060FF000                      # stack for the called routines
REC_BUF = 0x060F0000                 # scratch input-record buffer (0x8000 bytes)
VIEW0 = 0x060D1000                   # r4 argument of the slot functions (polygon work area, master)

# addresses of the original routines (real, not DBG)
INIT_GAME_WORK = 0x06004844
RAND = 0x0601D6CC
SOUND_STUBS = (0x06016FBC, RAND)            # RAND is stubbed (returns its own address) in the M4 oracle traces: Race(real_rand=True) runs the real generator
SET_SLAVE_EXEC = 0x06017B90                                  # slave::set_slave_exec: the job runs at once on the second CPU object
SLAVE_SP = 0x060FE800
MAT_TMP = 0x060FD2C0                        # scratch matrix (between the end of OBJECT.MDL and the input record buffer)
MYLINE_SET = 0x060049B8
CAR_MOVE2P = 0x06005EE4
CAR_DISP2P = 0x06005F44
P2SYNC = 0x060067A4
CAMERA_CTRL = 0x06005E20
CAR_MOVE_CTRL = 0x06005E84
NORMAL_OTINIT = 0x06006214
CALC_PMAP_SUB = 0x06006668
OTHER_CTRL = 0x0600CAD0
SYNC_CTRL = 0x060067D0
CD_LOAD = 0x0600F550                                         # gfs file loader wrapper: r4 -> u16 + file name; here the scatter-load file is put in memory at once
DISPLAY_STUBS = (0x06010710, 0x06010680, 0x060101E8)      # dispCountDown, dispTachoMeter, dispCar (draw into the polygon pipeline)


def fx(v):
    return s32(v) / 65536.0


def snd_status(frame, b0):
    """the stand-in for the 68000's status byte: a deterministic function of the frame counter and the class byte (0 = silent, 1 = playing, other values)"""
    return (0, 1, 0, 0x20)[(((frame & 0xFFFFFFFF) * 7 + (b0 & 255) * 13 + 3) >> 3) & 3]


class Race:
    def __init__(self, course=0, game_type=0, cartype=0, mt=0, difficulty=0, hooks=True, render=False, rec_buf=REC_BUF, real_rand=False, setup=None, mirror=0, opponents=False, rank=None, cmp=0, ref_rec=None, steer=0, ghost=None, replay=None, rtimes=None, ending=False, recs=None):
        self.course = course
        self.in_frame = False
        self.camera = render
        self.mem = Mem()
        self.cpu = CPU(self.mem)
        m = self.mem
        m.load(0x06004000, open(os.path.join(DISC, "0RALLY.DAT"), "rb").read())
        for fname in ("PRGDATA.DAT", "BG%s.DAT" % COURSES[course]):
            s = Scatter(os.path.join(DISC, fname))
            for b in s.blocks:
                if b.dest < 0x08000000:              # work RAM blocks only (VDP2 VRAM blocks are not needed)
                    m.load(b.dest, s.expanded(b))
        # settings the menus would have written
        m.w16(0x060AA860, game_type)
        m.w16(0x060AA87E, course)
        m.w16(0x060AA880, course * 2)                       # init_game_work_start's stage indices (the replay's sound timeline reads the first)
        m.w16(0x060AA882, course * 4)
        m.w16(0x060AA884, course * 8)
        m.w8(0x060AA808, difficulty)
        m.w8(0x060AA809, mt)
        m.w8(0x060AA80A, cartype)
        m.w8(0x060AA810, 2 if replay is not None else 0)       # play mode: 2 = a replay of the recorded input words (the result screen's "replay: yes": the run just made, at 0x002CF800)
        m.w8(0x060AA811, 1 if setup else 0)             # setup mode 1 = the four tuning bytes below apply (steering, tyres, suspension front / rear)
        for k, v in enumerate(setup or ()):
            m.w8(0x060AA817 + k, v)
        m.w8(0x060AA80C, 3)
        if steer:
            m.w8(0x060AA817, steer)                  # the steering tuning byte (carconfig reads it whatever the set-up mode is): the menus leave the option's default there
        m.w8(0x060AA82E, mirror)
        m.w8(0x060AA803, 0x04)          # pad 1 is an analog device (steering axis byte)
        m.w8(0x0604000D, m.r8(0x0604000D) | 1)      # the boot code sets bit 0 (0x06004084): English text (CGDATAUS.DAT), mph, the USA label layout; the oracle used to leave it 0 (docs/hud.md)
        if render:                                   # course model (0x06048000), VDP1 bitmaps + palettes, VDP2 backgrounds of this course
            for fname in ("%s.MAD" % COURSES[course], "BG%s.DAT" % COURSES[course]):
                s = Scatter(os.path.join(DISC, fname))
                for b in s.blocks:
                    m.load(b.dest, s.expanded(b))
        self.slave = CPU(self.mem)
        self.slave.hooks = self.cpu.hooks
        self.hooked = []
        if hooks:
            for a in SOUND_STUBS + DISPLAY_STUBS:
                if a == RAND and real_rand:
                    continue                         # 0x0601D6CC is the game's random number generator (r = r * 5 + 0x3537 at 0x060D0050), not a sound call: stubbed in the M4 traces, real for event objects
                self.hook_ret(a)                             # dispCar is un-hooked only while draw_pass builds a frame (stale depth-bucket entries otherwise)
            if render:
                self.cpu.hooks[CD_LOAD] = self._cd_load
        if cmp:                                      # the compare with a best run (time attack, practice): [0x060AA80E] = the option, the reference lists as record_dataset leaves them: the run's section times
            m.w8(0x060AA80E, cmp)                    # (R+0x54 list = 0x060AA538, 18 longs of a 3 lap run) for the option 1, the best lap's (R+0x58 list = 0x060AA658, 6 longs) for the option 2
            if ref_rec:
                if cmp == 1:
                    m.load(0x060AA538, ref_rec[0:0x48])
                else:
                    m.load(0x060AA658, ref_rec[0x120:0x138])
        self.init_rec = bytes(m.read(0x060AA400, 0x400))     # the record buffers as the race start finds them (init_game_work clears the first 0x138 bytes)
        self.replay = replay is not None
        if replay is not None:                       # rec_dataset in the replay mode: no compare, no ghost, the input words come from [0x060AAA5C] = 0x002CF800
            m.load(0x002A2000, open(os.path.join(DISC, "CAMERA.DAT"), "rb").read())      # set_replay_camera_data (boot): the cut-scene cameras of the replay
            m.w8(0x060D0089, course)                                                       # the stage of the camera table (init_game_work_start)
            m.load(0x002CF800, replay[:0xF7FE])
            m.w32(0x060AAA5C, 0x002CF800)
            m.w8(0x060AA80E, 0)
            m.w8(0x060AA815, 0)
            m.w32(0x060AAA60, 0x002CF800)
            for k, v in enumerate(rtimes or ()):                  # record_replay_start: the run's times (R+0x8C the total, R+0x90 .. the laps)
                m.w32(0x060B058C + 4 * k, v)
        if ghost is not None:
            self.ghost_prepare(ghost, cartype, mt, setup, steer)      # rec_dataset runs in the race set-up screen, before init_game_work (whose carconfig configures player 2 from these bytes)
        self.call(INIT_GAME_WORK)
        self.recs = recs
        if recs:                                     # a stored course record (record_dataset): [0x060AA184 / 188 / 18C] = the thresholds of the record box flag, R+0x24 = the stored best total
            m.w32(0x060AA184, recs[0]); m.w32(0x060AA188, recs[1]); m.w32(0x060AA18C, recs[2])
            m.w32(CAR0 + 0x524, recs[3])
        self.init_r = bytes(m.read(0x060B0500, 0x100))
        # input-record buffer of the live play mode (R+0x74 / R+0x78 = write pointer / end); the real game points it at its replay store
        if replay is not None:                       # ending_prepare: R+0x74 / R+0x78 = the recorded stage's words (a later stage of the arcade chain's lap_time_init leaves the pointers alone)
            m.w32(CAR0 + 0x574, 0x002CF800)
            m.w32(CAR0 + 0x578, 0x002CF800 + 0xF7FE)
        if replay is None:
            m.w32(CAR0 + 0x574, rec_buf)
            m.w32(CAR0 + 0x578, rec_buf + 0x8000 if rec_buf == REC_BUF else rec_buf + 0x1000)
        self.cpu.hooks[SET_SLAVE_EXEC] = self._set_slave_exec
        self.mem.sinit = self._sinit
        self.ghost = ghost is not None
        if ghost is not None:
            self.call(MYLINE_SET)
        self.game_type = game_type
        if ending:                                   # the ending's replay (ending_setup 0x0601F048): the player's presentation is ending_plmt_start
            m.w32(CAR0 + 4, 0x060066F8)
        self.opponents = opponents
        if opponents:
            self.opp_start(rank)

    # --- the ghost ("myline"): the stored best run of a time attack (game type 4) races in slot 1 ------------------------------------
    def ghost_prepare(self, rec, cartype, mt, setup, steer):
        """what rec_dataset leaves for a time attack with a stored run, and game_init's myline_set: the stored block of the stage (0x002C0000 + stage * 0x3D00: +0 checksum word (non zero = a run
        is stored), +20 car, +21 set-up mode, +22 transmission, +104 the recorded input words), the bytes [0x060AA812 / 813 / 815 / 816] and [0x060AAA60]"""
        m = self.mem
        blk = 0x002C0000 + 0x3D00 * self.course
        m.load(blk, bytes(0x3D00))
        m.w32(blk, 0x12345678)
        m.w8(blk + 20, cartype)
        m.w8(blk + 21, 1 if setup else 0)
        m.w8(blk + 22, mt)
        m.load(blk + 104, rec[:0x3C98])
        m.w8(0x060AA813, cartype)
        m.w8(0x060AA816, 1 if setup else 0)
        for k, v in enumerate(setup or ()):                      # player 2's tuning bytes [0x060AA81C ..] (the stored block's +24 ..): the same as player 1's
            m.w8(0x060AA81C + k, v)
        if steer:
            m.w8(0x060AA81C, steer)
        m.w8(0x060AA812, mt)
        m.w8(0x060AA815, 4)
        m.w32(0x060AAA60, blk + 104)

    def step_2p(self, accel, brake, steer, shift, view):
        """one pass of game_ctrl for the game types 4 / 6 / 8 (0x06005C30): car_move2p_ctrl (slot 0 and slot 1: the ghost), camera, car_disp2p_ctrl, p2sync_ctrl"""
        self.set_pad(accel, brake, steer)
        self.mem.w16(0x060AA00A, (0x2000 if shift > 0 else 0x1000 if shift < 0 else 0) | (0x0400 if view else 0))
        self.run_master(CAR_MOVE2P, r4=VIEW0)
        self.cpu.r[14] = CAR0                                    # replay_cactl (0x0600FC94: the replay's camera buttons) runs with the player's slot
        self.cpu.gbr = CAR0
        self.run_master(0x0600FC94)
        if self.camera:
            self.run_master(CAMERA_CTRL, r4=VIEW0)
        self.run_master(CAR_DISP2P, r4=VIEW0)
        self.run_master(P2SYNC)

    # --- opponents (M7) ----------------------------------------------------------------------------------------------------
    def opp_start(self, rank=None):
        """the race start-up routine 0x06010FE0 for game types 0 / 2: slot functions once (the player's normal_plinit), normal_otinit (spawns the other cars), camera, slot functions again
        (the player's normal_plstart_wait, the first-frame handler of the other cars), camera, calc_pmap_sub, otherCtrl.  rank: the player's start rank when it differs from the one the
        race set-up stores (arcade: 15 on the first stage, else the rank of the previous one)."""
        m = self.mem
        self.set_pad(False, False, 0.0)
        m.w16(0x060AA00A, 0)
        self.run_master(CAR_MOVE_CTRL)
        if rank is not None:
            m.w8(CAR0 + 0xB6, rank)
        self.run_master(NORMAL_OTINIT)
        if self.camera:
            self.run_master(CAMERA_CTRL, r4=VIEW0)
        self.run_master(CAR_MOVE_CTRL)
        if self.camera:
            self.run_master(CAMERA_CTRL, r4=VIEW0)
        self.run_master(CALC_PMAP_SUB)
        self.run_master(OTHER_CTRL)

    def opp_pass(self):
        """game_ctrl's loop over the other cars' slots (handler [+0] then [+4]), sync_ctrl and otherCtrl"""
        m = self.mem
        for k in range(8):
            base = CAR0 + 0x600 + 0x400 * k
            fn = m.r32(base)
            if fn:
                if not fn & 1:
                    self.cpu.r[14] = base
                    self.cpu.gbr = base
                    self.cpu.r[15] = SP
                    self.cpu.r[4] = VIEW0
                    self.cpu.call(fn)
                    fn4 = m.r32(base + 4)
                    if fn4:
                        self.cpu.r[14] = base
                        self.cpu.gbr = base
                        self.cpu.r[15] = SP
                        self.cpu.r[4] = VIEW0
                        self.cpu.call(fn4)
        self.run_master(SYNC_CTRL)
        self.run_master(OTHER_CTRL)

    # --- sound events (M9) -----------------------------------------------------------------------------------------------
    SND_ENTRIES = (0x06016FBC, 0x0601704C, 0x06016F10, 0x06016F24, 0x06016F30, 0x06016F3C, 0x06016F50, 0x06016F60, 0x06016FA4, 0x060175A4, 0x06017580, 0x0600F756)
    SND_STATUS = 0x06016F04                                      # FUN_06016F04: r0 = table, r4 = id -> the 68000's status byte of the id's class (sound RAM 0x25A00780 + 2 * class)

    def snd_trace(self):
        """log every call of the sound library's entry points (address, r4, r5, r6) instead of running them; the status function answers with snd_status() of the frame counter [0x060AA168]
        and the class byte (the port's test makes the same answers, tools/test_drive.c).  Events collect in self.snd_events."""
        self.snd_events = []
        for a in self.SND_ENTRIES:
            def h(c, a=a):
                self.snd_events.append((a, c.r[4] & 0xFFFFFFFF, c.r[5] & 0xFFFFFFFF, c.r[6] & 0xFFFFFFFF))
                c.ret()
            self.cpu.hooks[a] = h

        def st(c):
            b0 = self.mem.r8(0x060A1330 + 4 * (c.r[4] & 0xFFFFFFFF))
            c.r[0] = snd_status(self.mem.r32(0x060AA168), b0)
            c.ret()
        self.cpu.hooks[self.SND_STATUS] = st

    def snd_snapshot(self):
        """the sound state after a pass: the gamesd work bytes 0x060AA012..19, the timer word 0x060AA030, the flag bytes 0x060AA019 / 0x060AA02D, the engine longs, the wall counters, the message bytes"""
        m = self.mem
        return bytes(m.read(0x060AA012, 8)) + bytes(m.read(0x060AA030, 2)) + bytes([m.r8(0x060AA02D)]) + bytes(m.read(0x25A007C0, 8)) + bytes(m.read(0x060AFE68, 4)) + bytes(m.read(0x060AAA44, 2))

    # --- plumbing --------------------------------------------------------------------------------------------------------
    def hook_ret(self, addr):
        self.cpu.hooks[addr] = lambda c: c.ret()
        self.hooked.append(addr)

    def call(self, addr, **regs):
        cpu = self.cpu
        cpu.r[14] = CAR0
        cpu.gbr = CAR0
        cpu.r[15] = SP
        cpu.r[4] = VIEW0
        for k, v in regs.items():
            cpu.r[int(k[1:])] = v
        return cpu.call(addr)

    def _sinit(self):
        """a store to 0x21000000 (SINIT) starts the slave: run the job whose pointer is in the mailbox"""
        job = self.mem.r32(0x002A4000)
        if job:
            self._run_slave_job(job)

    def _set_slave_exec(self, c):
        self._run_slave_job(c.r[4])
        c.ret()

    def _run_slave_job(self, job):
        m = self.mem
        s = self.slave
        saved = m.master
        m.master = False
        try:
            s.r[15] = SLAVE_SP
            s.call(job)
        finally:
            m.master = saved
        m.w32(0x002A4000, 0)                         # mailbox free again

    def _cd_load(self, c):
        m = self.mem
        a = c.r[4] + 2
        raw_dest = None
        if m.r8(a) == 0 and m.r8(c.r[4] + 10):              # the display init's request record (0x06011540): {u16 0, u16 3, u32 destination, name}: a plain file (MAPDATA.DAT)
            raw_dest = m.r32(c.r[4] + 4)
            a = c.r[4] + 10
        name = bytearray()
        while m.r8(a):
            name.append(m.r8(a)); a += 1
        path = os.path.join(DISC, name.decode("ascii"))
        if raw_dest is not None and os.path.isfile(path):
            m.load(raw_dest, open(path, "rb").read())
        elif name and os.path.isfile(path):
            s = Scatter(path)
            for b in s.blocks:
                m.load(b.dest, s.expanded(b))
        c.ret()

    def run_master(self, addr, **regs):
        """call a routine with explicit registers only (no car/matrix defaults)"""
        cpu = self.cpu
        cpu.r[15] = SP
        for k, v in regs.items():
            cpu.r[int(k[1:])] = v & 0xFFFFFFFF
        return cpu.call(addr)

    # --- car access ------------------------------------------------------------------------------------------------------
    def car32(self, off):
        return self.mem.r32(CAR0 + off)

    def carf(self, off):
        return fx(self.mem.r32(CAR0 + off))

    def car16(self, off):
        return self.mem.r16(CAR0 + off)

    def car8(self, off):
        return self.mem.r8(CAR0 + off)

    # --- one logic pass --------------------------------------------------------------------------------------------------
    def set_pad(self, accel=False, brake=False, steer=0.0):
        """what the SMPC pad read would have left at 0x060AA990 (pad 1): the analog device layout - held bits B (0x0100) = accelerator,
        Y+Z (0x0030) = brake, and the axis byte in the top byte of the long at 0x060AA99C (127 = centre, 255 = full right)"""
        m = self.mem
        m.w16(0x060AA990, (0x0100 if accel else 0) | (0x0030 if brake else 0))
        axis = max(0, min(255, int(round(127 + 128 * steer))))
        m.w32(0x060AA99C, axis << 24)

    def step(self, accel=True, brake=False, steer=0.0, shift=0, view=0):
        """one logic pass: fn0 (move) then fn4 (presentation / countdown) of slot 0, like the single-player path of game_ctrl.  shift: +1 / -1 = the shift up / down button
        (the analog pad's Down / Up bits, tested against the OR-switch word 0x060AA00A) for manual transmissions; view: the camera view toggle button"""
        if self.ghost or (self.replay and self.game_type in (4, 6)):
            self.step_2p(accel, brake, steer, shift, view)
            return
        self.set_pad(accel, brake, steer)
        self.mem.w16(0x060AA00A, (0x2000 if shift > 0 else 0x1000 if shift < 0 else 0) | (0x0400 if view else 0))      # view: the camera view button (A / X of the analog pad's key row)
        fn0 = self.car32(0)
        if not fn0 & 1:
            self.call(fn0)
        if self.replay:                                  # game_ctrl's one player path: replay_cactl (0x0600FC94) between the slot function and the camera
            self.cpu.r[14] = CAR0
            self.cpu.gbr = CAR0
            self.run_master(0x0600FC94)
        if self.camera:
            self.run_master(CAMERA_CTRL, r4=VIEW0)     # builds the view matrix at 0x060D1000 (only needed for rendering)
        fn4 = self.car32(4)
        if fn4 and not fn4 & 1:
            self.call(fn4)
        if self.opponents:
            self.opp_pass()

    # --- rendering (M5): the game's own polygon pipeline, headless ----------------------------------------------------------
    def init_render(self):
        """what the boot code does once: both polygon-maker work areas + flat-gouraud table, then the 224 perspective scale"""
        self.run_master(0x060183D4)                     # mkpoly_init
        self.run_master(0x0601F6D0)                     # ending::set_game_pers -> perspective(224)

    def draw_frame(self):
        """frame init, camera, draw_course (master + slave halves) and game_zsort, as game_ctrl does after the car functions;
        returns the 512 KB VDP1 VRAM as the V-IN DMA would leave it (command table = first 0xD000 bytes of 0x060C0000)"""
        m = self.mem
        self.run_master(0x06018634)                     # m_mkpoly_frame_init
        m.master = False
        self.slave.r[15] = SLAVE_SP
        self.slave.call(0x0601864A)                     # s_mkpoly_frame_init
        m.master = True
        self.run_master(CAMERA_CTRL, r4=VIEW0)
        cnt = m.r16(0x060AA000)
        self.run_master(0x060187C0, r4=VIEW0, r5=0x060AA104, r6=cnt)       # mkpoly::draw_course
        self.run_master(0x0601B284)                     # zsort::game_zsort
        vram = bytearray(m.read(0x25C00000, 0x80000))
        vram[0:0xD000] = m.read(0x060C0000, 0xD000)
        return bytes(vram)

    def load_game_textures(self):
        """texture::set_game_texture: car / object model + textures (OBJECT.MAD through the CD hook), course tag, animal textures"""
        flags = self.mem.r8(0x060AA803)
        self.mem.w8(0x060AA803, flags | 0x10)           # bit 4 = "files come from the CD" (cd_file_init sets it): the loader hook is used
        try:
            self.run_master(0x0601AB6C)
        finally:
            self.mem.w8(0x060AA803, flags)

    def draw_parts(self, insts, fn=0x06018908):
        """frame init, then the game's own draw function (default draw_polygon) for each (part address, 3x4 matrix as 12 floats), then game_zsort;
        returns the VDP1 VRAM image (see draw_frame).  Used for the object gallery (tools/export_objects.py)."""
        m = self.mem
        self.run_master(0x06018634)
        m.master = False
        self.slave.r[15] = SLAVE_SP
        self.slave.call(0x0601864A)
        m.master = True
        for part, mat in insts:
            for k in range(12):
                m.w32(MAT_TMP + 4 * k, int(round(mat[k] * 65536.0)) & 0xFFFFFFFF)
            self.run_master(fn, r4=MAT_TMP, r5=part)
        self.run_master(0x0601B284)
        vram = bytearray(m.read(0x25C00000, 0x80000))
        vram[0:0xD000] = m.read(0x060C0000, 0xD000)
        return bytes(vram)

    def draw_pass(self, accel=True, brake=False, steer=0.0, car=True, events=False, hud=False):
        """one complete race pass as game_ctrl does it, ending with the frame built: frame init, slot-0 move, camera, slot-0 display (player's car),
        draw_course, game_zsort.  Returns the VDP1 VRAM image (see draw_frame)."""
        m = self.mem
        self.in_frame = True
        self.set_pad(accel, brake, steer)
        self.run_master(0x06018634)
        m.master = False
        self.slave.r[15] = SLAVE_SP
        self.slave.call(0x0601864A)
        m.master = True
        fn0 = self.car32(0)
        if not fn0 & 1:
            self.call(fn0)
        self.run_master(CAMERA_CTRL, r4=VIEW0)
        fn4 = self.car32(4)
        popped = {}
        for ad in ((0x060101E8,) if car else ()) + ((0x06010680, 0x06010710) if hud else ()):       # draw the player's car (dispCar) / the tachometer and count-down sprites for this frame only
            h = self.cpu.hooks.pop(ad, None)
            if h is not None:
                popped[ad] = h
        self.hud_calls = []                             # inputs of dispTachoMeter (rpm word) / dispCountDown (index, scale) as the game's own code drew them in this frame
        if hud:
            def watch_count_down(c):
                self.hud_calls.append(("cdown", c.r[5] & 0xFFFFFFFF, s32(c.r[6])))
                c.step_orig()
            def watch_tacho(c):
                self.hud_calls.append(("tach", m.r32(CAR0 + 0x8C)))
                c.step_orig()
            self.cpu.hooks[0x06010710] = watch_count_down
            self.cpu.hooks[0x06010680] = watch_tacho
        try:
            if fn4 and not fn4 & 1:
                self.call(fn4)
        finally:
            self.cpu.hooks.pop(0x06010710, None)
            self.cpu.hooks.pop(0x06010680, None)
            self.cpu.hooks.update(popped)
        if events:                                      # game_ctrl step 6 / 7
            self.ev_in = {"rng": m.r32(0x060D0050), "game_type": m.r16(0x060AA860), "c5": m.r8(0x060B00C5), "seg": m.r16(0x060B0060), "c8": m.r16(0x060B00C8),
                          "ca": m.r16(0x060B00CA), "x": s32(m.r32(0x060B0018)), "y": s32(m.r32(0x060B001C)), "z": s32(m.r32(0x060B0020)), "yaw": s32(m.r32(0x060B0010)),
                          "nseg": m.r16(0x060AA006),
                          "heli": bytes(m.read(0x060EFE00, 512)), "dust": bytes(m.read(0x060D2000, 512))}                      # event objects of the visible blocks, then the dust / spray list (the race start must have loaded EVENTTBL: bit 4)
            self.run_master(0x0601BCB8, r4=0x060AA140, r5=m.r16(0x060AA002))
            if m.r8(0x060B00F5) < 8:
                self.run_master(0x0601D410)
        cnt = m.r16(0x060AA000)
        self.run_master(0x060187C0, r4=VIEW0, r5=0x060AA104, r6=cnt)
        self.run_master(0x0601B284)
        self.in_frame = False
        vram = bytearray(m.read(0x25C00000, 0x80000))
        vram[0:0xD000] = m.read(0x060C0000, 0xD000)
        return bytes(vram)
