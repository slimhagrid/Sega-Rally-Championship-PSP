"""M8: a general VDP2 reference renderer for the menu screens (tools only): NBG0 - NBG3 and RBG0 (as a scrolled / affine tile or bitmap layer), 16 / 256 / 2048 colour and 32K
cells, one- and two-word pattern names, 8x8 and 16x16 characters, plane sizes 1x1 .. 2x2, bitmaps, priorities, back screen, colour offset.  Layers are composed over the back
screen by priority (equal priority: RBG0 over NBG0 over NBG1 over NBG2 over NBG3).  The VDP1 sprite layer is not drawn here.

    from vdp2_screen import render, state_of
    img = render(state_of(menu.mem))        # (224, 352, 3) uint8
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from vdp2 import Vdp2State, _cram_rgb, back_screen  # noqa: E402

W, H = 352, 224


def state_of(mem):
    """the VDP2 state out of an emulated memory: the register block, VRAM, colour RAM"""
    cram = mem.read(0x25F00000, 0x1000)
    if not any(cram):                                           # the V-IN recipe has not copied the colour RAM shadow yet
        cram = mem.read(0x060B8000, 0x1000)
    return Vdp2State(mem.read(0x25F80000, 0x120), mem.read(0x25E00000, 0x80000), cram)


def _hw(st):
    return ((st.reg(0x00) >> 0) & 7) in (1, 3) and 352 or 320


def _planes(st, n):
    """tile layer geometry for NBG n (0..3)"""
    ctl = st.reg(0x28 if n < 2 else 0x2A)
    sh = 8 * (n & 1) if n < 2 else 4 * (n - 2)
    return ctl, sh


def _cells(st, n):
    """-> dict of the layer's format: char size (1 = 8x8, 2 = 16x16), colours (16 / 256 / 2048 / 32768 / 16M), bitmap flag, bitmap size"""
    if n < 2:
        ctl = st.reg(0x28) >> (8 * n)
        chsz = 2 if ctl & 1 else 1
        bm = (ctl >> 1) & 1
        bmsz = ((ctl >> 2) & 3)
        cn = (ctl >> 4) & 7
    elif n < 4:
        ctl = st.reg(0x2A) >> (4 * (n - 2))
        chsz = 2 if ctl & 1 else 1
        bm, bmsz = 0, 0
        cn = 1 if (ctl >> 1) & 1 else 0
        if cn == 0:
            cn = 0
    else:                                                       # RBG0
        ctl = st.reg(0x2A)
        chsz = 2 if (ctl >> 8) & 1 else 1
        bm = (ctl >> 9) & 1
        bmsz = (ctl >> 10) & 1
        cn = (ctl >> 12) & 7
    colours = {0: 16, 1: 256, 2: 2048, 3: 32768, 4: 1 << 24}.get(cn, 256)
    return dict(chsz=chsz, bitmap=bm, bmsz=bmsz, colours=colours)


def _dots(vram, base, ncell, colours):
    """8x8 dots (ncell, 8, 8) of the cells starting at VRAM byte offsets `base` (array), as colour indexes (low bits only, no palette)"""
    if colours == 16:
        idx = base[:, None] + np.arange(32)[None, :]
        b = vram[np.minimum(idx, len(vram) - 1)]
        d = np.empty((len(base), 64), dtype=np.uint16)
        d[:, 0::2] = b >> 4
        d[:, 1::2] = b & 15
        return d.reshape(-1, 8, 8)
    if colours == 256:
        idx = base[:, None] + np.arange(64)[None, :]
        return vram[np.minimum(idx, len(vram) - 1)].astype(np.uint16).reshape(-1, 8, 8)
    idx = base[:, None] + np.arange(128)[None, :]
    b = vram[np.minimum(idx, len(vram) - 1)].astype(np.uint32)
    w = (b[:, 0::2] << 8) | b[:, 1::2]
    return w.reshape(-1, 8, 8).astype(np.uint16)


