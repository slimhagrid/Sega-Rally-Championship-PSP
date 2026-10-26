"""M5 (event objects): test vectors for src/event.c - what the game's own object dispatcher draws, pass after pass.

    python tools/evvec.py [course] [game_type] [passes] [--tour] [--laps=N]      -> extracted/oracle/ev_<course>_<type>.vec (_heli with --laps)

The oracle (pilot, chase camera) runs a race; the race start loads EVENTTBL.MAD and builds the slots by itself (bit 4 of [0x060AA803]).  After every pass the dispatcher
(FUN_0601BCB8, visible block list 0x060AA140) runs - slave half first, the job runs synchronously - with the draw functions replaced by recorders.  The vector holds, per pass,
the inputs of the object code (random seed 0x060D0050, camera matrix, the car fields and globals it reads, block list) and the draws in order: slot number, kind, part, matrix
(or the view-space position and mirror scale of a billboard).  tools/test_event.c replays them through src/event.c (state carries over from pass to pass, like in the game).

M9 (sound): the vector is "EVE2" now: after the draws of every pass the sound inputs / outputs of the objects follow (the helicopter's pan word, its start sound, the start object's push and
hit sound): i32 view_fx[12] (the camera matrix as the game holds it), i32 mode810, i32 car_state (+0x0A word), i32 push_dx, push_dz (what the dispatch added to the car's +0x64 / +0x6C),
u32 nsnd, nsnd x { u32 fn (0x06016FBC play / 0x0601704C word), u32 r4 }.  --startobj appends synthetic passes with the car put around the start object's box (desert, arcade).

Vector (little endian): "EVE2" (the older "EVEC" files lack the sound part), u32 nframes, u32 course, u32 game_type, then per pass
    u32 rng; f32 view[12]; i32 car_byte_c5, car_seg, car_c8, car_ca, car_x, car_y, car_z, car_yaw, nseg_word; u32 nblocks; u8 blocks[nblocks] padded to 4;
    u32 ndraws; ndraws x { i32 slot; u32 kind (0 poly, 1 clip, 2 people, 3 splash, 4 color); i32 part; i32 r6; f32 a[12] }
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import oracle  # noqa: E402
import conv_objects  # noqa: E402
from saturn_env import Race, VIEW0, CAR0  # noqa: E402
from sh2 import s32  # noqa: E402

# initial state functions of the rarer object types (0x0601BFEC table): type -> address
RARE = {5: 0x0601CC2C, 10: 0x0601CCAC, 20: 0x0601C470, 16: 0x0601C06C, 19: 0x0601C370, 9: 0x0601CB14, 18: 0x0601C2F4, 17: 0x0601C2A0}
DRAWS = {0x06018908: 0, 0x06018A10: 1, 0x06019870: 2, 0x06019936: 3, 0x0601978C: 4}
OUT = os.path.join(ROOT, "extracted", "oracle")


def main():
    a = sys.argv[1:]
    opts = [x for x in a if x.startswith("--")]
    a = [x for x in a if not x.startswith("--")]
    course = int(a[0]) if a else 0
    gt = int(a[1]) if len(a) > 1 else 0
    npass = int(a[2]) if len(a) > 2 else 900
    part_index = {ad: i for i, (ad, _) in enumerate(conv_objects.part_symbols() + conv_objects.animal_symbols())}
    start_blocks = None
    r = Race(course, game_type=gt, render=True, rec_buf=0x060FD400, real_rand=True)
    r.init_render()
    r.load_game_textures()
    m = r.mem
    m.w8(0x060AA803, m.r8(0x060AA803) | 0x10)
    m.w8(0x060AA01B, 2)
    log = []
    heli = [x for x in opts if x.startswith("--laps=")]
    if heli:
        m.w16(0x060AA006, int(heli[0].split("=")[1]))           # a one-lap race: the helicopter (desert, last lap, slice 0xD6) shows up after about 1,800 passes

    def rec(c):
        kind = DRAWS[c.pc]
        part = c.r[5]
        e = [kind, (c.gbr - 0x060E4000) // 0x40, part_index.get(part, -1), s32(c.r[6])]
        if kind in (2, 3):
            e.append([s32(m.r32(c.r[4] + o)) / 65536.0 for o in (0x0C, 0x1C, 0x2C)] + [0.0] * 9)
        else:
            e.append([s32(m.r32(c.r[4] + 4 * k)) / 65536.0 for k in range(12)])
        log.append(e)
        c.ret()
    for ad in DRAWS:
        r.cpu.hooks[ad] = rec
    r.snd_trace()                                              # the sound library's entry points log their calls (the dispatch's own are collected per pass)
    drv = oracle.make_driver("drive", r)
    out = bytearray()
    frames = 0

    def record(blocks):
        """dispatch once with the given block list and append the frame"""
        nonlocal out, frames
        log.clear()
        rng = m.r32(0x060D0050)
        nb = len(blocks)
        for k, bv in enumerate(blocks):
            m.w8(0x060AA140 + k, bv)
        m.w16(0x060AA002, nb)
        r.snd_events = []
        push0 = (m.r32(CAR0 + 0x64), m.r32(CAR0 + 0x6C))
        r.run_master(0x0601BCB8, r4=0x060AA140, r5=nb)
        snd = [(a, r4) for a, r4, _, _ in r.snd_events]
        r.snd_events = []
        out += struct.pack("<I12f", rng, *[s32(m.r32(VIEW0 + 4 * k)) / 65536.0 for k in range(12)])
        out += struct.pack("<9i", m.r8(0x060B00C5), m.r16(0x060B0060), m.r16(0x060B00C8), m.r16(0x060B00CA), s32(m.r32(0x060B0018)), s32(m.r32(0x060B001C)),
                           s32(m.r32(0x060B0020)), s32(m.r32(0x060B0010)), m.r16(0x060AA006))
        out += struct.pack("<I", nb) + bytes(blocks) + bytes(-nb % 4)
        out += struct.pack("<I", len(log))
        for kind, slot, part, r6, arr in log:
            out += struct.pack("<iIii12f", slot, kind, part, r6, *arr)
        out += struct.pack("<12I", *[m.r32(VIEW0 + 4 * k) for k in range(12)])
        out += struct.pack("<3i", m.r8(0x060AA810), m.r16(CAR0 + 0x0A), ((m.r32(CAR0 + 0x64) - push0[0] + 0x80000000) & 0xFFFFFFFF) - 0x80000000)
        out += struct.pack("<i", ((m.r32(CAR0 + 0x6C) - push0[1] + 0x80000000) & 0xFFFFFFFF) - 0x80000000)
        out += struct.pack("<I", len(snd)) + b"".join(struct.pack("<II", a, r4) for a, r4 in snd)
        frames += 1

    if "--startobj" in opts:
        npass = 4                                          # the start object works while the car is near the start line (the gates of the lap's end switch it off: flag 0x060D0102): a short run
    for i in range(npass):
        cmd = drv.next(r, i)
        if cmd is None:
            break
        r.step(*cmd)
        if i == 0:
            continue                                   # slots are built during pass 0
        start_blocks = bytes(m.r8(0x060AA140 + k) for k in range(m.r16(0x060AA002)))
        record(start_blocks)
    if "--startobj" in opts:
        # the start object's box (desert start, x -369 +- 3 m, z -296 +- 32.5 m): the car is put at positions around it and every position is dispatched 18 times (the hit sound plays when the 15 pass
        # counter has run out, then every 15 passes); with the car's state word 0 (on the grid) and in the modes that have the sounds off
        assert start_blocks, "the start object was never drawn"
        for variant in range(3):
            for dx in (-9.0, -5.0, -3.5, -3.0, -2.5, -1.0, 0.0, 1.0, 2.5, 3.0, 3.5, 5.0, 9.0):
                for dz in (-45.0, -34.0, -32.0, -20.0, -5.0, 0.0, 12.0, 31.0, 33.0, 40.0):
                    if variant == 1 and (dx not in (-5.0, 5.0) or dz not in (0.0, 12.0)):
                        continue
                    if variant == 2 and (dx not in (-5.0, 3.5) or dz not in (-20.0, 31.0)):
                        continue
                    m.w32(CAR0 + 0x18, int(round((-369.0 + dx) * 65536.0)) & 0xFFFFFFFF)
                    m.w32(CAR0 + 0x20, int(round((-296.0 + dz) * 65536.0)) & 0xFFFFFFFF)
                    m.w16(CAR0 + 0x0A, 0 if variant == 1 else 2)
                    m.w8(0x060AA810, 2 if variant == 2 else 0)
                    for _ in range(18):
                        record(start_blocks)
        m.w8(0x060AA810, 0)
    if "--tour" in opts:
        # the rarer object types never come into view along the pilot's line: point a synthetic camera at each of them (the vector holds the camera and the block list, so the replay is exact)
        recs = {}
        for b in range(128):
            off, cnt = m.r16(0x060D0C00 + 4 * b), m.r16(0x060D0C02 + 4 * b)
            for k in range(cnt):
                recs[off // 0x40 + k] = b
        want = {5, 10, 20, 16, 19, 2, 22, 6}
        base = [s32(m.r32(VIEW0 + 4 * k)) for k in range(12)]
        rot = [[base[4 * i + j] / 65536.0 for j in range(3)] for i in range(3)]
        done = {}
        for slot in sorted(recs):
            S = 0x060E4000 + 0x40 * slot
            fn = m.r32(S)
            # type of the object is known from its initial state function only through the table: use the slot's parameter words instead -> tour every slot whose state is a known rare one
            typ = None
            for t, addr in RARE.items():
                if fn == addr or m.r32(S) == addr:
                    typ = t
            if typ is None or done.get(typ, 0) >= 6:
                continue
            done[typ] = done.get(typ, 0) + 1
            for fr in range(36):
                pos = [s32(m.r32(S + o)) / 65536.0 for o in (0x14, 0x18, 0x1C)]
                tr = [-sum(rot[i][j] * pos[j] for j in range(3)) for i in range(3)]
                tr = [tr[0], tr[1] - 1.0, tr[2] + 14.0]
                for i in range(3):
                    for j in range(3):
                        m.w32(VIEW0 + 4 * (4 * i + j), int(round(rot[i][j] * 65536.0)) & 0xFFFFFFFF)
                    m.w32(VIEW0 + 4 * (4 * i + 3), int(round(tr[i] * 65536.0)) & 0xFFFFFFFF)
                for k in range(12):                              # the slave half works on its own copy of the camera matrix (0x060D1800)
                    m.w32(0x060D1800 + 4 * k, m.r32(VIEW0 + 4 * k))
                record(bytes([recs[slot]]))
    path = os.path.join(OUT, "ev_%d_%d%s.vec" % (course, gt, "_startobj" if "--startobj" in opts else ("_heli" if heli else "")))
    with open(path, "wb") as fo:
        fo.write(b"EVE2" + struct.pack("<III", frames, course, gt) + out)
    print("%s: %d passes, %d bytes" % (path, frames, os.path.getsize(path)))


if __name__ == "__main__":
    main()
