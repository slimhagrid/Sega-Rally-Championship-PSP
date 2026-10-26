"""which VDP2 scroll layer draws the pixels of a screen rectangle (from a render_mtr dump <prefix>.vram/.reg/.cram): tools/pspfix_layers.py prefix x0 y0 x1 y1"""
import struct, sys

def u16(b, o): return struct.unpack('>H', b[o:o + 2])[0]
def u32(b, o): return struct.unpack('>I', b[o:o + 4])[0]

class Layers:
    def __init__(self, prefix):
        self.v = open(prefix + '.vram', 'rb').read()
        self.r = open(prefix + '.reg', 'rb').read()
        self.cram = open(prefix + '.cram', 'rb').read()
        R = lambda o: u16(self.r, o)
        chctla, chctlb, plsz, mpofn = R(0x28), R(0x2A), R(0x3A), R(0x3C)
        self.L = []
        for n in range(4):
            ctl = [chctla, chctla >> 8, chctlb, chctlb >> 4][n]
            l = dict(n=n, on=(R(0x20) >> n) & 1, chsz=2 if ctl & 1 else 1)
            cn = (ctl >> 4) & 7 if n < 2 else (2 if ctl & 2 else 0)
            if n < 2: l['colors'] = {0: 16, 1: 256, 2: 2048, 3: 32768}.get(cn, 0)
            else: l['colors'] = 256 if ctl & 2 else 16
            l['bitmap'] = (ctl >> 1) & 1 if n < 2 else 0
            pnc = R(0x30 + 2 * n)
            l['onew'], l['cnsm'], l['sup'] = (pnc >> 15) & 1, (pnc >> 14) & 1, pnc & 0x3FF
            sz = (plsz >> (2 * n)) & 3
            l['pw'] = 1 if sz == 0 else 2
            l['ph'] = 2 if sz == 3 else 1
            off = (mpofn >> (4 * n)) & 7
            l['plane'] = []
            for k in range(4):
                w = R(0x40 + 4 * n + 2 * (k >> 1))
                m = ((w >> 8) if (k & 1) else w) & 0x3F
                l['plane'].append((m | (off << 6)) * 0x2000 * (1 if l['onew'] else 2) * l['pw'] * l['ph'])
            sc = [(0x70, 0x74), (0x80, 0x84), (0x90, 0x92), (0x94, 0x96)][n]
            l['scx'] = (R(sc[0]) & 0x7FF); l['scy'] = (R(sc[1]) & 0x7FF)
            self.L.append(l)

    def cell(self, n, x, y):
        """(pn address, char, pal, pixel index) of the layer's dot at screen (x, y)"""
        l = self.L[n]
        if not l['on'] or l['bitmap'] or l['chsz'] != 1: return None
        ax, ay = (x + l['scx']) & 1023, (y + l['scy']) & 1023
        pl = ((ay >> 9) << 1) + (ax >> 9)
        ix, iy = ax & 511, ay & 511
        cx, cy = ix >> 3, iy >> 3
        a = l['plane'][pl] + ((cy * 64 + cx) * (2 if l['onew'] else 4))
        if l['onew']:
            pn = u16(self.v, a)
            ch = (pn & 0xFFF) | ((l['sup'] & 0x1C) << 10) if l['cnsm'] else (pn & 0x3FF) | ((l['sup'] & 0x1F) << 10)
        else:
            pn = u32(self.v, a); ch = pn & 0x7FFF
        base = ch * 0x20
        px, py = ix & 7, iy & 7
        if l['colors'] == 256: d = self.v[(base + py * 8 + px) & 0x7FFFF]
        elif l['colors'] == 16:
            b = self.v[(base + py * 4 + (px >> 1)) & 0x7FFFF]; d = (b & 15) if px & 1 else b >> 4
        else: d = None
        return a, ch, pn, d

if __name__ == '__main__':
    L = Layers(sys.argv[1])
    x0, y0, x1, y1 = map(int, sys.argv[2:6])
    for n in range(4):
        cnt = 0; cells = set()
        for y in range(y0, y1):
            for x in range(x0, x1):
                c = L.cell(n, x, y)
                if c and c[3]: cnt += 1; cells.add(c[0])
        print('NBG%d on=%d colors=%s onew=%d sup=%x planes=%s scroll=(%d,%d): %d opaque dots, %d cell-map entries' % (n, L.L[n]['on'], L.L[n]['colors'], L.L[n]['onew'], L.L[n]['sup'], [hex(p) for p in L.L[n]['plane']], L.L[n]['scx'], L.L[n]['scy'], cnt, len(cells)))
        if cells: print('   map addr range %x..%x' % (min(cells), max(cells)))

