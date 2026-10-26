"""M8 (menus): convert the animated model of the ceremony (ENDING.MDL: the crew of the car celebrating, 40 textured parts) to `extracted/psp/course/endcar.rcp` (the RCP1 container of
conv_objects.py, kind 1 = objects, textures) and `endcar.names` (part number = the position in the animation table, address).

    python tools/conv_endcar.py

The model image is block 0 of ENDING.MAD (loaded to 0x06048000 by `set_ceremony_model` 0x0601AE4C), block 1 is the texture data (ENDING.TEX, 0x00200000): the same loader as OBJECT.MAD
(texture::set_car_texture): the list at 0x0601B064 names {texture table 0x06048000, CG block 0x029B0001, CL block 0x029BB886}, `entry_texture` adds the CG / CL pointers to the table's
entries, the blocks go to the VDP1 VRAM.  The animation table (0x0601EDE4 for the Celica, 0x0601EE8C for the Delta and the Stratos: the same, but for parts 6 and 37: those two models are parts 40 and 41 of the file) lists the 40 part models in the order
FUN_0601CF44 draws them; its key frames (104 frames x 40 parts x 20 bytes: x, y, z as 16.16 longs, three 16 bit angles) stay in the console memory the port loads ENDING.MAD into.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import rcmodel  # noqa: E402
from datfile import Scatter  # noqa: E402
from conv_course import TRI_CORNERS, pot  # noqa: E402
import conv_objects as co  # noqa: E402

DISC = co.DISC
OUT = co.OUT
BASE = 0x06048000
LIST = 0x0601B064
SHIFT = 0xFD850000                      # developer address + this = load address in Low WRAM (0x029B0000 -> 0x00200000)
TABLE = 0x0601EDE4                      # the animation table of the Celica (the Delta's and the Stratos's is 0x0601EE8C)
NPARTS = 40


def convert():
    s = Scatter(os.path.join(DISC, "ENDING.MAD"))
    assert s.blocks[0].dest == BASE and s.blocks[1].dest == 0x00200000
    mdl = bytearray(s.expanded(s.blocks[0]))
    tex = s.expanded(s.blocks[1])
    code = open(os.path.join(DISC, "0RALLY.DAT"), "rb").read()

    def l(a):
        return struct.unpack(">I", code[a - 0x06004000:a - 0x06004000 + 4])[0]

    def w(a):
        return struct.unpack(">H", code[a - 0x06004000:a - 0x06004000 + 2])[0]

    assert w(TABLE) == 104 and w(TABLE + 2) == NPARTS
    ptrs = [l(TABLE + 8 + 4 * i) for i in range(NPARTS)]
    st = [l(0x0601EE8C + 8 + 4 * i) for i in range(NPARTS)]
    diff = [i for i in range(NPARTS) if st[i] != ptrs[i]]
    assert diff == [6, 37], diff                    # the Stratos's table (0x0601EE8C) differs in two parts (6 and 37): parts 40 / 41 of the file are those models
    ptrs += [st[6], st[37]]
    vram = bytearray(0x80000)
    tables, heads = {}, {}
    co._load_group(code, LIST, mdl, BASE, tex, 0x00200000, SHIFT, co.CG_START, co.CL_START, vram, tables, heads)
    parts = rcmodel.parse_parts(bytes(mdl), ptrs, BASE)

    tex_index, textures = {}, []

    def tex_of(ent):
        key = (ent[2], ent[1], ent[3])
        if key not in tex_index:
            tex_index[key] = len(textures)
            textures.append(key)
        return tex_index[key]

    pbytes, vbytes, polys = [], bytearray(), bytearray()
    nv_total = np_total = 0
    for part in parts:
        pbytes.append((nv_total, np_total, len(part.verts), len(part.polys)))
        for (x, y, z) in part.verts:
            vbytes += struct.pack("<3f", x * co.SCALE, y * co.SCALE, z * co.SCALE)
        for p in part.polys:
            idx = list(p.idx)
            kv = idx + [idx[2]] if p.triangle else idx
            cv = [idx[i] for i in TRI_CORNERS[p.corner_mode]] if p.triangle else idx
            if p.textured:
                assert part.tex_table in tables
                tex_id, colr = tex_of(tables[part.tex_table][p.w1 // 8]), 0
            else:
                tex_id, colr = 0xFFFF, p.w1
            polys += struct.pack("<HHH4H4HH", p.flags, tex_id, colr, *kv, *cv, 0)
        nv_total += len(part.verts)
        np_total += len(part.polys)

    texdata = []
    for (srca, colr, size) in textures:
        tw_, th_ = ((size >> 8) & 0x3F) * 8, size & 0xFF
        tw, th = pot(tw_), pot(th_)
        pix = bytearray(tw * th // 2)
        for y in range(th):
            for x in range(tw):
                sx, sy = min(x, tw_ - 1), min(y, th_ - 1)
                v = vram[srca * 8 + sy * (tw_ // 2) + sx // 2]
                px = (v & 15) if (sx & 1) else (v >> 4)
                o = (y * tw + x) // 2
                pix[o] |= (px << 4) if (x & 1) else px
        clut = bytearray()
        for i in range(16):
            v = co.be16(vram, colr * 8 + 2 * i)
            r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
            clut += struct.pack("<H", r | (g << 5) | (b << 10) | (0 if i == 0 else 0x8000))
        texdata.append((tw_, th_, tw, th, bytes(pix), bytes(clut)))

    hdr = 64
    part_off = hdr
    tex_off = part_off + 16 * len(parts)
    poly_off = tex_off + 16 * len(texdata)
    vert_off = poly_off + len(polys)
    vis_off = vert_off + len(vbytes)
    blob_off = (vis_off + 15) & ~15
    blob, texrec = bytearray(), bytearray()
    for (w_, h_, tw, th, pix, clut) in texdata:
        po = blob_off + len(blob)
        blob += pix + b"\x00" * (-len(pix) % 16)
        cpo = blob_off + len(blob)
        blob += clut + b"\x00" * (-len(clut) % 16)
        texrec += struct.pack("<HHHHII", w_, h_, tw, th, po, cpo)
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "endcar.rcp")
    with open(path, "wb") as f:
        f.write(b"RCP1" + struct.pack("<11I", len(parts), len(texdata), np_total, nv_total, part_off, tex_off, poly_off, vert_off, vis_off, 0, 1))
        f.write(b"\x00" * (hdr - f.tell()))
        for fv, fp, nv, npl in pbytes:
            f.write(struct.pack("<IIHHI", fv, fp, nv, npl, 0))
        f.write(texrec)
        f.write(polys)
        f.write(vbytes)
        f.write(b"\x00" * (blob_off - f.tell()))
        f.write(blob)
    with open(os.path.join(OUT, "endcar.names"), "w") as f:
        for i, a in enumerate(ptrs):
            f.write("%d %08x\n" % (i, a))
    print("ENDCAR: %d parts, %d polygons, %d vertices, %d textures -> %s (%s bytes)" % (len(parts), np_total, nv_total, len(texdata), path, format(os.path.getsize(path), ",")))


if __name__ == "__main__":
    convert()
