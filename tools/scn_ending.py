"""M8 scenarios: the ceremony (0x0601E4E8, src/menu_ending.c) and the ending (0x0601EF34) after the goal of the last arcade stage.  The scenarios start in the ceremony with the end state of
the race patched into the console memory (like tools/scn_post.py); the C port (src/menu_ending.c) is compared with the original's code tick for tick.

The polygon routines the C port does not run (the 3D parts go to the host) are stubbed in the oracle so that its time per tick stays small: draw_polygon2 0x06018960 (the
3D parts of the models; the other draw routines are stubbed by menu_env.py already)."""
from menu_drive import START, A, B, C, UP, DOWN, LEFT, RIGHT, X, Y, Z, L, R, AXIS, IGNORE, POKE   # noqa: F401

SCENARIOS = {}
START_AT = {}
PATCHES = {}
FINAL = {}

VEC_CER = 0x0601E4E8
VEC_END_INIT = 0x0601EF34
VEC_END = 0x0601F0A0
VEC_CALLBACK = 0x0602FFD0
STUBS = (0x06018960,)                                   # draw_polygon2

# The race engine: the player's car and its physics, the rival cars, the scenery objects, the HUD, the chase camera.  src/menu_ending.c calls these routines through engine(addr) (the PSP host binds
# them to the native engine: src/drive.c, opp.c, event.c, camera.c); the oracle stubs the same routines, so the ending's own code is compared with the engine frozen: the car slots keep what
# the scenario patched (POKE changes them at a given tick, like the engine's passes would).
ENGINE = (0x06014270,       # lap_time_init
          0x06009938,       # carconfig
          0x06005E84,       # car_move_ctrl (the state functions of the car slots)
          0x06006214,       # normal_otinit (the rival cars)
          0x06006668,       # calc_pmap_sub (the player's road blocks)
          0x0600CAD0,       # otherCtrl (the rival cars together)
          0x06006030,       # normal_plinit (slot 0, state function)
          0x060066F8,       # ending_plmt_start (slot 0, presentation)
          0x0601BCB8,       # the scenery objects
          0x0601D410,       # the HUD
          0x0600FC5C,       # replay_ca (the chase camera of the replay)
          0x0600FAF0)       # normal_ca


def b(v, n=1):
    return v.to_bytes(n, "big")


def state(car=0, mirror=0, extra=()):
    """what the race and the screens before it left for the ceremony: the car, the mirror course flag, the callback the ending runs when it is over"""
    p = [(0x060AA80A, b(car)), (0x060AA82E, b(mirror)), (0x060D0024, b(VEC_CALLBACK, 4)), (0x060AA810, b(0))]
    p += list(extra)
    return p


ANIM_LOG = 0x06070000                                    # src/menu_ending.c anim_parts: the matrix and the model of every part of the animated model (52 bytes per part), logged by menu_anim_log


def rec_poly(c):
    """draw_polygon2 (the pipeline is the host's): the animated model's parts (FUN_0601CF44: r4 = the matrix, r5 = the part's model) are logged where the port logs them, the rest is skipped"""
    r5 = c.r[5] & 0xFFFFFFFF
    if not hasattr(c, "anim_parts"):
        c.anim_parts = {}
        for tbl in (0x0601EDE4, 0x0601EE8C):
            for k in range(40):
                c.anim_parts.setdefault(c.m.r32(tbl + 8 + 4 * k), k)
    k = c.anim_parts.get(r5)
    if k is not None:
        r4 = c.r[4] & 0xFFFFFFFF
        for j in range(12):
            c.m.w32(ANIM_LOG + 52 * k + 4 * j, c.m.r32(r4 + 4 * j))
        c.m.w32(ANIM_LOG + 52 * k + 48, r5)
    c.ret()


def stubs(d, engine=False):
    for a in STUBS + (ENGINE if engine else ()):
        d.m.cpu.hooks[a] = rec_poly if a == 0x06018960 else (lambda c: c.ret())
    for a in list(d.m.cpu.hooks):                       # menu_env.py stubs everything from 0x06015F90 as "the sound driver interface", but 0x06015F90 .. 0x06016338 are the scroll routines
        if 0x06015F90 <= a < 0x0601633C:                # (set_grpara_int .. calc_rscroll: the sky's rotation parameters): the ceremony and the ending run them, so the oracle runs the real code (like scn_photo.py)
            del d.m.cpu.hooks[a]


