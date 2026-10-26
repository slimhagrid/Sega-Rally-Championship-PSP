"""Parser for the game's course / object models (formats from docs/render_pipeline.md section 3 and 4, verified there on 1,024 course parts).

    from rcmodel import load_course
    c = load_course("extracted/disc/files/DESERT.MAD")
    c.parts[i].verts / .polys ;  c.textures[k] ;  c.bitmap(tex) -> list of rows of palette indices ;  c.palette(tex) -> 16 x (r, g, b, a)

Model memory image (block 0 of *.MAD, loaded at 0x06048000): 128 absolute part pointers in the first 0x200 bytes; part header (0x14 bytes, big endian):
    u16 nverts, u16 npolys, u32 ->verts (int16 x,y,z), u32 ->polys, u32 ->texture table (0xFFFFFFFF = none), u32 ->normals
polygon: u16 flags, u16 w1 (textured: byte offset into the texture table entries, else CMDCOLR), 3|4 x u16 vertex byte offset (vertex number * 16) [+ 3 x i16 normal when flags & 0x20]
texture table: u16 count, u16 pad, u16 cg_base, u16 cl_base, then count x {CMDPMOD, CMDCOLR, CMDSRCA, CMDSIZE}: addresses are in units of 8 bytes of VDP1 VRAM.
"""
import struct
from dataclasses import dataclass, field

from datfile import Scatter

MODEL_BASE = 0x06048000
VRAM = 0x25C00000


@dataclass
class Poly:
    flags: int
    w1: int
    idx: tuple            # vertex numbers (3 or 4)
    normal: tuple = None

    @property
    def triangle(self):
        return bool(self.flags & 1)

    @property
    def cmd_type(self):   # (flags >> 5) & 15: 0 flat, 2 textured, ... (see render doc 3.3)
        return (self.flags >> 5) & 15

    @property
    def textured(self):
        return self.cmd_type in (2, 3, 6, 10, 11, 14)

    @property
    def hflip(self):
        return bool(self.flags & 0x1000)

    @property
    def vflip(self):
        return bool(self.flags & 0x2000)

    @property
    def corner_mode(self):
        return (self.flags >> 9) & 7


@dataclass
class Part:
    index: int
    addr: int
    verts: list
    polys: list
    tex_table: int       # absolute pointer or None


@dataclass
class Texture:
    pmod: int
    colr: int
    srca: int
    size: int

    @property
    def width(self):
        return ((self.size >> 8) & 0x3F) * 8

    @property
    def height(self):
        return self.size & 0xFF

    @property
    def key(self):
        return (self.srca, self.colr, self.size)


class Course:
    pass


def _u16(b, o):
    return struct.unpack(">H", b[o:o + 2])[0]


def _u32(b, o):
    return struct.unpack(">I", b[o:o + 4])[0]


def parse_parts(model, ptrs, base=MODEL_BASE):
    parts = []
    for i, ptr in enumerate(ptrs):
        o = ptr - base
        nv, npoly, pv, pp, pt, pn = struct.unpack(">HHIIII", model[o:o + 20])
        ov = pv - base
        verts = [struct.unpack(">3h", model[ov + 6 * k:ov + 6 * k + 6]) for k in range(nv)]
        polys = []
        q = pp - base
        for _ in range(npoly):
            flags, w1 = struct.unpack(">HH", model[q:q + 4])
            n = 3 if flags & 1 else 4
            idx = struct.unpack(">%dH" % n, model[q + 4:q + 4 + 2 * n])
            q += 4 + 2 * n
            nrm = None
            if flags & 0x20:
                nrm = struct.unpack(">3h", model[q:q + 6])
                q += 6
            polys.append(Poly(flags, w1, tuple(i >> 4 for i in idx), nrm))
        parts.append(Part(i, ptr, verts, polys, None if pt == 0xFFFFFFFF else pt))
    return parts


def load_course(path):
    s = Scatter(path)
    model = s.data(s.blocks[0])
    assert s.blocks[0].dest == MODEL_BASE
    c = Course()
    c.model = model
    c.cg = c.cl = None
    c.cg_base = c.cl_base = None
    for b in s.blocks[1:]:
        if b.dest >= VRAM and b.dest < VRAM + 0x80000:
            if b.dest == 0x25C3B000:
                c.cg, c.cg_base = s.data(b), b.dest - VRAM
            elif b.dest == 0x25C15000:
                c.cl, c.cl_base = s.data(b), b.dest - VRAM
    nparts = 128
    ptrs = struct.unpack(">%dI" % nparts, model[:4 * nparts])
    c.parts = parse_parts(model, ptrs)
    # texture tables (one per part that has one; the course shares one table in practice: collect by pointer)
    c.tables = {}
    for p in c.parts:
        if p.tex_table is not None and p.tex_table not in c.tables:
            o = p.tex_table - MODEL_BASE
            count = _u16(model, o)
            ents = [Texture(*struct.unpack(">4H", model[o + 8 + 8 * k:o + 16 + 8 * k])) for k in range(count)]
            c.tables[p.tex_table] = ents
    return c


def bitmap(course, tex):
    """4bpp bitmap as rows of palette indices (first pixel = high nibble)."""
    off = tex.srca * 8 - course.cg_base
    w, h = tex.width, tex.height
    rows = []
    for y in range(h):
        row = []
        for x in range(w // 2):
            v = course.cg[off + y * (w // 2) + x]
            row.extend((v >> 4, v & 15))
        rows.append(row)
    return rows


def palette(course, tex):
    """16 RGBA entries from the CMDCOLR palette (RGB555, red in the low bits; index 0 = transparent)."""
    off = tex.colr * 8 - course.cl_base
    out = []
    for i in range(16):
        v = _u16(course.cl, off + 2 * i)
        r, g, b = (v & 31) << 3, ((v >> 5) & 31) << 3, ((v >> 10) & 31) << 3
        out.append((r | r >> 5, g | g >> 5, b | b >> 5, 0 if i == 0 else 255))
    return out
