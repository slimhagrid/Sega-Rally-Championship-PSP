"""M5: convert one course to the part-based PSP pack `extracted/psp/course/<name>.rcp` used by the renderer (the game draws a course as a *list of parts*, nearest
first, painter-sorted by a per-polygon depth key; the `.rcm` of M3 is a flattened viewer pack and does not keep parts).

    python tools/conv_parts.py [DESERT ...]            (default: all four 1P courses)

RCP1 (little endian), offsets from the start of the file:
    header (64 bytes): "RCP1", u32 nparts (128), u32 ntex, u32 npoly, u32 nverts, u32 part_off, u32 tex_off, u32 poly_off, u32 vert_off, u32 vis_off, u32 vis_size, 6 x u32 reserved
    part   (16 bytes): u32 first_vert, u32 first_poly, u16 nverts, u16 npolys, u32 pad          vertex / polygon numbers are global; polygon vertex indices are part-local
    vertex (12 bytes): f32 x, y, z   in metres (model int16 / 32), game axes (x east, y up, z north; left-handed, the game's own view matrix is used as is)
    poly   (24 bytes): u16 flags (the game's w0: bit0 triangle, bits1-2 depth-key mode: 0 mean, 1 min, 2 max, 3 max+4, bit4 single sided, bit12/13 flips), u16 tex (index into the texture table, 0xFFFF = flat
                       colour), u16 colr (RGB555 of a flat polygon, bit15 set), u16 kv[4] (vertices the depth key is computed from: A B C D, a triangle repeats C),
                       u16 cv[4] (vertices at the four sprite corners P0..P3 after the triangle corner mode of render_pipeline.md 3.3), u16 pad
    texture (16 bytes): u16 w, u16 h, u16 tw, u16 th, u32 pix_off, u32 clut_off   -- same as RCM1 (T4 data, low nibble first; 16 x RGBA5551 palette, index 0 transparent)
    vis: the 16 "visible part" lists of `car::get_poly_map` (0x060081B8; table of 16 pointers at 0x0600821C in 0RALLY.DAT, indexed by the car's display-angle sector +0xC7): blob =
         u32 offset[16] (from the blob start) then the lists; a list = u8 flag, then signed bytes (part-index offset * 4) up to and including 0xFF.
         get_poly_map: part = (block id + offset/4) & 0x7F for each entry while the running polygon count stays <= 1500, taking `n` entries where n = low nibble of car+0xF1 (flag 0) or
         high nibble (flag != 0), at least one.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import rcmodel  # noqa: E402
from conv_course import TRI_CORNERS, pot  # noqa: E402

DISC = os.path.join(ROOT, "extracted", "disc", "files")
OUT = os.path.join(ROOT, "extracted", "psp", "course")
SCALE = 1.0 / 32.0
GET_POLY_MAP_TABLE = 0x0600821C


def vis_blob():
    code = open(os.path.join(DISC, "0RALLY.DAT"), "rb").read()
    base = 0x06004000

    def b(a, n=1):
        return code[a - base:a - base + n]

    ptrs = [struct.unpack(">I", b(GET_POLY_MAP_TABLE + 4 * k, 4))[0] for k in range(16)]
    lists = {}
    for p in sorted(set(ptrs)):
        n = 1
        while code[p - base + n] != 0xFF:
            n += 1
        lists[p] = b(p, n + 1)
    blob = bytearray(64)
    offs = {}
    for p, data in lists.items():
        offs[p] = len(blob)
        blob += data
    for k, p in enumerate(ptrs):
        struct.pack_into("<I", blob, 4 * k, offs[p])
    return bytes(blob)


def convert(name):
    c = rcmodel.load_course(os.path.join(DISC, name + ".MAD"))
    table = list(c.tables.values())
    assert len(table) == 1
    table = table[0]
    tex_index, textures = {}, []

    def tex_of(entry):
        if entry.key not in tex_index:
            tex_index[entry.key] = len(textures)
            textures.append(entry)
        return tex_index[entry.key]

    parts, verts, polys = [], bytearray(), bytearray()
    nv_total = np_total = 0
    for part in c.parts:
        parts.append((nv_total, np_total, len(part.verts), len(part.polys)))
        for (x, y, z) in part.verts:
            verts += struct.pack("<3f", x * SCALE, y * SCALE, z * SCALE)
        for p in part.polys:
            idx = list(p.idx)
            kv = idx + [idx[2]] if p.triangle else idx
            cv = [idx[i] for i in TRI_CORNERS[p.corner_mode]] if p.triangle else idx
            if p.textured:
                tex, colr = tex_of(table[p.w1 // 8]), 0
            else:
                tex, colr = 0xFFFF, p.w1
            polys += struct.pack("<HHH4H4HH", p.flags, tex, colr, *kv, *cv, 0)
        nv_total += len(part.verts)
        np_total += len(part.polys)

    texrec, blob = bytearray(), bytearray()
    tex_bytes = []
    for ent in textures:
        w, h = ent.width, ent.height
        tw, th = pot(w), pot(h)
        rows = rcmodel.bitmap(c, ent)
        pix = bytearray(tw * th // 2)
        for y in range(th):
            for x in range(tw):
                px = rows[min(y, h - 1)][min(x, w - 1)] if (y < h and x < w) else 0
                o = (y * tw + x) // 2
                pix[o] |= (px << 4) if (x & 1) else px
        pal = rcmodel.palette(c, ent)
        clut = b"".join(struct.pack("<H", (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | (0x8000 if a else 0)) for r, g, b, a in pal)
        tex_bytes.append((w, h, tw, th, bytes(pix), clut))

    vis = vis_blob()
    hdr = 64
    part_off = hdr
    tex_off = part_off + 16 * len(parts)
    poly_off = tex_off + 16 * len(tex_bytes)
    vert_off = poly_off + len(polys)
    vis_off = vert_off + len(verts)
    blob_off = (vis_off + len(vis) + 15) & ~15
    for (w, h, tw, th, pix, clut) in tex_bytes:
        po = blob_off + len(blob)
        blob += pix + b"\x00" * (-len(pix) % 16)
        co = blob_off + len(blob)
        blob += clut + b"\x00" * (-len(clut) % 16)
        texrec += struct.pack("<HHHHII", w, h, tw, th, po, co)
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name.lower() + ".rcp")
    with open(path, "wb") as f:
        f.write(b"RCP1" + struct.pack("<10I", len(parts), len(tex_bytes), np_total, nv_total, part_off, tex_off, poly_off, vert_off, vis_off, len(vis)))
        f.write(b"\x00" * (hdr - f.tell()))
        for fv, fp, nv, npl in parts:
            f.write(struct.pack("<IIHHI", fv, fp, nv, npl, 0))
        f.write(texrec)
        f.write(polys)
        f.write(verts)
        f.write(vis)
        f.write(b"\x00" * (blob_off - f.tell()))
        f.write(blob)
    print("%s: %d parts, %d polygons, %d vertices, %d textures -> %s (%s bytes)" % (name, len(parts), np_total, nv_total, len(tex_bytes), path, format(os.path.getsize(path), ",")))
    return path


if __name__ == "__main__":
    for n in (sys.argv[1:] or ["DESERT", "FOREST", "LAKE", "MOUNT"]):
        convert(n.upper())
