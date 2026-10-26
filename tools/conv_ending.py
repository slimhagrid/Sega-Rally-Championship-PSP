"""M8 (menus): convert the confetti of the ceremony (ENDING.MDL, loaded to 0x06048000 by `set_ceremony_model` 0x0601AE4C: five single flat quads, 0x0601EB74 names them) to
`extracted/psp/course/ending.rcp` (the RCP1 container of conv_objects.py, kind 2, the same as alpha.rcp) and `ending.names` (part index, address).

    python tools/conv_ending.py

(The other parts of ENDING.MDL: the animated car of the ceremony, 40 textured parts, are not converted; see docs/menus.md 14.)

ALPHA.MDL = a model image: the table `alphabet_poly_tbl` (0x06048000, 29 part pointers: the letters of the chosen name, A .. Z, '.', and two more) and `alphabet_poly_tbl2` (0x06048074,
29 part pointers: the same letters in the wheel) in front of the parts.  Every polygon is a flat polygon with a face normal (type 1: `draw_color_polygon` paints it in the colour the
caller gives and shades it by the face normal and the light: CMDGRDA = grey level of the normal).  The renderer's kind 2 shades per vertex, so every polygon gets its own vertices with
the face normal as vertex normal (a polygon is flat: the same level on all its corners).  The colour of the polygons (w1) is replaced at draw time (scene_set_color).
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import rcmodel  # noqa: E402
from conv_course import TRI_CORNERS  # noqa: E402

DISC = os.path.join(ROOT, "extracted", "disc", "files")
OUT = os.path.join(ROOT, "extracted", "psp", "course")
BASE = 0x06048000
SCALE = 1.0 / 1024.0


def main():
    from datfile import Scatter
    sc = Scatter(os.path.join(DISC, "ENDING.MAD"))
    mdl = sc.expanded(sc.blocks[0])
    assert sc.blocks[0].dest == BASE
    addrs = [0x0604AB78, 0x0604ABB8, 0x0604ABF8, 0x0604AC38, 0x0604AC78]          # the confetti (the table at 0x0601EB74 repeats the first three)
    parts = rcmodel.parse_parts(mdl, addrs, BASE)
    pbytes, vbytes, polys, norms = [], bytearray(), bytearray(), bytearray()
    nv_total = np_total = 0
    for addr, part in zip(addrs, parts):
        nv0, np0 = nv_total, np_total
        for p in part.polys:
            assert p.cmd_type == 1 and p.normal is not None, "polygon type %d" % p.cmd_type
            n = 3 if p.triangle else 4
            base = nv_total - nv0                       # vertex numbers are relative to the part's first vertex
            for k in range(n):
                x, y, z = part.verts[p.idx[k]]
                vbytes += struct.pack("<3f", x * SCALE, y * SCALE, z * SCALE)
                norms += struct.pack("<3h", *p.normal)
                nv_total += 1
            idx = list(range(base, base + n))
            kv = idx + [idx[2]] if p.triangle else idx
            cv = [idx[i] for i in TRI_CORNERS[p.corner_mode]] if p.triangle else idx
            polys += struct.pack("<HHH4H4HH", p.flags, 0xFFFF, p.w1 & 0x7FFF, *kv, *cv, 0)
            np_total += 1
        pbytes.append((nv0, np0, nv_total - nv0, np_total - np0))
    hdr = 64
    part_off = hdr
    tex_off = part_off + 16 * len(parts)
    poly_off = tex_off
    vert_off = poly_off + len(polys)
    vis_off = vert_off + len(vbytes)
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "ending.rcp")
    with open(path, "wb") as f:
        f.write(b"RCP1" + struct.pack("<11I", len(parts), 0, np_total, nv_total, part_off, tex_off, poly_off, vert_off, vis_off, len(norms), 2))
        f.write(b"\x00" * (hdr - f.tell()))
        for fv, fp, nv, npl in pbytes:
            f.write(struct.pack("<IIHHI", fv, fp, nv, npl, 0))
        f.write(polys)
        f.write(vbytes)
        f.write(norms)
    with open(os.path.join(OUT, "ending.names"), "w") as f:
        for i, a in enumerate(addrs):
            f.write("%d %08x\n" % (i, a))
    print("ENDING: %d parts, %d polygons, %d vertices -> %s (%s bytes)" % (len(parts), np_total, nv_total, path, format(os.path.getsize(path), ",")))


if __name__ == "__main__":
    main()