def _tile_plane(st, n):
    """a tile layer as (colour index array, opaque array, plane w, plane h): the planes of the layer joined"""
    f = _cells(st, n)
    vram = np.frombuffer(st.vram, dtype=np.uint8)
    pncr = st.reg(0x30 + 2 * n) if n < 4 else st.reg(0x38)
    one_word = bool(pncr & 0x8000)
    aux = (pncr >> 14) & 1
    sup = pncr & 0x3FF
    chsz = f["chsz"]
    cells_per_plane = 64 // chsz
    plsz = (st.reg(0x3A) >> (2 * n)) & 3 if n < 4 else (st.reg(0x3A) >> 8) & 3
    pw, ph = (1, 1) if plsz == 0 else (2, 1) if plsz == 1 else (2, 2)
    if n < 4:
        mpofn = (st.reg(0x3C) >> (4 * n)) & 7
        base_reg = 0x40 + 4 * n
        maps = []
        for k in range(4):
            w_ = st.reg(base_reg + 2 * (k // 2))
            maps.append(((w_ >> 8) if (k & 1) else w_) & 0x3F)
        maps = [m | (mpofn << 6) for m in maps]
        grid = [maps[0], maps[1], maps[2], maps[3]]
        ncol, nrow = pw, ph
    else:                                                       # RBG0 parameter A: 16 maps (4 x 4 planes)
        mpofr = st.reg(0x3E) & 7
        grid = []
        for k in range(16):
            w_ = st.reg(0x50 + 2 * (k // 2))
            grid.append((((w_ >> 8) if (k & 1) else w_) & 0x3F) | (mpofr << 6))
        ncol, nrow = 4, 4
        pw, ph = 1, 1
        if plsz:
            pw, ph = (2, 1) if plsz == 1 else (2, 2)
    name_bytes = 2 if one_word else 4
    plane_bytes = cells_per_plane * cells_per_plane * name_bytes
    rows = []
    opaque_rows = []
    for pr in range(nrow if n < 4 else 4):
        row, orow = [], []
        for pc in range(ncol if n < 4 else 4):
            mp = grid[pr * 2 + pc] if n < 4 else grid[pr * 4 + pc]
            addr = mp * plane_bytes
            if one_word:
                pn = np.frombuffer(st.vram[addr:addr + plane_bytes], dtype=">u2").astype(np.int64).reshape(cells_per_plane, cells_per_plane)
                if aux:
                    char = (pn & 0xFFF) | ((sup & 0x1C) << 10)
                    vf = hf = np.zeros_like(pn, dtype=bool)
                else:
                    char = (pn & 0x3FF) | ((sup & 0x1F) << 10)
                    vf, hf = ((pn >> 11) & 1) != 0, ((pn >> 10) & 1) != 0
                if f["colours"] == 16:
                    pal = ((pn >> 12) & 15) | ((sup >> 5 & 7) << 4)
                    palbase = pal << 4
                else:
                    pal = (pn >> 12) & 7
                    palbase = pal << 8
            else:
                pn = np.frombuffer(st.vram[addr:addr + plane_bytes], dtype=">u4").astype(np.int64).reshape(cells_per_plane, cells_per_plane)
                char = pn & 0x7FFF
                vf, hf = ((pn >> 31) & 1) != 0, ((pn >> 30) & 1) != 0
                pal = (pn >> 16) & 0x7F
                palbase = pal << 4 if f["colours"] == 16 else ((pal >> 4) & 7) << 8
            bytes_per_cell = {16: 32, 256: 64}.get(f["colours"], 128)
            if chsz == 1:
                base = (char * 0x20).reshape(-1)
                if one_word and not aux:
                    pass
                dots = _dots(vram, base, len(base), f["colours"]).reshape(cells_per_plane, cells_per_plane, 8, 8)
                dots = np.where(vf[:, :, None, None], dots[:, :, ::-1, :], dots)
                dots = np.where(hf[:, :, None, None], dots[:, :, :, ::-1], dots)
                img = dots.transpose(0, 2, 1, 3).reshape(cells_per_plane * 8, cells_per_plane * 8)
                pimg = np.repeat(np.repeat(palbase, 8, axis=0), 8, axis=1)
            else:                                               # 16x16 characters: four cells (n, n+1, n+2, n+3 in cell units of the character number x 4)
                base0 = (char * 0x20).reshape(-1)
                sub = []
                for k in range(4):
                    sub.append(_dots(vram, base0 + k * bytes_per_cell, len(base0), f["colours"]).reshape(cells_per_plane, cells_per_plane, 8, 8))
                ch = np.empty((cells_per_plane, cells_per_plane, 16, 16), dtype=np.uint16)
                ch[:, :, :8, :8], ch[:, :, :8, 8:], ch[:, :, 8:, :8], ch[:, :, 8:, 8:] = sub
                ch = np.where(vf[:, :, None, None], ch[:, :, ::-1, :], ch)
                ch = np.where(hf[:, :, None, None], ch[:, :, :, ::-1], ch)
                img = ch.transpose(0, 2, 1, 3).reshape(cells_per_plane * 16, cells_per_plane * 16)
                pimg = np.repeat(np.repeat(palbase, 16, axis=0), 16, axis=1)
            idx = (img.astype(np.uint32) + pimg.astype(np.uint32)).astype(np.uint32)
            mask = (img & ({16: 15, 256: 255}.get(f["colours"], 0x7FF))) != 0
            row.append(idx)
            orow.append(mask)
        rows.append(np.concatenate(row, axis=1))
        opaque_rows.append(np.concatenate(orow, axis=1))
    return np.concatenate(rows, axis=0), np.concatenate(opaque_rows, axis=0)


def _bitmap(st, n):
    f = _cells(st, n)
    vram = np.frombuffer(st.vram, dtype=np.uint8)
    if n < 2:
        sizes = {0: (512, 256), 1: (512, 512), 2: (1024, 256), 3: (1024, 512)}
        w, h = sizes[f["bmsz"]]
        mpofn = (st.reg(0x3C) >> (4 * n)) & 7
        addr = mpofn * 0x20000
        pal = (st.reg(0x2C + 2 * n) & 7) << 8 if f["colours"] == 256 else (st.reg(0x2C + 2 * n) & 7) << 4
    else:
        w, h = (512, 256) if f["bmsz"] == 0 else (512, 512)
        addr = (st.reg(0x3E) & 7) * 0x20000
        pal = (st.reg(0x2E) & 7) << 8 if f["colours"] == 256 else (st.reg(0x2E) & 7) << 4
    if f["colours"] == 256:
        d = vram[addr:addr + w * h].astype(np.uint32)
        mask = d != 0
        idx = d + pal
    elif f["colours"] == 16:
        b = vram[addr:addr + w * h // 2]
        d = np.empty(w * h, dtype=np.uint32)
        d[0::2] = b >> 4
        d[1::2] = b & 15
        mask = d != 0
        idx = d + pal
    else:
        raise NotImplementedError("bitmap colours %d" % f["colours"])
    return idx.reshape(h, w), mask.reshape(h, w)


def _layer(st, n):
    f = _cells(st, n)
    if f["bitmap"]:
        idx, mask = _bitmap(st, n)
    else:
        idx, mask = _tile_plane(st, n)
    return idx, mask, f


def _scroll(st, n):
    if n == 0:
        return st.reg(0x70) & 0x7FF, st.reg(0x74) & 0x7FF
    if n == 1:
        return st.reg(0x80) & 0x7FF, st.reg(0x84) & 0x7FF
    return st.reg(0x90 + 4 * (n - 2)) & 0x7FF, st.reg(0x92 + 4 * (n - 2)) & 0x7FF


def _rbg_params(st):
    from vdp2 import rotation_params
    return rotation_params(st)


def render(st, width=W, height=H, layers=None, with_back=True, offset=False):
    """compose the layers: returns (height, width, 3) uint8"""
    bgon = st.reg(0x20)
    prio = {0: st.reg(0xF8) & 7, 1: (st.reg(0xF8) >> 8) & 7, 2: st.reg(0xFA) & 7, 3: (st.reg(0xFA) >> 8) & 7, 4: st.reg(0xFC) & 7}
    order = []
    for n in (3, 2, 1, 0, 4):                                    # drawing order at equal priority: later = on top
        on = (bgon >> n) & 1 if n < 4 else (bgon >> 4) & 1
        if layers is not None and n not in layers:
            continue
        if on and prio[n] > 0:
            order.append((prio[n], n))
    order.sort(key=lambda t: (t[0], [3, 2, 1, 0, 4].index(t[1])))
    bk = back_screen(st)
    out = np.zeros((height, width, 3), dtype=np.uint8)
    if with_back:
        out[:] = np.array(bk, dtype=np.uint8)
    ys, xs = np.mgrid[0:height, 0:width]
    for _, n in order:
        try:
            idx, mask, f = _layer(st, n)
        except NotImplementedError:
            continue
        ph, pw = idx.shape
        if n < 4:
            scx, scy = _scroll(st, n)
            zx = st.reg(0x78 + 8 * n) if n < 2 else 0
            sy, sx = (ys + scy) % ph, (xs + scx) % pw
        else:
            p = _rbg_params(st)
            h = np.arange(width, dtype=np.float64)[None, :]
            v = np.arange(height, dtype=np.float64)[:, None]
            xs_ = p["Xst"] + p["dXst"] * v + p["dX"] * h
            ys_ = p["Yst"] + p["dYst"] * v + p["dY"] * h
            X = p["A"] * (xs_ - p["Px"]) + p["B"] * (ys_ - p["Py"]) + p["A"] * (p["Px"] - p["Cx"]) + p["B"] * (p["Py"] - p["Cy"]) + p["Cx"] + p["Mx"]
            Y = p["D"] * (xs_ - p["Px"]) + p["E"] * (ys_ - p["Py"]) + p["D"] * (p["Px"] - p["Cx"]) + p["E"] * (p["Py"] - p["Cy"]) + p["Cy"] + p["My"]
            sx = np.floor(X).astype(np.int64) % pw
            sy = np.floor(Y).astype(np.int64) % ph
        ci, o = idx[sy, sx], mask[sy, sx]
        if f["colours"] == 32768:
            v = ci.astype(np.int32)
            r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
            rr, gg, bb = ((r << 3) | (r >> 2)).astype(np.uint8), ((g << 3) | (g >> 2)).astype(np.uint8), ((b << 3) | (b >> 2)).astype(np.uint8)
        else:
            rr, gg, bb = _cram_rgb(st, ci.astype(np.int64))
        rgb = np.stack([rr, gg, bb], axis=-1)
        out[o] = rgb[o]
    # colour offset (A): COAR / COAG / COAB 0x114 / 0x116 / 0x118 (9 bit signed), enabled by CLOFEN 0x110 bit 6 (back) etc. - applied to everything here when any layer bit is set
    if offset and st.reg(0x110) & 0x7F:
        def sx9(v):
            v &= 0x1FF
            return v - 512 if v >= 256 else v
        off = np.array([sx9(st.reg(0x114)), sx9(st.reg(0x116)), sx9(st.reg(0x118))], dtype=np.int32)
        out = np.clip(out.astype(np.int32) + off, 0, 255).astype(np.uint8)
    return out
