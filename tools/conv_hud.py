"""M5 (HUD): character data and palettes of the race HUD -> extracted/psp/course/hud.dat (read by src/hud.c).

    python tools/conv_hud.py

What the VDP2 text layers NBG2 / NBG3 draw from (docs/hud.md): 8x8 cells of 256-colour dots in VDP2 VRAM 0x18000 .. 0x24000 (CGDATAUS.DAT, loaded by the menus before the race) and the
palettes the display set-up copies into colour RAM (cl_* tables of PRGDATA.DAT).  The cells are arranged as a 256 x 192 T8 atlas (32 x 24 cells, cell k = (VRAM address - 0x18000) / 64), the
palettes as 8 x 256 RGBA5551 entries (dot 0 of every palette is transparent: the layers show the picture behind).

HUD1 file (little endian):  "HUD1", u32 width (256), u32 height (192), u8 atlas[width * height], u16 clut[8 * 256]
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import hud  # noqa: E402
from saturn_env import Race  # noqa: E402

CG0, CGN = 0x18000, 0xC000
OUT = os.path.join(ROOT, "extracted", "psp", "course")


def main():
    r = Race(0, game_type=0)
    hud.setup(r)
    m = r.mem
    cg = bytes(m.read(0x25E00000 + CG0, CGN))
    atlas = bytearray(256 * 192)
    for k in range(CGN // 64):
        x0, y0 = (k % 32) * 8, (k // 32) * 8
        for row in range(8):
            atlas[(y0 + row) * 256 + x0:(y0 + row) * 256 + x0 + 8] = cg[64 * k + 8 * row:64 * k + 8 * row + 8]
    clut = []
    for p in range(8):
        for i in range(256):
            w = m.r16(0x060B8000 + 2 * (256 * p + i))
            clut.append(0 if i == 0 else ((w & 0x7FFF) | 0x8000))
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "hud.dat")
    with open(path, "wb") as f:
        f.write(b"HUD1" + struct.pack("<II", 256, 192) + bytes(atlas) + struct.pack("<%dH" % len(clut), *clut))
    used = sorted(set(c for c in cg if c))
    print("%s: %d bytes, %d cells, %d distinct dot values, palettes with colours: %s" % (
        path, os.path.getsize(path), CGN // 64, len(used), [p for p in range(8) if any(clut[256 * p + i] for i in range(1, 256))]))


if __name__ == "__main__":
    main()