def layer_png(L, n, x0, y0, x1, y1, path, scale=3):
    from PIL import Image
    im = Image.new('RGB', (x1 - x0, y1 - y0))
    for y in range(y0, y1):
        for x in range(x0, x1):
            c = L.cell(n, x, y)
            if c and c[3]:
                col = u16(L.cram, 2 * ((c[3] + 0) & 0x3FF) % 4096)
                im.putpixel((x - x0, y - y0), ((col & 31) << 3, ((col >> 5) & 31) << 3, ((col >> 10) & 31) << 3))
    im.resize(((x1 - x0) * scale, (y1 - y0) * scale), Image.NEAREST).save(path)

def fnv(data, h=0x811C9DC5):
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h

def box_info(L, n, x0, y0, x1, y1, show=False):
    """the cell box (map address of its top-left cell, stride, w, h) that covers the screen rectangle on layer n, its bitmap (list of rows of indexes), the hash of the bitmap and the chars shared with cells outside the box"""
    l = L.L[n]
    ax0, ay0 = (x0 + l['scx']) & 1023, (y0 + l['scy']) & 1023
    cx0, cy0 = ax0 >> 3, ay0 >> 3
    cx1, cy1 = ((x1 - 1 + l['scx']) & 1023) >> 3, ((y1 - 1 + l['scy']) & 1023) >> 3
    w, h = cx1 - cx0 + 1, cy1 - cy0 + 1
    cells = []
    bm = [[0] * (w * 8) for _ in range(h * 8)]
    for r in range(h):
        row = []
        for c in range(w):
            sx, sy = (cx0 + c) * 8 - l['scx'], (cy0 + r) * 8 - l['scy']
            info = L.cell(n, sx & 1023 if False else sx, sy)
            a = None
            # recompute the address through cell(): use the dot (sx, sy) inside the cell
            ax, ay = ((cx0 + c) * 8) & 1023, ((cy0 + r) * 8) & 1023
            pl = ((ay >> 9) << 1) + (ax >> 9)
            ix, iy = ax & 511, ay & 511
            a = l['plane'][pl] + (((iy >> 3) * 64 + (ix >> 3)) * (2 if l['onew'] else 4))
            if l['onew']:
                pn = u16(L.v, a)
                ch = (pn & 0xFFF) | ((l['sup'] & 0x1C) << 10) if l['cnsm'] else (pn & 0x3FF) | ((l['sup'] & 0x1F) << 10)
            else:
                pn = u32(L.v, a); ch = pn & 0x7FFF
            row.append((a, ch, pn))
            for y in range(8):
                for x in range(8):
                    bm[r * 8 + y][c * 8 + x] = L.v[(ch * 0x20 + y * 8 + x) & 0x7FFFF]
        cells.append(row)
    flat = bytes(i for r in bm for i in r)
    inbox = set(c[1] for r in cells for c in r)
    # chars used by cells of the whole plane(s) of this layer outside the box
    boxaddrs = set(c[0] for r in cells for c in r)
    shared = set()
    for pb in set(l['plane']):
        for off in range(0, 0x2000 * (1 if l['onew'] else 2), 2 if l['onew'] else 4):
            a = pb + off
            if a in boxaddrs: continue
            if l['onew']:
                pn = u16(L.v, a); ch = (pn & 0xFFF) | ((l['sup'] & 0x1C) << 10) if l['cnsm'] else (pn & 0x3FF) | ((l['sup'] & 0x1F) << 10)
            else:
                pn = u32(L.v, a); ch = pn & 0x7FFF
            if ch in inbox: shared.add(ch)
    return dict(addr=cells[0][0][0], stride=128 if l['onew'] else 256, w=w, h=h, bm=bm, hash=fnv(flat), shared=shared, cells=cells, pal=(cells[0][0][2]))

def show_bm(bm, x0=0, x1=None, y0=0, y1=None):
    x1 = x1 or len(bm[0]); y1 = y1 or len(bm)
    for y in range(y0, y1):
        print('%2d ' % y + ' '.join('%2s' % (bm[y][x] or '.') for x in range(x0, x1)))
