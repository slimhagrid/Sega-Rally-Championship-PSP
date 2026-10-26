"""M5: a small software VDP1 (tools only): executes a command table (as it sits in VDP1 VRAM) and returns the 352x224 frame buffer as RGB.

    img = render(vram)          # vram: bytes of the 512 KB VDP1 VRAM; returns uint8 array (224, 352, 3)

Implemented: command chain (JP next / assign / call / return, SKIP), end, system clip, user clip (inside / outside), local coordinate,
normal / scaled / distorted sprites (the game only uses distorted: cmd 2), polygons (cmd 4) and polylines/lines (drawn as lines),
16-colour LUT / 64 / 128 / 256 bank / RGB texels, transparency (colour code 0 unless SPD), H/V flip, gouraud (per-corner table, texel + (g - 16) per 5-bit
channel, clamped), mesh (checkerboard).  Not implemented: half-luminance / half-transparency / shadow (no data uses them, render_pipeline.md 8.1), end codes.
A textured quad is drawn the way the chip does it: along the edges A-D and B-C, one line of texels per step (here oversampled x2 so no holes appear).
"""
import struct

import numpy as np

W, H = 352, 224


def _s16(v):
    return v - 65536 if v & 0x8000 else v


def rgb555(v):
    """VDP1 RGB word (red in the low bits) -> (r, g, b) 0..255"""
    return ((v & 31) << 3, ((v >> 5) & 31) << 3, ((v >> 10) & 31) << 3)


