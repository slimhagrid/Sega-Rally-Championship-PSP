"""M8 scenarios: car set-up (setting_main 0x06026A30) and option (option_init 0x06032F60) screens, entered from the mode select (0x06022894)."""
from menu_drive import START, A, B, C, UP, DOWN, LEFT, RIGHT, X, Y, Z, L, R  # noqa: E402,F401

ST = 0x060A8800
ST2 = 0x060A8808
CURSOR = 0x060A88A4


def st(d):
    return d.mem.r32(ST)


def wait_st(d, a, maxf=900):
    if not d.until(lambda d: st(d) == a, maxf):
        raise RuntimeError("state %08x not reached (now %08x)" % (a, st(d)))


def settle(d):
    """wait until the second handler is idle and the state is an input state"""
    d.until(lambda d: d.mem.r16(0x060A88E4) == 0 and st(d) in (0x06026B54, 0x06026C9E, 0x06026D6E), 600)
    d.run(4)


def hold(d, btn, n):
    d.run(n, btn)
    d.run(3, 0)


def enter_setting(d):
    """mode select -> cursor 3 -> confirm; returns in the first input state of the set-up screen"""
    d.run(200)
    for _ in range(3):
        d.tap(DOWN)
        d.run(8)
    d.tap(C)
    wait_st(d, 0x06026B54, 3000)
    d.run(12)


def leave(d):
    """run until the mode select is back"""
    d.until(lambda d: d.vec() == 0x06022894, 2000)
    d.run(40)


def goto_slot(d, n, btn=DOWN):
    for _ in range(12):
        if d.mem.r16(CURSOR) == n:
            break
        d.tap(btn)
        d.run(12)


def rows_tour(d):
    """in the rows state: every row down and up (wrap), left / right on every row"""
    for i in range(9):                                  # down: 0 -> 7 -> wrap to 0
        d.tap(DOWN)
        settle(d)
    for i in range(2):                                  # up: wrap to 7, 6
        d.tap(UP)
        settle(d)
    for i in range(8):
        d.tap(UP)
        settle(d)
    # now on row 0 (cursor)
    for row in range(7):
        for k in range(4):
            d.tap(LEFT)
            d.run(14)
        for k in range(7):
            d.tap(RIGHT)
            d.run(14)
        d.tap(DOWN)
        settle(d)


def sc_set_car(d):
    """tune 0x07 (3 stages), save on: tour of the car slots and of the rows, store the set-up of slot 2, exit with save"""
    enter_setting(d)
    d.tap(DOWN); d.run(12)
    d.tap(DOWN); d.run(12)
    d.tap(UP); d.run(12)
    d.tap(UP); d.run(12)
    d.tap(UP); d.run(12)                                # wrap to the top
    goto_slot(d, 1)
    d.tap(C)                                            # slot 1 -> the rows
    wait_st(d, 0x06026D6E)
    settle(d)
    rows_tour(d)
    d.tap(B)                                            # back to the car row
    wait_st(d, 0x06026B54)
    settle(d)
    goto_slot(d, 2, UP)
    d.tap(C)                                            # slot 2 (edit again)
    wait_st(d, 0x06026D6E)
    settle(d)
    for i in range(6):                                  # to row 6
        d.tap(DOWN)
        settle(d)
    d.tap(RIGHT); d.run(14)
    d.tap(C)                                            # store the set-up (flash, save)
    d.run(200)
    wait_st(d, 0x06026D6E)
    settle(d)
    d.tap(C)                                            # row 7 = back: exit with save
    leave(d)


