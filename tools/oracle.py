"""M4: the oracle.  Runs the ORIGINAL driving code (docs/physics.md) on the real course data in the SH-2 interpreter (`sh2.py`, `saturn_env.py`),
fed with a recorded or scripted input stream, and writes a per-step trace of the car.  The C port (M6) is checked against these traces.

    python tools/oracle.py run   --course 0 --scenario drive [--type 2] [--mt 0] [--car 0] [--diff 0] [--max 12000] [--slots]     -> extracted/oracle/desert_drive.csv (+ .inp)
    python tools/oracle.py replay extracted/oracle/desert_drive.inp                      re-runs a recorded input file (settings in its header line), compares with the .csv
    python tools/oracle.py compare a.csv b.csv                                            diff two traces (oracle vs C port)
    python tools/oracle.py list                                                           the scenarios
    (run ... --slots also writes <name>.slots: the whole car slot, 0x600 bytes, after every pass)
    (run ... --hudend: like --hud --render, and keeps stepping 420 passes after the goal / game over; also writes <name>.hude, the end-of-race screen's NBG0 name tables + registers)
    (run ... --vmax 2.4 --curve 0.5: other pilot parameters)

Trace columns (`TRACE_COLS`): inputs of the pass (accel, brake, axis byte) then the raw 32-bit car fields after the pass (signed 16.16 unless noted).
Input files (.inp): one line per logic pass, `accel brake axis`; steps before the race starts (countdown) are part of the file, so the same file replays
from `Race()` creation.
Scenarios: `drive` follows the AI guide line with pure pursuit until the goal / game over; `accel`, `brake`, `slalom`, `coast` are fixed scripts used for
unit-level parity (they start from the grid, wait for GO, then apply the script).
"""
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from saturn_env import Race, CAR0, COURSES, fx  # noqa: E402
from sh2 import s32  # noqa: E402

OUT = os.path.join(ROOT, "extracted", "oracle")
GUIDE = (0x00299ED8, 0x0029B1C8, 0x0029C4B8, 0x0029D7A8)       # guide_africa / yosemite / corse / europe = desert, forest, mount, lake
NGUIDE = 300
FN_RACE = 0x060062F4                                              # normal_plmove (racing)
FN_GOAL = (0x0600640C, 0x06006470)                                # goali_plmove, goal_plmove
FN_GOVER = 0x060063F0                                             # gover_plmove

# (name, offset, size, signed) - the logged car fields
FIELDS = [
    ("px", 0x18, 4), ("py", 0x1C, 4), ("pz", 0x20, 4),
    ("rx", 0x0C, 4), ("ry", 0x10, 4), ("rz", 0x14, 4),
    ("vx", 0x24, 4), ("vy", 0x28, 4), ("vz", 0x2C, 4),
    ("wx", 0x70, 4), ("wy", 0x74, 4), ("wz", 0x78, 4),
    ("steer", 0x30, 4), ("speed", 0x94, 4), ("rpm", 0x8C, 4),
    ("gear", 0xB0, 2), ("contact", 0xB2, 1), ("mask", 0xF6, 1),
    ("surf0", 0x1D0, 2), ("surf1", 0x1E4, 2), ("surf2", 0x1F8, 2), ("surf3", 0x20C, 2),
    ("state", 0x0A, 2), ("mode", 0xF5, 1), ("seg", 0x60, 2), ("lap", 0xC8, 2), ("timer", 0x534, 4),
]
TRACE_COLS = ["step", "accel", "brake", "axis", "fn0"] + [f[0] for f in FIELDS]


def read_field(r, off, size):
    m = r.mem
    if size == 4:
        return s32(m.r32(CAR0 + off))
    if size == 2:
        v = m.r16(CAR0 + off)
        return v - 0x10000 if v & 0x8000 else v
    v = m.r8(CAR0 + off)
    return v - 0x100 if v & 0x80 else v


def snapshot(r):
    return [r.car32(0)] + [read_field(r, off, sz) for _, off, sz in FIELDS]


def axis_of(steer):
    return max(0, min(255, int(round(127 + 128 * steer))))


# --- drivers: object with .next(race, step) -> (accel, brake, steer) -----------------------------------------------------------------
class Script:
    """fixed script after GO: list of (steps, accel, brake, steer)"""

    def __init__(self, parts):
        self.parts = parts
        self.t = None

    def next(self, r, i):
        if r.car32(0) != FN_RACE:
            return True, False, 0.0
        if self.t is None:
            self.t = 0
        t = self.t
        self.t += 1
        for n, a, b, s in self.parts:
            if t < n:
                return a, b, s(t) if callable(s) else s
            t -= n
        return None


