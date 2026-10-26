"""M8 scenarios of the arcade-mode selection chain (SELECT GAME -> car select -> course select), see src/menu_arcade.c.

Most scenarios start at the mode select (START_AT 0x06022894, the state the title leaves) and press their way through the chain; the ones that need a state the earlier
screens would have left (unlock bits, the remembered rows, the analog pad) start at a screen of the chain and PATCH the memory.  The race start (start_init 0x06015408) is not
part of the menus: the scenarios that reach it end there (FINAL: the oracle records the exit stores of the last screen as a trailer, the C test compares them when its main
loop dispatches that vector).

Pad bits: START 0x0800, A 0x0400, C 0x0200, B 0x0100, UP 0x1000, DOWN 0x2000, LEFT 0x4000, RIGHT 0x8000, X 0x40, Y 0x20, Z 0x10, L 0x08, R 0x80 (tools/menu_drive.py).
"""
import os

from menu_drive import START, A, B, C, UP, DOWN, LEFT, RIGHT, X, Y, Z, L, R, AXIS   # noqa: F401
from menu_env import DISC

MODE = 0x06022894
G_MAIN, C_MAIN, CRS_MAIN = 0x0601FAC4, 0x0602015C, 0x06020EA4
G_INIT, C_INIT, CRS_INIT = 0x0601F978, 0x06020010, 0x06020D34
G_IN, C_IN, CRS_IN = 0x0601FB6E, 0x06020336, 0x06020FA8        # the input states
SLIDE = 0x06021570
RACE = 0x06015408
UNLOCK = 0x06040062                                             # bit 1: the lake course, bit 2: the Stratos


def wait_state(d, addr, extra=4):
    """run until the screen's own state handler [0x060A8800] is `addr`"""
    d.until(lambda d: d.mem.r32(0x060A8800) == addr, maxf=4000)
    d.run(extra)


def to_gsel(d):
    """mode select -> arcade -> SELECT GAME, fade-in done"""
    d.run(200)
    d.tap(C)
    d.until(lambda d: d.vec() == G_MAIN)
    wait_state(d, G_IN)


def to_csel(d):
    """-> car select, fade-in done"""
    d.until(lambda d: d.vec() == C_MAIN)
    wait_state(d, C_IN)


def to_crs(d):
    d.until(lambda d: d.vec() == CRS_MAIN)
    wait_state(d, CRS_IN)


def at_exit(d):
    """run until the screen's loop is about to end (the exit flag is set: [0x060AA803] bit 1), the next pass leaves the screen"""
    d.until(lambda d: d.mem.r8(0x060AA803) & 2, maxf=4000)


def taps(d, seq, gap=4):
    for b in seq:
        d.tap(b)
        d.run(gap)


# ---- SELECT GAME ----------------------------------------------------------------------------------------------------
def sc_arc_g1(d):
    """mode select -> arcade -> SELECT GAME, wait for the fade-in and a few passes"""
    to_gsel(d)
    d.run(40)


def sc_arc_flow(d):
    """arcade, arcade style, the first car -> the race"""
    to_gsel(d)
    d.tap(C)
    to_csel(d)
    d.tap(C)
    at_exit(d)


def sc_arc_gkeys(d):
    """SELECT GAME: every button; practice / arcade; confirm with START; the mirror cheat (C + Y held)"""
    to_gsel(d)
    taps(d, (UP, DOWN, L, R, X, Y, Z, RIGHT, LEFT, RIGHT, RIGHT, LEFT, UP), 6)
    taps(d, (RIGHT,), 6)
    d.run(5, C | Y)                      # C + Y held: the mirror bit, and C confirms (practice)
    at_exit(d)
    d.until(lambda d: d.vec() == C_MAIN)
    d.run(10)


def sc_arc_gback(d):
    """SELECT GAME -> B -> the mode select again (cursor entry kept), arcade once more"""
    to_gsel(d)
    taps(d, (RIGHT,), 8)
    d.tap(B)
    d.until(lambda d: d.vec() == MODE)
    d.until(lambda d: d.vec() == 0x06022B5C)
    d.run(60)


