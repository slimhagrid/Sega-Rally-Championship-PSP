"""M8 scenarios: the photo module that follows a won practice race (src/menu_photo.c).  START_AT the module's init 0x0600E6A4 with the end state of the race patched in (as scn_post.py)."""
import os
from menu_drive import START, A, B, C, UP, DOWN, LEFT, RIGHT, X, Y, Z, L, R, AXIS, IGNORE   # noqa: F401

SCENARIOS = {}
START_AT = {}
PATCHES = {}
FINAL = {}


def b(v, n=1):
    return v.to_bytes(n, "big")


# The scenery objects of the course (spectators, zebras, birds, gates, flags .. : the event module's 64-byte slots at 0x060E4000, the helicopter slot at 0x060EFE00, the shared random
# generator 0x060D0050) are run by the original's object dispatcher 0x0601BCB8 every frame.  The port sets the slots up (0x0601BF18) but does not run their state functions: the PSP host
# draws the scenery with src/event.c (docs/events.md: the same state machines on the host's own data, verified separately against the oracle), so these ranges are left out of the comparison.
OBJECTS = [(IGNORE, (0x060E4000).to_bytes(4, "big") + (0x060F0000).to_bytes(4, "big")),           # the slot pool (up to 615 objects of 64 bytes: the forest) and the helicopter slot
           (IGNORE, (0x060D0050).to_bytes(4, "big") + (0x060D0054).to_bytes(4, "big"))]


def state(stage=0, car=0, trans=0, extra=()):
    p = [(0x060AA860, b(2, 2)), (0x060AA87E, b(stage, 2)), (0x060B00F5, b(8)), (0x060B00B6, b(1)), (0x060AA810, b(0)),
         (0x060AA80A, b(car)), (0x060AA809, b(trans)), (0x060AA808, b(1)), (0x060AA80C, b(1))]
    p += OBJECTS
    p += list(extra)
    return p


def unstub_scroll(d):
    """the harness (menu_env.py) stubs every function from 0x06015F90 as "the sound driver interface", but 0x06015F90 .. 0x06016338 are scroll routines (clr_hscr_rr, set_tp_rr, set_grpara_int,
    set_grpara_int2p, set_rpara_int, init_rpara_int, set_rpara, calc_rscroll; functions.tsv module scroll): the photo scene runs them (the road-follow rotation parameters), so the
    oracle runs the real code here"""
    for a in list(d.m.cpu.hooks):
        if 0x06015F90 <= a < 0x0601633C:
            del d.m.cpu.hooks[a]


def add(name, patches, fn, final=None):
    def run(d, fn=fn):
        unstub_scroll(d)
        fn(d)
    SCENARIOS[name] = run
    START_AT[name] = 0x0600E6A4
    PATCHES[name] = patches
    if final:
        FINAL[name] = final


def sc_run(n):
    def f(d):
        d.run(n)
    return f


VEC_NEXT = 0x0602FFD0                                   # the post-goal dispatcher's continuation after a ceremony (menu_post.c post_goal_ceremony): the module ends there


def sc_full(d):
    d.until(lambda d: d.vec() == VEC_NEXT, maxf=3000)


add("photo_probe", state(), sc_run(30))
add("photo_desert", state(0), sc_full, final=VEC_NEXT)
add("photo_forest", state(1, car=1, trans=1), sc_full, final=VEC_NEXT)
add("photo_mount", state(2), sc_full, final=VEC_NEXT)
add("photo_lake", state(3, car=1), sc_full, final=VEC_NEXT)
add("photo_mirror", state(1, extra=[(0x060AA82E, b(1))]), sc_full, final=VEC_NEXT)         # the mirror course: the scroll control negates the yaw words


def sc_start(at, keys=START):
    """START pressed after `at` passes: the module goes to the fade out (state 8) and ends early"""
    def f(d):
        d.run(at)
        d.run(2, keys)
        d.run(1, 0)
        d.until(lambda d: d.vec() == VEC_NEXT, maxf=3000)
    return f


add("photo_start", state(2), sc_start(300), final=VEC_NEXT)
add("photo_start2", state(0), sc_start(5), final=VEC_NEXT)                                 # during the first fade in
add("photo_keys", state(3), sc_start(700, A | B | C | X | Y | Z | L | R | UP | DOWN | LEFT | RIGHT), final=VEC_NEXT)      # every key but START does nothing


# ---- the whole chain after a won practice race: the photo module, the game over screen's set-up, the name entry, the ranking screen (scn_post.py) ----
import scn_post                                          # noqa: E402  (tools/ is on sys.path)


def post_state(stage, total, **kw):
    """scn_post.state() (the race end state incl. the record time) + the object ranges the photo scene leaves out"""
    return scn_post.state(2, stage, 8, 1, total=total, **kw) + OBJECTS


def sc_chain(letters=(0, 1, 2)):
    def f(d):
        d.until(lambda d: d.vec() == VEC_NEXT, maxf=3000)
        scn_post.name_entry_script(d, letters)
        scn_post.rank_and_over(d)
    return f


def sc_chain_none(d):
    d.until(lambda d: d.vec() == VEC_NEXT, maxf=3000)
    scn_post.rank_and_over(d)


def add_chain(name, patches, fn):
    SCENARIOS[name] = lambda d, fn=fn: (unstub_scroll(d), fn(d))[1]
    START_AT[name] = 0x0600E6A4
    PATCHES[name] = patches


add_chain("photo_chain", post_state(1, 0x00024700), sc_chain())                          # a time that reaches the table: the name entry follows the photo
add_chain("photo_chain_none", post_state(2, 0x00500000), sc_chain_none)                  # a time that does not: the ranking screen sets itself up