class Pilot:
    """pure pursuit on the AI guide line (300 points, 16.16 x/y/z) with a curvature-based speed limit"""

    def __init__(self, race, vmax=2.1, curve_k=1.0, steer_k=1.6):
        g = GUIDE[race.course]
        m = race.mem
        self.pts = [(s32(m.r32(g + 16 * k)) / 65536.0, s32(m.r32(g + 16 * k + 8)) / 65536.0) for k in range(NGUIDE)]
        self.near = 0
        self.vmax = vmax                      # m/step (2.1 = 227 km/h)
        self.cool = 0
        self.curve_k = curve_k
        self.steer_k = steer_k

    def nearest(self, x, z):
        best, bi = 1e30, self.near
        for d in range(-6, 40):
            k = (self.near + d) % NGUIDE
            px, pz = self.pts[k]
            dd = (px - x) ** 2 + (pz - z) ** 2
            if dd < best:
                best, bi = dd, k
        self.near = bi
        return bi

    def ahead(self, k, dist):
        """guide point `dist` metres along the line from index k"""
        acc = 0.0
        while acc < dist:
            a = self.pts[k % NGUIDE]
            b = self.pts[(k + 1) % NGUIDE]
            acc += math.hypot(b[0] - a[0], b[1] - a[1])
            k += 1
        return self.pts[k % NGUIDE]

    def next(self, r, i):
        if r.car32(0) not in (FN_RACE,):
            return True, False, 0.0
        x, z = r.carf(0x18), r.carf(0x20)
        yaw = r.carf(0x10)
        v = r.carf(0x94)
        heading = -yaw
        k = self.nearest(x, z)
        ms = v * 30.0
        tx, tz = self.ahead(k, 6.0 + 0.45 * ms)
        fx_, fz_ = self.ahead(k, 25.0 + 1.6 * ms)
        e1 = wrap(math.atan2(tx - x, tz - z) - heading)
        e2 = wrap(math.atan2(fx_ - x, fz_ - z) - heading)
        steer = max(-1.0, min(1.0, self.steer_k * e1))
        curve = abs(wrap(e2 - e1))
        vlim = self.vmax / (1.0 + self.curve_k * curve) if curve > 0.05 else self.vmax
        brake = v > vlim * 1.12 and abs(e1) < 1.0
        accel = v < vlim and not brake
        shift = 0
        if r.mem.r8(CAR0 + 0xF4):                                # manual transmission: neutral at the start, shift on the engine speed
            gear, rpm = r.car16(0xB0), r.carf(0x8C)
            if self.cool > 0:
                self.cool -= 1
            elif gear < 4 and (gear == 0 or rpm > 7600):
                shift, self.cool = 1, 6
            elif gear > 1 and rpm < 3000:
                shift, self.cool = -1, 6
        return accel, brake, steer, shift


def wrap(a):
    while a > math.pi:
        a -= 2 * math.pi
    while a < -math.pi:
        a += 2 * math.pi
    return a


SCENARIOS = {
    "drive": "pure pursuit on the guide line until goal / game over",
    "accel": "full throttle, no steering, 240 passes after GO",
    "brake": "full throttle 150 passes, then full brake 90 passes",
    "slalom": "full throttle with a +-0.6 sine steering (period 60 passes), 300 passes after GO",
    "coast": "throttle 120 passes, then release 120 passes",
    "idle": "no throttle at all (the clock runs out: game over)",
}


def make_driver(name, race, pilot=None):
    if name == "drive":
        return Pilot(race, **(pilot or {}))
    if name == "idle":
        return Script([(100000, False, False, 0.0)])
    if name == "accel":
        return Script([(240, True, False, 0.0)])
    if name == "brake":
        return Script([(150, True, False, 0.0), (90, False, True, 0.0)])
    if name == "slalom":
        return Script([(300, True, False, lambda t: 0.6 * math.sin(2 * math.pi * t / 60.0))])
    if name == "coast":
        return Script([(120, True, False, 0.0), (120, False, False, 0.0)])
    raise SystemExit("unknown scenario %r (see `oracle.py list`)" % name)


def ended(r):
    fn0 = r.car32(0)
    return fn0 in FN_GOAL or fn0 == FN_GOVER


SLOT_BYTES = 0x600                                              # car slot incl. the lap / time block at +0x500