def sc_arc_gstart(d):
    """SELECT GAME: START confirms ARCADE, then B in the car select returns to SELECT GAME (arcade entry), A confirms"""
    to_gsel(d)
    d.tap(START)
    to_csel(d)
    d.tap(B)
    d.until(lambda d: d.vec() == G_MAIN)
    wait_state(d, G_IN)
    d.tap(A)
    to_csel(d)
    d.run(20)


# ---- car select -------------------------------------------------------------------------------------------------------
def sc_arc_cnav(d):
    """practice style, car select: walk over the four rows with every direction, B back to SELECT GAME, arcade again, a different remembered row, confirm"""
    to_gsel(d)
    taps(d, (RIGHT,), 6)
    d.tap(START)
    to_csel(d)
    taps(d, (UP, DOWN, DOWN, UP, UP, RIGHT, UP, DOWN, DOWN, UP, RIGHT, RIGHT, LEFT, LEFT, LEFT, LEFT, RIGHT, L, R, X, Y, Z), 5)
    d.tap(B)
    d.until(lambda d: d.vec() == G_MAIN)
    wait_state(d, G_IN)
    taps(d, (LEFT,), 6)
    d.tap(A)
    to_csel(d)
    taps(d, (RIGHT, RIGHT, DOWN, UP), 5)
    d.tap(C)
    at_exit(d)


# ---- course select ------------------------------------------------------------------------------------------------------
def sc_arc_crs(d):
    """practice: car select -> course select (lake locked): LEFT / RIGHT with the clamp at both ends, B back to the car select, confirm again, take the second course"""
    to_gsel(d)
    taps(d, (RIGHT,), 6)
    d.tap(C)
    to_csel(d)
    d.tap(C)
    to_crs(d)
    taps(d, (RIGHT, RIGHT, RIGHT, RIGHT, LEFT, LEFT, LEFT, LEFT, UP, DOWN, L, R, X, Y, Z, RIGHT), 8)
    d.tap(B)
    d.until(lambda d: d.vec() == C_MAIN)
    wait_state(d, C_IN)
    d.tap(START)
    to_crs(d)
    taps(d, (RIGHT,), 8)
    d.tap(A)
    at_exit(d)


def row_of(d):
    return d.mem.r16(0x06040012) - (0x10000 if d.mem.r16(0x06040012) & 0x8000 else 0)


def c_goto(d, target):
    """car select: LEFT / RIGHT (one row each) until the cursor row is `target`"""
    for _ in range(12):
        r = row_of(d)
        if r == target:
            return
        d.tap(RIGHT if target > r else LEFT)
        d.run(6)


def sc_arc_rows(d):
    """everything unlocked, practice, the picture variants of option byte 0 (0x0604000D): the car select, every row confirmed once (each time back from the course select with B)"""
    wait_state_g(d)
    taps(d, (RIGHT,), 6)
    d.tap(C)
    for row in (-1, 0, 1, 2, 3, 4):
        to_csel(d)
        c_goto(d, row)
        d.run(4)
        d.tap(C)
        to_crs(d)
        d.tap(B)
    to_csel(d)
    d.tap(START)
    to_crs(d)
    d.tap(A)
    at_exit(d)


def wait_state_g(d):
    d.until(lambda d: d.vec() == G_MAIN)
    wait_state(d, G_IN)


def sc_arc_stratos_nav(d):
    """arcade, Stratos unlocked: the end rows -1 / 4 with their wrap and clamp, C + X held on the Stratos (no cheat), confirm the Stratos"""
    wait_state_g(d)
    d.tap(C)
    to_csel(d)
    taps(d, (LEFT, LEFT, LEFT, LEFT, UP, DOWN, UP, UP, DOWN, DOWN, LEFT), 5)       # 1, 0, -1, clamp, clamp, UP -> 0, ...
    taps(d, (RIGHT, RIGHT, RIGHT, RIGHT, RIGHT, RIGHT, RIGHT, UP, DOWN, DOWN, UP, UP), 5)
    c_goto(d, 4)
    d.run(8, C | X)
    at_exit(d)


