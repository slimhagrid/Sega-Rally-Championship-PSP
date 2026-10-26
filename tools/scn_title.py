"""M8 scenarios: the title / attract module (module adv 0x0600D204 + advctrl 0x0600DC28, src/menu_adv.c): the sports logo, the title with its fly-over of the course and the demo run,
the ranking pages and the thanks-to pictures, START to the mode select.  They start at adv_init 0x0600D214 (the main vector the power-on leaves); the first pass after the power-on has
[0x060AA82B] = 1, so the module starts in the sports logo (state 24); [0x060AA82B] = 0 (what the title leaves behind when START was pressed) starts in the title (state 12).

The scenery objects (the slot pool 0x060E4000 .., the helicopter slot) are run by the original's object dispatcher 0x0601BCB8; the port sets them up and leaves the dispatching to the host
(like scn_photo.py), so these ranges are left out of the comparison."""
import os
import sys

from menu_drive import START, A, B, C, UP, DOWN, LEFT, RIGHT, X, Y, Z, L, R, AXIS, IGNORE, POKE   # noqa: F401

SCENARIOS = {}
START_AT = {}
PATCHES = {}
FINAL = {}

VEC_TITLE = 0x0600D214
VEC_BUP = 0x0602F64E                                    # the backup memory check screen the title hands over to (the PSP host's bup_screen_skip goes on to the mode select)


def b(v, n=1):
    return v.to_bytes(n, "big")


OBJECTS = [(IGNORE, (0x060E4000).to_bytes(4, "big") + (0x060F0000).to_bytes(4, "big")),
           (IGNORE, (0x060D0050).to_bytes(4, "big") + (0x060D0054).to_bytes(4, "big")),
           # the cars' display (dispCar 0x060101E8 with the tyre marks / dust switched on by spray_init 0x0601D074): the work area at the end of the car blocks (+0x3C0 .. +0x400: the wheels' mark
           # points, FUN_0601D0E6) belongs to the spray system, which the PSP host replaces with src/spray.c (verified separately); the port keeps the choice of the draw routine (+896 / +900) only
           (IGNORE, (0x060B03C0).to_bytes(4, "big") + (0x060B0400).to_bytes(4, "big")),
           (IGNORE, (0x060B09C0).to_bytes(4, "big") + (0x060B0A00).to_bytes(4, "big"))]


def unstub_scroll(d):
    """menu_env.py stubs 0x06015F90 .. 0x0601633C as the sound driver; the demo runs the scroll routines (rscroll_ctrl): the oracle runs the real code (like scn_photo.py)"""
    for a in list(d.m.cpu.hooks):
        if 0x06015F90 <= a < 0x0601633C:
            del d.m.cpu.hooks[a]


ENGINE = (0x06009938,)          # carconfig: the race engine's tuning of the car (tables at 0x060A9400 ..): nothing in the title reads it (the cars are replayed from the recording), so it is stubbed, as the ending's engine routines are


def add(name, patches, fn, final=None, start=VEC_TITLE):
    def run(d, fn=fn):
        unstub_scroll(d)
        for a in ENGINE:
            d.m.cpu.hooks[a] = lambda c: c.ret()
        fn(d)
    SCENARIOS[name] = run
    START_AT[name] = start
    PATCHES[name] = list(patches) + OBJECTS
    if final:
        FINAL[name] = final


def sc_run(n):
    def f(d):
        d.run(n)
    return f


add("title_probe", [], sc_run(400))


# the whole attract loop from the power-on: the sports logo, the demo run (desert), the title, the Sega logo, the sports logo again, the demo of the forest, the mountain, back to the desert
add("title_full", [], sc_run(5800))


def to_bup(d, maxf=3000):
    """START taps every 40 passes until the title hands over to the backup memory check (a START in the fade-in only ends the fade)"""
    for k in range(maxf):
        if d.vec() == VEC_BUP:
            return
        d.run(1, START if k % 40 < 3 else 0)
    raise RuntimeError("the title did not hand over")


