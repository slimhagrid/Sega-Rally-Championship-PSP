"""M8 scenarios of the record screens (rec_*) and the save data (bup_*).  Entered through the mode select (cursor entry 4 = record_main 0x0602CC74).

Everything starts at the mode select (START_AT 0x06022894): DOWN x4 (the battle entry is skipped or not by the 2nd pad) and confirm with C.  PATCHES hold the state the earlier screens would
have left: the unlock byte 0x06040062 (bit 1 / 2 = the X+Y / X Y Z Y X cheats of the mode select, bit 4 = the arcade ranking), record tables with ties / every car class / every name letter,
and a stored replay in low work RAM (0x002C0000 + course * 0x3D00: the run that the time attack records highlight and replay)."""
import random
import struct

from menu_drive import UP, DOWN, LEFT, RIGHT, A, B, C, START, X, Y, Z, L, R   # noqa: F401

REC_TABLE = 0x06041000          # 68 pointers to the record lists (see src/menu_rec_data.c)


def enter_records(d):
    """mode select -> record_main: DOWN x4, confirm with C, wait until record_main took over (the vector word shows the exit target, so look at the screen state instead)"""
    d.run(40)
    for _ in range(4):
        d.tap(DOWN)
        d.run(8)
    d.tap(C)
    d.until(lambda d: d.mem.r8(0x060A8C14) == 1 and d.mem.r8(0x060A8C16) == 2, maxf=900)


def keys(d, seq, wait=30):
    """a string of key letters: U D L R = tap that direction, a b c s = A B C START, 'h' + digit = hold the next letter for digit * 20 ticks, w = wait"""
    names = {"U": UP, "D": DOWN, "L": LEFT, "R": RIGHT, "A": A, "B": B, "C": C, "S": START, "X": X, "Y": Y, "Z": Z}
    i = 0
    while i < len(seq):
        ch = seq[i]
        if ch == "h":
            n = int(seq[i + 1])
            d.run(n * 20, names[seq[i + 2]])
            d.run(wait)
            i += 3
            continue
        if ch == "w":
            d.run(60)
        else:
            d.tap(names[ch])
            d.run(wait)
        i += 1


def walk(seed, n, dirs="UDLR"):
    r = random.Random(seed)
    return "".join(r.choice(dirs) for _ in range(n))


# ---- the state patches ------------------------------------------------------------------------------------------------
def p8(addr, *vals):
    return (addr, bytes(vals))


def rec_entry(ticks, a, b, c):
    """one record: time 0x00MMSSCC (a u32), the info word: bit 28 .. 29 class, bit 24 flag, 25..27 more flags, three name letters (5 bits each) in bits 16 / 8 / 0"""
    return struct.pack(">II", ticks, (c << 28) | b)


