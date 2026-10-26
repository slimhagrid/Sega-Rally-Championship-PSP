"""Convert the race background (VDP2 RBG0 sky / horizon panorama) of the four courses to `extracted/psp/course/<name>.rbg`.

    python -I tools/conv_bg.py [DESERT ...]       (default: all four)        --preview also writes extracted/analysis/preview_<name>_bg.png

Source: `BG<COURSE>.DAT` (VDP2 character data + name table + 256-colour palette) and the game's own VDP2 set-up code, run in the SH-2 interpreter by
`tools/vdp2.py` (docs/vdp2_background.md).  The race background is ONE 1024 x 512 pixel panorama (360 degrees of camera yaw), 256 colours, dot 0 transparent.

RBG1 file, little endian:
    0x00  char[4]  "RBG1"
    0x04  u16 pano_w (1024)   u16 pano_h (512)   u16 tex_w (512)   u16 tex_h (512)
    0x0C  u16 ntex (2)        u16 flags          (bit 0: dot 0 is transparent -> show the back colour; always 1)
    0x10  f32 base_x          f32 base_y         panorama coordinate (pixels) of the screen's TOP-LEFT corner at yaw 0 / roll 0 (screen 352 x 224 px):
                                                 base_x = Mx0 (the Mx of the rotation table) and base_y = My0; the centre sample is (base_x + 176, base_y + 112)
    0x18  f32 yaw_px_per_unit                    1/64: panorama x advance per unit of the 16-bit camera yaw (65536 = one turn = 1024 px)
    0x1C  f32 roll_scale                         0.5: the plane is rotated by roll16 * roll_scale (16-bit angle units; 65536 = 360 deg) about the screen centre
    0x20  u16 screen_w (352)  u16 screen_h (224)  u16 centre_x (176)  u16 centre_y (112)
    0x28  u16 back_color (RGBA5551, PSP order: R bits 0-4, G 5-9, B 10-14, A bit 15)    u16 pad
    0x2C  u32 clut_off        u32 tex_off[0]  u32 tex_off[1]            (offsets from the file start; 0x40 bytes of header (padded), then the data)
    clut   : 256 x u16 RGBA5551 (PSP GU_PSM_5551; entry 0 = 0x0000 = transparent)
    tex[i] : tex_w x tex_h bytes, PSP GU_PSM_T8 (one palette index per byte, row-major); tex[0] = panorama x 0..511, tex[1] = x 512..1023
Display rule (what the VDP2 does, section 4 of the doc): with  px = base_x + yaw16 / 64   (yaw16 = unsigned 16-bit camera yaw, negate both yaw and roll
in mirror mode),  theta = roll16 * roll_scale * 2 pi / 65536,  screen pixel (x, y) shows the panorama pixel
    u = (px + 176 + cos(theta) * (x - 176) + sin(theta) * (y - 112))  mod 1024
    v = (base_y + 112 - sin(theta) * (x - 176) + cos(theta) * (y - 112))  mod 512
For roll = 0 that is a plain horizontal scroll: draw the panorama so that column u = px is at screen x = 0 (wrap at 1024, i.e. split the quad at the
seam between the two textures), rows base_y .. base_y + 223 are visible (wrapping at 512).  Pixels with palette index 0 show the back colour.
"""
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import vdp2  # noqa: E402

OUT = os.path.join(ROOT, "extracted", "psp", "course")
HDR = 0x40                                   # header padded to 64 bytes so the palette and both bitmaps are 16-byte aligned (GE requirement)


def rgba5551(word):
    return (word & 0x7FFF) | 0x8000


