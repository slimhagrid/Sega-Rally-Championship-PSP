"""VDP2 reference renderer for the race background (tools only, never part of the PSP build).  Engineering notes: docs/vdp2_background.md.

    python -I tools/vdp2.py --course 0 --pass 330          -> extracted/ref/desert_p330_full.png (VDP1 polygons over the VDP2 sky), + _bg.png
    python -I tools/vdp2.py --course 1 --static --yaw 0x4000 --roll 0 -> background only, no race run (bare machine), camera angles given

What the race shows on the VDP2 (all verified by running the game's own set-up code, see the doc):
  * RBG0, rotation parameter set A, 2048 x 2048 px plane (4 x 4 planes of one 512 x 512 page each, alternating two pages = one 1024 x 512 panorama),
    8x8 cells, 256 colours (palette 5 of colour RAM mode 1), colour 0 transparent, 1-word pattern names (12-bit character number, no flip).
  * NBG2 / NBG3 carry HUD text (name tables written by the display routines), NBG0 / NBG1 are off; they are NOT rendered here.
  * Sprite layer (VDP1 frame buffer) has priority 2, RBG0 priority 2 -> the sprite wins; both are below NBG0..3.
  * Per pass `rscroll_ctrl` -> `normal_rmove` -> `set_grpara2` turns the camera yaw into the X scroll (1024 px per turn) and the camera roll into a rotation
    of the plane around the screen centre; the vertical position is a per-course constant (the camera pitch is NOT used in a 1P race).

API
    st = build_state(race_or_course, yaw=None, pitch=None, roll=None)   # Vdp2State: regs / vram / cram after the game's own init + one scroll update
    rgb, mask = render_background(st_or_race)                            # (224, 352, 3) uint8 + bool mask (False = back screen colour shown)
    out = compose(vdp1_img, vdp1_drawn_mask, bg)                         # sprite layer over the background
"""
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from datfile import Scatter  # noqa: E402
from sh2 import CPU, Mem, SimError  # noqa: E402

DISC = os.path.join(ROOT, "extracted", "disc", "files")
COURSES = ["DESERT", "FOREST", "MOUNT", "LAKE"]
W, H = 352, 224
SP = 0x060FF000

# real addresses in 0RALLY.DAT (render_pipeline.md / vdp2_background.md)
A_INIT_VDP2SYS = 0x06005518      # vdp::init_vdp2sys: registers from datgbal::vdp2init_tbl (PRGDATA block 0x00298000)
A_INIT_GSCROLL = 0x06015B80      # scroll::init_gscroll: game rotation-parameter table, priorities, window; DMA2 table -> VRAM 0x7FF00
A_RSCROLL_CTRL = 0x06005E54      # gctrl::rscroll_ctrl: jmp [0x060AA9F0] (= plctrl::normal_rmove -> scroll::set_grpara2)
A_SET_GRPARA_INT = 0x06015FEC    # per-V-IN smoothing of the table at 0x060B901C towards the targets at 0x060AA9F8..
CAM = 0x060AA9B0                 # camera work: +0x14 pitch, +0x16 yaw, +0x18 roll (s16, 65536 = 360 deg)
BGON_RACE = 0x001C               # NBG2 + NBG3 + RBG0 (written by the display init, e.g. 0x0601159C; render_pipeline docs)


# ---------------------------------------------------------------------------------------------------------------------------------
# machine: game memory + SH-2, with the SCU DMA the V-IN handler would run
# ---------------------------------------------------------------------------------------------------------------------------------
def _clone(mem):
    m = Mem()
    m.hw[:] = mem.hw
    m.lw[:] = mem.lw
    m.pages = {k: bytearray(v) for k, v in mem.pages.items()}
    return m


def _bare_mem(course, game_type=2):
    """0RALLY.DAT + PRGDATA.DAT + BG<course>.DAT (all blocks, VDP2 VRAM included) and the few settings the menus write"""
    m = Mem()
    m.load(0x06004000, open(os.path.join(DISC, "0RALLY.DAT"), "rb").read())
    for fname in ("PRGDATA.DAT", "BG%s.DAT" % COURSES[course]):
        s = Scatter(os.path.join(DISC, fname))
        for b in s.blocks:
            m.load(b.dest, s.expanded(b))
    m.w16(0x060AA860, game_type)           # game mode (0/2 = 1P)
    m.w16(0x060AA87E, course)
    m.w16(0x060AA882, course * 4)
    m.w8(0x060AA810, 0)
    m.w8(0x060AA82E, 0)                    # mirror flag
    m.w32(0x060AA9F0, 0x06006BBC)          # init_game_work: rscroll handler = plctrl::normal_rmove
    return m


