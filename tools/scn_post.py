"""M8 scenarios: what follows a race (src/menu_post.c, menu_name.c, menu_result.c): the post-goal dispatcher 0x0602FF84, the name entry, the ranking screen 0x06031BC4, the game over screen
0x06032378 and the result screen 0x06032514.  The scenario starts in the dispatcher with the race's end state patched into the console memory (the race itself is play.c); the C port is
compared tick for tick with the original's code."""
from menu_drive import START, A, B, C, UP, DOWN, LEFT, RIGHT, X, Y, Z, L, R, AXIS   # noqa: F401

SCENARIOS = {}
START_AT = {}
PATCHES = {}
FINAL = {}

VEC_RANK = 0x06031BC4
VEC_OVER = 0x06032378
VEC_SELECT = 0x0601F978
VEC_START = 0x06015408
END = 0x060E5200                                        # the "END" letter of the wheel (letter 28), 0x060E5180 = "DEL" (27)
DEL = 0x060E5180


def b(v, n=1):
    return v.to_bytes(n, "big")


def state(typ, stage, ctrl, placing, mode=0, car=0, trans=0, setup=0, laps=1, total=0x00024700, best_lap=0x00004000, flags=0, mirror=0, replay_ret=0, unlock=0, extra=()):
    """the console memory a finished race leaves: game type, stage, control byte (8 goal, 10 game over, 12 ..), placing, ..., total time (BCD), best lap"""
    p = [(0x060AA860, b(typ, 2)), (0x060AA87E, b(stage, 2)), (0x060B00F5, b(ctrl)), (0x060B00B6, b(placing)), (0x060AA810, b(mode)),
         (0x060AA80A, b(car)), (0x060AA809, b(trans)), (0x060AA811, b(setup)), (0x060AA80C, b(laps)), (0x060B058C, b(total, 4)), (0x060B050C, b(best_lap, 4)),
         (0x060AA82A, b(flags)), (0x060AA82E, b(mirror)), (0x060A8C17, b(replay_ret)), (0x06040062, b(unlock)), (0x060AA825, b(0))]
    # the lap times of the run (cumulative BCD) for the stored replay
    p.append((0x060AA400, b(0x00004000, 4) + b(0x00008000, 4) + b(0x00011000, 4)))
    p += list(extra)
    return p


# ---- helpers that drive the original's screens ----
def wheel_to(d, target, maxf=1200):
    """turn the letter wheel (one tap = one letter: the wheel runs on to the next detent) until the letter actor `target` is in front and the wheel rests"""
    for _ in range(maxf):
        cur = d.mem.r32(0x060A8C68)
        rest = d.mem.r32(0x060A8C30) == 0 and d.mem.r8(0x060A8C6D) == 0
        if not rest:
            d.run(1, 0)
            continue
        if cur == target:
            return True
        n = 0                                           # hops forward (next = +72)
        a = cur
        while a != target and n < 29:
            a = d.mem.r32(a + 72)
            n += 1
        d.run(1, RIGHT if n <= 14 else LEFT)
    return False


def slot(k):
    return 0x060E4400 + 0x80 * k                        # letter k (0 .. 28; 27 = DEL, 28 = END)


def pick(d, letters):
    for k in letters:
        wheel_to(d, slot(k))
        d.run(2, C)
        d.run(1, 0)
        d.until(lambda d: d.mem.r8(0x060A8C6D) == 0 or d.mem.r8(0x060A8C60) != 1, maxf=300)


def wait_wheel(d):
    d.until(lambda d: d.mem.r8(0x060A8C60) == 1, maxf=400)
    d.run(10)


def name_entry_script(d, letters=(0, 1, 2), end=True):
    wait_wheel(d)
    pick(d, letters)
    if end:
        wheel_to(d, END)
        d.run(2, C)
        d.run(1, 0)
    d.until(lambda d: d.mem.r8(0x060A8C60) >= 4 or d.vec() != VEC_RANK, maxf=600)


def rank_and_over(d, keys=(), wait_over=0):
    """the ranking screen: wait for the input state (3), press the keys [(buttons, ticks)], confirm; then the game over screen if the ranking screen leaves to it: confirm after
    `wait_over` ticks (default: at once), or let its time-out run"""
    d.until(lambda d: d.mem.r8(0x060A8C61) == 3 or d.mem.r8(0x060AA803) & 2, maxf=1500)
    d.run(20)
    for btn, n in keys:
        d.run(n, btn)
        d.run(30, 0)
    d.run(2, C)
    d.run(1, 0)
    d.until(lambda d: d.mem.r8(0x060AA803) & 2, maxf=400)       # the fade to white is over: the screen leaves
    d.run(2)
    if d.vec() == VEC_OVER:                                     # the game over screen
        d.run(wait_over if wait_over else 40)
        d.run(2, C)
        d.run(1, 0)
    d.until(lambda d: d.vec() in (VEC_SELECT, VEC_START), maxf=800)
    d.run(5)


def sc_name(letters=(0, 1, 2), keys=(), wait_over=0):
    def f(d):
        name_entry_script(d, letters)
        rank_and_over(d, keys, wait_over)
    return f