def convert(course, preview=False):
    st = vdp2.build_state(course, yaw=0, roll=0, pitch=0)
    p = vdp2.rotation_params(st)
    idx, _ = vdp2.rbg0_plane(st)
    pano = idx[:512, :1024]
    # the 2048 x 2048 plane must be this panorama tiled (2 pages alternating, rows identical)
    assert (idx == np.tile(pano, (4, 2))).all(), "plane is not a tiled 1024x512 panorama"
    pal = np.unique(pano >> 8)
    assert len(pal) == 1, "more than one palette used: %s" % pal
    base = int(pal[0]) * 256
    cram = np.frombuffer(st.cram, dtype=">u2")[base:base + 256].astype(np.uint16)
    clut = np.array([0] + [rgba5551(int(c)) for c in cram[1:]], dtype="<u2")
    dots = (pano & 0xFF).astype(np.uint8)
    # panorama x of the screen's left edge at yaw 0 = Mx (the rotation is about the screen centre; for roll 0 the centre sample is Mx + 176)
    base_x, base_y = p["Mx"], p["My"]
    bk = vdp2.back_screen(st)
    back = ((bk[0] >> 3) | ((bk[1] >> 3) << 5) | ((bk[2] >> 3) << 10)) | 0x8000
    tw = th = 512
    tex = [np.ascontiguousarray(dots[:, 0:512]), np.ascontiguousarray(dots[:, 512:1024])]
    ntex = 2
    clut_off = HDR
    tex_off = [clut_off + 512 + i * tw * th for i in range(ntex)]
    head = b"RBG1" + struct.pack("<HHHH", 1024, 512, tw, th) + struct.pack("<HH", ntex, 1)
    head += struct.pack("<ffff", base_x, base_y, 1.0 / 64.0, 0.5)
    head += struct.pack("<HHHH", vdp2.W, vdp2.H, 176, 112) + struct.pack("<HH", back, 0)
    head += struct.pack("<III", clut_off, tex_off[0], tex_off[1])
    head += bytes(HDR - len(head))
    assert len(head) == HDR, len(head)
    name = vdp2.COURSES[course].lower()
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + ".rbg")
    with open(path, "wb") as f:
        f.write(head + clut.tobytes() + b"".join(t.tobytes() for t in tex))
    print("%-6s -> %s  (%d bytes; base (%.1f, %.1f), %d colours used, dot 0 pixels %d)" % (
        vdp2.COURSES[course], path, os.path.getsize(path), base_x, base_y, len(np.unique(dots)), int((dots == 0).sum())))
    if preview:
        from PIL import Image
        r, g, b = vdp2._cram_rgb(st, pano)
        d = os.path.join(ROOT, "extracted", "analysis")
        os.makedirs(d, exist_ok=True)
        Image.fromarray(np.stack([r, g, b], -1)).save(os.path.join(d, "preview_%s_bg.png" % name))
    return path


def check(course):
    """read the file back and compare with the VDP2 reference renderer for a few yaw values (roll 0 and roll != 0)"""
    name = vdp2.COURSES[course].lower()
    d = open(os.path.join(OUT, name + ".rbg"), "rb").read()
    assert d[:4] == b"RBG1"
    pw, ph, tw, th, ntex, flags = struct.unpack_from("<HHHHHH", d, 4)
    bx, by, ypu, rs = struct.unpack_from("<ffff", d, 0x10)
    clut_off, t0, t1 = struct.unpack_from("<III", d, 0x2C)
    clut = np.frombuffer(d, dtype="<u2", count=256, offset=clut_off)
    pano = np.concatenate([np.frombuffer(d, np.uint8, tw * th, t).reshape(th, tw) for t in (t0, t1)], axis=1)
    bad = 0
    for yaw, roll in ((0, 0), (0x1234, 0), (0xC000, 0), (0x5555, 0x1800), (0xFFFF, 0xF800)):
        st = vdp2.build_state(course, yaw=yaw, roll=roll)
        ref, mask = vdp2.render_background(st)
        # decoder of the documented display rule
        sroll = roll - 65536 if roll & 0x8000 else roll
        th_ = sroll * rs * 2 * np.pi / 65536.0
        yy, xx = np.mgrid[0:224, 0:352].astype(np.float64)
        px = bx + (yaw >> 6)
        u = np.floor(px + 176 + np.cos(th_) * (xx - 176) + np.sin(th_) * (yy - 112)).astype(int) % 1024
        v = np.floor(by + 112 - np.sin(th_) * (xx - 176) + np.cos(th_) * (yy - 112)).astype(int) % 512
        ci = pano[v, u]
        c = clut[ci].astype(np.int32)
        r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
        rgb = np.stack([(r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2)], -1)
        rgb[ci == 0] = ref[~mask][0] if (~mask).any() else 0
        diff = (np.abs(rgb.astype(int) - ref.astype(int)).sum(-1) > 0).mean()
        print("  %s yaw %04x roll %04x: %.3f%% pixels differ from the VDP2 renderer" % (name, yaw, roll, 100 * diff))
        bad += diff > (0.0 if roll == 0 else 0.12)          # the game's sine table makes A..E multiples of 1/256, so rotated frames differ by a few pixels
    return bad == 0


def main():
    a = sys.argv[1:]
    names = [x.upper() for x in a if not x.startswith("--")] or vdp2.COURSES
    ok = True
    for n in names:
        c = vdp2.COURSES.index(n)
        convert(c, "--preview" in a)
        if "--check" in a:
            ok &= check(c)
    if not ok:
        sys.exit("check failed")


if __name__ == "__main__":
    main()