def run(course, scenario, max_steps=12000, mt=0, cartype=0, difficulty=0, game_type=2, verbose=True, slots=None, setup=None, mirror=0, cams=None, render=False, view=0, toggles=(), huds=None, skys=None, hude=None, post=60, pilot=None, opps=None, rank=None, perturb=(), snds=None, cmp=0, ref_rec=None, steer=0, ghost=None, slots1=None, replay=None, rtimes=None, ending=False, recs=None):
    r = Race(course, game_type=game_type, mt=mt, cartype=cartype, difficulty=difficulty, setup=setup, mirror=mirror, render=render or huds is not None or opps is not None, opponents=opps is not None, rank=rank, cmp=cmp, ref_rec=ref_rec, steer=steer, ghost=ghost, replay=replay, rtimes=rtimes, ending=ending, recs=recs)
    hudstate = hud_start(r, huds, hude)
    if skys is not None:
        sky_start(r)
    r.mem.w8(0x060AA01B, view)                         # camera view: 0 = A (in the car), 2 = B (chase)
    r.camera = r.camera or cams is not None                          # run the camera code every pass (view matrix at 0x060D1000)
    drv = make_driver(scenario, r, pilot)
    if snds is not None:
        r.snd_trace()
    rows, inputs = [], []
    watch = [(int(w.split(":")[0], 16), int(w.split(":")[1])) for w in os.environ.get("ORACLE_WATCH", "").split(",") if w]     # development: ORACLE_WATCH=hexaddr:len,... prints every change of those bytes after a pass
    watched = {}

    def watch_pass(n):
        for a, ln in watch:
            now = bytes(r.mem.read(a, ln))
            if watched.get(a) != now:
                print("WATCH pass %d %08x: %s" % (n, a, now.hex()))
                watched[a] = now
    for i in range(max_steps):
        cmd = drv.next(r, i)
        if cmd is None:
            break
        a, b, s = cmd[:3]
        sh = cmd[3] if len(cmd) > 3 else 0
        vw = 1 if i in toggles else 0
        ax = axis_of(s)
        if snds is not None:
            r.mem.w32(HUD_FRAME, i)                         # the frame counter the sound code reads: the pass number
        r.step(a, b, (ax - 127) / 128.0, sh, vw)
        hud_pass(r, hudstate, huds)
        if watch:
            watch_pass(len(rows))
        if snds is not None:
            snds.append(snd_record(r))
        if i in perturb:
            opp_perturb(r)
        if opps is not None:
            opps.append(opp_snapshot(r))
        if skys is not None:
            skys.append(sky_pass(r))
        inputs.append((int(a), int(b), ax, sh, vw))
        rows.append([i, int(a), int(b), ax] + snapshot(r))
        if slots is not None:
            slots.append(r.mem.read(CAR0, SLOT_BYTES))
        if slots1 is not None:
            slots1.append(r.mem.read(CAR0 + SLOT_BYTES, SLOT_BYTES))
        if cams is not None:
            cams.append(cam_record(r))
        if ended(r):
            for _ in range(post):                                # let the goal sequence play out a little (--hudend: the whole end-of-race screen)
                if snds is not None:
                    r.mem.w32(HUD_FRAME, len(rows))
                r.step(False, False, 0.0)
                hud_pass(r, hudstate, huds)
                if watch:
                    watch_pass(len(rows))
                if snds is not None:
                    snds.append(snd_record(r))
                if opps is not None:
                    opps.append(opp_snapshot(r))
                if skys is not None:
                    skys.append(sky_pass(r))
                inputs.append((0, 0, 127, 0, 0))
                rows.append([len(rows), 0, 0, 127] + snapshot(r))
                if slots is not None:
                    slots.append(r.mem.read(CAR0, SLOT_BYTES))
                if slots1 is not None:
                    slots1.append(r.mem.read(CAR0 + SLOT_BYTES, SLOT_BYTES))
                if cams is not None:
                    cams.append(cam_record(r))
            break
        if verbose and i % 300 == 0:
            print("  pass %5d  %-12s %6.1f km/h  seg %3d lap %d  t=%4x" % (i, "race" if r.car32(0) == FN_RACE else "grid", r.carf(0x94) * 108,
                                                                           r.car16(0x60), r.car16(0xC8), r.mem.r32(CAR0 + 0x534) & 0xFFFF))
    return r, rows, inputs


def snd_record(r):
    """the sound events of one pass (u32 count, 4 longs each) and the sound state afterwards (Race.snd_snapshot)"""
    ev, r.snd_events = r.snd_events, []
    return struct.pack("<I", len(ev)) + b"".join(struct.pack("<4I", *e) for e in ev) + r.snd_snapshot()


OPP_BYTES = 0x200                                               # the part of each other car's slot that is compared (the move / collision fields, +0x00 .. +0x1FF)
OPP_GLOBALS = ((0x060B02B0, 0x10), (0x060B07A0, 0x44))          # car count, rank / progress words, guide table pointer; the sorted car list + frame counter


def opp_snapshot(r):
    """the other cars after a pass: 8 slots x OPP_BYTES, then the globals of otherCtrl"""
    m = r.mem
    return b"".join(bytes(m.read(CAR0 + 0x600 + 0x400 * k, OPP_BYTES)) for k in range(8)) + b"".join(bytes(m.read(a, n)) for a, n in OPP_GLOBALS)