def sc_arc_cheat(d):
    """arcade: C + X held on a normal car sets unlock bit 4 and the set-up byte; then the car select again (B) now shows the Stratos rows"""
    wait_state_g(d)
    d.tap(C)
    to_csel(d)
    d.run(8, C | X)
    d.until(lambda d: d.vec() == RACE or d.mem.r8(0x060AA803) & 2, maxf=3000)


def sc_arc_time_a(d):
    """arcade style: let the count-downs of SELECT GAME and the car select run out"""
    wait_state_g(d)
    d.until(lambda d: d.vec() == C_MAIN, maxf=3000)
    wait_state(d, C_IN)
    at_exit(d)
    d.until(lambda d: d.mem.r8(0x060AA803) & 2, maxf=3000)


def sc_arc_time_p(d):
    """practice style: the count-downs of SELECT GAME, the car select and the course select all run out"""
    wait_state_g(d)
    d.until(lambda d: d.vec() == C_MAIN, maxf=3000)
    wait_state(d, C_IN)
    d.until(lambda d: d.vec() == CRS_MAIN, maxf=3000)
    wait_state(d, CRS_IN)
    at_exit(d)


def run_down(d, extra=0):
    """run until the count-down shows 0 seconds and `extra` frames are left"""
    d.until(lambda d: d.mem.r16(0x060A88B6) == 0 and d.mem.r16(0x060A88B8) <= extra, maxf=3000)


def sc_arc_lake(d):
    """lake unlocked, practice: the strip slides to the lake and back (keys during the slide are ignored), the time runs out during a slide, then confirm"""
    wait_state_g(d)
    taps(d, (RIGHT,), 6)
    d.tap(C)
    to_csel(d)
    d.tap(C)
    to_crs(d)
    taps(d, (RIGHT, RIGHT), 6)
    d.tap(RIGHT)                         # 2 -> 3: slide right
    d.run(1, LEFT)                       # a key during the slide is ignored
    d.until(lambda d: d.mem.r32(0x060A8800) == CRS_IN)
    d.run(6)
    d.tap(LEFT)                          # 3 -> 2: slide left
    d.run(1, RIGHT)
    d.until(lambda d: d.mem.r32(0x060A8800) == CRS_IN)
    taps(d, (LEFT, LEFT, LEFT, RIGHT, RIGHT, RIGHT), 10)   # down to the first course, up to the lake (slide), clamp at the lake
    d.until(lambda d: d.mem.r32(0x060A8800) == CRS_IN)
    run_down(d, 6)
    d.tap(LEFT)                          # a slide that the time-out interrupts
    at_exit(d)


def sc_arc_lake_start(d):
    """practice with the lake as the saved course: the course select starts on the lake (scroll position 0x260), slide away and back, confirm the lake"""
    wait_state_g(d)
    taps(d, (RIGHT,), 6)
    d.tap(C)
    to_csel(d)
    d.tap(C)
    to_crs(d)
    taps(d, (UP, DOWN, RIGHT), 6)        # RIGHT on the lake: nothing left to slide
    d.tap(LEFT)                          # 3 -> 2: slide
    d.until(lambda d: d.mem.r32(0x060A8800) == CRS_IN)
    d.tap(RIGHT)
    d.until(lambda d: d.mem.r32(0x060A8800) == CRS_IN)
    d.tap(C)
    at_exit(d)


def sc_arc_ana_hi(d):
    """analog pad (the axis byte held at 255), everything unlocked, practice: SELECT GAME -> PRACTICE, the car select -> the last row, the course select -> the lake"""
    wait_state_g(d)
    d.run(6)
    d.tap(C)
    to_csel(d)
    d.run(10)
    d.tap(C)
    to_crs(d)
    d.run(40)
    d.tap(C)
    at_exit(d)