def sc_set_choice(d):
    """no tune stage, save off: the car choice (slot 0), default set-up, rows with the pad's other buttons, exit by cancel"""
    enter_setting(d)
    d.tap(UP); d.run(12)
    d.tap(DOWN); d.run(12)                              # one slot only: stays
    d.tap(C)                                            # slot 0 -> car choice
    wait_st(d, 0x06026C9E)
    settle(d)
    d.tap(RIGHT); d.run(14)
    d.tap(RIGHT); d.run(14)
    d.tap(LEFT); d.run(14)
    d.tap(UP); d.run(14)                                # vertical keys do nothing here
    d.tap(B)                                            # cancel -> car row
    wait_st(d, 0x06026B54)
    settle(d)
    d.tap(A)                                            # A also confirms
    wait_st(d, 0x06026C9E)
    settle(d)
    d.tap(RIGHT); d.run(14)
    d.tap(START)
    wait_st(d, 0x06026D6E)
    settle(d)
    # rows with combined directions and the buttons that do nothing
    d.run(8, UP | LEFT); d.run(3)
    settle(d)
    d.run(8, DOWN | RIGHT); d.run(3)
    settle(d)
    d.tap(X); d.tap(Y); d.tap(Z); d.tap(L); d.tap(R)
    hold(d, DOWN, 70)                                   # key repeat down
    settle(d)
    hold(d, RIGHT, 70)                                  # key repeat right
    hold(d, LEFT, 70)
    hold(d, UP, 90)
    settle(d)
    d.tap(B)                                            # cancel -> car row
    wait_st(d, 0x06026B54)
    settle(d)
    d.tap(B)                                            # cancel on the car row: leave without saving
    leave(d)


def sc_set_new(d):
    """tune 0x03 (2 stages), save on: edit the last slot -> a new tune stage; back out of the rows during the slide; exit"""
    enter_setting(d)
    goto_slot(d, 2)
    d.tap(C)
    wait_st(d, 0x06026D6E)
    settle(d)
    d.tap(DOWN); d.run(2)                               # a second key while the slide runs
    d.tap(UP)
    settle(d)
    for i in range(6):
        d.tap(DOWN)
        settle(d)
    d.tap(RIGHT); d.run(14)
    d.tap(RIGHT); d.run(14)
    d.tap(LEFT); d.run(14)
    d.tap(C)                                            # store: slot == tune count -> new tune stage
    d.run(200)
    wait_st(d, 0x06026D6E)
    settle(d)
    d.tap(UP)                                           # row 6 (tune row of the new stage)
    settle(d)
    d.tap(RIGHT); d.run(14)
    d.tap(B)                                            # back to the car row
    wait_st(d, 0x06026B54)
    settle(d)
    goto_slot(d, 1)
    d.tap(C)
    wait_st(d, 0x06026D6E)
    settle(d)
    d.run(40)
    d.tap(B)                                            # back out while ...
    d.run(3)
    d.tap(B)
    wait_st(d, 0x06026B54)
    settle(d)
    goto_slot(d, 3)
    d.tap(C)
    wait_st(d, 0x06026D6E)
    settle(d)
    d.tap(DOWN); settle(d)
    d.tap(DOWN); settle(d)
    d.tap(RIGHT); d.run(14)
    d.tap(DOWN); settle(d)
    d.tap(DOWN); settle(d)
    d.tap(DOWN); settle(d)                              # row 7
    d.tap(START)                                        # exit with save
    leave(d)


def sc_set_full(d):
    """tune 0x0F (4 stages, save on): all five car slots, the default set-up with 4 stages, the tune row at its widest, store the last slot (no new stage: already complete)"""
    enter_setting(d)
    goto_slot(d, 4)
    d.tap(C)
    wait_st(d, 0x06026D6E)
    settle(d)
    for i in range(6):
        d.tap(DOWN)
        settle(d)
    for k in range(5):
        d.tap(RIGHT); d.run(14)
    for k in range(5):
        d.tap(LEFT); d.run(14)
    d.tap(C)                                            # store
    d.run(200)
    wait_st(d, 0x06026D6E)
    settle(d)
    d.tap(UP)
    settle(d)
    d.tap(B)                                            # cancel to the car row
    wait_st(d, 0x06026B54)
    settle(d)
    goto_slot(d, 0, UP)
    d.tap(C)                                            # slot 0: the car choice
    wait_st(d, 0x06026C9E)
    settle(d)
    d.tap(RIGHT); d.run(14)
    d.tap(C)
    wait_st(d, 0x06026D6E)
    settle(d)
    d.run(60)
    d.tap(DOWN); settle(d)
    d.tap(RIGHT); d.run(14)
    d.tap(B)
    wait_st(d, 0x06026B54)
    settle(d)
    d.tap(B)                                            # leave
    leave(d)


def sc_set_one(d):
    """tune 0x01 (one stage), save on with the cartridge flag: slot 1 edited, left through the back row"""
    enter_setting(d)
    goto_slot(d, 1)
    d.tap(C)
    wait_st(d, 0x06026D6E)
    settle(d)
    for i in range(7):
        d.tap(DOWN)
        settle(d)
    d.tap(C)                                            # the back row
    leave(d)


