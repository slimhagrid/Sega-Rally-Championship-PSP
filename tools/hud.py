"""M5 (HUD): the in-race display of the original, run in the oracle (tools only).  Documentation: docs/hud.md.

The HUD has two parts:
  * VDP2 NBG2 / NBG3 (priority 6, 256 colours, 8x8 cells, 1-word names, one 64 x 64 page each at 0x3A000 / 0x3C000): text, gauges, labels.  `game_display` (0x060123F8) rewrites the name
    tables every pass; the character data (CGDATAUS.DAT, VDP2 VRAM 0x18000 ..), the palettes (cl_* tables of PRGDATA.DAT, copied to the colour RAM shadow by the table at 0x06004274)
    and the static labels (`init_game_disp`, 0x0601151C) are set up once at the race start.
  * VDP1 polygons through the object pipeline: the tachometer needle (`dispTachoMeter`, 0x06010680) and the count-down numerals (`dispCountDown`, 0x06010710).

    setup(race)      one-time display set-up (the oracle does not run the game's own boot path)
    display(race)    one `game_display` pass
    state(race)      Vdp2State with the HUD registers / VRAM / colour RAM
    render(race)     (rgb (224, 352, 3), mask) of NBG2 over NBG3
    name_tables(race)  {2: (64, 64) uint16 (NBG2), 3: NBG3 planes A / B, 4: NBG3 planes C / D}
    scroll(race)     (x2, y2, x3, y3)
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import vdp2  # noqa: E402
from datfile import Scatter  # noqa: E402

DISC = os.path.join(ROOT, "extracted", "disc", "files")
INIT_VDP2SYS = 0x06005518
COPY_PALETTES = 0x06004754          # copies the cl_* tables into the colour RAM shadow: r4 = table (0x06004274: game types except 10, 0x060042F0: 2P battle)
PALETTE_TABLE = 0x06004274
INIT_GAME_DISP = 0x0601151C
GAME_DISPLAY = 0x060123F8
NT_BASE = {2: 0x25E3A000, 3: 0x25E3C000, 4: 0x25E3E000}  # name table pages in VDP2 VRAM: NBG2 (map 0x1D in all four planes), NBG3 planes A / B (0x1E) and C / D (0x1F)
CGDATA = "CGDATAUS.DAT"             # the USA release: English text


def setup(r):
    """load the character data, set the VDP2 registers, copy the palettes, draw the static labels (what the game does when the race screen is built)"""
    m = r.mem
    vdp2._scu_dma(m)
    s = Scatter(os.path.join(DISC, CGDATA))
    for b in s.blocks:
        m.load(b.dest, s.expanded(b))
    r.call(INIT_VDP2SYS)
    r.call(COPY_PALETTES, r4=PALETTE_TABLE)
    r.call(INIT_GAME_DISP)
    m.w16(0x060AA864, 0x200)           # NBG0 x scroll: the screens before the race (stage select, code 0x06020DEC) leave 0x200 behind, the oracle starts without them; the game-over screen is drawn on the
                                       # plane that x shows (docs/hud.md 7), the goal screen sets x itself


def display(r):
    r.call(GAME_DISPLAY)


SCROLL = 0x060AA874                 # shadow of NBG2 / NBG3 scroll (x, y words; the V-IN copies them, masked 0x3FF, to the registers 0x25F80090 .. 96)


def scroll(r):
    """(x2, y2, x3, y3)"""
    m = r.mem
    return tuple(m.r16(SCROLL + 2 * k) & 0x3FF for k in range(4))


END_SCROLL = 0x060AA864             # shadow of the NBG0 scroll (x, y long; the integer part is the high word) the end-of-race screens move
BGON_SHADOW = 0x060AA8E6            # the V-IN copies it to BGON


def end_chars(r):
    """the result screen's character file: CR<stage>.DAT (arcade) / PR<stage>.DAT (practice), the cells NBG0 draws from (VRAM 0x00040 ..), docs/hud.md 7"""
    from saturn_env import COURSES
    t = r.mem.r16(0x060AA860)
    if t >= 4:
        return "CGDATA_F.DAT"                                  # time attack (4, 6), 2 player (8, 10): 0x06004514
    return ("CR" if t == 0 else "PR") + COURSES[r.course] + ".DAT"


def state(r, chars=None):
    """chars: a character file name (end_chars) loaded into the VRAM copy before the render (NBG0 of the end-of-race screens)"""
    m = r.mem
    regs = bytearray(m.read(0x25F80000, 0x120))
    for k, v in enumerate(scroll(r)):
        regs[0x90 + 2 * k] = v >> 8
        regs[0x91 + 2 * k] = v & 0xFF
    for off, a in ((0x70, END_SCROLL), (0x74, END_SCROLL + 4)):
        v = m.r16(a) & 0x3FF
        regs[off], regs[off + 1] = v >> 8, v & 0xFF
    v = m.r16(BGON_SHADOW)
    regs[0x20], regs[0x21] = v >> 8, v & 0xFF
    vram = bytearray(m.read(0x25E00000, 0x80000))
    if chars:
        s = Scatter(os.path.join(DISC, chars))
        for b in s.blocks:
            if 0x25E00000 <= b.dest < 0x25E80000:
                d = s.expanded(b)
                vram[b.dest - 0x25E00000:b.dest - 0x25E00000 + len(d)] = d
    return vdp2.Vdp2State(bytes(regs), bytes(vram), m.read(0x060B8000, 0x1000), {})      # colour RAM = its shadow (the V-IN DMA copies it)


def render(r, chars=None):
    return vdp2.render_hud(state(r, chars))


def name_tables(r):
    m = r.mem
    return {n: np.frombuffer(bytes(m.read(a, 0x2000)), dtype=">u2").reshape(64, 64).astype(np.uint16) for n, a in NT_BASE.items()}
