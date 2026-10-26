"""M8 (menus): convert the 3D models of the menu screens (SELECT.MAD: the course models of the time attack course / lap pages and the objects of the car set-up screen) to
`extracted/psp/course/select.rcp` (the RCP1 container of conv_objects.py, kind 2) and `select.names` (part index, address, used by).

    python tools/conv_select.py

SELECT.MAD = model image at 0x06048000 (SELECT.MDL, parts addressed by the screens' tables) + SELECT.TEX.  The parts used by the screens: the time attack course table 0x06023C04
(4 courses) and the set-up object table 0x06023C50 (rows), 11 different parts.  Every polygon is type 4 (flat colour + per-vertex brightness: CMDCOLR in w1 with bit 15 set, no texture),
so the pack has no textures; the per-vertex normals (int16 x, y, z; 16384 = 1.0; pointer at +16 of the part header) are stored in the `vis` block (kind 2) for the renderer's lighting.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import rcmodel  # noqa: E402
from datfile import Scatter  # noqa: E402
from conv_course import TRI_CORNERS  # noqa: E402

DISC = os.path.join(ROOT, "extracted", "disc", "files")
OUT = os.path.join(ROOT, "extracted", "psp", "course")
BASE = 0x06048000
SCALE = 1.0 / 1024.0

# part header addresses used by the screens (tables in 0RALLY.DAT: 0x06023C04 time attack courses, 0x06023C50 set-up objects)
PARTS = [
    (0x06066C74, "time attack course 0"), (0x06069D74, "time attack course 1"), (0x0606C2F4, "time attack course 2"), (0x0606F1F8, "time attack course 3"),
    (0x060545D0, "set-up object"), (0x0605672C, "set-up object"), (0x06061264, "set-up object"), (0x0605CF2C, "set-up object"),
    (0x06059C18, "set-up object"), (0x0604FEA8, "set-up object"), (0x06064E00, "set-up object"),
]


def tables_from_game():
    """the two tables of 0RALLY.DAT (tools/rd.py: the program image at 0x06004000): they must name the parts above"""
    import rd

    def u32(a):
        return struct.unpack(">I", bytes(rd.IMG[a - 0x06004000:a - 0x06004000 + 4]))[0]
    ta = [u32(0x06023C04 + 4 * i) for i in range(4)]
    obj = [u32(0x06023C50 + 4 * i) for i in range(8)]
    return ta, obj


def main():
    s = Scatter(os.path.join(DISC, "SELECT.MAD"))
    mdl = s.data(s.blocks[0])
    assert s.blocks[0].dest == BASE
    ta, obj = tables_from_game()
    used = set(ta + obj)
    assert used <= set(a for a, _ in PARTS), "a part of the game's tables is missing in PARTS: %s" % [hex(a) for a in used - set(a for a, _ in PARTS)]
    parts = rcmodel.parse_parts(mdl, [a for a, _ in PARTS], BASE)
    pbytes, vbytes, polys, norms = [], bytearray(), bytearray(), bytearray()
    nv_total = np_total = 0
    for (addr, _), part in zip(PARTS, parts):
        o = addr - BASE
        nv, npoly, pv, pp, pt, pn = struct.unpack(">HHIIII", mdl[o:o + 20])
        assert pt == 0xFFFFFFFF, "textured part"
        pbytes.append((nv_total, np_total, len(part.verts), len(part.polys)))
        for (x, y, z) in part.verts:
            vbytes += struct.pack("<3f", x * SCALE, y * SCALE, z * SCALE)
        on = pn - BASE
        for k in range(nv):
            norms += struct.pack("<3h", *struct.unpack(">3h", mdl[on + 6 * k:on + 6 * k + 6]))
        for p in part.polys:
            assert p.cmd_type == 4, "polygon type %d" % p.cmd_type
            idx = list(p.idx)
            kv = idx + [idx[2]] if p.triangle else idx
            cv = [idx[i] for i in TRI_CORNERS[p.corner_mode]] if p.triangle else idx
            polys += struct.pack("<HHH4H4HH", p.flags, 0xFFFF, p.w1 & 0x7FFF, *kv, *cv, 0)
        nv_total += len(part.verts)
        np_total += len(part.polys)

    hdr = 64
    part_off = hdr
    tex_off = part_off + 16 * len(parts)
    poly_off = tex_off
    vert_off = poly_off + len(polys)
    vis_off = vert_off + len(vbytes)
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "select.rcp")
    with open(path, "wb") as f:
        f.write(b"RCP1" + struct.pack("<11I", len(parts), 0, np_total, nv_total, part_off, tex_off, poly_off, vert_off, vis_off, len(norms), 2))   # last word = kind 2: shaded models
        f.write(b"\x00" * (hdr - f.tell()))
        for fv, fp, nv, npl in pbytes:
            f.write(struct.pack("<IIHHI", fv, fp, nv, npl, 0))
        f.write(polys)
        f.write(vbytes)
        f.write(norms)
    with open(os.path.join(OUT, "select.names"), "w") as f:
        for i, (a, n) in enumerate(PARTS):
            f.write("%d %08x %s\n" % (i, a, n))
    print("SELECT: %d parts, %d polygons, %d vertices -> %s (%s bytes)" % (len(parts), np_total, nv_total, path, format(os.path.getsize(path), ",")))


if __name__ == "__main__":
    main()
