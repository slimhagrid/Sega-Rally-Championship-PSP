"""M6 (driving core): the constant tables of the car physics and the race flow -> `extracted/psp/course/phys.dat` (read by src/drive.c).

    python tools/conv_phys.py

The tables are copied byte for byte with their original (big endian) contents and original addresses, and src/drive.c reads them by those addresses, so every table
access in the port can be compared with the SH-2 code (docs/physics.md 6.2).  Formulas and scalar constants are in the C code; only arrays are data.

PHY1 file (little endian header): "PHY1", u32 nregions, then per region u32 address, u32 size, followed by the region bytes (big endian, as in the console's memory).
Sources: 0RALLY.DAT (loaded at 0x06004000), PRGDATA.DAT blocks (0x060A1000: `carDyn*`, `sincos_tbl`; 0x00298000: `pinit_tbl`, goal poses, time-extension tables, AI guide lines).
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from datfile import Scatter  # noqa: E402

DISC = os.path.join(ROOT, "extracted", "disc", "files")
OUT = os.path.join(ROOT, "extracted", "psp", "course")

# (name, first address, end address)
REGIONS = [
    ("corner points", 0x06007674, 0x060076A4),               # FUN_0600760C: body corners in the car frame
    ("wheel offsets", 0x06007924, 0x06007954),               # FUN_06007718: wheel positions (track units)
    ("w_valtobit_tbl", 0x06008544, 0x06008568),
    ("gear ratios A, B", 0x06008DA4, 0x06008DD4),
    ("torque curves AT, MT", 0x06008DD4, 0x060090FC),
    ("digital ramps", 0x06009914, 0x06009938),
    ("tuning tables, car dyn pointers, special car dyn", 0x06009A98, 0x06009E8C),
    ("lap/time code tables", 0x06014000, 0x06015500),         # the jump tables of the lap code (read with pm_r32 like the original), literal pools stay in the code
    ("car dyn tables", 0x060A1490, 0x060A15A0),              # cardyntable, carDynTarmac / Gravel / Water / Special
    ("sincos_tbl", 0x060A15A0, 0x060A55A0),
    ("PRGDATA tables: HUD digits, BCD time tables", 0x060A1000, 0x060A1490),
    ("HUD tables: lap time rows, navigator messages, ...", 0x00298000, 0x00299300),
    ("spray: surface -> routine words, wheel offsets", 0x0601D328, 0x0601D410),
    ("pinit, goal poses, time extension tables, AI guide lines", 0x00299300, 0x0029EAA0),
    ("replay: window rows of replay_display", 0x06013B6C, 0x06013B94),
    ("other cars: lane widths", 0x0600A354, 0x0600A374),
    ("other cars: wheel offsets, speed scale table", 0x0600B824, 0x0600B874),
    ("other cars: body corners", 0x0600BC38, 0x0600BC68),
    ("other cars: first-frame inertia constants", 0x0600CAB8, 0x0600CAD0),
    ("other cars: LOD table, rank table, spawn tables (otherdat), acceleration tables", 0x0600CC30, 0x0600D210),
]


def main():
    rally = open(os.path.join(DISC, "0RALLY.DAT"), "rb").read()
    mem = {}                                                  # address -> bytes of the block that covers it
    prg = Scatter(os.path.join(DISC, "PRGDATA.DAT"))
    blocks = [(b.dest, prg.expanded(b)) for b in prg.blocks]

    def read(a, n):
        if 0x06004000 <= a < 0x06004000 + len(rally):
            return rally[a - 0x06004000:a - 0x06004000 + n]
        for base, data in blocks:
            if base <= a and a + n <= base + len(data):
                return data[a - base:a - base + n]
        raise SystemExit("no source for %08x .. %08x" % (a, a + n))

    out = bytearray(b"PHY1" + struct.pack("<I", len(REGIONS)))
    body = bytearray()
    for name, a0, a1 in REGIONS:
        data = read(a0, a1 - a0)
        out += struct.pack("<II", a0, a1 - a0)
        body += data
        print("%-50s %08x .. %08x %6d bytes" % (name, a0, a1, a1 - a0))
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "phys.dat")
    with open(path, "wb") as f:
        f.write(out + body)
    print("-> %s (%d bytes)" % (path, len(out) + len(body)))


if __name__ == "__main__":
    main()
