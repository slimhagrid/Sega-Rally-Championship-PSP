"""M5 (event objects): EVENTTBL.MAD -> `extracted/psp/course/events.dat` (read by src/event.c).

    python tools/conv_events.py

EVENTTBL.MAD block 0 (loaded at 0x00200000) = 8 pointers (4 courses x 1P, then 4 x 2P) -> per course 128 block pointers -> per block {u32 count; count x 24-byte records}
(FUN_0601BF18 copies them into 0x40-byte slots, docs: NOTES.md M5): record = u16 w0, u16 type, i32 L1 (-> slot+0x10), i32 L2 (-> +0x24, an angle), i32 x, y, z (-> +0x14, +0x18, +0x1C; 16.16 metres).
The slot of an object is its running number over all blocks in block order (offset = 0x40 * number; the parity flag word of the slot is (number + 1) & 1 for the master / slave split).

EVT1 file (little endian):
    "EVT1"  u32 ncourses (4, the 1P tables)
    per course: u32 nobjects, u32 offset of the block table (128 x {u32 first, u32 count}), u32 offset of the records
    records: u32 type, u32 w0, i32 L1, i32 L2, i32 x, i32 y, i32 z   (28 bytes)
    u32 offset of the MOT section (at file offset 8 + 12 * ncourses), the section: u32 ntables, per table { u32 N frames, u32 nparts, u32 data offset (in the motion block), i16 part[nparts] (object.rcp
    numbers, padded to 4) }, u32 motion block size, the motion block (EVENTTBL.MAD block 1 = ANIMAL.MOT, loaded at 0x002B0000: per frame and part 20 bytes: translation 3 x i32, rotation z, y, x as i16
    "turns" (65536 = one turn), 2 bytes pad).  The tables are the keyframe animations the animal objects pass to FUN_0601CE8C (bird, elephant, zebra).  After the motion block: 2 x 32 i16 = the flag animation tables (part numbers in animation order).
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from datfile import Scatter  # noqa: E402
import conv_objects  # noqa: E402

DISC = os.path.join(ROOT, "extracted", "disc", "files")
OUT = os.path.join(ROOT, "extracted", "psp", "course")
MOTION_TABLES = (0x0601CDA8, 0x0601CDB4, 0x0601CDC0, 0x0601CDDC, 0x0601CDF8, 0x0601CE64, 0x0601CE78)      # bird1 body 21, bird1 body 32, bird1 wings, bird2, elephant, zebra body, zebra tail


def main():
    s = Scatter(os.path.join(DISC, "EVENTTBL.MAD"))
    d = s.data(s.blocks[0])
    base = s.blocks[0].dest

    def u32(a):
        return struct.unpack(">I", d[a - base:a - base + 4])[0]

    def u16(a):
        return struct.unpack(">H", d[a - base:a - base + 2])[0]

    def s32(a):
        return struct.unpack(">i", d[a - base:a - base + 4])[0]

    courses = []
    for c in range(4):
        tbl = u32(base + 4 * c)
        blocks, recs = [], []
        for b in range(128):
            p = u32(tbl + 4 * b)
            n = u32(p)
            blocks.append((len(recs), n))
            a = p + 4
            for _ in range(n):
                recs.append((u16(a + 2), u16(a), s32(a + 4), s32(a + 8), s32(a + 12), s32(a + 16), s32(a + 20)))
                a += 24
        courses.append((blocks, recs))
    out = bytearray(b"EVT1" + struct.pack("<I", 4))
    head = len(out) + 12 * 4 + 4
    body = bytearray()
    pos = head
    entries = []
    for blocks, recs in courses:
        bt = b"".join(struct.pack("<II", f, n) for f, n in blocks)
        rb = b"".join(struct.pack("<II5i", *r) for r in recs)
        entries.append((len(recs), pos, pos + len(bt)))
        body += bt + rb
        pos += len(bt) + len(rb)
    for e in entries:
        out += struct.pack("<III", *e)
    # motion tables: the descriptors the animal objects use (0x0601CDA8 ... 0x0601CE78 in 0RALLY.DAT), parts by address -> object.rcp numbers
    code = open(os.path.join(DISC, "0RALLY.DAT"), "rb").read()
    index = {a: i for i, (a, _) in enumerate(conv_objects.part_symbols() + conv_objects.animal_symbols())}
    tabs = bytearray()
    for ta in MOTION_TABLES:
        o = ta - 0x06004000
        n, k, ptr = struct.unpack(">HHI", code[o:o + 8])
        assert (ptr & 0xFFFFFF) >= 0x2B0000
        parts = [index[struct.unpack(">I", code[o + 8 + 4 * i:o + 12 + 4 * i])[0]] for i in range(k)]
        tabs += struct.pack("<III", n, k, (ptr & 0xFFFFFF) - 0x2B0000) + struct.pack("<%dh" % k, *parts) + bytes(-2 * k % 4)
    mot = s.data(s.blocks[1])
    assert s.blocks[1].dest == 0x2B0000
    mdl, _, _, _ = conv_objects.load_objects()
    flags = []
    for ta in (0x060F5EE0, 0x060F5F60):                  # FlagAnimation1 / 2: 32 pointers to the flag parts in animation order
        for i in range(32):
            ptr = struct.unpack(">I", mdl[ta - 0x060F0000 + 4 * i:ta - 0x060F0000 + 4 * i + 4])[0]
            flags.append(index[ptr])
    motion = struct.pack("<I", len(MOTION_TABLES)) + bytes(tabs) + struct.pack("<I", len(mot)) + mot + struct.pack("<64h", *flags)
    out += struct.pack("<I", pos)
    body += motion
    out += body
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "events.dat")
    open(path, "wb").write(out)
    print("events.dat: objects per course %s -> %s (%d bytes)" % ([len(r) for _, r in courses], path, len(out)))


if __name__ == "__main__":
    main()