def sc_cer(d):
    stubs(d)
    d.until(lambda d: d.vec() == VEC_END_INIT, maxf=4000)


def add(name, patches, fn, start=VEC_CER, final=None):
    SCENARIOS[name] = fn
    START_AT[name] = start
    PATCHES[name] = patches
    if final:
        FINAL[name] = final


# the ceremony alone: ends where the ending starts (the next screen's dispatch)
add("cer_0", state(car=0), sc_cer, final=VEC_END_INIT)
add("cer_1", state(car=1, mirror=1), sc_cer, final=VEC_END_INIT)
add("cer_2", state(car=2), sc_cer, final=VEC_END_INIT)


# ---- the callback the ending runs when it is over (0x0602FFD0: init_game_over, then the post-goal continuation: name entry, ranking, game over): the post-race scenarios of scn_post.py started
# there with the end state of the last stage ----
import scn_post   # noqa: E402  (tools/ is on the path)

VEC_RANK = 0x06031BC4


def add_cb(name, patches, fn):
    SCENARIOS[name] = fn
    START_AT[name] = VEC_CALLBACK
    PATCHES[name] = patches


# the lake goal (game type 0, stage 3, control byte 8): a time that reaches the table -> name entry, ranking, game over
add_cb("end_cb0", scn_post.state(0, 3, 8, 1, total=0x00033500), scn_post.sc_name((0, 1, 2)))
# a time that does not reach the table: the ranking screen is set up from scratch
add_cb("end_cb1", scn_post.state(0, 3, 8, 2, total=0x00500000), scn_post.sc_none())


# ---- the ending (0x0601EF34): ending_init, then ending_main until it hands over to the callback.  The race engine is frozen (ENGINE above); the car slot is what init_game_work leaves (cleared)
# plus the POKEs ----
def hist(minutes=0, seconds=5, hund=0, rank=1, ptr=0x00300000):
    """the result history the race left: the stage's rank byte [0x060AFE02], the recorded inputs' start [0x060AFE0C], the total time (BCD) of the run [0x060AFE54 + 1 .. 3] (= the length of the staff roll)"""
    return [(0x060AFE02, b(rank)), (0x060AFE0C, b(ptr, 4)), (0x060AFE54, bytes([0, minutes, seconds, hund]))]


def poke(tick, addr, data):
    return (POKE, b(tick, 4) + b(addr, 4) + data)


def sc_end_init(d):
    stubs(d, engine=True)
    d.until(lambda d: d.vec() == VEC_END, maxf=400)


def sc_end_run(d):
    stubs(d, engine=True)
    d.until(lambda d: d.vec() == VEC_CALLBACK, maxf=4000)


add("end_init", state(car=0) + hist(), sc_end_init, start=VEC_END_INIT, final=VEC_END)
add("end_run0", state(car=0) + hist(0, 5, 0), sc_end_run, start=VEC_END_INIT, final=VEC_CALLBACK)


# camera numbers of the lake in the order of the first slice that uses them (CAMERA.DAT: the 256 byte table slice -> camera number at 0x002A26E8, the directory at 0x002A27E8)
def lake_cam_slices():
    import os
    import struct
    from menu_env import ROOT
    d = open(os.path.join(ROOT, "extracted", "disc", "files", "CAMERA.DAT"), "rb").read()
    tbl = struct.unpack(">8I", d[:32])[6] - 0x002A2000
    seen, out = set(), []
    for i, c in enumerate(d[tbl:tbl + 256]):
        if c not in seen:
            seen.add(c)
            out.append(i)
    return out


CAM_SLICES = lake_cam_slices()
CAR_POS = 0x060B0018                                    # the player's block: position x, y, z (16.16); the road block word at +0x48 (0x060B0060)
SEG = 0x060B0060
PAD_REPLAY = 0x060AA00A                                 # the pad word the replay's pad routine leaves for replay_cactl


def cam_pokes(first=20, every=4):
    p = []
    for k, sl in enumerate(CAM_SLICES):
        p.append(poke(first + every * k, SEG, b(sl, 2)))
    return p