def opp_nearest_guide(r):
    """the guide index nearest to the player (the C test makes the same choice from the header)"""
    m = r.mem
    g = m.r32(0x060B02B8)
    px, pz = s32(m.r32(CAR0 + 0x18)), s32(m.r32(CAR0 + 0x20))
    best, bi = None, 0
    for k in range(300):
        d = (s32(m.r32(g + 16 * k)) - px) ** 2 + (s32(m.r32(g + 16 * k + 8)) - pz) ** 2
        if best is None or d < best:
            best, bi = d, k
    return bi


def opp_perturb(r):
    """synthetic car-car contact (tools/test_drive.c does the same): the first two other cars are put next to each other on the guide line just ahead of the player, at LOD 2"""
    m = r.mem
    idx = opp_nearest_guide(r) + 1
    g = m.r32(0x060B02B8)
    px, py, pz = (m.r32(g + 16 * idx + o) for o in (0, 4, 8))
    for k, (dx, dz) in enumerate(((0, 0), (0x8000, 0x5000))):
        b = CAR0 + 0x600 + 0x400 * k
        m.w32(b + 0x18, (px + dx) & 0xFFFFFFFF); m.w32(b + 0x1C, (py + 0x4CCC) & 0xFFFFFFFF); m.w32(b + 0x20, (pz + dz) & 0xFFFFFFFF)
        m.w16(b + 0x10A, idx); m.w16(b + 0x102, 2); m.w16(b + 0x104, 2)
        for o in (0x24, 0x28, 0x2C, 0x128):
            m.w32(b + o, 0)
        m.w8(b + 0xF0, (m.r8(CAR0 + 0xF0) + 1) & 0x7F)


SYNTH_PASSES = 330


def hudsynth(path, seed=1):
    """synthetic end-of-race cases for tools/test_hudend.c: the display set-up of the race screen, then a car slot with the given control byte / placing / lap / times is put in memory and
    `game_display` runs SYNTH_PASSES times; covers what the real traces cannot reach (arcade goal on every stage, every placing, 10+ minute times ...).
    file: "HSY1", u32 cases, per case: u32 course, game type, ctrl, rank, lap, laps, R[0x50] (slot +0x500), the HUDE_WORDS initial words, then per pass: the changes (as in .hude) and the byte R[0]."""
    import random
    import hud as hudmod
    rnd = random.Random(seed)
    cases = []
    for course in range(4):
        for gt in (0, 2):
            for ctrl in (8, 10):
                for rep in range(3):
                    cases.append((course, gt, ctrl, rnd.choice((0, 1, 2, 3, 4, 5, 6, 7, 8, 255)), rnd.randint(0, 4) if (gt == 2 and ctrl == 10) else rnd.randint(0, 2), rnd.choice((2, 3, 3, 4)), rep))
    with open(path, "wb") as f:
        f.write(b"HSY1" + struct.pack("<I", len(cases)))
        for course, gt, ctrl, rank, lap, laps, rep in cases:
            r = Race(course, game_type=gt, render=True)
            hud_setup(r, hudmod)
            m = r.mem
            m.w8(CAR0 + 0xF5, ctrl)
            m.w8(CAR0 + 0xB6, rank)
            m.w16(CAR0 + 0xC8, lap)
            m.w16(0x060AA006, laps)
            m.w8(CAR0 + 0x500, 0)
            rb = bytearray(0x50)
            for off in range(0x0C, 0x50, 4):                 # times: byte 0 free, then minutes, seconds, hundredths (random bytes: BCD or not, some with 10+ minutes)
                rb[off:off + 4] = bytes([rnd.randint(0, 255), rnd.choice((rnd.randint(0, 9), rnd.randint(0, 0x1F), rnd.randint(0, 255))), rnd.randint(0, 0x59), rnd.randint(0, 0x99)])
            rb[0] = 0
            rb[1:12] = bytes(rnd.randint(0, 255) for _ in range(11))
            m.load(CAR0 + 0x500, bytes(rb))
            m.w8(0x060AA803, 0x64)
            f.write(struct.pack("<7I", course, gt, ctrl, rank, lap, laps, 0)[:24])
            f.write(bytes(m.read(CAR0 + 0x500, 0x50)))
            prev = hude_snapshot(r)
            f.write(prev)
            for n in range(SYNTH_PASSES):
                m.w32(HUD_FRAME, n)
                hudmod.display(r)
                cur = hude_snapshot(r)
                f.write(changes(prev, cur) + bytes([m.r8(CAR0 + 0x500)]))
                prev = cur
    print("%d cases -> %s" % (len(cases), path))


