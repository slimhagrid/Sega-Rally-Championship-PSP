"""M5 (objects): the car display tables of `dispcar` -> `extracted/psp/course/cars.dat` (read by src/car.c).

    python tools/conv_cars.py

The game's car display (dispcar::dispGhost @0x06010214, FUN_06010630 @0x06010630 and the three tiers FUN_0601029e / 0x060103BE / 0x0601042C, docs/render_pipeline.md 3.5) picks a
*descriptor* {wheel offsets ptr, shadow part, tyre part list ptr, body part, brake-light-off part, brake-light-on part} from a table at 0x06010874: 14 car types x 4 words
{near normal, near damaged, far normal, far damaged}.  The descriptors, the wheel offset tables (4 x 3 longs, 16.16 metres) and the tyre lists are read from 0RALLY.DAT.
Part pointers are mapped to part numbers of `object.rcp` (conv_objects.py); parts the 1P object file does not hold (the 2P models) are -1.

CAR1 file (little endian):
    0x00 "CAR1"  u32 ntypes (14)  f32 tier thresholds[3] (112, 128, 200 m: full model below the first, `_s` body above the second, nothing beyond the third)
    0x14 f32 shadow_dy (0x51E / 65536)  f32 k_pitch (0x3CED9168 / 2^32)  f32 k_roll (0x553F7CEE / 2^32)   0x20 .. 0x40 reserved
    0x40 ntypes x 4 descriptors x 64 bytes:  i16 body, shadow, br_off, br_on, tyre[4]  (16 bytes);  f32 wheel[4][3] (48 bytes)
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import conv_objects  # noqa: E402

DISC = os.path.join(ROOT, "extracted", "disc", "files")
OUT = os.path.join(ROOT, "extracted", "psp", "course")
BASE = 0x06004000
TABLE = 0x06010874
NTYPES = 14
THRESH = 0x060101DC


def main():
    code = open(os.path.join(DISC, "0RALLY.DAT"), "rb").read()

    def u32(a):
        return struct.unpack(">I", code[a - BASE:a - BASE + 4])[0]

    def s32(a):
        return struct.unpack(">i", code[a - BASE:a - BASE + 4])[0]

    index = {a: i for i, (a, _) in enumerate(conv_objects.part_symbols())}

    def part(a):
        return index.get(a, -1) if a else -1

    out = bytearray()
    thr = [s32(THRESH + 4 * k) / 65536.0 for k in range(3)]
    shadow_dy = 0x51E / 65536.0
    kp, kr = 0x3CED9168 / 2.0 ** 32, 0x553F7CEE / 2.0 ** 32
    out += b"CAR1" + struct.pack("<I3f3f", NTYPES, *thr, shadow_dy, kp, kr)
    out += b"\x00" * (0x40 - len(out))
    unknown = set()
    for t in range(NTYPES):
        for v in range(4):
            d = u32(TABLE + 16 * t + 4 * v)
            w, kage, ty, body, bo, bn = (u32(d + 4 * k) for k in range(6))
            tyres = [u32(ty + 4 * i) for i in range(4)]
            ids = [part(body), part(kage), part(bo), part(bn)] + [part(x) for x in tyres]
            for a, p in zip([body, kage, bo, bn] + tyres, ids):
                if a and p < 0:
                    unknown.add(a)
            out += struct.pack("<8h", *ids)
            for i in range(4):
                out += struct.pack("<3f", *(s32(w + 12 * i + 4 * k) / 65536.0 for k in range(3)))
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "cars.dat")
    open(path, "wb").write(out)
    print("cars.dat: %d types x 4 descriptors, thresholds %s -> %s (%d bytes); %d part pointers outside OBJECT.MDL (2P models)" % (
        NTYPES, thr, path, len(out), len(unknown)))


if __name__ == "__main__":
    main()
