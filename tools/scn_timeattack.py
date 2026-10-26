"""M8 scenarios: the TIME ATTACK screens (cs_tm_attck / cs_tm_att_pset, entered from the mode select with cursor 1).

All start at the mode select (START_AT 0x06022894), press DOWN once and the confirm button C, then drive the time attack pages:
car (page 0) -> [tuned car (page 1)] -> transmission (page 2) -> course (page 3) -> lap mode (page 4) -> race start (leaves the package: the scenario ends there).
The save data bytes of the unlock state (0x06040062 unlock bits: 2 lake, 4 stratos; 0x06040063 tune flags + the tuned car records at 0x06040064 + 8 * n) are patched per scenario.
"""
from menu_drive import UP, DOWN, LEFT, RIGHT, B, C, A, START, X, Y, Z, L, R  # noqa: F401

START_AT = {}
PATCHES = {}
SCENARIOS = {}
FINAL = {}                                      # scenarios that end in the race start (start_init 0x06015408, not part of the menus)
RACE = 0x06015408

# the page state handlers ([0x060A8800])
S_CAR, S_TUNED, S_TRANS, S_COURSE, S_LAP = 0x060243D6, 0x06024574, 0x060246DE, 0x060247CE, 0x060248C6
S_FADE_BACK, S_FADE_RACE = 0x0602767E, 0x0602769C

# unlock states: (0x06040062 bits, 0x06040063 tune byte)
ALL = [(0x06040062, bytes([6, 3]))]                                     # lake + stratos, two tuned cars
# the tuned cars: record n at 0x06040064 + 8n: +1..4 set-up, +5 transmission, +6 car, +7 set-up; two records
TUNED = [(0x06040064, bytes([0, 1, 2, 1, 2, 1, 2, 3, 0, 2, 1, 2, 1, 0, 3, 1]))]


def state(d):
    return d.mem.r32(0x060A8800)


def wait_state(d, s, maxf=2000):
    """run until the screen's state handler is s and let it settle for a few passes"""
    ok = d.until(lambda d: state(d) == s, maxf)
    d.run(6)
    return ok


def finish(d):
    """run until the screen has decided to leave (the done flag makes the main loop set [0x060AA803] bit 1): the scenario ends on that tick, before the next
    screen (the race start, not part of this package) would run"""
    d.until(lambda d: d.mem.r8(0x060AA803) & 2, 3000)


def enter(d):
    """mode select -> time attack car page"""
    d.run(200)
    d.tap(DOWN)
    d.run(20)
    d.tap(C)
    wait_state(d, S_CAR)


def reg(name, fn, patches=None):
    SCENARIOS[name] = fn
    START_AT[name] = 0x06022894
    if patches:
        PATCHES[name] = patches


def sc_ta_basic(d):
    enter(d)
    d.tap(DOWN); d.run(40)
    d.tap(UP); d.run(40)
    d.tap(B)
    d.run(150)


reg("ta_basic", sc_ta_basic)


def sc_ta_full(d):
    """the whole way through with the default save (nothing unlocked): car, transmission, course (wrap), mirror, lap mode, go"""
    enter(d)
    d.tap(DOWN); d.run(30)                      # car 1
    d.tap(DOWN); d.run(30)                      # wraps to 0
    d.tap(UP); d.run(30)                        # wraps to 1
    d.tap(C)                                    # -> transmission page (no slide)
    wait_state(d, S_TRANS)
    d.tap(DOWN); d.run(20)
    d.tap(UP); d.run(20)
    d.tap(DOWN); d.run(20)
    d.tap(C)                                    # -> slide -> course page
    wait_state(d, S_COURSE)
    d.tap(DOWN); d.run(30)
    d.tap(DOWN); d.run(30)
    d.tap(DOWN); d.run(30)                      # wraps (lake locked: 3 courses)
    d.tap(UP); d.run(30)
    d.run(10, Y); d.run(3, Y | C); d.run(3)     # mirror + confirm
    wait_state(d, S_LAP)
    d.tap(DOWN); d.run(30)
    d.tap(UP); d.run(30)
    d.tap(DOWN); d.run(30)
    d.tap(A)                                    # go
    finish(d)


reg("ta_full", sc_ta_full)
FINAL["ta_full"] = RACE


def cancel_chain(d):
    """lap page -> cancel (slide back to the course page) -> cancel (car page) -> cancel (fade out to the mode select, and into the mode select again)"""
    d.tap(DOWN); d.run(30)
    d.tap(C)                                    # -> transmission
    wait_state(d, S_TRANS)
    d.tap(C)                                    # -> course
    wait_state(d, S_COURSE)
    d.tap(DOWN); d.run(20)
    d.tap(START)                                # -> lap
    wait_state(d, S_LAP)
    d.tap(DOWN); d.run(20)
    d.tap(B)                                    # lap -> course
    wait_state(d, S_COURSE)
    d.tap(B)                                    # course -> car
    wait_state(d, S_CAR)
    d.tap(C)
    wait_state(d, S_TRANS)
    d.tap(B)                                    # transmission -> car
    wait_state(d, S_CAR)
    d.tap(B)                                    # car -> mode select
    d.run(200)


