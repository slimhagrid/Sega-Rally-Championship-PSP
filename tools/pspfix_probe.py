"""probe of a picture area of VDP2 VRAM dumped by render_mtr (<out>.vram/.cram): decode an NBG cell box into an indexed bitmap (256 colours, 8x8 cells)"""
import struct, sys
from PIL import Image

def load(prefix):
    return open(prefix + '.vram', 'rb').read(), open(prefix + '.cram', 'rb').read()

def cell_box(v, plane, row, col, w, h, sup=0x004):
    """returns (bitmap rows as lists of palette indexes, list of (char, row, col))"""
    bm = [[0] * (w * 8) for _ in range(h * 8)]
    cells = []
    for r in range(h):
        for c in range(w):
            off = plane + (row + r) * 128 + (col + c) * 2
            p = struct.unpack('>H', v[off:off + 2])[0]
            ch = (p & 0x3FF) | ((sup & 0x1F) << 10)
            cells.append((ch, row + r, col + c))
            base = ch * 0x20
            for y in range(8):
                for x in range(8):
                    bm[r * 8 + y][c * 8 + x] = v[base + y * 8 + x]
    return bm, cells

def save(bm, cram, path, scale=3):
    h, w = len(bm), len(bm[0])
    im = Image.new('RGB', (w, h))
    for y in range(h):
        for x in range(w):
            i = bm[y][x]
            c = struct.unpack('>H', cram[2 * i:2 * i + 2])[0]
            im.putpixel((x, y), ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3))
    im.resize((w * scale, h * scale), Image.NEAREST).save(path)

if __name__ == '__main__':
    v, cram = load(sys.argv[1])
    bm, cells = cell_box(v, 0x3C000, 24, 2, 40, 4)
    save(bm, cram, sys.argv[2])
    from collections import Counter
    print(Counter(i for r in bm for i in r).most_common())