def patch_lists():
    """time ties and every flag / class combination: the arcade course list 0 (0x06041220, 16 entries), the arcade car list 0 (0x06041420, 8 entries) and the time attack list 0 (0x06041520, 6 entries)"""
    out = []
    base = 0x06041220
    for k in range(16):
        t = 0x00020000 + 0x00010000 * (k // 3) + 0x0100 * (k % 3) + k
        info = ((k & 3) << 28) | ((k & 4) << 22) | ((k & 8) << 22) | ((1 + k) << 16) | ((26 - k) << 8) | (k + 3)
        out.append((base + 8 * k, struct.pack(">II", t, info)))
    base = 0x06041420
    for k in range(8):
        t = 0x00030000 + 0x00010000 * (k // 2)
        info = ((k & 3) << 28) | ((k & 1) << 24) | ((k & 2) << 25) | ((3 * k + 1) << 16) | ((k + 5) << 8) | (31 - k)
        out.append((base + 8 * k, struct.pack(">II", t, info)))
    base = 0x06041520
    for k in range(6):
        t = 0x00030000 + 0x00010000 * (k // 2)
        info = ((k & 3) << 28) | ((k & 1) << 24) | ((k & 2) << 25) | ((2 * k + 1) << 16) | ((k + 9) << 8) | (20 - k)
        out.append((base + 8 * k, struct.pack(">II", t, info)))
    return out


def patch_replay(course):
    """a stored run of `course` in low work RAM (0x002C0000 + course * 0x3D00): +0 flag, +4 time (matches a record of the list -> the row is highlighted and can be replayed)"""
    base = 0x002C0000 + course * 0x3D00
    t = 0x00030000                  # = the first time of the patched time attack list
    return [(base, struct.pack(">I", 1)), (base + 4, struct.pack(">I", t)), (base + 20, bytes([0, 0, 0]))]


def flags(v):
    return [p8(0x06040062, v)]


def sc_rec_enter(d):
    enter_records(d)
    d.run(300)


def mk_tour(seed, n):
    def sc(d):
        enter_records(d)
        d.run(60)
        keys(d, walk(seed, n), wait=24)
        d.run(60)
    return sc


def mk_exit(key):
    def sc(d):
        enter_records(d)
        d.run(60)
        keys(d, "RDLU", wait=24)
        d.tap(key)
        d.run(120)
    return sc


SCENARIOS = {"rec_enter": sc_rec_enter,
             "rec_tour0": mk_tour(1, 70),
             "rec_tour1": mk_tour(2, 90),
             "rec_tour2": mk_tour(3, 90),
             "rec_tour3": mk_tour(4, 90),
             "rec_exitB": mk_exit(B),
             "rec_exitA": mk_exit(A),
             }
START_AT = {k: 0x06022894 for k in SCENARIOS}
PATCHES = {
    "rec_tour0": flags(0x00),
    "rec_tour1": flags(0x16) + patch_lists(),
    "rec_tour2": flags(0x06) + patch_lists() + patch_replay(0),
    "rec_tour3": flags(0x12),
}


# ---- rec_dataset / rec_connect / the sorts, called directly (START_AT = record_dataset: the original runs it once as the first "screen", then the scenario ends) ----
def w8(a, v):
    return (a, bytes([v & 0xFF]))


def w16(a, v):
    return (a, struct.pack(">H", v & 0xFFFF))


def ds_state(mode, course=0, c80c=1, c811=0, c80a=0, c809=0, f803=0x11, f3f=0x00, a38=(1, 1, 0), b3b=1, b3c=1, b3d=1, c810=0, ret17=0, replay=False, extra=()):
    p = [w16(0x060AA860, mode), w16(0x060AA87E, course), w8(0x060AA80C, c80c), w8(0x060AA811, c811), w8(0x060AA80A, c80a), w8(0x060AA809, c809),
         w8(0x060AA803, f803), w8(0x06042A3F, f3f), w8(0x060AA810, c810), w8(0x060A8C17, ret17),
         w8(0x06042A38, a38[0]), w8(0x06042A39, a38[1]), w8(0x06042A3A, a38[2]), w8(0x06042A3B, b3b), w8(0x06042A3C, b3c), w8(0x06042A3D, b3d)]
    if replay:
        base = 0x002C0000 + course * 0x3D00
        p += [(base, struct.pack(">I", 1)), (base + 4, struct.pack(">IIII", 0x00012345, 0x00023456, 0x00034567, 0x00045678)),
              (base + 20, bytes([2, 1, 0, 0, 0, 3, 1, 2, 0, 0, 0, 0])), (base + 32, bytes(range(1, 73)))]
    return p + [(0x06041E20, _cmp_data())] + list(extra)


def _cmp_data():
    """the lap time lists the sums are built from (0x06041E20 ..): u16 values, some large so that the sums pass the 10 << 16 carry adjust of rec_dataset"""
    r = random.Random(5)
    out = bytearray()
    for _ in range(0xC10 // 2):
        out += struct.pack(">H", r.choice((r.randrange(0x400, 0x2000), r.randrange(0x6000, 0xFFFF), 0x7FFF)))
    return bytes(out)


def sc_one_pass(d):
    d.run(1)


DS = {
    "rec_ds0a": ds_state(0, c80c=3, f803=0x1D, f3f=0x32, b3b=0),
    "rec_ds0b": ds_state(0, c80c=1, c811=1, f803=0x11, f3f=0x02, a38=(3, 2, 2)),
    "rec_ds0c": ds_state(0, course=5, c80c=2, c811=3, c80a=5, f803=0x15, f3f=0x13, a38=(4, 0, 1), c809=1),
    "rec_ds2a": ds_state(2, course=0, f803=0x19, f3f=0x21, b3b=0),
    "rec_ds2b": ds_state(2, course=3, c80c=2, f803=0x1D, f3f=0x31, a38=(2, 3, 1), c80a=3, c811=2),
    "rec_ds4a": ds_state(4, course=1, replay=True, b3c=0, c810=0, c80a=3, c811=1, c809=1),
    "rec_ds4b": ds_state(4, course=2, replay=True, b3c=1, c810=0, c80a=2, c811=0, b3d=0),
    "rec_ds4c": ds_state(4, course=1, replay=False, c810=0, c80a=5, c811=1, c809=0),
    "rec_ds4d": ds_state(4, course=3, replay=True, c810=2, c80a=2, c811=0, c809=1),
    "rec_ds6": ds_state(6, course=2, c80a=4, c811=1, c809=1, b3d=0),
    "rec_ds6b": ds_state(6, course=0, c80a=1, c811=0, c809=0, b3d=1),
    "rec_dsR2": ds_state(2, course=1, ret17=1, f803=0x11),
    "rec_dsR4": ds_state(4, course=2, ret17=1, replay=True),
}
for _n, _p in DS.items():
    SCENARIOS[_n] = sc_one_pass
    START_AT[_n] = 0x0602C6C0
    PATCHES[_n] = _p


# ---- longer random tours (state dependent choices are fine: the oracle records the pad words) ----
def mk_walk(seed, n, weights=(2, 2, 3, 4, 1, 1, 1, 1)):
    def sc(d):
        enter_records(d)
        d.run(60)
        r = random.Random(seed)
        dirs = [UP, DOWN, LEFT, RIGHT, R, Z, L, X]
        for _ in range(n):
            k = r.choices(dirs, weights)[0]
            if r.random() < 0.08:
                d.run(r.choice((30, 50, 70)), k)          # a held key: auto repeat
            else:
                d.tap(k)
            d.run(r.choice((12, 20, 28)))
        d.run(60)
    return sc


for _i, (_seed, _fl) in enumerate(((11, 0x00), (12, 0x02), (13, 0x04), (14, 0x10), (15, 0x16), (16, 0x14))):
    _n = "rec_walk%d" % _i
    SCENARIOS[_n] = mk_walk(_seed, 160)
    START_AT[_n] = 0x06022894
    PATCHES[_n] = flags(_fl) + (patch_lists() if _i % 2 else [])


# ---- the return from a replay: record_main entered directly with [0x060A8C17] = 1 ----
def ret_state(p8, p9, p10, p11, p12, p13, p14, p15, fl=0x00, extra=()):
    return [w8(0x060A8C17, 1), w8(0x06040062, fl), (0x06040048, bytes([p8, p9, p10, p11, p12, p13, p14, p15]))] + patch_lists() + list(extra)


def sc_ret_idle(d):
    d.run(200)
    keys(d, "DDUUR", wait=24)
    d.run(40)


def sc_ret_rap(d):
    d.run(120)
    keys(d, "RRDDDUUUD", wait=26)
    d.run(30)


def sc_ret_short(d):
    d.run(120)
    keys(d, "DU", wait=24)
    d.run(30)


def sc_ret_replay(d):
    d.run(150)
    d.tap(A)
    d.run(3)


def sc_ret_exit(key):
    def sc(d):
        d.run(120)
        d.tap(key)
        d.run(60)
    return sc


RET = {
    "rec_ret_ar": (sc_ret_idle, ret_state(4, 4, 4, 4, 4, 5, 5, 4)),
    "rec_ret_ar2": (sc_ret_idle, ret_state(4, 5, 5, 6, 6, 4, 4, 5, fl=0x16)),
    "rec_ret_ar3": (sc_ret_idle, ret_state(4, 4, 5, 4, 4, 5, 5, 4, fl=0x10)),
    "rec_ret_ar4": (sc_ret_idle, ret_state(4, 4, 5, 4, 4, 6, 5, 4, fl=0x16)),
    "rec_ret_rap": (sc_ret_rap, ret_state(4, 5, 4, 6, 4, 4, 5, 4, fl=0x02)),
    "rec_ret_rap2": (sc_ret_rap, ret_state(4, 5, 4, 7, 4, 4, 5, 4, fl=0x00)),
    "rec_ret_ta": (sc_ret_idle, ret_state(5, 4, 4, 4, 5, 5, 5, 5, fl=0x04)),
    "rec_ret_ta2": (sc_ret_idle, ret_state(5, 4, 4, 5, 4, 4, 4, 4, fl=0x06)),
    "rec_ret_replay": (sc_ret_replay, ret_state(5, 4, 4, 4, 5, 5, 5, 5, fl=0x00, extra=patch_replay(0))),
    "rec_ret_exitC": (sc_ret_exit(C), ret_state(5, 4, 4, 4, 5, 5, 5, 5)),
}
# the time attack page requests: every combination of "all" (4) entries in the lower levels (sort_a / sort_b / sort_c / the merges)
for _i, _c in enumerate(((5, 4, 4), (5, 4, 5), (5, 5, 4), (4, 4, 5), (4, 5, 4), (4, 4, 4), (4, 5, 5), (6, 4, 4), (7, 5, 4))):
    RET["rec_ret_q%d" % _i] = (sc_ret_short, ret_state(5, 4, 4, 4 + (_i % 3), _c[0], 5, _c[1], _c[2], fl=0x16))
for _n, (_f, _p) in RET.items():
    SCENARIOS[_n] = _f
    START_AT[_n] = 0x0602CC74
    PATCHES[_n] = _p


# ---- the save code called directly: bup_main_save (0x0602FC40) packs the records into the image at 0x060EEC00, bup_replay_save (0x0602FCB4) stores a faster time attack run ----
def bup_patches(save_mode, dev=0, lists=True):
    return [w8(0x06042A32, dev), w8(0x06042A33, save_mode)] + (patch_lists() if lists else [])


def rp_state(mode, course, new_time, old_time, save_mode):
    base = 0x002C0000 + course * 0x3D00
    p = [w16(0x060AA860, mode), w16(0x060AA87E, course), w8(0x06042A33, save_mode), w8(0x060AA80A, 3), w8(0x060AA811, 1), w8(0x060AA809, 2),
         (0x060B058C, struct.pack(">IIII", new_time, 0x11111111, 0x22222222, 0x33333333)),
         (0x060AA817, bytes([5, 6, 7, 8, 9])),
         (0x060AA400, struct.pack(">18I", *[0x01000000 + 0x10101 * k for k in range(18)])),
         (0x002CF800, bytes(((k * 7 + 3) & 0xFF) for k in range(256))),
         (0x002CF800 + 0x3D00 - 104 - 16, bytes([0xAB] * 16))]
    if old_time:
        p += [(base, struct.pack(">I", 1)), (base + 4, struct.pack(">I", old_time)), (base + 20, bytes([1, 1, 1])), (base + 104, bytes([0xEE] * 64))]
    return p


BUP = {
    "rec_bup_main": bup_patches(1),
    "rec_bup_main2": bup_patches(2, dev=1),
    "rec_bup_main0": bup_patches(0),
    "rec_bup_rp_a": rp_state(4, 2, 0x00012345, 0x00020000, 2),
    "rec_bup_rp_b": rp_state(4, 0, 0x00010101, 0x00030000, 1),
    "rec_bup_rp_c": rp_state(4, 3, 0x00040000, 0x00030000, 2),
    "rec_bup_rp_d": rp_state(4, 1, 0x00012345, 0, 2),
    "rec_bup_rp_e": rp_state(0, 1, 0x00012345, 0x00050000, 2),
}
for _n, _p in BUP.items():
    SCENARIOS[_n] = sc_one_pass
    START_AT[_n] = 0x0602FC40 if "main" in _n else 0x0602FCB4
    PATCHES[_n] = _p


# ---- the title demo's record header slide: the init once (START_AT), then the main loop runs adv_recordact every pass ----
def sc_adv_rec(d):
    d.run(600)


SCENARIOS["rec_adv"] = sc_adv_rec
START_AT["rec_adv"] = 0x0602E5F0
PATCHES["rec_adv"] = [(0x060A001C, struct.pack(">I", 0x0602E638))]
SCENARIOS["rec_adv2"] = sc_adv_rec
START_AT["rec_adv2"] = 0x0602E5F0
PATCHES["rec_adv2"] = [(0x060A001C, struct.pack(">I", 0x0602E638)), w8(0x060AAAC6, 0xFE), w16(0x25E7FE08, 0x0123), w16(0x25E7FE24, 0xFF00)]


# ---- systematic tours: every level cycled up and down, every level entered and left, with each unlock combination ----
CYCLE = "DDDDUUUUDU"
FULL = ("DU" + "R" + "DUD" + "U" + "R" + "DDUUD" + "R" + CYCLE + "LL" + "R" + "D" + "R" + CYCLE + "LL" + "L"          # arcade: page type, course, ranking, car list
        + "D" + "R" + CYCLE + "R" + CYCLE + "R" + CYCLE + "R" + CYCLE + "LLLL" + "L" + "DU")                          # time attack: course, car, set-up, transmission


def mk_full(order):
    def sc(d):
        enter_records(d)
        d.run(60)
        keys(d, order, wait=26)
        d.run(60)
    return sc


for _i, _fl in enumerate((0x00, 0x16, 0x02, 0x04, 0x10, 0x12, 0x14)):
    _n = "rec_full%d" % _i
    SCENARIOS[_n] = mk_full(FULL)
    START_AT[_n] = 0x06022894
    PATCHES[_n] = flags(_fl) + patch_lists() + patch_replay(0)
