"""M3: convert one course (`DESERT`, `FOREST`, `LAKE`, `MOUNT`) from the disc files to the PSP pack `extracted/psp/course/<name>.rcm`.

    python tools/conv_course.py [DESERT ...]            (default: all four 1P courses)
    python tools/conv_course.py DESERT --preview        also writes extracted/analysis/preview_<name>_top.png and a texture sheet

Input: `<NAME>.MAD` (scatter-load: block 0 = model, block 1 = 4bpp bitmaps for VDP1 VRAM 0x3B000, block 2 = 16-colour palettes for VRAM 0x15000).
Output RCM1 (little endian):
    header  : "RCM1", u32 ntex, u32 nbatch, u32 nverts, f32 bbox[6] (xmin ymin zmin xmax ymax zmax, metres), f32 start[3] (centre of part 0), f32 dir[2] (x, z towards
              part 1: the driving direction), u32 tex_off, u32 batch_off, u32 vert_off     (72 bytes; coordinates are the game's: x east, y up, z north = left-handed)
    texture : u16 w, u16 h (original), u16 tw, u16 th (power of two, tw >= 32 so a T4 row is >= 16 bytes), u32 pix_off, u32 clut_off     (offsets from file start; 16 bytes per entry)
              pix = PSP GU_PSM_T4 (tw*th/2 bytes, low nibble = first pixel), clut = 16 x u16 RGBA5551 (index 0 has alpha 0)
    batch   : u32 texture index (0xFFFFFFFF = flat colour), u32 first vertex, u32 vertex count      (triangle lists, sorted by texture)
    vertex  : f32 u, f32 v (normalised to the padded texture), u32 colour (ABGR), f32 x, y, z       (24 bytes; x = east, y = up, z = north in metres)
World scale: the model's int16 coordinates are 1/32 metre (measured against the collision data: block quads span +-500 x +-850 units, the model +-15000 x +-27000).
The VDP1 distorted sprite maps the texture corners to polygon corners P0 (0,0) P1 (w,0) P2 (w,h) P3 (0,h); flags bit 12 / 13 mirror it; triangles use the corner
modes of docs/render_pipeline.md 3.3.  Two PSP triangles per quad: (P0,P1,P2) and (P0,P2,P3): affine instead of the VDP1's scanline mapping (same for parallelograms).
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import rcmodel  # noqa: E402

DISC = os.path.join(ROOT, "extracted", "disc", "files")
OUT = os.path.join(ROOT, "extracted", "psp", "course")
SCALE = 1.0 / 32.0

# triangle -> four sprite corners P0..P3 as indices into (A, B, C)
TRI_CORNERS = {0: (0, 1, 2, 2), 1: (0, 0, 1, 2), 2: (0, 1, 1, 2), 3: (0, 1, 2, 2), 4: (0, 1, 2, 0)}


def pot(n):
    p = 32 if n <= 32 else 1
    while p < n:
        p <<= 1
    return p


def abgr_from_cmdcolr(c):
    r, g, b = (c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3
    r |= r >> 5
    g |= g >> 5
    b |= b >> 5
    return 0xFF000000 | (b << 16) | (g << 8) | r


def convert(name, preview=False):
    c = rcmodel.load_course(os.path.join(DISC, name + ".MAD"))
    table = list(c.tables.values())
    assert len(table) == 1, "expected one shared texture table"
    table = table[0]

    # textures actually used, de-duplicated by (bitmap, palette, size)
    tex_index, textures = {}, []

    def tex_of(entry):
        k = entry.key
        if k not in tex_index:
            tex_index[k] = len(textures)
            textures.append(entry)
        return tex_index[k]

    batches = {}          # texture index (or -1) -> list of vertex tuples
    nflat = ntex_poly = 0
    for part in c.parts:
        for poly in part.polys:
            v = [part.verts[i] for i in poly.idx]
            P = [(x * SCALE, y * SCALE, z * SCALE) for (x, y, z) in v]
            if poly.triangle:
                corners = [P[i] for i in TRI_CORNERS[poly.corner_mode]]
            else:
                corners = P[:4]
            if poly.textured:
                ent = table[poly.w1 // 8]
                ti = tex_of(ent)
                w, h = ent.width, ent.height
                tw, th = pot(w), pot(h)
                uv = [(0, 0), (w, 0), (w, h), (0, h)]
                if poly.hflip:
                    uv = [(w - u, vv) for u, vv in uv]
                if poly.vflip:
                    uv = [(u, h - vv) for u, vv in uv]
                uv = [(u / tw, vv / th) for u, vv in uv]
                col = 0xFFFFFFFF
                ntex_poly += 1
            else:
                ti = -1
                uv = [(0.0, 0.0)] * 4
                col = abgr_from_cmdcolr(poly.w1)
                nflat += 1
            out = batches.setdefault(ti, [])
            for tri in ((0, 1, 2), (0, 2, 3)):
                pts = [corners[i] for i in tri]
                if pts[0] == pts[1] or pts[1] == pts[2] or pts[0] == pts[2]:
                    continue                       # degenerate (triangle polygons repeat a corner)
                for i in tri:
                    out.append((uv[i][0], uv[i][1], col) + corners[i])

    # texture blobs
    tex_bytes = []
    for ent in textures:
        w, h = ent.width, ent.height
        tw, th = pot(w), pot(h)
        rows = rcmodel.bitmap(c, ent)
        pix = bytearray(tw * th // 2)
        for y in range(th):
            for x in range(tw):
                p = rows[min(y, h - 1)][min(x, w - 1)] if (y < h and x < w) else 0
                o = (y * tw + x) // 2
                if x & 1:
                    pix[o] |= p << 4
                else:
                    pix[o] |= p
        pal = rcmodel.palette(c, ent)
        clut = b"".join(struct.pack("<H", ((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | (0x8000 if a else 0)))
                        for r, g, b, a in pal)
        tex_bytes.append((w, h, tw, th, bytes(pix), clut))

    order = sorted(batches, key=lambda k: (k < 0, k))
    nverts = sum(len(batches[k]) for k in order)
    xs = [vt[3] for k in order for vt in batches[k]]
    ys = [vt[4] for k in order for vt in batches[k]]
    zs = [vt[5] for k in order for vt in batches[k]]
    hdr_size = 4 + 4 * 3 + 24 + 20 + 12
    tex_off = hdr_size
    batch_off = tex_off + 16 * len(textures)
    vert_off = batch_off + 12 * len(order)
    blob_off = vert_off + 24 * nverts
    blob = bytearray()
    texrec = bytearray()
    blob_off = (blob_off + 15) & ~15           # pixel data and palettes are 16-byte aligned (GE requirement)
    for (w, h, tw, th, pix, clut) in tex_bytes:
        po = blob_off + len(blob)
        blob += pix
        blob += b"\x00" * (-len(blob) % 16)
        co = blob_off + len(blob)
        blob += clut
        blob += b"\x00" * (-len(blob) % 16)
        texrec += struct.pack("<HHHHII", w, h, tw, th, po, co)
    batchrec = bytearray()
    verts = bytearray()
    first = 0
    for k in order:
        n = len(batches[k])
        batchrec += struct.pack("<III", 0xFFFFFFFF if k < 0 else k, first, n)
        for (u, v, col, x, y, z) in batches[k]:
            verts += struct.pack("<ffIfff", u, v, col, x, y, z)
        first += n
    def centre(part):
        n = len(part.verts)
        return tuple(sum(v[k] for v in part.verts) / n * SCALE for k in range(3))
    c0, c1 = centre(c.parts[0]), centre(c.parts[1])
    sx, sy, sz = c0
    ddx, ddz = c1[0] - c0[0], c1[2] - c0[2]
    ln = (ddx * ddx + ddz * ddz) ** 0.5 or 1.0
    ddx, ddz = ddx / ln, ddz / ln
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name.lower() + ".rcm")
    with open(path, "wb") as f:
        f.write(b"RCM1" + struct.pack("<III", len(textures), len(order), nverts))
        f.write(struct.pack("<6f", min(xs), min(ys), min(zs), max(xs), max(ys), max(zs)))
        f.write(struct.pack("<5f", sx, sy, sz, ddx, ddz))
        f.write(struct.pack("<III", tex_off, batch_off, vert_off))
        f.write(texrec)
        f.write(batchrec)
        f.write(verts)
        f.write(b"\x00" * (blob_off - (vert_off + len(verts))))
        f.write(blob)
    print(f"{name}: {len(c.parts)} parts, {sum(len(p.polys) for p in c.parts)} polygons ({ntex_poly} textured, {nflat} flat) -> {nverts // 3} triangles, "
          f"{len(textures)} textures of {len(table)} table entries, {len(order)} batches, {os.path.getsize(path):,} bytes, "
          f"bbox x {min(xs):.0f}..{max(xs):.0f} y {min(ys):.0f}..{max(ys):.0f} z {min(zs):.0f}..{max(zs):.0f} m")
    if preview:
        write_preview(name, c, textures, tex_bytes, batches, order, (min(xs), max(xs), min(zs), max(zs)))
    return path


def write_preview(name, c, textures, tex_bytes, batches, order, bb):
    from PIL import Image, ImageDraw
    adir = os.path.join(ROOT, "extracted", "analysis")
    # mean colour per texture -> top-down map
    mean = []
    for (w, h, tw, th, pix, clut) in tex_bytes:
        pal = [struct.unpack("<H", clut[2 * i:2 * i + 2])[0] for i in range(16)]
        cols = []
        for y in range(h):
            for x in range(w):
                o = (y * tw + x) // 2
                p = (pix[o] >> 4) if x & 1 else (pix[o] & 15)
                if p:
                    v = pal[p]
                    cols.append(((v & 31) << 3, ((v >> 5) & 31) << 3, ((v >> 10) & 31) << 3))
        n = max(1, len(cols))
        mean.append(tuple(sum(c_[i] for c_ in cols) // n for i in range(3)))
    S = 1.0
    W = int((bb[1] - bb[0]) * S) + 20
    H = int((bb[3] - bb[2]) * S) + 20
    img = Image.new("RGB", (W, H), (40, 60, 90))
    d = ImageDraw.Draw(img)
    for k in order:
        col = mean[k] if k >= 0 else None
        vs = batches[k]
        for i in range(0, len(vs), 3):
            pts = [((vs[i + j][3] - bb[0]) * S + 10, (bb[3] - vs[i + j][5]) * S + 10) for j in range(3)]
            fill = col if col is not None else tuple((vs[i][2] >> s) & 255 for s in (0, 8, 16))
            d.polygon(pts, fill=fill)
    img.save(os.path.join(adir, f"preview_{name.lower()}_top.png"))
    # texture sheet of the first 96 textures
    cell = 72
    cols_n = 12
    rows_n = (min(len(tex_bytes), 96) + cols_n - 1) // cols_n
    sheet = Image.new("RGBA", (cols_n * cell, rows_n * cell), (60, 0, 60, 255))
    for i, (w, h, tw, th, pix, clut) in enumerate(tex_bytes[:96]):
        pal = [struct.unpack("<H", clut[2 * i_:2 * i_ + 2])[0] for i_ in range(16)]
        im = Image.new("RGBA", (w, h))
        px = im.load()
        for y in range(h):
            for x in range(w):
                o = (y * tw + x) // 2
                p = (pix[o] >> 4) if x & 1 else (pix[o] & 15)
                v = pal[p]
                px[x, y] = (((v & 31) << 3), (((v >> 5) & 31) << 3), (((v >> 10) & 31) << 3), 255 if v & 0x8000 else 0)
        sheet.alpha_composite(im.crop((0, 0, min(w, cell), min(h, cell))), ((i % cols_n) * cell, (i // cols_n) * cell))
    sheet.save(os.path.join(adir, f"preview_{name.lower()}_textures.png"))


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    for n in (args or ["DESERT", "FOREST", "LAKE", "MOUNT"]):
        convert(n.upper(), preview="--preview" in sys.argv)