def sc_ta_cancel(d):
    enter(d)
    cancel_chain(d)


reg("ta_cancel", sc_ta_cancel)


def sc_ta_keys(d):
    """key repeat on the pages and every button on every page"""
    enter(d)
    d.run(120, DOWN)                            # held: the repeat runs through the car cursor
    d.run(5)
    d.run(60, UP)
    d.run(5)
    for btn in (X, Y, Z, L, R, LEFT, RIGHT):
        d.run(3, btn); d.run(8)
    d.tap(A)                                    # confirm with A -> transmission
    wait_state(d, S_TRANS)
    d.run(100, DOWN)
    d.run(5)
    for btn in (X, Z, L, R, LEFT, RIGHT, UP | DOWN):
        d.run(3, btn); d.run(8)
    d.tap(START)                                # -> course
    wait_state(d, S_COURSE)
    d.run(150, DOWN)                            # held on the course page
    d.run(5)
    for btn in (X, Z, L, R, LEFT, RIGHT, Y):
        d.run(3, btn); d.run(8)
    d.run(4, Y | C)                             # mirror
    wait_state(d, S_LAP)
    d.run(80, DOWN)
    d.run(5)
    for btn in (X, Z, L, R, LEFT, RIGHT, UP | DOWN):
        d.run(3, btn); d.run(8)
    d.tap(C)                                    # go
    finish(d)


reg("ta_keys", sc_ta_keys)
FINAL["ta_keys"] = RACE

# the unlock combinations of the save data: (stratos, lake, tune byte)
UNLOCKS = {
    "ta_u_s": (1, 0, 0), "ta_u_l": (0, 1, 0), "ta_u_t1": (0, 0, 1), "ta_u_sl": (1, 1, 0), "ta_u_st3": (1, 0, 3), "ta_u_slt": (1, 1, 7), "ta_u_t15": (0, 1, 15),
}


def unlock_patch(stratos, lake, tune):
    return [(0x06040062, bytes([(4 if stratos else 0) | (2 if lake else 0), tune])), TUNED[0]]


def sc_ta_unlock(d):
    """every car row, the tuned car page, every course (the lake course when unlocked), both lap modes, mirror, go"""
    enter(d)
    hi = d.mem.r8(0x060A88FE)
    for _ in range(hi + 2):                     # every car row and a wrap
        d.tap(DOWN); d.run(25)
    for _ in range(2):
        d.tap(UP); d.run(25)
    if d.mem.r8(0x060A88FB):                    # the tune row: the tuned car list
        d.until(lambda d: d.mem.r16(0x060A88A4) == hi)
        d.tap(C)
        wait_state(d, S_TUNED)
        for _ in range(d.mem.r8(0x0604002A) + 1):
            d.tap(DOWN); d.run(30)
        d.tap(UP); d.run(30)
        d.tap(B)                                # back to the car page
        wait_state(d, S_CAR)
        d.until(lambda d: d.mem.r16(0x060A88A4) == hi)
        d.tap(C)
        wait_state(d, S_TUNED)
        d.tap(DOWN); d.run(30)
        d.tap(C)                                # tuned car -> course page (no transmission page)
    else:
        d.tap(C)                                # a car -> transmission
        wait_state(d, S_TRANS)
        d.tap(DOWN); d.run(20)
        d.tap(C)
    wait_state(d, S_COURSE)
    for _ in range(d.mem.r8(0x060A8901) + 2):
        d.tap(DOWN); d.run(25)
    d.tap(UP); d.run(25)
    d.tap(C)
    wait_state(d, S_LAP)
    d.tap(DOWN); d.run(30)
    d.tap(C)
    finish(d)


for _n, _u in UNLOCKS.items():
    reg(_n, sc_ta_unlock, unlock_patch(*_u))
    FINAL[_n] = RACE


def sc_ta_resume(d):
    """the tuned car row, back out to the car page, out to the mode select and into the time attack again (the saved car mode comes back at the start)"""
    enter(d)
    hi = d.mem.r8(0x060A88FE)
    for _ in range(hi):
        d.tap(DOWN); d.run(25)
    d.tap(C)
    wait_state(d, S_TUNED)
    d.tap(DOWN); d.run(30)
    d.tap(B)                                    # tuned list -> car page
    wait_state(d, S_CAR)
    d.tap(B)                                    # car page -> mode select
    d.until(lambda d: d.vec() == 0x06022B5C, 600)
    d.run(150)
    d.tap(C)                                    # again (the mode select cursor is still on the time attack)
    wait_state(d, S_CAR)
    d.tap(UP); d.run(30)
    d.tap(C)                                    # a normal car, the transmission page
    wait_state(d, S_TRANS)
    d.tap(B)
    wait_state(d, S_CAR)
    d.run(40)