class Vdp1:
    def __init__(self, vram):
        self.vram = vram
        self.fb = np.zeros((H, W), dtype=np.uint16)          # RGB555 + 0x8000 flag where drawn
        self.drawn = np.zeros((H, W), dtype=bool)
        self.sys_clip = (0, 0, W - 1, H - 1)
        self.user_clip = (0, 0, W - 1, H - 1)
        self.local = (0, 0)
        self.ncmd = 0

    def u16(self, a):
        return (self.vram[a] << 8) | self.vram[a + 1]

    def run(self, start=0, maxcmds=4000):
        i = start
        stack = []
        for _ in range(maxcmds):
            a = i * 8
            ctrl = self.u16(a)
            link = self.u16(a + 2)
            if ctrl & 0x8000:
                break
            jp = (ctrl >> 12) & 7
            if not (jp & 4):
                self.exec(a, ctrl)
            if jp & 3 == 0:
                i += 4
            elif jp & 3 == 1:
                i = link
            elif jp & 3 == 2:
                stack.append(i + 4)
                i = link
            else:
                if not stack:
                    break
                i = stack.pop()
        return self

    # --- commands ----------------------------------------------------------------------------------------------------------
    def exec(self, a, ctrl):
        cmd = ctrl & 15
        pm = self.u16(a + 4)
        p = [(_s16(self.u16(a + 0xC + 4 * k)), _s16(self.u16(a + 0xE + 4 * k))) for k in range(4)]
        if cmd == 8:
            self.user_clip = (p[0][0], p[0][1], p[2][0], p[2][1])
        elif cmd == 9:
            self.sys_clip = (0, 0, p[2][0], p[2][1])
        elif cmd == 10:
            self.local = p[0]
        elif cmd in (0, 1, 2, 4, 5, 6):
            self.ncmd += 1
            lx, ly = self.local
            if cmd == 0:                                         # normal sprite: A = top-left, size from CMDSIZE
                size = self.u16(a + 0xA)
                w, h = ((size >> 8) & 0x3F) * 8, size & 0xFF
                x, y = p[0]
                q = [(x, y), (x + w - 1, y), (x + w - 1, y + h - 1), (x, y + h - 1)]
            elif cmd == 1:                                       # scaled sprite: zoom point is rarely used; A + C corners
                (xa, ya), (xc, yc) = p[0], p[2]
                q = [(xa, ya), (xc, ya), (xc, yc), (xa, yc)]
            else:
                q = p
            q = [(x + lx, y + ly) for x, y in q]
            if cmd in (5, 6):
                self.polyline(q if cmd == 5 else q[:2], pm, a, cmd == 5)
            elif cmd == 4:
                self.quad(q, pm, a, textured=False)
            else:
                self.quad(q, pm, a, textured=True, ctrl=ctrl)

    def clip_mask(self, xs, ys, pm):
        sx0, sy0, sx1, sy1 = self.sys_clip
        ok = (xs >= sx0) & (xs <= sx1) & (ys >= sy0) & (ys <= sy1)
        if pm & 0x400:                                           # user clip enabled
            ux0, uy0, ux1, uy1 = self.user_clip
            inside = (xs >= ux0) & (xs <= ux1) & (ys >= uy0) & (ys <= uy1)
            ok &= ~inside if (pm & 0x200) else inside
        return ok

    def polyline(self, pts, pm, a, close):
        col = self.u16(a + 6)
        n = len(pts)
        for k in range(n if close else n - 1):
            (x0, y0), (x1, y1) = pts[k], pts[(k + 1) % n]
            m = max(abs(x1 - x0), abs(y1 - y0)) + 1
            t = np.linspace(0, 1, 2 * m)
            xs = np.round(x0 + (x1 - x0) * t).astype(int)
            ys = np.round(y0 + (y1 - y0) * t).astype(int)
            ok = self.clip_mask(xs, ys, pm)
            self.fb[ys[ok], xs[ok]] = col & 0x7FFF
            self.drawn[ys[ok], xs[ok]] = True

    def gouraud(self, a, pm):
        if (pm & 7) != 4:
            return None
        g = self.u16(a + 0x1C) * 8
        return [self.u16(g + 2 * k) for k in range(4)]

    def quad(self, q, pm, a, textured, ctrl=0):
        (xa, ya), (xb, yb), (xc, yc), (xd, yd) = q
        n = int(max(max(abs(xd - xa), abs(yd - ya)), max(abs(xc - xb), abs(yc - yb)))) + 1
        n2 = max(2 * n, 2)
        t = np.linspace(0.0, 1.0, n2)
        lx, ly = xa + (xd - xa) * t, ya + (yd - ya) * t           # left edge A -> D
        rx, ry = xb + (xc - xb) * t, yb + (yc - yb) * t           # right edge B -> C
        m = int(max(np.max(np.maximum(np.abs(rx - lx), np.abs(ry - ly))), 1)) + 1
        if n * m > 400000:                                         # off-screen monsters: bound the work
            return
        m2 = max(2 * m, 2)
        s = np.linspace(0.0, 1.0, m2)
        xs = lx[:, None] + (rx - lx)[:, None] * s[None, :]
        ys = ly[:, None] + (ry - ly)[:, None] * s[None, :]
        tt = np.broadcast_to(t[:, None], xs.shape)
        ss = np.broadcast_to(s[None, :], xs.shape)
        xi = np.round(xs).astype(int)
        yi = np.round(ys).astype(int)
        ok = self.clip_mask(xi, yi, pm)
        if pm & 0x100:                                             # mesh: every other pixel
            ok &= ((xi + yi) & 1) == 0
        col = np.zeros(xs.shape, dtype=np.int32)                   # RGB555 (no flag)
        if textured:
            size = self.u16(a + 0xA)
            tw, th = ((size >> 8) & 0x3F) * 8, size & 0xFF
            if tw == 0 or th == 0:
                return
            u = np.minimum((ss * tw).astype(int), tw - 1)
            v = np.minimum((tt * th).astype(int), th - 1)
            if ctrl & 0x10:
                u = tw - 1 - u
            if ctrl & 0x20:
                v = th - 1 - v
            srca = self.u16(a + 8) * 8
            colr = self.u16(a + 6)
            mode = (pm >> 3) & 7
            vr = np.frombuffer(self.vram, dtype=np.uint8)
            if mode == 1:                                          # 16-colour LUT
                byte = vr[srca + v * (tw // 2) + (u >> 1)]
                idx = np.where(u & 1, byte & 15, byte >> 4)
                lut = np.frombuffer(self.vram[colr * 8:colr * 8 + 32], dtype=">u2").astype(np.int32)
                col = lut[idx]
                transparent = (idx == 0) & ((pm & 0x40) == 0)
            elif mode == 0:                                        # 16-colour bank (colour = bank | index)
                byte = vr[srca + v * (tw // 2) + (u >> 1)]
                idx = np.where(u & 1, byte & 15, byte >> 4)
                col = (colr & 0xFFF0) | idx
                transparent = (idx == 0) & ((pm & 0x40) == 0)
            elif mode == 4:                                        # RGB
                off = srca + 2 * (v * tw + u)
                col = (vr[off].astype(np.int32) << 8) | vr[off + 1]
                transparent = (col == 0) & ((pm & 0x40) == 0)
            else:                                                  # 64 / 128 / 256 colour banks
                idx = vr[srca + v * tw + u].astype(np.int32)
                col = (colr & {2: 0xFFC0, 3: 0xFF80, 5: 0xFF00}.get(mode, 0xFF00)) | idx
                transparent = (idx == 0) & ((pm & 0x40) == 0)
            ok &= ~transparent
        else:
            c = self.u16(a + 6)
            col[:] = c
            if not (pm & 0x40) and (c & 0x7FFF) == 0 and not (c & 0x8000):
                return
        gr = self.gouraud(a, pm)
        if gr is not None:
            r = np.zeros(xs.shape); g = np.zeros(xs.shape); b = np.zeros(xs.shape)
            corners = ((0, 0), (0, 1), (1, 1), (1, 0))             # A (t0,s0) B (t0,s1) C (t1,s1) D (t1,s0)
            for (ct, cs), gc in zip(corners, gr):
                wgt = (tt if ct else 1 - tt) * (ss if cs else 1 - ss)
                r += wgt * (gc & 31); g += wgt * ((gc >> 5) & 31); b += wgt * ((gc >> 10) & 31)
            cr = np.clip((col & 31) + np.round(r).astype(int) - 16, 0, 31)
            cg = np.clip(((col >> 5) & 31) + np.round(g).astype(int) - 16, 0, 31)
            cb = np.clip(((col >> 10) & 31) + np.round(b).astype(int) - 16, 0, 31)
            col = cr | (cg << 5) | (cb << 10)
        self.fb[yi[ok], xi[ok]] = (col[ok] & 0x7FFF).astype(np.uint16)
        self.drawn[yi[ok], xi[ok]] = True

    def image(self, bg=(0, 0, 0)):
        r = ((self.fb & 31) << 3).astype(np.uint8)
        g = (((self.fb >> 5) & 31) << 3).astype(np.uint8)
        b = (((self.fb >> 10) & 31) << 3).astype(np.uint8)
        img = np.stack([r, g, b], axis=-1)
        img[~self.drawn] = bg
        return img


def render(vram, bg=(0, 0, 0)):
    return Vdp1(vram).run(0).image(bg)