CAM_BYTES = 0xB4 + 0x30 + 0x30 + 2 + 18

HUD_NT = 0x25E3A000                                   # the three 64 x 64 name table pages of NBG2 / NBG3 (tools/hud.py)
HUD_SCROLL = 0x060AA874                               # shadow of the NBG2 / NBG3 scroll words (x2, y2, x3, y3)
HUD_FRAME = 0x060AA168                                # the free-running frame counter the HUD blinks with (the real loop counts it; here the pass index)


def hud_snapshot(r):
    """the HUD's output: 0x6000 bytes of name tables + the four scroll words, as 0x3004 big endian words"""
    return bytes(r.mem.read(HUD_NT, 0x6000)) + bytes(r.mem.read(HUD_SCROLL, 8))


def hud_setup(r, hudmod):
    """the race screen's display set-up as the game does it, with MAPDATA.DAT loaded (the dial face, the gear box ...), see tools/refframe.py capture"""
    r.init_render()                                    # the CD loader hook the display set-up's file request needs comes with the render set-up (Race(render=True))
    r.load_game_textures()
    hudmod.setup(r)


HUDE_NT = 0x25E28000                                  # NBG0's four name table pages (maps 0x0A .. 0x0D x 0x4000, 64 x 64 two-word cells): the end-of-race screens (docs/hud.md 7)
HUDE_BYTES = 0x10000
HUDE_REGS = ((0x060AA864, 2), (0x060AA868, 2), (0x060AA8E6, 2), (0x25F80020, 2), (0x25F800D0, 2), (0x060AA178, 4), (0x060AA17C, 4), (0x060AA803, 1), (0x060AAA45, 1))      # NBG0 scroll x / y (integer parts), BGON shadow / register, WCTLA, state, timer, flag byte, message byte
HUDE_WORDS = HUDE_BYTES // 2 + 11


def hude_snapshot(r):
    """the end-of-race screen's output as big endian words: the NBG0 pages, then the registers above (a long = two words, a byte = one word)"""
    m = r.mem
    out = bytearray(m.read(HUDE_NT, HUDE_BYTES))
    for a, n in HUDE_REGS:
        v = m.r32(a) if n == 4 else m.r16(a) if n == 2 else m.r8(a)
        out += struct.pack(">I", v) if n == 4 else struct.pack(">H", v)
    return bytes(out)


def hud_start(r, huds, hude=None):
    """HUD observation (--hud): the display set-up of the race screen (tools/hud.py), then every pass runs game_display; the name tables are logged as changes"""
    if huds is None:
        return None
    import hud as hudmod
    hud_setup(r, hudmod)
    snap = hud_snapshot(r)
    huds.append(snap)
    st = {"mod": hudmod, "prev": snap, "n": 0, "hude": hude}
    if hude is not None:
        st["eprev"] = hude_snapshot(r)
        hude.append(st["eprev"])
    return st