OPT = 0x060A8CC0


def ost(d):
    return d.mem.r8(OPT)


def wait_ost(d, n, maxf=900):
    if not d.until(lambda d: ost(d) == n, maxf):
        raise RuntimeError("option state %d not reached (now %d)" % (n, ost(d)))


def enter_option(d):
    """mode select -> cursor 5 -> confirm; returns in the first input state (page 1) of the option screen"""
    d.run(200)
    for _ in range(5):
        d.tap(DOWN)
        d.run(8)
    d.tap(C)
    d.until(lambda d: d.vec() == 0x06032F60, 3000)
    wait_ost(d, 1, 3000)
    d.run(12)


def opt_leave(d, btn=START):
    d.tap(btn)
    d.until(lambda d: d.vec() == 0x06022894, 3000)
    d.run(40)


def opt_goto(d, item):
    for _ in range(12):
        if d.mem.r8(OPT + 1) == item:
            break
        d.tap(DOWN)
        d.run(6)


def sc_opt_p1(d):
    """page 1: every item up / down with the wrap, left / right on every item (wrap both ways), key repeat, both directions at once, leave by the last item"""
    enter_option(d)
    d.tap(UP); d.run(8)                                 # wrap to the last item
    d.tap(DOWN); d.run(8)                               # and back to the first
    for item in range(9):
        for k in range(5):
            d.tap(RIGHT); d.run(8)
        for k in range(7):
            d.tap(LEFT); d.run(8)
        d.run(6, LEFT | RIGHT); d.run(4)                # both: left wins
        d.run(6, RIGHT); d.run(4)
        d.tap(DOWN); d.run(8)
    hold(d, RIGHT, 70)                                  # key repeat
    hold(d, LEFT, 90)
    hold(d, DOWN, 80)
    hold(d, UP, 80)
    d.tap(X); d.tap(Y); d.tap(Z); d.tap(L); d.tap(R)
    opt_goto(d, 8)
    d.tap(C)                                            # the last item leaves
    d.run(20)
    d.tap(A)                                            # a button during the fade ends it at once
    d.until(lambda d: d.vec() == 0x06022894, 3000)
    d.run(40)


def opt_page2(d):
    """enter page 2 from page 1 (item 6)"""
    opt_goto(d, 6)
    d.tap(C)
    wait_ost(d, 2, 600)
    d.run(10)


def sc_opt_snd(d):
    """the sound page with the secret items hidden (unlock bit 1 clear): stereo / mono, every value, the three tests, back by the last item"""
    enter_option(d)
    opt_page2(d)
    d.tap(RIGHT); d.run(8)                              # item 0 stereo -> on
    d.tap(RIGHT); d.run(8)
    d.tap(LEFT); d.run(8)
    for item in range(1, 10):
        d.tap(DOWN); d.run(8)
        for k in range(3):
            d.tap(RIGHT); d.run(8)
    for k in range(3):                                  # walk up over the hidden item
        d.tap(UP); d.run(8)
    for item in (6, 7, 8):
        opt_p2_goto(d, item)
        rng = d.mem.r8(0x060334A0 + item)
        for v in range(rng + 1):
            d.tap(C); d.run(12)                         # the test of this value
            d.tap(RIGHT); d.run(8)
    opt_p2_goto(d, 9)
    d.tap(C)                                            # back to page 1
    wait_ost(d, 1, 600)
    d.run(10)
    opt_leave(d, B)


def opt_p2_goto(d, item):
    for _ in range(14):
        if d.mem.r8(OPT + 1) == item:
            break
        d.tap(DOWN)
        d.run(6)


def sc_opt_snd2(d):
    """the sound page with the secret items (unlock bit 1 set): cursor over all ten items, values, B and START back"""
    enter_option(d)
    opt_page2(d)
    for item in range(10):
        d.tap(DOWN); d.run(8)
        d.tap(LEFT); d.run(8)
        d.tap(RIGHT); d.run(8)
    for item in range(10):
        d.tap(UP); d.run(8)
    hold(d, DOWN, 80)
    hold(d, RIGHT, 100)
    opt_p2_goto(d, 6)
    d.tap(A); d.run(12)                                 # A also starts a test
    d.tap(B)                                            # B: back to page 1
    wait_ost(d, 1, 600)
    d.run(10)
    opt_page2(d)
    d.tap(START)                                        # START: back too
    wait_ost(d, 1, 600)
    d.run(10)
    opt_leave(d, B)


