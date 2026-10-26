"""the VDP2 registers the original race screen's display set-up (init_game_disp) leaves:  python tools/hud_regs.py [game type] [course]
(the game over screen after the ranking relies on them: the PSP race does not program them, src/menu_host.c)"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle  # noqa: E402
import hud as hudmod  # noqa: E402

g = int(sys.argv[1]) if len(sys.argv) > 1 else 2
c = int(sys.argv[2]) if len(sys.argv) > 2 else 0
r = oracle.Race(c, game_type=g, render=True)
oracle.hud_setup(r, hudmod)
for off in range(0, 0x120, 2):
    v = r.mem.r16(0x25F80000 + off)
    if v:
        print("0x%03X %04X" % (off, v))
