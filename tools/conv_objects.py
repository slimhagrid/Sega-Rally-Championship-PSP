"""M5 (objects): convert OBJECT.MAD (cars, tyres, shadows, flags, signs, people, animals, countdown ...) to `extracted/psp/course/object.rcp`
(same RCP1 container as the courses, see conv_parts.py) and `object.names` (part index -> symbol name, address).

    python tools/conv_objects.py [--verify]

What the game does at race start (`texture::set_car_texture`, docs/render_pipeline.md 4.2) is repeated here: OBJECT.MAD = model image (0x060F0000, no part table;
parts are the `*_poly` symbols of RALLY.DBG) + OBJECT.TEX (0x00200000: {u32 size; data} blocks).  A list at 0x0601AF8C in 0RALLY.DAT names for each model family
{texture table, cg block, cl block} (developer addresses, +type in the low bits); `entry_texture` adds the current CG / CL pointer to every table entry, `set_texture_cg/cl`
copies the block to VDP1 VRAM and advances the pointer.  CG pointer starts at 0x3000, CL pointer at 0x2100 (8-byte units).  --verify runs the real loader in the oracle
and compares the relocated tables and the VRAM bytes.

Part vertices are int16 / 1024 (an object matrix is converted with `<< 6`, a course matrix with `<< 11`: render_pipeline.md 2.2) -> the converter writes metres, so the
renderer applies the instance matrix (view space) as it is.
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

DISC = os.path.join(ROOT, "extracted", "disc", "files")
OUT = os.path.join(ROOT, "extracted", "psp", "course")
SCALE = 1.0 / 1024.0
MDL_BASE = 0x060F0000
LIST = 0x0601AF8C
CG_START, CL_START = 0x3000, 0x2100
DEV_SHIFT = 0xFDEE0000                    # developer address + this = load address in Low WRAM (0x02320000 -> 0x00200000)


def part_symbols():
    out = []
    with open(os.path.join(ROOT, "extracted", "analysis", "symbols.tsv")) as f:
        for line in f:
            p = line.rstrip("\n").split("\t")
            if len(p) >= 3 and p[2].endswith("_poly") and MDL_BASE <= int(p[0], 16) < 0x06100000:
                out.append((int(p[0], 16), p[2]))
    out.sort()
    return out


def be16(b, o):
    return struct.unpack(">H", b[o:o + 2])[0]


def be32(b, o):
    return struct.unpack(">I", b[o:o + 4])[0]


def _load_group(code, list_addr, mdl, mdl_base, tex, tex_dest, shift, cg, cl, vram, tables, heads):
    """one {texture table, cg block, cl block} list of 0RALLY.DAT (see the module doc): relocate the tables of `mdl`, copy the blocks of `tex` to VRAM"""
    a = list_addr
    while True:
        t = be32(code, a - 0x06004000)
        if t == 0xFFFFFFFF:
            break
        g = be32(code, a + 4 - 0x06004000)
        c = be32(code, a + 8 - 0x06004000)
        a += 12
        o = t - mdl_base
        count = be16(mdl, o)
        assert be16(mdl, o + 4) == 0 and be16(mdl, o + 6) == 0
        struct.pack_into(">HH", mdl, o + 4, cg, cl)
        ents = []
        for k in range(count):
            pmod, colr, srca, size = struct.unpack(">4H", mdl[o + 8 + 8 * k:o + 16 + 8 * k])
            ents.append((pmod, colr + cl, srca + cg, size))
        tables[t] = ents
        heads[t] = (cg, cl)
        for dev, kind in ((g, "cg"), (c, "cl")):
            dev &= ~3
            off = dev + shift - tex_dest & 0xFFFFFFFF
            size = be32(tex, off)
            data = tex[off + 4:off + 4 + size]
            if kind == "cg":
                vram[cg * 8:cg * 8 + size] = data
                cg += (size + 7) // 8
            else:
                vram[cl * 8:cl * 8 + size] = data
                cl += (size + 7) // 8
    return cg, cl


ANIMAL_LIST = 0x0601AFFC
ANIMAL_CG, ANIMAL_CL = 0xF800, 0x2EC0           # base pointers the game uses for ANIMAL2 (read back from the oracle: table header of animal2_texttbl)
ANIMAL_BASE = 0x06098000
ANIMAL_SHIFT = 0xFDE68000


def load_all():
    """returns (OBJECT model, ANIMAL2 model, relocated tables {addr: [(pmod, colr, srca, size)]}, vram bytearray, table headers {addr: (cg_base, cl_base)})"""
    s = Scatter(os.path.join(DISC, "OBJECT.MAD"))
    mdl = bytearray(s.data(s.blocks[0]))
    tex = s.data(s.blocks[1])
    assert s.blocks[0].dest == MDL_BASE and s.blocks[1].dest == 0x00200000
    s2 = Scatter(os.path.join(DISC, "ANIMAL2.MAD"))
    mdl2 = bytearray(s2.data(s2.blocks[0]))
    tex2 = s2.data(s2.blocks[1])
    assert s2.blocks[0].dest == ANIMAL_BASE and s2.blocks[1].dest == 0x00200000
    code = open(os.path.join(DISC, "0RALLY.DAT"), "rb").read()
    vram = bytearray(0x80000)
    tables, heads = {}, {}
    _load_group(code, LIST, mdl, MDL_BASE, tex, 0x00200000, DEV_SHIFT, CG_START, CL_START, vram, tables, heads)
    _load_group(code, ANIMAL_LIST, mdl2, ANIMAL_BASE, tex2, 0x00200000, ANIMAL_SHIFT, ANIMAL_CG, ANIMAL_CL, vram, tables, heads)
    return bytes(mdl), bytes(mdl2), tables, vram, heads


def load_objects():
    """returns (OBJECT model bytes, relocated tables, vram bytearray, table headers) - the OBJECT.MAD part of load_all (the tables dict also holds the ANIMAL2 ones)"""
    mdl, mdl2, tables, vram, heads = load_all()
    return mdl, tables, vram, heads


def animal_symbols():
    """ANIMAL2.MDL parts (the symbol table also holds the OBJECT2P layout at the same addresses: only the animal names are taken)"""
    out = []
    with open(os.path.join(ROOT, "extracted", "analysis", "symbols.tsv")) as f:
        for line in f:
            p = line.strip().split(chr(9))
            if len(p) >= 3 and p[2].endswith("_poly") and (p[2].startswith("zou_") or p[2].startswith("shima")) and ANIMAL_BASE <= int(p[0], 16) < 0x060A0000:
                out.append((int(p[0], 16), p[2]))
    out.sort()
    return out


def convert():
    mdl, mdl2, tables, vram, heads = load_all()
    syms1 = part_symbols()
    syms2 = animal_symbols()
    syms = syms1 + syms2                            # OBJECT.MDL parts first (their numbers are used by the frame tools), then the ANIMAL2 ones
    parts = rcmodel.parse_parts(mdl, [a for a, _ in syms1], MDL_BASE) + rcmodel.parse_parts(mdl2, [a for a, _ in syms2], ANIMAL_BASE)
    for i, pt in enumerate(parts):
        pt.index = i
    tex_index, textures = {}, []

    def tex_of(ent):
        key = (ent[2], ent[1], ent[3])
        if key not in tex_index:
            tex_index[key] = len(textures)
            textures.append(key)
        return tex_index[key]

    pbytes, vbytes, polys = [], bytearray(), bytearray()
    missing = set()
    nv_total = np_total = 0
    for part in parts:
        pbytes.append((nv_total, np_total, len(part.verts), len(part.polys)))
        for (x, y, z) in part.verts:
            vbytes += struct.pack("<3f", x * SCALE, y * SCALE, z * SCALE)
        for p in part.polys:
            idx = list(p.idx)
            kv = idx + [idx[2]] if p.triangle else idx
            cv = [idx[i] for i in TRI_CORNERS[p.corner_mode]] if p.triangle else idx
            if p.textured and part.tex_table in tables:
                tex, colr = tex_of(tables[part.tex_table][p.w1 // 8]), 0
            elif p.textured:                                   # developer test parts (cube, ball ...): their table is never loaded by the game
                tex, colr = 0xFFFF, 0xFC1F
                missing.add(syms[part.index][1])
            else:
                tex, colr = 0xFFFF, p.w1
            polys += struct.pack("<HHH4H4HH", p.flags, tex, colr, *kv, *cv, 0)
        nv_total += len(part.verts)
        np_total += len(part.polys)

    if missing:
        print("parts with a texture table the game never loads (drawn flat magenta): %s" % ", ".join(sorted(missing)))
    texrec, blob = bytearray(), bytearray()
    texdata = []
    for (srca, colr, size) in textures:
        w, h = ((size >> 8) & 0x3F) * 8, size & 0xFF
        tw, th = pot(w), pot(h)
        pix = bytearray(tw * th // 2)
        for y in range(th):
            for x in range(tw):
                sx, sy = min(x, w - 1), min(y, h - 1)
                v = vram[srca * 8 + sy * (w // 2) + sx // 2]
                px = (v & 15) if (sx & 1) else (v >> 4)
                o = (y * tw + x) // 2
                pix[o] |= (px << 4) if (x & 1) else px
        clut = bytearray()
        for i in range(16):
            v = be16(vram, colr * 8 + 2 * i)
            r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
            clut += struct.pack("<H", r | (g << 5) | (b << 10) | (0 if i == 0 else 0x8000))
        texdata.append((w, h, tw, th, bytes(pix), bytes(clut)))

    hdr = 64
    part_off = hdr
    tex_off = part_off + 16 * len(parts)
    poly_off = tex_off + 16 * len(texdata)
    vert_off = poly_off + len(polys)
    vis_off = vert_off + len(vbytes)
    blob_off = (vis_off + 15) & ~15
    for (w, h, tw, th, pix, clut) in texdata:
        po = blob_off + len(blob)
        blob += pix + b"\x00" * (-len(pix) % 16)
        co = blob_off + len(blob)
        blob += clut + b"\x00" * (-len(clut) % 16)
        texrec += struct.pack("<HHHHII", w, h, tw, th, po, co)
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "object.rcp")
    with open(path, "wb") as f:
        f.write(b"RCP1" + struct.pack("<11I", len(parts), len(texdata), np_total, nv_total, part_off, tex_off, poly_off, vert_off, vis_off, 0, 1))   # last word = kind 1: objects
        f.write(b"\x00" * (hdr - f.tell()))
        for fv, fp, nv, npl in pbytes:
            f.write(struct.pack("<IIHHI", fv, fp, nv, npl, 0))
        f.write(texrec)
        f.write(polys)
        f.write(vbytes)
        f.write(b"\x00" * (blob_off - f.tell()))
        f.write(blob)
    with open(os.path.join(OUT, "object.names"), "w") as f:
        for i, (a, n) in enumerate(syms):
            f.write("%d %08x %s\n" % (i, a, n))
    print("OBJECT: %d parts, %d polygons, %d vertices, %d textures -> %s (%s bytes)" % (len(parts), np_total, nv_total, len(texdata), path, format(os.path.getsize(path), ",")))
    return mdl, tables, vram, heads


def verify(tables, vram, heads):
    from saturn_env import Race
    r = Race(0, game_type=2, render=True)
    r.load_game_textures()
    m = r.mem
    bad = 0
    for t, ents in tables.items():
        cgb, clb = m.r16(t + 4), m.r16(t + 6)
        if (cgb, clb) != heads[t]:
            print("table %08x: bases game (%x, %x) mine %s" % (t, cgb, clb, heads[t]))
            bad += 1
        for k, e in enumerate(ents):
            g = tuple(m.r16(t + 8 + 8 * k + 2 * j) for j in range(4))
            if g != e:
                bad += 1
                if bad < 10:
                    print("table %08x entry %d: game %s mine %s" % (t, k, g, e))
    g = m.read(0x25C00000 + CG_START * 8, 0x1D620)
    if g != bytes(vram[CG_START * 8:CG_START * 8 + 0x1D620]):
        print("CG bytes differ")
        bad += 1
    g = m.read(0x25C00000 + CL_START * 8, 0x3C00)
    if g != bytes(vram[CL_START * 8:CL_START * 8 + 0x3C00]):
        print("CL bytes differ")
        bad += 1
    for name, base, size in (("ANIMAL2 CG", ANIMAL_CG * 8, 0x3EA0), ("ANIMAL2 CL", ANIMAL_CL * 8, 0x5A0)):
        if m.read(0x25C00000 + base, size) != bytes(vram[base:base + size]):
            print(name + " bytes differ")
            bad += 1
    print("verify: %s" % ("OK (tables + VRAM identical to the game's loader)" if not bad else "%d mismatches" % bad))
    return bad == 0


if __name__ == "__main__":
    mdl, tables, vram, heads = convert()
    if "--verify" in sys.argv:
        sys.exit(0 if verify(tables, vram, heads) else 1)
