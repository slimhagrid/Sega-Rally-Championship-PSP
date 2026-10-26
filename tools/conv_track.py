"""M3: convert the track / collision tables of one course (`BG<course>.DAT`, six plain blocks in Low WRAM) to `extracted/psp/course/<name>.rct`.

    python tools/conv_track.py [DESERT ...]          (default: all four) ; --check = self test only

Layouts: docs/collision_world_ai.md section 1 (verified there by the game code and by centroid look-ups):
  0x00240000 (0x4000)  collision-area grid: 64 x 64 u32 pointers (0 = empty) to lists `[u16 count][u16 poly index * count]` inside the 0x248000 block
  0x00244000 (0x4000)  block grid: 64 x 64 u32 pointers to lists `[u8 count][u8 quad index * count]` inside the 0x290000 block
  0x00248000           collision polygons, stride 0x34: u32 flags (type << 16 | triangle bit 0), 4 x (x, z) s32 in "11.20" (units * 2^20), normal (nx, ny, nz) s32 8.24,
                       s32 d (floor: plane constant * 2^22; wall: edge length * 2^17); followed by the lists the 0x240000 grid points to
  0x00290000           block quads: 128 x 0x24: u8 A, B, A2, block id, 4 x (x, z) s32 16.16; followed by the byte lists of the block grid
  0x00293000           slices: 256 x 0x2c: u8 attr, section, 0, slice id, 4 x (x, z) s32 16.16, unit road direction (x, z) s32 16.16
  0x00296000           block -> slice list table: 128 u32 pointers to byte lists `[u8 count][u8 slice * count]` that follow the 256 slice records in the 0x293000 block
Cells cover x, z in [-1024, 1024) units, 32 units per cell, row 0 = z +1024 (docs 1.2).

RCT1 output (little endian, offsets from the file start):
  "RCT1", u32 npoly, u32 nquad, u32 nslice, u32 off_poly, u32 off_quad, u32 off_slice, u32 off_cgrid, u32 off_bgrid, u32 off_blk2slice, u32 off_pool, u32 pool_bytes
  poly   (52 bytes): u32 flags, 4 x (s32 x, s32 z), s32 nx, ny, nz, s32 d
  quad   (36 bytes): u8 a, b, a2, id; 4 x (s32 x, s32 z)
  slice  (44 bytes): u8 attr, section, zero, id; 4 x (s32 x, s32 z), s32 dirx, dirz
  cgrid / bgrid: 4096 x u32 = byte offset into the pool of the list (0xFFFFFFFF = empty); pool lists keep the original encodings (u16 count + u16 items / u8 count + u8 items)
  blk2slice: 128 x u32 = pool offsets of the slice lists
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
NONE = 0xFFFFFFFF


def parse(name):
    s = Scatter(os.path.join(DISC, "BG%s.DAT" % name))
    blk = {b.dest: s.expanded(b) for b in s.blocks}
    for need in (0x240000, 0x244000, 0x248000, 0x290000, 0x293000, 0x296000):
        assert need in blk, "block %x missing in BG%s.DAT" % (need, name)
    return blk


def convert(name):
    blk = parse(name)
    cgrid_src, bgrid_src, coli, quads, slices, b2s = (blk[k] for k in (0x240000, 0x244000, 0x248000, 0x290000, 0x293000, 0x296000))
    pool = bytearray()
    cache = {}

    def add_list(src_block, base, ptr, width):
        """copy the list at LWRAM address ptr (inside src_block, loaded at base) into the pool; returns its pool offset"""
        if ptr == 0:
            return NONE
        off = ptr - base
        n = struct.unpack(">H", src_block[off:off + 2])[0] if width == 2 else src_block[off]
        end = off + (2 + 2 * n if width == 2 else 1 + n)
        key = (id(src_block), off)
        if key not in cache:
            while len(pool) % 4:
                pool.append(0)
            cache[key] = len(pool)
            if width == 2:
                pool.extend(struct.pack("<H%dH" % n, n, *struct.unpack(">%dH" % n, src_block[off + 2:end])))
            else:
                pool.extend(src_block[off:end])
        return cache[key]

    cgrid = [add_list(coli, 0x248000, struct.unpack(">I", cgrid_src[4 * i:4 * i + 4])[0], 2) for i in range(4096)]
    bgrid = [add_list(quads, 0x290000, struct.unpack(">I", bgrid_src[4 * i:4 * i + 4])[0], 1) for i in range(4096)]
    b2s_off = [add_list(slices, 0x293000, struct.unpack(">I", b2s[4 * i:4 * i + 4])[0], 1) for i in range(128)]

    # number of collision polygons: the highest index any list refers to + 1 (the polygons come first in the block)
    maxidx = 0
    for off in set(c for c in cgrid if c != NONE):
        n = struct.unpack("<H", pool[off:off + 2])[0]
        for k in range(n):
            maxidx = max(maxidx, struct.unpack("<H", pool[off + 2 + 2 * k:off + 4 + 2 * k])[0])
    npoly = maxidx + 1
    polys = bytearray()
    for i in range(npoly):
        r = coli[i * 0x34:(i + 1) * 0x34]
        flags = struct.unpack(">I", r[:4])[0]
        vals = struct.unpack(">8i4i", r[4:])
        polys += struct.pack("<I8i4i", flags, *vals)
    quadb = bytearray()
    for i in range(128):
        r = quads[i * 0x24:(i + 1) * 0x24]
        quadb += struct.pack("<4B8i", *struct.unpack(">4B8i", r))
    sliceb = bytearray()
    for i in range(256):
        r = slices[i * 0x2C:(i + 1) * 0x2C]
        sliceb += struct.pack("<4B10i", *struct.unpack(">4B10i", r))

    hdr_size = 4 + 11 * 4
    off_poly = hdr_size
    off_quad = off_poly + len(polys)
    off_slice = off_quad + len(quadb)
    off_cgrid = off_slice + len(sliceb)
    off_bgrid = off_cgrid + 4096 * 4
    off_b2s = off_bgrid + 4096 * 4
    off_pool = off_b2s + 128 * 4
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name.lower() + ".rct")
    with open(path, "wb") as f:
        f.write(b"RCT1" + struct.pack("<11I", npoly, 128, 256, off_poly, off_quad, off_slice, off_cgrid, off_bgrid, off_b2s, off_pool, len(pool)))
        f.write(polys)
        f.write(quadb)
        f.write(sliceb)
        f.write(struct.pack("<4096I", *cgrid))
        f.write(struct.pack("<4096I", *bgrid))
        f.write(struct.pack("<128I", *b2s_off))
        f.write(pool)
    walls = sum(1 for i in range(npoly) if (struct.unpack("<I", polys[i * 52:i * 52 + 4])[0] >> 16) == 0)
    print(f"{name}: {npoly} collision polygons ({walls} walls), 128 block quads, 256 slices, pool {len(pool):,} bytes -> {path} ({os.path.getsize(path):,} bytes)")
    return path


class Track:
    """Reader of an RCT1 file (used by the self test; the PSP side will read the same structures)."""

    def __init__(self, path):
        self.b = open(path, "rb").read()
        assert self.b[:4] == b"RCT1"
        (self.npoly, self.nquad, self.nslice, self.o_poly, self.o_quad, self.o_slice, self.o_cg, self.o_bg, self.o_b2s, self.o_pool,
         self.pool_bytes) = struct.unpack("<11I", self.b[4:48])

    def poly(self, i):
        v = struct.unpack("<I8i4i", self.b[self.o_poly + 52 * i:self.o_poly + 52 * i + 48 + 4])
        return v[0], [(v[1 + 2 * k] / 2**20, v[2 + 2 * k] / 2**20) for k in range(4)], v[9:12], v[12]

    def cell_list(self, grid, col, row, width):
        off = struct.unpack("<I", self.b[grid + 4 * (row * 64 + col):grid + 4 * (row * 64 + col) + 4])[0]
        if off == NONE:
            return []
        p = self.o_pool + off
        if width == 2:
            n = struct.unpack("<H", self.b[p:p + 2])[0]
            return list(struct.unpack("<%dH" % n, self.b[p + 2:p + 2 + 2 * n]))
        n = self.b[p]
        return list(self.b[p + 1:p + 1 + n])

    def find_poly(self, x, z):
        """first collision polygon (list order) containing the point (x, z) in units, or None  (docs 1.2 / 1.4)"""
        col = int((x + 1024) // 32)
        row = int((1024 - z) // 32)
        if not (0 <= col < 64 and 0 <= row < 64):
            return None
        for i in self.cell_list(self.o_cg, col, row, 2):
            flags, pts, _, _ = self.poly(i)
            pts = pts[:3] if flags & 1 else pts
            sgn = []
            for k in range(len(pts)):
                (x0, z0), (x1, z1) = pts[k], pts[(k + 1) % len(pts)]
                sgn.append((x0 - x) * (z1 - z) - (x1 - x) * (z0 - z))
            if all(s >= 0 for s in sgn) or all(s <= 0 for s in sgn):
                return i
        return None

    def block_quad(self, i):
        v = struct.unpack("<4B8i", self.b[self.o_quad + 36 * i:self.o_quad + 36 * i + 36])
        return v[:4], [(v[4 + 2 * k] / 65536, v[5 + 2 * k] / 65536) for k in range(4)]

    def find_block(self, x, z):
        col = int((x + 1024) // 32)
        row = int((1024 - z) // 32)
        for qi in self.cell_list(self.o_bg, col, row, 1):
            h, pts = self.block_quad(qi)
            s = []
            for k in range(4):
                (x0, z0), (x1, z1) = pts[k], pts[(k + 1) % 4]
                s.append((x0 - x) * (z1 - z) - (x1 - x) * (z0 - z))
            if all(v >= 0 for v in s) or all(v <= 0 for v in s):
                return qi, h[3]
        return None


def check(name):
    t = Track(os.path.join(OUT, name.lower() + ".rct"))
    ok = bad = 0
    step = max(1, t.npoly // 500)
    for i in range(0, t.npoly, step):
        flags, pts, _, _ = t.poly(i)
        n = 3 if flags & 1 else 4
        cx = sum(p[0] for p in pts[:n]) / n
        cz = sum(p[1] for p in pts[:n]) / n
        j = t.find_poly(cx, cz)
        if j is None:
            bad += 1
        else:
            fj = t.poly(j)[0] >> 16
            if (fj == 0) == ((flags >> 16) == 0):
                ok += 1
            else:
                bad += 1
    qok = 0
    for i in range(128):
        h, pts = t.block_quad(i)
        cx = sum(p[0] for p in pts) / 4
        cz = sum(p[1] for p in pts) / 4
        r = t.find_block(cx, cz)
        qok += bool(r and r[1] == h[3])
    print(f"{name}: collision centroid look-ups {ok}/{ok + bad} find a polygon of the same class, block centroid look-ups {qok}/128")
    return bad == 0 and qok == 128


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    names = [a.upper() for a in args] or ["DESERT", "FOREST", "LAKE", "MOUNT"]
    for n in names:
        if "--check" not in sys.argv:
            convert(n)
        check(n)