def sc_arc_ana_lo(d):
    """analog pad (axis 0), nothing unlocked: SELECT GAME -> ARCADE, the car select -> the first row, confirm -> race"""
    wait_state_g(d)
    d.run(6)
    d.tap(START)
    to_csel(d)
    d.run(10)
    d.tap(A)
    at_exit(d)


def sc_arc_ana_mid(d):
    """analog pad (axis 127 = centre) in practice style with row 3 / course 2 remembered, then a pad with a small deflection"""
    wait_state_g(d)
    d.run(10)
    d.tap(C)
    to_csel(d)
    d.run(10)
    d.tap(C)
    to_crs(d)
    d.run(10)
    d.tap(B)
    to_csel(d)
    d.tap(B)
    wait_state_g(d)
    d.tap(B)
    d.until(lambda d: d.vec() == 0x06022B5C)


def sc_arc_ana_arc(d):
    """analog pad at the centre in arcade style: SELECT GAME keeps its entry, time out of SELECT GAME is not waited for, A confirms"""
    wait_state_g(d)
    d.run(10)
    d.tap(A)
    to_csel(d)
    d.run(10)
    d.tap(C)
    at_exit(d)


SCENARIOS = {"arc_g1": sc_arc_g1, "arc_flow": sc_arc_flow, "arc_gkeys": sc_arc_gkeys, "arc_gback": sc_arc_gback, "arc_gstart": sc_arc_gstart,
             "arc_cnav": sc_arc_cnav, "arc_crs": sc_arc_crs}
START_AT = {n: MODE for n in SCENARIOS}
FINAL = {"arc_flow": RACE, "arc_cnav": RACE, "arc_crs": RACE}
PATCHES = {}
ALL = bytes([0x06])
ANALOG = bytes([0x10])
SCENARIOS.update({
    "arc_rows": sc_arc_rows, "arc_stratos": sc_arc_stratos_nav, "arc_cheat": sc_arc_cheat, "arc_time_a": sc_arc_time_a, "arc_time_p": sc_arc_time_p,
    "arc_lake": sc_arc_lake, "arc_lake_start": sc_arc_lake_start,
    "arc_ana_hi": sc_arc_ana_hi, "arc_ana_lo": sc_arc_ana_lo, "arc_ana_mid": sc_arc_ana_mid, "arc_ana_arc": sc_arc_ana_arc,
})
for _n in ("arc_rows", "arc_stratos", "arc_cheat", "arc_time_a", "arc_time_p", "arc_lake", "arc_lake_start", "arc_ana_hi", "arc_ana_lo", "arc_ana_mid", "arc_ana_arc"):
    START_AT[_n] = G_INIT
FINAL.update({"arc_rows": RACE, "arc_stratos": RACE, "arc_cheat": RACE, "arc_time_a": RACE, "arc_time_p": RACE, "arc_lake": RACE, "arc_lake_start": RACE,
              "arc_ana_hi": RACE, "arc_ana_lo": RACE, "arc_ana_arc": RACE})
PATCHES.update({
    "arc_rows": [(UNLOCK, ALL), (0x0604000D, bytes([0]))],
    "arc_stratos": [(UNLOCK, ALL)],
    "arc_time_p": [(0x06040010, bytes([0, 1]))],
    "arc_lake": [(UNLOCK, ALL)],
    "arc_lake_start": [(UNLOCK, ALL), (0x06040010, bytes([0, 1])), (0x06040018, bytes([0, 3]))],
    "arc_ana_hi": [(UNLOCK, ALL), (0x060AA9A4, ANALOG), (AXIS, bytes([255]))],
    "arc_ana_lo": [(0x060AA9A4, ANALOG), (AXIS, bytes([0]))],
    "arc_ana_mid": [(UNLOCK, ALL), (0x06040010, bytes([0, 1])), (0x06040012, bytes([0, 3])), (0x06040014, bytes([0, 3])), (0x06040016, bytes([0, 3])),
                    (0x06040018, bytes([0, 2])), (0x060AA9A4, ANALOG), (AXIS, bytes([127]))],
    "arc_ana_arc": [(0x06040012, bytes([0, 2])), (0x06040014, bytes([0, 2])), (0x060AA9A4, ANALOG), (AXIS, bytes([130]))],
})


