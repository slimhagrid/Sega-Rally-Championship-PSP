"""M8 (menus): the PSP controller picture of the option KEY CONFIG page: art/psp_pad.png (640 x 400, magenta = transparent, five times the size of the 128 x 80 dots of the
Saturn pad picture it replaces) -> `extracted/psp/menu/psp_pad.bin`:  "PAD1", u16 width, u16 height, then width * height RGB bytes (255, 0, 255 = transparent).
The host (src/menu_pspfix.c) maps the colours to the nearest entries of the palette the original picture uses.

    python tools/conv_pspad.py
"""
import os
import struct
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H = 128, 80


def main():
    im = Image.open(os.path.join(ROOT, "art", "psp_pad.png")).convert("RGB")
    sx, sy = im.width // W, im.height // H
    out = bytearray()
    for y in range(H):
        for x in range(W):
            r, g, b = im.getpixel((x * sx + sx // 2, y * sy + sy // 2))
            out += bytes((255, 0, 255) if (r > 200 and b > 200 and g < 90) else (r, g, b))
    d = os.path.join(ROOT, "extracted", "psp", "menu")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "psp_pad.bin"), "wb") as f:
        f.write(b"PAD1" + struct.pack("<HH", W, H) + out)
    print("psp_pad.bin", len(out) + 8, "bytes")


if __name__ == "__main__":
    main()
