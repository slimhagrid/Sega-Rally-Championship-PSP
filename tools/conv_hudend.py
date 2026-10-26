"""M6 (HUD, end of the race): character data of the goal / game-over screens (VDP2 NBG0) -> extracted/psp/course/hude_<cr|pr>_<stage>.dat and hude_f.dat (time attack, CGDATA_F.DAT) (read by src/hud.c).

    python tools/conv_hudend.py

NBG0 draws from 8x8 cells of 256-colour dots in VDP2 VRAM 0x00000 .. 0x18000: the result-screen files CR<STAGE>.DAT (arcade game) / PR<STAGE>.DAT (practice) hold the character blocks
(0x00040 .. 0x162C0; the part from 0x77C0 on differs per stage and game; the block at 0x10440 is the same in GMGOVER.DAT, which the game-over path loads).  The palettes are the ones of hud.dat
(cl_* tables, tools/conv_hud.py): the cells say palette 0x10 = palette 1.  Name-table words (src/hudgen.c) do not depend on any of this.
The cells are arranged as a 256 x 384 T8 atlas (32 x 48 cells, cell k = VRAM address / 64).

HUDE1 file (little endian):  "HUDE", u32 width (256), u32 height (384), u8 atlas[width * height]
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
STAGES = ("DESERT", "FOREST", "MOUNT", "LAKE")
VRAM_BASE, VRAM_BYTES = 0x25E00000, 0x18000


def vram_of(fname):
    """the character region as the loader leaves it"""
    s = Scatter(os.path.join(DISC, fname))
    mem = bytearray(VRAM_BYTES)
    for b in s.blocks:
        if VRAM_BASE <= b.dest < VRAM_BASE + VRAM_BYTES:
            d = s.expanded(b)
            o = b.dest - VRAM_BASE
            mem[o:o + len(d)] = d[:VRAM_BYTES - o]
    return bytes(mem)


def atlas_of(mem):
    ncell = VRAM_BYTES // 64
    atlas = bytearray(256 * 384)
    for k in range(ncell):
        x0, y0 = (k % 32) * 8, (k // 32) * 8
        for row in range(8):
            atlas[(y0 + row) * 256 + x0:(y0 + row) * 256 + x0 + 8] = mem[64 * k + 8 * row:64 * k + 8 * row + 8]
    return bytes(atlas)


def main():
    os.makedirs(OUT, exist_ok=True)
    for pre in ("CR", "PR"):
        for st in STAGES:
            mem = vram_of("%s%s.DAT" % (pre, st))
            path = os.path.join(OUT, "hude_%s_%s.dat" % (pre.lower(), st.lower()))
            with open(path, "wb") as f:
                f.write(b"HUDE" + struct.pack("<II", 256, 384) + atlas_of(mem))
            print("%s: %d cells used" % (path, sum(1 for k in range(VRAM_BYTES // 64) if any(mem[64 * k:64 * k + 64]))))
    mem = vram_of("CGDATA_F.DAT")                                                       # game types 4 / 6 (time attack) and 8: the best time box and the FINISH picture (0x060044C4 .. 0x06004514)
    path = os.path.join(OUT, "hude_f.dat")
    with open(path, "wb") as f:
        f.write(b"HUDE" + struct.pack("<II", 256, 384) + atlas_of(mem))
    print("%s: %d cells used" % (path, sum(1 for k in range(VRAM_BYTES // 64) if any(mem[64 * k:64 * k + 64]))))


if __name__ == "__main__":
    main()