def sc_none(keys=(), wait_over=0):
    def f(d):
        rank_and_over(d, keys, wait_over)
    return f


def add(name, patches, fn, final=None):
    SCENARIOS[name] = fn
    START_AT[name] = 0x0602FF84
    PATCHES[name] = patches
    if final:
        FINAL[name] = final


# practice: a time that reaches the table -> the name entry, the ranking screen, the game over screen
add("post_p0", state(2, 0, 8, 2, total=0x00024700), sc_name((0, 1, 2)))
# practice: a time that does not reach the table (the ranking screen set-up from scratch)
add("post_p1", state(2, 2, 8, 2, total=0x00500000), sc_none())
# arcade, rank 9 of 16: the table scrolls (held keys), the continue / quit choice (left / right), the game over screen runs into its time-out
add("post_a0", state(0, 2, 8, 3, total=0x00033500, car=1, trans=1), sc_name((7, 4, 11), keys=[(DOWN, 12), (UP, 12), (LEFT, 1), (RIGHT, 1), (R, 12)], wait_over=380))
# arcade game over in the last stage with the 48 lap flag
add("post_a1", state(0, 3, 10, 0, laps=3, total=0x00100800, car=1), sc_name((18, 4, 24)))


# ---- the name entry: B takes the last letter back, DEL, a forbidden word, END before three letters ----
def sc_edit(d):
    wait_wheel(d)
    pick(d, [0])
    d.run(2, B); d.run(1, 0)                            # B: the letter flies back
    d.run(80)
    pick(d, [0, 27])                                    # a letter, then DEL (count 1)
    pick(d, [1, 2, 27, 27])                             # two letters, then DEL (count 2), DEL again (count 1)
    pick(d, [18, 4, 23])                                # S E X: a forbidden word
    wheel_to(d, END)
    d.run(2, C); d.run(1, 0)                            # END: the word is refused, the letters fly away
    d.run(200)
    pick(d, [0, 1])
    wheel_to(d, END)
    d.run(2, C); d.run(1, 0)                            # END with two letters
    d.until(lambda d: d.mem.r8(0x060A8C60) >= 4 or d.vec() != VEC_RANK, maxf=600)
    rank_and_over(d)


add("post_edit", state(2, 1, 8, 2, total=0x00023000), sc_edit)


# ---- the analog pad / wheel: the wheel turns on its own, letters are taken at some ticks ----
def sc_analog(d):
    wait_wheel(d)
    for k in range(7):
        d.run(53)
        d.run(2, C); d.run(1, 0)
    d.run(150)


add("post_ana", state(2, 1, 8, 2, total=0x00023000, extra=[(0x060AA803, b(0x14)), (AXIS, b(160))]), sc_analog)
add("post_ana2", state(2, 1, 8, 2, total=0x00023000, extra=[(0x060AA803, b(0x14)), (AXIS, b(60))]), sc_analog)


# ---- no key at all: the time of the name entry runs out and the machine chooses the letters ----
def sc_timeout(d):
    d.until(lambda d: d.mem.r8(0x060A8C60) >= 4 or d.vec() != VEC_RANK, maxf=7000)
    rank_and_over(d)


add("post_timeout", state(2, 1, 8, 2, total=0x00023000), sc_timeout)

# ---- no name entry: the Stratos, the mirror course, a replay, a game over before the last stage; a practice rank that is not 1 ----
add("post_stratos", state(0, 2, 8, 3, total=0x00033500, car=2), sc_none())
add("post_mirror", state(0, 1, 8, 2, total=0x00033500, mirror=1), sc_none())
add("post_gameover", state(0, 1, 10, 0, total=0x00033500, extra=[(0x060A8C62, b(0xFF))]), sc_none(wait_over=0))
add("post_replay", state(2, 1, 8, 2, total=0x00023000, mode=2, extra=[(0x060A8C62, b(0xFF))]), sc_none())
# ---- the wheel turned to the left (key LEFT = +224), taking a letter with the key A / START, the other end of the wheel ----
def sc_left(d):
    wait_wheel(d)
    for k in range(4):
        d.run(2, LEFT); d.run(40, 0)                    # one letter to the other side per tap
    d.run(2, A); d.run(1, 0)
    d.run(120)
    pick(d, [25, 24])
    wheel_to(d, END)
    d.run(2, START); d.run(1, 0)
    d.until(lambda d: d.mem.r8(0x060A8C60) >= 4 or d.vec() != VEC_RANK, maxf=600)
    rank_and_over(d)


add("post_left", state(2, 3, 8, 2, total=0x00022000), sc_left)
# a goal with the control byte 12 (the next stage starts after the name entry), and the SELECT GAME flag of the ranking screen
add("post_c12", state(0, 3, 12, 3, total=0x00032000), sc_name((12, 14, 21)))
add("post_sel", state(0, 1, 10, 0, total=0x00033500, extra=[(0x060A8C62, b(0xFF)), (0x060AA821, b(1))]), sc_none())


