"""M8 scenarios: what follows a race (plctrl::game_exit 0x06006A18, src/menu_race.c): the next screen of the arcade chain / practice, the unlocks, the result history.  The scenario starts
in game_exit with the race's end state patched into the console memory (the race itself is play.c); the C port is compared tick for tick with the original's code."""
CASES = [
    # type, stage, control byte (8 goal, 10 game over), placing, difficulty, mode [0x060AA810], flags [0x060AA82A]
    (0, 0, 8, 1, 1, 0, 0), (0, 1, 8, 3, 0, 0, 0), (0, 2, 8, 1, 0, 0, 0), (0, 2, 8, 2, 0, 0, 0), (0, 2, 8, 1, 0, 2, 1), (0, 2, 8, 1, 0, 2, 0),
    (0, 3, 8, 1, 1, 0, 1), (0, 3, 8, 1, 0, 0, 0), (0, 3, 8, 2, 1, 0, 0), (0, 0, 10, 0, 0, 0, 0), (0, 3, 10, 0, 0, 0, 0), (0, 2, 10, 2, 1, 0, 0),
    (2, 0, 8, 1, 0, 0, 0), (2, 1, 10, 0, 0, 0, 0), (2, 3, 8, 4, 1, 0, 0),
]
UNPORTED = (0x0602FF84, 0x06032378)


def next_vector(t, stage, ctrl, placing, mode, flags):
    if t == 2:
        return 0x06032378 if ctrl == 10 else 0x0602FF84
    if ctrl == 10:
        return 0x0602FF84 if stage == 3 else 0x06032378
    if stage == 3:
        return 0x0602FF84
    if stage == 2 and (placing != 1 or (mode == 2 and flags & 1)):
        return 0x0602FF84
    return 0x06015408


SCENARIOS = {}
START_AT = {}
PATCHES = {}
FINAL = {}


def _run(d):
    d.until(lambda d: d.vec() == 0x060154EC, maxf=3000)                 # the next stage's start screen runs up to the race set-up (like the start_* scenarios)


def _none(d):
    pass                                                                # the next screen is outside the package: the trace ends where the main loop dispatches it


for _i, (_t, _s, _c, _p, _df, _m, _fl) in enumerate(CASES):
    _n = "race_%d" % _i
    SCENARIOS[_n] = _none if next_vector(_t, _s, _c, _p, _m, _fl) in UNPORTED else _run
    START_AT[_n] = 0x06006A18
    _r = bytes((7 * k + _i + 1) & 0xFF for k in range(0x80))             # a recognisable R block (stage times, total, input record pointer ...)
    PATCHES[_n] = [(0x060AA860, bytes([0, _t])), (0x060AA87E, bytes([0, _s])), (0x060AA882, bytes([0, _s * 4])), (0x060B00F5, bytes([_c])), (0x060B00B6, bytes([_p])),
                   (0x060AA808, bytes([_df])), (0x060AA810, bytes([_m])), (0x060AA82A, bytes([_fl])), (0x06040062, bytes([0])), (0x060B0500, _r), (0x060AA825, bytes([0]))]
    _v = next_vector(_t, _s, _c, _p, _m, _fl)
    if _v in UNPORTED:
        FINAL[_n] = _v