reg("ta_resume", sc_ta_resume, unlock_patch(1, 1, 3))


def sc_mash(seed):
    """a pseudo random pad (not the confirm keys on the lap page: the race start is not part of this scenario) while the screen is the time attack; ends when the screen leaves"""
    import random

    def fn(d):
        rnd = random.Random(seed)
        enter(d)
        pads = [UP, DOWN, B, C, START, A, X, Y, Z, L, R, LEFT, RIGHT, 0, 0, 0, C | Y, UP | LEFT, DOWN | RIGHT]
        for _ in range(900):
            btn = rnd.choice(pads)
            if state(d) == S_LAP:
                btn &= ~(C | START | A)
            d.run(rnd.randint(1, 6), btn)
            d.run(rnd.randint(0, 9))
            if d.vec() != 0x0602425C:
                break
        d.run(100)
    return fn


reg("ta_mash1", sc_mash(1))
reg("ta_mash2", sc_mash(2), unlock_patch(1, 1, 7))
reg("ta_mash3", sc_mash(3), unlock_patch(0, 1, 15))


def sc_ta_entry(d):
    """the entry the result / name entry screens use (tm_set_tm_att 0x060262A4: set up from scratch, fade in, then tm_attck_main): the car page for a while, a car change, back out"""
    wait_state(d, S_CAR)
    d.tap(DOWN); d.run(40)
    d.tap(UP); d.run(40)
    d.tap(B)
    d.run(150)


SCENARIOS["ta_entry"] = sc_ta_entry
START_AT["ta_entry"] = 0x060262A4
SCENARIOS["ta_entry2"] = sc_ta_entry                    # [0x060A88EA] != 0: the screen was already set up by the caller
START_AT["ta_entry2"] = 0x060262A4
PATCHES["ta_entry2"] = [(0x060A88EA, bytes([0, 1]))]


def sc_ta_cancel_s(d):
    """cancel chain with the stratos unlocked (the car name boxes are cleared differently)"""
    enter(d)
    cancel_chain(d)


reg("ta_cancel_s", sc_ta_cancel_s, unlock_patch(1, 0, 0))


def sc_ta_go3(d):
    """the shortest way: first car, auto, first course, 3 laps -> go"""
    enter(d)
    d.tap(C)
    wait_state(d, S_TRANS)
    d.tap(START)
    wait_state(d, S_COURSE)
    d.tap(A)
    wait_state(d, S_LAP)
    d.tap(C)
    finish(d)


reg("ta_go3", sc_ta_go3)
FINAL["ta_go3"] = RACE


def sc_ta_return(d):
    """return_tm_att 0x06026284 (the result screens' way back): the time attack main vector with the answer 1, then a short visit"""
    d.until(lambda d: d.vec() == 0x0602425C, 50)
    wait_state(d, S_CAR)
    d.tap(DOWN); d.run(40)
    d.tap(B)
    d.run(120)


def tm_att0_map_block():
    """the map tables block of TM_ATT0.DAT (LWRAM 0x00244000): the data the mode select's slide has loaded before the time attack starts, patched in because this scenario enters the
    time attack directly (the pictures copy from it: an empty block would make the original's box copy loop run 2^32 times)"""
    import os
    from menu_env import ROOT
    from datfile import Scatter
    s = Scatter(os.path.join(ROOT, "extracted", "disc", "files", "TM_ATT0.DAT"))
    for blk in s.blocks:
        if blk.dest == 0x00244000:
            return bytes(s.expanded(blk))
    raise RuntimeError("TM_ATT0.DAT: no block at 0x00244000")


def mkpoly_state():
    """what mkpoly_init (0x060183D4, run by the mode select / tm_set_tm_att before the sprites are drawn) leaves: the pipeline's work area tables and the VDP1 shade table"""
    import struct
    import sh2dis
    mk = lambda a: b"".join(struct.pack(">I", sh2dis.l(a + 4 * i)) for i in range(48))
    shade = b"".join(struct.pack(">II", (0x04210421 * i) & 0xFFFFFFFF, (0x04210421 * i) & 0xFFFFFFFF) for i in range(32))
    return [(0x060D1C00, mk(0x06018500)), (0x060D1400, mk(0x06018440)), (0x25C10000, shade)]


SCENARIOS["ta_return"] = sc_ta_return
START_AT["ta_return"] = 0x06026284
PATCHES["ta_return"] = [(0x00244000, tm_att0_map_block())] + mkpoly_state()

SCENARIOS["ta_entry3"] = sc_ta_entry                    # the model packs are already loaded and the car picture offset is mid-slide
START_AT["ta_entry3"] = 0x060262A4
PATCHES["ta_entry3"] = [(0x060A88EA, bytes([0, 1])), (0x060A88E8, bytes([0, 1])), (0x060D008C, b"OBJ "), (0x060D0090, b"SELE")]