def sc_arc_ana_hi_ns(d):
    """analog pad (axis 255), nothing unlocked: SELECT GAME -> PRACTICE, the car select -> clamped to the last row, the course select -> the last open course (the lake is locked)"""
    wait_state_g(d)
    d.run(6)
    d.tap(C)
    to_csel(d)
    d.run(10)
    d.tap(C)
    to_crs(d)
    d.run(10)
    d.tap(C)
    at_exit(d)


def sc_arc_ana_lo_st(d):
    """analog pad (axis 0), only the Stratos unlocked: SELECT GAME -> ARCADE, the car select -> clamped to the Stratos row -1, confirm"""
    wait_state_g(d)
    d.run(6)
    d.tap(C)
    to_csel(d)
    d.run(10)
    d.tap(C)
    at_exit(d)


def sc_arc_ana_crs(d):
    """the course select alone (the picture file is preloaded by the patch): the analog axis decides the course; a few passes, then confirm"""
    d.until(lambda d: d.vec() == CRS_MAIN)
    wait_state(d, CRS_IN)
    d.run(30)
    d.tap(C)
    at_exit(d)


SCRL_SLT = open(os.path.join(DISC, "SCRL_SLT.DAT"), "rb").read()      # what SELECT GAME loads (raw) to 0x00244000: the later screens of a scenario that starts after it need it
SCENARIOS.update({"arc_ana_hi_ns": sc_arc_ana_hi_ns, "arc_ana_lo_st": sc_arc_ana_lo_st,
                  "arc_ana_crs_a": sc_arc_ana_crs, "arc_ana_crs_b": sc_arc_ana_crs, "arc_ana_crs_c": sc_arc_ana_crs, "arc_ana_crs_d": sc_arc_ana_crs})
for _n in ("arc_ana_hi_ns", "arc_ana_lo_st"):
    START_AT[_n] = G_INIT
for _n in ("arc_ana_crs_a", "arc_ana_crs_b", "arc_ana_crs_c", "arc_ana_crs_d"):
    START_AT[_n] = CRS_INIT
FINAL.update({n: RACE for n in ("arc_ana_hi_ns", "arc_ana_lo_st", "arc_ana_crs_a", "arc_ana_crs_b", "arc_ana_crs_c", "arc_ana_crs_d")})
PATCHES.update({
    "arc_ana_hi_ns": [(0x060AA9A4, ANALOG), (AXIS, bytes([255]))],
    "arc_ana_lo_st": [(UNLOCK, bytes([0x04])), (0x060AA9A4, ANALOG), (AXIS, bytes([0]))],
    "arc_ana_crs_a": [(0x00244000, SCRL_SLT), (UNLOCK, ALL), (0x060AA9A4, ANALOG), (AXIS, bytes([0]))],                                  # low axis: the first course
    "arc_ana_crs_b": [(0x00244000, SCRL_SLT), (UNLOCK, ALL), (0x060AA9A4, ANALOG), (AXIS, bytes([255]))],                                # high axis: the lake (slide)
    "arc_ana_crs_c": [(0x00244000, SCRL_SLT), (UNLOCK, ALL), (0x06040018, bytes([0, 3])), (0x060AA9A4, ANALOG), (AXIS, bytes([60]))],    # started on the lake, axis to the left: slide back
    "arc_ana_crs_d": [(0x00244000, SCRL_SLT), (UNLOCK, ALL), (0x06040018, bytes([0, 3])), (0x060AA9A4, ANALOG), (AXIS, bytes([200]))],  # started on the lake, axis to the right
})