def _scu_dma(m):
    """SCU DMA (direct mode, software start) as the V-IN recipes use it: level n regs at 0x25FE0000 + 0x20 n: +0 read, +4 write, +8 count, +0x10 EN"""
    def watch(a, sz, v):
        a &= 0x1FFFFFFF
        if sz == 4 and a in (0x05FE0010, 0x05FE0030, 0x05FE0050) and (v & 1) and (v & 0x100):
            b = a - 0x10
            src, dst, cnt = m.r32(b), m.r32(b + 4), m.r32(b + 8)
            if a == 0x05FE0010:
                cnt &= 0xFFFFF
                cnt = cnt or 0x100000
            else:
                cnt &= 0xFFF
                cnt = cnt or 0x1000
            m.load(dst, m.read(src, cnt))
    m.watch = watch


class Vdp2State:
    """what the VDP2 holds when the frame is shown: registers (0x120 bytes), VRAM (512 KB), colour RAM (4 KB)"""

    def __init__(self, regs, vram, cram, info=None):
        self.regs = bytes(regs)
        self.vram = bytes(vram)
        self.cram = bytes(cram)
        self.info = info or {}

    def reg(self, off):
        return (self.regs[off] << 8) | self.regs[off + 1]


def build_state(src, course=None, yaw=None, pitch=None, roll=None, settle=None, bgon=BGON_RACE, game_type=2):
    """Run the game's own VDP2 set-up on a copy of the memory and return a Vdp2State.

    src: a saturn_env.Race (its memory at the current pass is used; the camera angles in it drive the scroll) or a course number 0..3 (bare machine; the
    camera angles default to 0 and can be given as yaw/pitch/roll in 16-bit units, 65536 = 360 deg).
    settle: None = use the targets the scroll update computed (what the PSP renderer does); n = run the game's per-V-IN smoothing `set_grpara_int` n times
    first (it halves the distance to the target, truncating to whole pixels, so ~30 ends 1 px below the target; the real display lags the target by a
    pass-history dependent fraction of a pixel to a few pixels)."""
    if hasattr(src, "mem"):
        m = _clone(src.mem)
        course = src.course
        racing = True
    else:
        course = int(src)
        m = _bare_mem(course, game_type)
        racing = False
    _scu_dma(m)
    cpu = CPU(m)

    def call(addr):
        cpu.r[15] = SP
        cpu.call(addr, maxsteps=2_000_000)

    if m.r8(0x25E60000) == 0 and m.r8(0x25E60001) == 0 and m.r32(0x25E60100) == 0:      # race built without render=True: load the course's VDP2 data
        s = Scatter(os.path.join(DISC, "BG%s.DAT" % COURSES[course]))
        for b in s.blocks:
            if b.dest >= 0x08000000 or b.dest == 0x060B8A00:
                m.load(b.dest, s.expanded(b))
    call(A_INIT_VDP2SYS)
    call(A_INIT_GSCROLL)
    for off, val in ((0x14, pitch), (0x16, yaw), (0x18, roll)):
        if val is not None:
            m.w16(CAM + off, val & 0xFFFF)
    if m.r32(0x060AA9F0) == 0:
        m.w32(0x060AA9F0, 0x06006BBC)
    call(A_RSCROLL_CTRL)
    tbl = 0x060B9000                                              # 1P game: the rotation parameter table the V-IN DMA2 copies to VRAM 0x7FF00
    if settle is None:
        a_t = m.r32(0x060AA9FC)                                   # targets of set_grpara2: Mx, A (= E), B, D
        m.w32(tbl + 0x44, m.r32(0x060AA9F8))
        m.w32(tbl + 0x1C, a_t)
        m.w32(tbl + 0x2C, a_t)
        m.w32(tbl + 0x20, m.r32(0x060AAA00))
        m.w32(tbl + 0x28, m.r32(0x060AAA04))
    else:
        for _ in range(settle):
            call(A_SET_GRPARA_INT)
    # V-IN recipe: DMA2 table -> VDP2 VRAM (rotation parameter table), DMA1 colour RAM shadow -> CRAM, BGON from the shadow word
    m.load(0x25E7FF00, m.read(m.r32(0x060AAA10) or 0x060B9000, 0x60))        # [0x060AAA10] = the buffer set_rpara last wrote (set by init_gspr; 1P game: 0x060B9000)
    m.load(0x25F00000, m.read(0x060B8000, 0x1000))
    m.w16(0x25F80020, bgon)
    m.w16(0x25E7FFFC, 0x0421)                                     # vdp::tv_on writes the back screen colour (tv_off: 0x7BDE)
    info = {"course": course, "yaw": m.r16(CAM + 0x16), "pitch": m.r16(CAM + 0x14), "roll": m.r16(CAM + 0x18),
            "mirror": m.r8(0x060AA82E), "game_type": m.r16(0x060AA860), "racing": racing}
    return Vdp2State(m.read(0x25F80000, 0x120), m.read(0x25E00000, 0x80000), m.read(0x25F00000, 0x1000), info)