# ---- the lap record of the best run (lap_store): the stored one is worse (replaced) / better (kept) ----
add("post_lap_a", state(2, 0, 8, 2, total=0x00024700, extra=[(0x06041F30, b(0x00030000, 4))]), sc_name((0, 1, 2)))
add("post_lap_b", state(2, 0, 8, 2, total=0x00024700, extra=[(0x06041F30, b(0x00001000, 4))]), sc_name((0, 1, 2)))


# ---- END as the first letter (three periods), the wheel held to the left for a whole letter ----
def sc_end0(d):
    wait_wheel(d)
    wheel_to(d, END)
    d.run(2, C); d.run(1, 0)
    d.until(lambda d: d.mem.r8(0x060A8C60) >= 4 or d.vec() != VEC_RANK, maxf=900)
    rank_and_over(d)


add("post_end0", state(2, 1, 8, 2, total=0x00023000), sc_end0)


def sc_left2(d):
    wait_wheel(d)
    d.run(16, LEFT); d.run(40, 0)                       # more than one letter in one go
    d.run(30, LEFT); d.run(60, 0)
    d.run(14, RIGHT); d.run(60, 0)
    pick(d, [0, 1])
    d.run(150)


add("post_left2", state(2, 1, 8, 2, total=0x00023000), sc_left2)


# ---- game type 6 (time attack, free run): the name entry, then the time attack menu again; a return from a replay ----
def sc_t6(d):
    name_entry_script(d, (3, 4, 5))
    d.until(lambda d: d.vec() == VEC_TM, maxf=900)
    d.run(40)


add("post_t6", state(6, 0, 8, 2, total=0x00024000, best_lap=0x00005234, extra=TA_LAPS if False else []), sc_t6)
add("post_retn", state(2, 1, 8, 2, total=0x00500000, replay_ret=1, extra=[(0x060A8C62, b(0xFF)), (0x060A8C63, b(1))]), lambda d: d.run(120))


# ---- time attack: the name entry of the best lap (state 6), then the result screen 0x06032514: the times count up, left / right chooses, confirm leaves ----
VEC_TM = 0x060262A4


def result_script(keys=(), settle=700):
    def f(d):
        d.until(lambda d: d.vec() == 0x06032514 or d.mem.r8(0x060A8CA0) == 1, maxf=3000)
        d.until(lambda d: d.mem.r8(0x060A8CA0) == 1, maxf=3000)
        d.run(settle)                                   # the count-up of the times
        for btn, n in keys:
            d.run(n, btn)
            d.run(25, 0)
        d.run(2, C)
        d.run(1, 0)
        d.until(lambda d: d.vec() in (VEC_TM, VEC_START) and d.mem.r8(0x060A8CA0) == 2, maxf=2000)
        d.until(lambda d: d.mem.r8(0x060AA803) & 2, maxf=2000)
        d.run(10)
    return f


def sc_t4(letters=(19, 14, 12), keys=(), settle=700):
    def f(d):
        name_entry_script(d, letters)
        result_script(keys, settle)(d)
    return f


TA_LAPS = [(0x060B0590, b(0x00005234, 4)), (0x060B0594, b(0x00005412, 4)), (0x060B0598, b(0x00005401, 4)), (0x060B00C8, b(3, 2))]
add("post_t4", state(4, 0, 8, 2, total=0x00015847, best_lap=0x00005234, extra=TA_LAPS), sc_t4(keys=[(LEFT, 1), (RIGHT, 1)]))


# ---- the result screen without a name entry: a game over (no choice, no times beyond the first), a replay (mode 2), a very long total time ----
add("post_t4_over", state(4, 1, 10, 0, total=0x00015847, extra=TA_LAPS), sc_t4(settle=500))
add("post_t4_rep", state(4, 0, 8, 2, mode=2, total=0x00015847, extra=TA_LAPS + [(0x060A8C62, b(0xFF)), (0x060A8C63, b(1))]), lambda d: (result_script(keys=[(LEFT, 1)], settle=500)(d)))
add("post_t4_big", state(4, 0, 8, 2, total=0x00110000, extra=TA_LAPS), sc_t4(settle=500))


# ---- the ranking table scrolled down and up with the keys held for several passes (states 5 and 6 of the ranking screen), the lap list of a run with a lap of a minute and more ----
def sc_scroll(d):
    name_entry_script(d, (7, 4, 11))
    d.until(lambda d: d.mem.r8(0x060A8C61) == 3, maxf=1500)
    d.run(20)
    for btn, n in [(DOWN, 12), (UP, 12), (UP, 12), (X, 12), (R, 12), (Z, 12), (L, 12)]:
        d.run(n, btn)
        d.run(30, 0)
    d.run(2, C)
    d.run(1, 0)
    d.until(lambda d: d.mem.r8(0x060AA803) & 2, maxf=400)
    d.run(60)


add("post_scroll", state(0, 2, 8, 3, total=0x00033500, car=1, trans=1), sc_scroll)
add("post_del0", state(2, 1, 8, 2, total=0x00023000), lambda d: (wait_wheel(d), pick(d, [27, 0, 1, 2]), d.run(300)))