def sc_opt_pad(d):
    """page 3 (the pads): pad 1 selection, back; then the colour fade"""
    enter_option(d)
    opt_goto(d, 7)
    d.tap(A)
    wait_ost(d, 6, 900)
    d.run(10)
    for k in range(6):
        d.tap(DOWN); d.run(8)
    for k in range(6):
        d.tap(UP); d.run(8)
    hold(d, DOWN, 60)
    d.tap(C)                                            # back to page 1
    wait_ost(d, 1, 900)
    d.run(10)
    opt_goto(d, 7)
    d.tap(C)
    wait_ost(d, 6, 900)
    d.run(10)
    d.tap(DOWN); d.run(8)
    d.tap(B)
    wait_ost(d, 1, 900)
    d.run(10)
    opt_leave(d, B)


def sc_opt_pad3(d):
    """page 3 with no pad 1 and an analog pad 2"""
    enter_option(d)
    opt_goto(d, 7)
    d.tap(C)
    wait_ost(d, 6, 900)
    d.run(10)
    for k in range(5):
        d.tap(DOWN); d.run(8)
    d.tap(START)
    wait_ost(d, 1, 900)
    d.run(10)
    opt_leave(d, START)


def sc_opt_pad2(d):
    """page 3 with an analog pad 1, a digital pad 2 that holds UP (key repeat of player 2), entered directly (the pad bytes survive)"""
    d.run(30)                                           # option_pict_init ran as the first handler, then option_init
    wait_ost(d, 1, 3000)
    d.run(12)
    opt_goto(d, 7)
    d.tap(C)
    wait_ost(d, 6, 900)
    d.run(200)
    d.tap(DOWN); d.run(8)
    d.tap(UP); d.run(8)
    d.tap(START)
    wait_ost(d, 1, 900)
    d.run(10)
    opt_leave(d, B)


SCENARIOS = {"set_car": sc_set_car, "set_choice": sc_set_choice, "set_new": sc_set_new, "opt_p1": sc_opt_p1, "opt_snd": sc_opt_snd,
             "opt_snd2": sc_opt_snd2, "opt_pad": sc_opt_pad, "opt_pad2": sc_opt_pad2, "opt_pad3": sc_opt_pad3, "set_full": sc_set_full, "set_one": sc_set_one}
START_AT = {"set_car": 0x06022894, "set_choice": 0x06022894, "set_new": 0x06022894, "opt_p1": 0x06022894, "opt_snd": 0x06022894,
            "opt_snd2": 0x06022894, "opt_pad": 0x06022894, "opt_pad2": 0x06033970, "opt_pad3": 0x06022894, "set_full": 0x06022894, "set_one": 0x06022894}
PATCHES = {
    "set_car": [(0x06040063, bytes([0x07])), (0x06042A33, bytes([1])), (0x06042A32, bytes([1]))],
    "set_choice": [],
    "set_new": [(0x06040063, bytes([0x03])), (0x06042A33, bytes([1])), (0x06042A32, bytes([0]))],
    "opt_p1": [],
    "opt_snd": [],
    "opt_snd2": [(0x06040062, bytes([0x02]))],
    "opt_pad": [(0x060AA9A4, bytes([0x01])), (0x060AA9A6, bytes([0x01]))],
    "opt_pad3": [(0x060AA9A4, bytes([0xF0])), (0x060AA9A6, bytes([0x01]))],
    "set_full": [(0x06040063, bytes([0x0F])), (0x06042A33, bytes([1])), (0x06042A32, bytes([0]))],
    "set_one": [(0x06040063, bytes([0x01])), (0x06042A33, bytes([2])), (0x06042A32, bytes([1]))],
    "opt_pad2": [(0x060A001C, bytes([0x06, 0x03, 0x2F, 0x60])), (0x060AA996, bytes([0x10, 0x00])), (0x060AA9A4, bytes([0x01])), (0x060AA9A6, bytes([0x00]))],
}