# every camera of the lake in turn, the car moved on the way (the cameras look at it)
add("end_cam", state(car=1) + hist(0, 3, 0) + cam_pokes() + [poke(30, CAR_POS, b(0x00F00000, 4) + b(0x00028000, 4) + b(0xFE100000, 4)), poke(70, CAR_POS, b(0xFFE00000, 4) + b(0x00030000, 4) + b(0x01000000, 4))],
    sc_end_run, start=VEC_END_INIT, final=VEC_CALLBACK)

# the pad toggles the cameras (A / B / C: chase camera <-> the cut scenes, X / Y / Z the view) and the scroll control follows
add("end_toggle", state(car=2, mirror=1) + hist(0, 4, 0x50)
    + [poke(40, PAD_REPLAY, b(0x0200, 2)), poke(41, PAD_REPLAY, b(0, 2)), poke(50, PAD_REPLAY, b(0x0040, 2)), poke(51, PAD_REPLAY, b(0, 2)),
       poke(60, PAD_REPLAY, b(0x0010, 2)), poke(61, PAD_REPLAY, b(0, 2)), poke(80, PAD_REPLAY, b(0x0100, 2)), poke(81, PAD_REPLAY, b(0, 2)),
       poke(90, PAD_REPLAY, b(0x0400, 2)), poke(91, PAD_REPLAY, b(0, 2)), poke(100, PAD_REPLAY, b(0x0020, 2)), poke(101, PAD_REPLAY, b(0, 2)),
       poke(110, PAD_REPLAY, b(0x0200, 2)), poke(111, PAD_REPLAY, b(0, 2))],
    sc_end_run, start=VEC_END_INIT, final=VEC_CALLBACK)


# the camera pause word holds the staff roll; the perspective keys (DOWN + Z with R / L)
def sc_end_keys(d):
    stubs(d, engine=True)
    d.run(40)
    d.run(30, 0x2000 | Z | R)                           # perspective +16 per pass
    d.run(10)
    d.run(30, 0x2000 | Z | L)
    d.run(5, 0x2000 | Z | L | R)
    d.run(5, Z | R)
    d.until(lambda d: d.vec() == VEC_CALLBACK, maxf=4000)


add("end_keys", state(car=0) + hist(0, 3, 0) + [poke(30, 0x060AA9EA, b(1, 2)), poke(60, 0x060AA9EA, b(0, 2))], sc_end_keys, start=VEC_END_INIT, final=VEC_CALLBACK)


# the other two kinds of camera record (the lake's CAMERA.DAT has only fixed eyes that look at the car): camera 3 (slice 0) becomes a fixed eye with its own angles (type 2), camera 4 (slice 6)
# the chase camera of the replay (type 0: the engine's replay_ca)
CAM_DIR = 0x002A27E8
add("end_cam2", state(car=0) + hist(0, 3, 0)
    + [(CAM_DIR + 4 * 3, b(2, 2)), (0x002A2870 + 12, b(0x0123, 2) + b(0xFE00, 2) + b(0x0040, 2)), (CAM_DIR + 4 * 4, b(0, 2))]
    + [poke(20, SEG, b(6, 2)), poke(30, SEG, b(0, 2)), poke(40, SEG, b(19, 2)), poke(50, SEG, b(6, 2)), poke(60, SEG, b(34, 2)), poke(70, SEG, b(0, 2))],
    sc_end_run, start=VEC_END_INIT, final=VEC_CALLBACK)

# a longer roll, the run's time over the 90 second limit; stage variants of the set-up bytes
add("end_long", state(car=1, mirror=0, extra=[(0x060AA809, b(1)), (0x060AA811, b(1)), (0x060AA808, b(2))]) + hist(1, 0x31, 0x12), sc_end_run, start=VEC_END_INIT, final=VEC_CALLBACK)


# the whole chain after the goal of the last arcade stage: the post-goal dispatcher 0x0602FF84 -> the ceremony -> the ending -> the callback (init_game_over, the name entry, the ranking, the game over screen)
def sc_chain(d):
    stubs(d, engine=True)
    d.until(lambda d: d.vec() == VEC_CALLBACK, maxf=4000)
    scn_post.sc_name((0, 1, 2))(d)


add("end_chain", scn_post.state(0, 3, 8, 1, total=0x00033500, car=1, trans=1) + hist(0, 2, 0x20), sc_chain, start=0x0602FF84)