def changes(prev, cur):
    out = bytearray()
    n = 0
    for k in range(0, len(cur), 2):
        if cur[k:k + 2] != prev[k:k + 2]:
            out += struct.pack(">HH", k // 2, struct.unpack(">H", cur[k:k + 2])[0])
            n += 1
    return struct.pack(">H", n) + bytes(out)


def hud_pass(r, st, huds):
    if st is None:
        return
    r.mem.w32(HUD_FRAME, st["n"])
    r.mem.w16(0x060AA004, st["n"] & 0xFFFF)                 # the race loop's second counter (0x060112CA: a word, replay_display blinks with its bit 3)
    st["n"] += 1
    st["mod"].display(r)
    cur = hud_snapshot(r)
    huds.append(changes(st["prev"], cur))
    st["prev"] = cur
    if st["hude"] is not None:
        cur = hude_snapshot(r)
        st["hude"].append(changes(st["eprev"], cur))
        st["eprev"] = cur


SKY_TABLE = 0x060B9000


def sky_start(r):
    """the rotation parameter table of the sky as the boot leaves it: init_vdp2sys + init_gscroll (docs/vdp2_background.md 3); the rscroll handler is the 1P one"""
    r.run_master(0x06005518)
    r.run_master(0x06015B80)
    if r.mem.r32(0x060AA9F0) == 0:
        r.mem.w32(0x060AA9F0, 0x06006BBC)


def sky_pass(r):
    """what the sky does once per pass: rscroll_ctrl (step 9 of the frame: set_grpara2 turns the camera angles into the targets 0x060AA9F8..), then the V-IN's set_grpara_int (recipe 38,
    one IIR step towards them); returns A, B, D, E, Mx of the rotation parameter table + the targets (Mx, A, B, D) as 9 big endian longs"""
    m = r.mem
    r.run_master(0x06005E54)
    r.run_master(0x06015FEC)
    t = SKY_TABLE
    return b"".join(m.read(a, 4) for a in (t + 0x1C, t + 0x20, t + 0x28, t + 0x2C, t + 0x44, 0x060AA9F8, 0x060AA9FC, 0x060AAA00, 0x060AAA04))


def cam_record(r):
    """the camera state after a pass: work area (0xB4 bytes at 0x060AA9B0), view matrix (0x060D1000), its copy (0x060D1800), camera mode and view bytes (0x060AA01A / 01B), part count + block list"""
    m = r.mem
    return (m.read(0x060AA9B0, 0xB4) + m.read(0x060D1000, 0x30) + m.read(0x060D1800, 0x30) + bytes([m.r8(0x060AA01A), m.r8(0x060AA01B)])
            + m.read(0x060AA002, 2) + m.read(0x060AA140, 16))        # + the get_poly_map result: part count and block list (needs the course model: --render)


def write_trace(path, rows, inputs, slots=None, settings="", cams=None):
    if cams is not None:                                        # <name>.cam: u32 passes, u32 bytes, camera_record() per pass
        with open(path + ".cam", "wb") as f:
            f.write(struct.pack("<II", len(cams), CAM_BYTES))
            for c in cams:
                f.write(c)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if slots is not None:                                       # whole car slot after every pass: u32 passes, u32 bytes per pass, data
        with open(path + ".slots", "wb") as f:
            f.write(struct.pack("<II", len(slots), SLOT_BYTES))
            for s in slots:
                f.write(s)
    with open(path + ".csv", "w", newline="\n") as f:
        f.write(",".join(TRACE_COLS) + "\n")
        for row in rows:
            f.write(",".join(str(v) if k != 4 else "%08x" % v for k, v in enumerate(row)) + "\n")
    with open(path + ".inp", "w", newline="\n") as f:
        f.write("# %s\n" % settings)
        for a, b, ax, sh, vw in inputs:
            f.write("%d %d %d %d %d\n" % (a, b, ax, sh, vw))


def read_inputs(inpfile):
    """-> (settings dict from the `# course=0 type=2 mt=0 car=0 diff=0` header, [(accel, brake, axis)])"""
    st, inp = {}, []
    for line in open(inpfile):
        if line.startswith("#"):
            st = dict(kv.split("=") for kv in line[1:].split())
        elif line.strip():
            inp.append(tuple(int(x) for x in line.split()))
    return st, inp


def replay(inpfile):
    st, inp = read_inputs(inpfile)
    setup = [int(x) for x in st["setup"].split(",")] if st.get("setup") else None
    r = Race(int(st.get("course", 0)), game_type=int(st.get("type", 2)), mt=int(st.get("mt", 0)), cartype=int(st.get("car", 0)),
             difficulty=int(st.get("diff", 0)), setup=setup, mirror=int(st.get("mirror", 0)))
    r.mem.w8(0x060AA01B, int(st.get("view", 0)))
    rows = []
    for i, t in enumerate(inp):
        a, b, ax = t[:3]
        r.step(bool(a), bool(b), (ax - 127) / 128.0, t[3] if len(t) > 3 else 0, t[4] if len(t) > 4 else 0)
        rows.append([i, a, b, ax] + snapshot(r))
    return rows


def main():
    args = sys.argv[1:]
    if not args or args[0] == "list":
        for k, v in SCENARIOS.items():
            print("%-8s %s" % (k, v))
        return
    cmd = args[0]

    def opt(name, default):
        return type(default)(args[args.index(name) + 1]) if name in args else default

    course = opt("--course", 0)
    if cmd == "run":
        scenario = opt("--scenario", "drive")
        slots = [] if "--slots" in args else None
        cams = [] if ("--camera" in args or "--render" in args) else None
        gt, mt, car, diff = opt("--type", 2), opt("--mt", 0), opt("--car", 0), opt("--diff", 0)
        setup_s, mirror, tag = opt("--setup", ""), opt("--mirror", 0), opt("--tag", "")
        view = opt("--view", 0)
        toggles = [int(x) for x in opt("--toggle", "").split(",") if x]
        setup = [int(x) for x in setup_s.split(",")] if setup_s else None
        hudend = "--hudend" in args
        huds = [] if ("--hud" in args or hudend) else None
        hude = [] if hudend else None
        skys = [] if "--sky" in args else None
        opps = [] if "--opp" in args else None
        rank = opt("--rank", -1)
        perturb = [int(x) for x in opt("--perturb", "").split(",") if x]
        snds = [] if "--snd" in args else None
        cmp_mode = opt("--cmp", 0)                              # the compare with a best run: [0x060AA80E], the reference lists from a .rec file of an earlier run (--ref)
        ref_rec = open(opt("--ref", ""), "rb").read() if "--ref" in args else None
        ghost = open(opt("--ghost", ""), "rb").read() if "--ghost" in args else None   # a stored best run (an earlier run's .irec): game type 4 races it in slot 1
        slots1 = [] if (ghost is not None and slots is not None) else None
        rtimes = [int(x, 16) for x in opt("--rtimes", "").split(",") if x] or None                  # the replayed run's times as the record's replay start leaves them in the R block: total, lap 1 .. 3 (BCD hex)
        replay_in = open(opt("--replay", ""), "rb").read() if "--replay" in args else None      # the replay of a run (an earlier run's .irec): play mode 2, the input words come from the work RAM
        recs = [int(x, 16) for x in opt("--recs", "").split(",") if x] or None                  # a stored course record: the three thresholds of the record box ([0x060AA184 / 188 / 18C]) and the stored best total (R+0x24), BCD hex
        pilot = {k[2:]: float(args[args.index(k) + 1]) for k in ("--vmax", "--curve_k", "--steer_k") if k in args}
        r, rows, inputs = run(course, scenario, opt("--max", 12000), mt=mt, cartype=car, difficulty=diff, game_type=gt, slots=slots, setup=setup, mirror=mirror, cams=cams, render="--render" in args,
                              view=view, toggles=toggles, huds=huds, skys=skys, hude=hude, post=420 if hudend else 60, pilot=pilot, opps=opps, rank=rank if rank >= 0 else None, perturb=perturb, snds=snds, cmp=cmp_mode, ref_rec=ref_rec, steer=opt("--steer", 0), ghost=ghost, slots1=slots1, replay=replay_in, rtimes=rtimes, ending="--ending" in args, recs=recs)
        path =os.path.join(OUT, "%s_%s%s" % (COURSES[course].lower(), scenario, tag))
        write_trace(path, rows, inputs, slots, "course=%d type=%d mt=%d car=%d diff=%d setup=%s mirror=%d view=%d opp=%d rank=%d perturb=%s steer=%d replay=%d rtimes=%s ending=%d recs=%s" % (course, gt, mt, car, diff, setup_s, mirror, view, 1 if opps is not None else 0, rank, ",".join(str(x) for x in perturb) or "-", opt("--steer", 0), 1 if replay_in is not None else 0, opt("--rtimes", "-"), 1 if "--ending" in args else 0, opt("--recs", "-")), cams)
        if recs:                                                # <name>.initr: the R block (0x060B0500, 0x100 bytes) as the race start finds it (a stored record in it)
            with open(path + ".initr", "wb") as f:
                f.write(r.init_r)
        if huds is not None:                                    # <name>.hud: the initial name tables + scroll (0x3004 words), then per pass: u16 changes, (u16 word index, u16 value) each
            with open(path + ".hud", "wb") as f:
                f.write(struct.pack("<I", len(huds) - 1))
                for h in huds:
                    f.write(h)
        if hude is not None:                                    # <name>.hude: u32 passes, the initial HUDE_WORDS words, then per pass: u16 changes, (u16 word index, u16 value) each
            with open(path + ".hude", "wb") as f:
                f.write(struct.pack("<I", len(hude) - 1))
                for h in hude:
                    f.write(h)
        if skys is not None:                                    # <name>.sky: u32 passes, then 9 longs per pass (sky_pass)
            with open(path + ".sky", "wb") as f:
                f.write(struct.pack("<I", len(skys)))
                for k in skys:
                    f.write(k)
        if opps is not None:                                    # <name>.opp: u32 passes, u32 bytes per pass, the snapshots (opp_snapshot)
            with open(path + ".opp", "wb") as f:
                f.write(struct.pack("<II", len(opps), len(opps[0])))
                for k in opps:
                    f.write(k)
        if snds is not None:                                    # <name>.snd: u32 passes, then per pass: u32 events, the events (fn, r4, r5, r6), the state snapshot (25 bytes)
            with open(path + ".snd", "wb") as f:
                f.write(struct.pack("<I", len(snds)))
                for k in snds:
                    f.write(k)
        if cmp_mode:                                            # <name>.cmp: u32 option, then the record buffers (0x400 bytes at 0x060AA400) as the race start found them (tools/test_drive.c -cmp)
            with open(path + ".cmp", "wb") as f:
                f.write(struct.pack("<I", cmp_mode) + r.init_rec)
        if slots1 is not None:                                  # <name>.slot1: like .slots for the ghost's car slot (0x060B0600, with its R block at +0x500)
            with open(path + ".slot1", "wb") as f:
                f.write(struct.pack("<II", len(slots1), SLOT_BYTES))
                for s1 in slots1:
                    f.write(s1)
        if replay_in is not None:                               # <name>.rplin: the input words the replay played
            with open(path + ".rplin", "wb") as f:
                f.write(replay_in)
        if ghost is not None:                                   # <name>.ghost: the stored run the ghost raced (the test reads it back)
            with open(path + ".ghost", "wb") as f:
                f.write(ghost)
        if "--irec" in args:                                    # <name>.irec: the player's recorded input words (R+0x74 .. the write pointer): 16 bit big endian per pass, axis byte then the button byte
            wp = r.mem.r32(CAR0 + 0x574)
            with open(path + ".irec", "wb") as f:
                f.write(r.mem.read(0x060F0000, wp - 0x060F0000))
        if "--rec" in args:                                     # <name>.rec: the race clock's record buffers (0x060AA400, 0x400 bytes) at the end of the run
            with open(path + ".rec", "wb") as f:
                f.write(r.mem.read(0x060AA400, 0x400))
        last = rows[-1]
        print("%s %s: %d passes, final fn0=%08x lap=%d seg=%d, %.1f km/h -> %s.csv" % (COURSES[course], scenario, len(rows), last[4], r.car16(0xC8), r.car16(0x60),
                                                                                  r.carf(0x94) * 108, path))
    elif cmd == "hudsynth":                                    # extracted/oracle/hudsynth.bin: synthetic end-of-race cases (tools/test_hudend.c)
        hudsynth(os.path.join(OUT, "hudsynth.bin"), opt("--seed", 1))
    elif cmd == "hudinit":                                     # extracted/psp/course/hud_init.dat: the name tables + scroll words after the race screen's display set-up (src/hudgen.c starts from them)
        import hud as hudmod
        out = os.path.join(ROOT, "extracted", "psp", "course", "hud_init.dat")
        oute = os.path.join(ROOT, "extracted", "psp", "course", "hude_init.dat")
        with open(out, "wb") as f, open(oute, "wb") as fe:
            f.write(b"HUD2")                                   # 20 snapshots of 0x600C bytes: game type 0 (arcade) courses 0..3, then 2 (practice), 4 (time attack, three laps), 6 (time attack, free run); index = game type / 2 * 4 + course; 16 + course: a time attack replay
            fe.write(b"HUE1")                                  # the same 16 set-ups: the NBG0 pages (static parts of the goal / game-over screens the display set-up draws), sparse: u32 count, (u32 cell index, u32 value) each
            for g, rp in ((0, 0), (2, 0), (4, 0), (6, 0), (4, 1)):                 # the last four sets: a replay of a time attack (play mode 2), index 16 + course
                for c in range(4):
                    rr = Race(c, game_type=g, render=True, replay=bytes(2) if rp else None)
                    hud_setup(rr, hudmod)
                    f.write(hud_snapshot(rr))
                    cells = [(i, rr.mem.r32(HUDE_NT + 4 * i)) for i in range(HUDE_BYTES // 4) if rr.mem.r32(HUDE_NT + 4 * i)]
                    fe.write(struct.pack("<I", len(cells)) + b"".join(struct.pack("<II", i, v) for i, v in cells))
        print("->", out, oute)
    elif cmd == "replay":
        inp = args[1]
        rows = replay(inp)
        ref = inp[:-4] + ".csv"
        if os.path.exists(ref):
            lines = open(ref).read().splitlines()[1:]
            bad = 0
            for row, line in zip(rows, lines):
                want = line.split(",")
                got = [str(v) if k != 4 else "%08x" % v for k, v in enumerate(row)]
                if want != got:
                    bad += 1
                    if bad < 4:
                        print("MISMATCH at pass %d:\n  want %s\n  got  %s" % (row[0], want, got))
            print("replay %s: %d passes, %d mismatching rows" % (inp, len(rows), bad))
            sys.exit(1 if bad else 0)
        print("replayed %d passes (no reference trace next to the input file)" % len(rows))
    elif cmd == "compare":                                     # compare two trace csv files (oracle vs the C port's output)
        a = open(args[1]).read().splitlines()
        b = open(args[2]).read().splitlines()
        cols = a[0].split(",")
        n = bad = 0
        for la, lb in zip(a[1:], b[1:]):
            if not la or not lb:
                continue
            n += 1
            if la != lb:
                bad += 1
                if bad <= 3:
                    xa, xb = la.split(","), lb.split(",")
                    print("pass %s: " % xa[0] + ", ".join("%s %s != %s" % (c, u, v) for c, u, v in zip(cols, xa, xb) if u != v))
        print("%d rows compared, %d differ (first difference above)" % (n, bad))
        sys.exit(1 if bad else 0)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