def sc_start_demo(at, then, keys=START):
    """START in the demo `at` passes after the start (fade out, back to the title), `then` passes later START at the title (the wait / the fade-in)"""
    def f(d):
        d.run(at)
        d.run(3, keys)
        d.run(1, 0)
        d.run(then)
        d.run(3, keys)
        d.run(1, 0)
        to_bup(d)
    return f


add("title_sd0", [], sc_start_demo(100, 40), final=VEC_BUP)          # START in the demo, START again while the title fades in
add("title_sd1", [], sc_start_demo(60, 300), final=VEC_BUP)          # ... START at the title after the fade
add("title_sd2", [], sc_start_demo(700, 120), final=VEC_BUP)         # START in the ranking pages' part of the demo
add("title_sd3", [(0x060AA87E, b(1, 2))], sc_start_demo(200, 200), final=VEC_BUP)     # the forest
add("title_sd4", [(0x060AA87E, b(2, 2))], sc_start_demo(500, 10), final=VEC_BUP)      # the mountain


def sc_title(at, rest=0):
    def f(d):
        d.run(at)
        d.run(3, START)
        d.run(1, 0)
        if rest:
            d.run(rest)
        to_bup(d)
    return f


# the title as the first screen (what the title leaves behind after START: [0x060AA82B] = 0)
add("title_t0", [(0x060AA82B, b(0))], sc_title(0), final=VEC_BUP)                # START at once
add("title_t1", [(0x060AA82B, b(0))], sc_title(5), final=VEC_BUP)
add("title_t2", [(0x060AA82B, b(0))], sc_title(60), final=VEC_BUP)
add("title_t3", [(0x060AA82B, b(0))], sc_title(200), final=VEC_BUP)
add("title_t4", [(0x060AA82B, b(0))], sc_run(1000))                              # no START: the title runs out, the pictures, the demo
add("title_t5", [(0x060AA82B, b(0)), (0x060AA87E, b(2, 2))], sc_run(700))


def sc_hold(d):
    """START held from the title's first pass until the screen is left (the latched presses are cleared every pass: only the edge counts)"""
    d.run(200, START)
    to_bup(d)


add("title_t6", [(0x060AA82B, b(0))], sc_hold, final=VEC_BUP)


# the ranking tables the title draws once at the start (init_adv_vdp2 -> FUN_0603125A): records with every kind of time (ten minutes and more, ties), name (blank letters), class and transmission
import random


def rec_patch(seed, base=0x06041220):
    rnd = random.Random(seed)
    out = []
    t = 0
    for k in range(16):
        if rnd.random() < 0.3 and k:
            pass                                        # a tie with the row above
        else:
            mm = rnd.choice([0, 1, 2, 3, 3, 4, 9, 10, 12, 15])
            t = (mm << 16) | (rnd.randrange(0, 60) // 10 << 12) | (rnd.randrange(10) << 8) | (rnd.randrange(10) << 4) | rnd.randrange(10)
            t = (t & 0xFFFF0000) | ((rnd.randrange(6) << 12) | (rnd.randrange(10) << 8) | (rnd.randrange(6) << 4) | rnd.randrange(10))
            t = ((mm // 10) << 20) | ((mm % 10) << 16) | (t & 0xFFFF)
        names = [rnd.choice([0, 0, 1, 2, 5, 26, 27]) for _ in range(3)]
        info = (rnd.randrange(4) << 28) | (rnd.randrange(2) << 30) | (rnd.randrange(2) << 24) | (names[0] << 16) | (names[1] << 8) | names[2]
        out.append(t.to_bytes(4, "big") + info.to_bytes(4, "big"))
    return [(base, b"".join(out))]


add("title_r0", rec_patch(1) + [(0x06042A3A, b(0))], sc_run(300))
add("title_r1", rec_patch(2, 0x06041320) + [(0x06042A3A, b(1))], sc_run(300))
add("title_r2", rec_patch(3) + [(0x06042A3A, b(1)), (0x060AA860, b(0, 2))], sc_run(300))


# the power-on test: the memory sat_boot() leaves (compared before the first tick), then the first passes of the title the original's main loop dispatches (the same set-up as every title scenario)
add("boot", [], sc_run(60), start=0)