# ---------------------------------------------------------------------------------------------------------------------------------
# RBG0
# ---------------------------------------------------------------------------------------------------------------------------------
def _sx(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def rotation_params(st):
    """decode rotation parameter table A from VRAM at RPTA (0x7FF00): all values as floats (16.16 -> units, matrix 1.0 = 1)"""
    rpta = (((st.reg(0xBC) & 7) << 16) | st.reg(0xBE)) * 2
    t = struct.unpack_from(">24I", st.vram, rpta)
    p = {}

    def f(v, mask, bits):
        return _sx(v & mask, bits) / 65536.0
    p["Xst"], p["Yst"], p["Zst"] = (f(t[i], 0x1FFFFFC0, 29) for i in range(3))
    p["dXst"], p["dYst"], p["dX"], p["dY"] = (f(t[i], 0x0007FFC0, 19) for i in range(3, 7))
    p["A"], p["B"], p["C"], p["D"], p["E"], p["F"] = (f(t[i], 0x000FFFC0, 20) for i in range(7, 13))
    pw = struct.unpack_from(">8H", st.vram, rpta + 0x34)
    p["Px"], p["Py"], p["Pz"] = (_sx(pw[i] & 0x3FFF, 14) for i in range(3))
    p["Cx"], p["Cy"], p["Cz"] = (_sx(pw[i] & 0x3FFF, 14) for i in range(4, 7))
    p["Mx"], p["My"] = (f(t[i], 0x3FFFFFC0, 30) for i in (17, 18))
    p["kx"], p["ky"] = (f(t[i], 0x00FFFFC0, 24) for i in (19, 20))
    p["addr"] = rpta
    return p


def _cram_rgb(st, idx):
    """colour RAM index array -> (r, g, b) uint8 arrays (mode 0/1: RGB555 words, 2: RGB888)"""
    mode = (st.reg(0x0E) >> 12) & 3
    cram = np.frombuffer(st.cram, dtype=">u2")
    if mode == 2:                                                 # RGB888: 4 bytes per colour (4 KB = 1024 colours)
        raw = np.frombuffer(st.cram, dtype=np.uint8).reshape(-1, 4)
        c = raw[idx & 0x3FF]
        return c[..., 3], c[..., 2], c[..., 1]
    v = cram[idx & (0x3FF if mode == 0 else 0x7FF)].astype(np.int32)
    r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
    return ((r << 3) | (r >> 2)).astype(np.uint8), ((g << 3) | (g >> 2)).astype(np.uint8), ((b << 3) | (b >> 2)).astype(np.uint8)


def rbg0_plane(st):
    """the whole RBG0 plane as colour RAM indexes: returns (idx (2048, 2048) uint16, opaque (2048, 2048) bool).  8x8 cells, 256 colours, 1-word names."""
    chctlb = st.reg(0x2A)
    pncr = st.reg(0x38)
    if ((chctlb >> 12) & 7) != 1 or (chctlb & 0x100) or (chctlb & 0x200) or not (pncr & 0x8000):
        raise NotImplementedError("RBG0 format (CHCTLB=%04x PNCR=%04x) is not the one the race uses" % (chctlb, pncr))
    auxmode = (pncr >> 14) & 1
    sup = pncr & 0x3FF
    plsz = (st.reg(0x3A) >> 8) & 3
    if plsz != 0:
        raise NotImplementedError("RBG0 plane size %d" % plsz)
    mpofr = st.reg(0x3E) & 7                                      # map offset (bits 2-0: parameter A)
    vram = np.frombuffer(st.vram, dtype=np.uint8)
    pages = {}
    rows = []
    for py in range(4):
        row = []
        for px in range(4):
            k = py * 4 + px
            w_ = st.reg(0x50 + 2 * (k // 2))
            mp = ((w_ >> 8) if (k & 1) else w_) & 0x3F
            mp |= mpofr << 6
            if mp not in pages:
                pages[mp] = _decode_page(st, vram, mp * 0x2000, auxmode, sup)
            row.append(pages[mp])
        rows.append(np.concatenate(row, axis=1))
    idx = np.concatenate(rows, axis=0)
    return idx, (idx & 0xFF) != 0


def _decode_page(st, vram, addr, auxmode, sup):
    pn = np.frombuffer(st.vram[addr:addr + 0x2000], dtype=">u2").astype(np.int64).reshape(64, 64)
    if auxmode:
        char = (pn & 0xFFF) | ((sup & 0x1C) << 10)
    else:
        char = (pn & 0x3FF) | ((sup & 0x1F) << 10)
    pal = (pn & 0x7000) >> 4                                      # 256-colour palette number -> colour index offset (x 0x100)
    cell = (char * 0x20)[:, :, None] + np.arange(64)[None, None, :]
    cells = vram[np.minimum(cell, 0x7FFFF)]                       # (64, 64, 64) dots of every 8x8 cell
    img = cells.reshape(64, 64, 8, 8).transpose(0, 2, 1, 3).reshape(512, 512).astype(np.uint16)
    palimg = np.repeat(np.repeat(pal, 8, axis=0), 8, axis=1).astype(np.uint16)
    return img | palimg


def nbg_plane(st, n):
    """NBG2 / NBG3 (the HUD text layers) as colour RAM indexes: the 2 x 2 planes of the layer (each one 64 x 64 page of 8x8 cells, 256 colours, 1-word names): returns
    (idx (1024, 1024) uint16, opaque bool).  Registers: CHCTLB 0x2A, PNCN2 / PNCN3 0x34 / 0x36, map numbers MPABN / MPCDN 0x48 + 4 (n - 2) (+ offset MPOFN 0x3C)."""
    chctlb = st.reg(0x2A)
    pnc = st.reg(0x34 + 2 * (n - 2))
    colours = (chctlb >> (1 if n == 2 else 5)) & 1
    if not colours or (chctlb & (1 if n == 2 else 0x10)) or not (pnc & 0x8000):
        raise NotImplementedError("NBG%d format (CHCTLB=%04x PNC=%04x) is not the one the race uses" % (n, chctlb, pnc))
    auxmode = (pnc >> 14) & 1
    sup = pnc & 0x3FF
    mpofn = (st.reg(0x3C) >> (8 + 4 * (n - 2))) & 7
    ab, cd = st.reg(0x48 + 4 * (n - 2)), st.reg(0x4A + 4 * (n - 2))
    maps = [ab & 0x3F, (ab >> 8) & 0x3F, cd & 0x3F, (cd >> 8) & 0x3F]         # planes A B / C D (the low byte of MPABN holds plane A, the high byte plane B)
    vram = np.frombuffer(st.vram, dtype=np.uint8)
    pages = {}
    rows = []
    for pr in range(2):
        row = []
        for pc in range(2):
            mp = maps[2 * pr + pc] | (mpofn << 6)
            if mp not in pages:
                pages[mp] = _decode_page(st, vram, mp * 0x2000, auxmode, sup)
            row.append(pages[mp])
        rows.append(np.concatenate(row, axis=1))
    idx = np.concatenate(rows, axis=0)
    return idx, (idx & 0xFF) != 0


def nbg0_plane(st):
    """NBG0 (the goal / game-over screens, docs/hud.md 7) as colour RAM indexes: the 2 x 2 planes (each one 64 x 64 page of 8x8 cells with two-word names, 256 colours): returns
    (idx (1024, 1024) uint16, opaque bool).  Registers: CHCTLA 0x28, PNCN0 0x30 (bit 15 clear = two-word names), PLSZ 0x3A, MPOFN 0x3C, map numbers MPABN0 / MPCDN0 0x40 / 0x42."""
    chctla = st.reg(0x28)
    if ((chctla >> 4) & 3) != 1 or (chctla & 1) or (chctla & 2) or (st.reg(0x30) & 0x8000) or (st.reg(0x3A) & 3):
        raise NotImplementedError("NBG0 format (CHCTLA=%04x PNCN0=%04x PLSZ=%04x) is not the one the end screens use" % (chctla, st.reg(0x30), st.reg(0x3A)))
    mpofn = st.reg(0x3C) & 7
    ab, cd = st.reg(0x40), st.reg(0x42)
    maps = [ab & 0x3F, (ab >> 8) & 0x3F, cd & 0x3F, (cd >> 8) & 0x3F]         # planes A B / C D: MPABN0 holds A in bits 5-0 and B in bits 13-8, MPCDN0 C and D
    vram = np.frombuffer(st.vram, dtype=np.uint8)
    rows = []
    for pr in range(2):
        row = []
        for pc in range(2):
            addr = (maps[2 * pr + pc] | (mpofn << 6)) * 0x4000
            pn = np.frombuffer(st.vram[addr:addr + 0x4000], dtype=">u4").astype(np.int64).reshape(64, 64)
            char = pn & 0x7FFF
            pal = ((pn >> 20) & 7) << 8
            cell = (char * 0x20)[:, :, None] + np.arange(64)[None, None, :]
            dots = vram[np.minimum(cell, 0x7FFFF)].reshape(64, 64, 8, 8)
            vf, hf = ((pn >> 31) & 1) != 0, ((pn >> 30) & 1) != 0
            dots = np.where(vf[:, :, None, None], dots[:, :, ::-1, :], dots)
            dots = np.where(hf[:, :, None, None], dots[:, :, :, ::-1], dots)
            img = dots.transpose(0, 2, 1, 3).reshape(512, 512).astype(np.uint16)
            row.append(img | np.repeat(np.repeat(pal, 8, axis=0), 8, axis=1).astype(np.uint16))
        rows.append(np.concatenate(row, axis=1))
    idx = np.concatenate(rows, axis=0)
    return idx, (idx & 0xFF) != 0


def render_hud(st):
    """NBG2 over NBG3 (equal priority: the lower number wins) as seen on screen, with their scroll positions (SCXN / SCYN 0x90 .. 0x96): (rgb (224, 352, 3), mask (224, 352))"""
    out = np.zeros((H, W, 3), dtype=np.uint8)
    mask = np.zeros((H, W), dtype=bool)
    bgon = st.reg(0x20)
    ys, xs = np.mgrid[0:H, 0:W]
    for n in (3, 2, 0):                                           # NBG3 first, NBG2 over it, NBG0 (the end-of-race screens) over both
        if not (bgon >> n) & 1:
            continue
        idx, op = nbg0_plane(st) if n == 0 else nbg_plane(st, n)
        scx, scy = (st.reg(0x70) & 0x3FF, st.reg(0x74) & 0x3FF) if n == 0 else (st.reg(0x90 + 4 * (n - 2)) & 0x3FF, st.reg(0x92 + 4 * (n - 2)) & 0x3FF)
        sy, sx = (ys + scy) & 1023, (xs + scx) & 1023
        ci, o = idx[sy, sx], op[sy, sx]
        r, g, b = _cram_rgb(st, ci)
        rgb = np.stack([r, g, b], axis=-1)
        out[o] = rgb[o]
        mask |= o
    return out, mask


def compose_hud(img, hud):
    """the HUD layers (priority 6) over the picture (VDP1 sprites + RBG0 + back screen)"""
    rgb, mask = hud
    out = np.array(img, dtype=np.uint8, copy=True)
    out[mask] = rgb[mask]
    return out


def render_background(src=None, st=None, plane=None, **kw):
    """RBG0 (+ back screen) as seen on screen: returns (rgb (224, 352, 3) uint8, mask (224, 352) bool: True where RBG0 is opaque; elsewhere the back screen
    colour is in rgb).  src: Race / course number (see build_state) or an already built Vdp2State."""
    if st is None:
        st = src if isinstance(src, Vdp2State) else build_state(src, **kw)
    p = rotation_params(st)
    idx, opaque = plane if plane is not None else rbg0_plane(st)
    h, v = np.meshgrid(np.arange(W, dtype=np.float64), np.arange(H, dtype=np.float64))
    xs = p["Xst"] + p["dXst"] * v + p["dX"] * h
    ys = p["Yst"] + p["dYst"] * v + p["dY"] * h
    zs = p["Zst"]
    xsp = p["A"] * (xs - p["Px"]) + p["B"] * (ys - p["Py"]) + p["C"] * (zs - p["Pz"])
    ysp = p["D"] * (xs - p["Px"]) + p["E"] * (ys - p["Py"]) + p["F"] * (zs - p["Pz"])
    xp = p["A"] * (p["Px"] - p["Cx"]) + p["B"] * (p["Py"] - p["Cy"]) + p["C"] * (p["Pz"] - p["Cz"]) + p["Cx"] + p["Mx"]
    yp = p["D"] * (p["Px"] - p["Cx"]) + p["E"] * (p["Py"] - p["Cy"]) + p["F"] * (p["Pz"] - p["Cz"]) + p["Cy"] + p["My"]
    X = (p["kx"] * xsp + xp)
    Y = (p["ky"] * ysp + yp)
    ix = np.floor(X).astype(np.int64) & 2047
    iy = np.floor(Y).astype(np.int64) & 2047
    ci = idx[iy, ix]
    op = opaque[iy, ix] & bool(st.reg(0x20) & 0x10)
    r, g, b = _cram_rgb(st, ci)
    rgb = np.stack([r, g, b], axis=-1)
    bk = back_screen(st)
    rgb[~op] = bk
    return rgb, op


def back_screen(st):
    """single-colour back screen (BKTA: VRAM address, one RGB555 word)"""
    a = (((st.reg(0xAC) & 7) << 16) | st.reg(0xAE)) * 2
    v = (st.vram[a] << 8) | st.vram[a + 1]
    r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
    return ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2))


def compose(vdp1_img, vdp1_drawn_mask, bg):
    """sprite layer (VDP1 frame buffer) over the background: where VDP1 drew nothing the background shows.  bg = rgb array or the (rgb, mask) tuple."""
    if isinstance(bg, tuple):
        bg = bg[0]
    out = np.array(bg, dtype=np.uint8, copy=True)
    m = np.asarray(vdp1_drawn_mask, dtype=bool)
    out[m] = np.asarray(vdp1_img)[m]
    return out


# ---------------------------------------------------------------------------------------------------------------------------------
def main():
    a = sys.argv[1:]

    def opt(n, d):
        if n not in a:
            return d
        return int(a[a.index(n) + 1], 0) if isinstance(d, int) else type(d)(a[a.index(n) + 1])
    course, npass = opt("--course", 0), opt("--pass", 330)
    out_dir = os.path.join(ROOT, "extracted", "ref")
    os.makedirs(out_dir, exist_ok=True)
    from PIL import Image
    name = "%s_p%d" % (COURSES[course].lower(), npass)
    if "--static" in a:
        kw = {k: opt("--" + k, -1) for k in ("yaw", "pitch", "roll")}
        kw = {k: v for k, v in kw.items() if v != -1}
        st = build_state(course, **kw)
        rgb, mask = render_background(st)
        Image.fromarray(rgb).save(os.path.join(out_dir, "%s_static_bg.png" % COURSES[course].lower()))
        print("static background saved, info", st.info)
        return
    import refframe
    import vdp1
    race, vram, info = refframe.capture(course, npass, opt("--type", 2))
    v1 = vdp1.Vdp1(vram).run(0)
    st = build_state(race)
    bg, mask = render_background(st)
    full = compose(v1.image(), v1.drawn, bg)
    Image.fromarray(bg).save(os.path.join(out_dir, name + "_bg.png"))
    Image.fromarray(full).save(os.path.join(out_dir, name + "_full.png"))
    print("%s pass %d: yaw %04x roll %04x -> %s_full.png  (%s)" % (COURSES[course], npass, st.info["yaw"], st.info["roll"], name, st.info))


if __name__ == "__main__":
    main()
