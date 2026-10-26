"""M8 scenarios: the stage start screen (start module 0x06015408), one per course."""
from menu_drive import *   # noqa: F401,F403


def _start(course):
    def run(d):
        d.until(lambda d: d.vec() == 0x060154EC, maxf=3000)       # after the fade-in the main vector becomes start_second (the race's data set-up follows, not ported here)
    return run


SCENARIOS = {}
START_AT = {}
PATCHES = {}
for _c, _n in enumerate(("desert", "forest", "mount", "lake")):
    _name = "start_" + _n
    SCENARIOS[_name] = _start(_c)
    START_AT[_name] = 0x06015408
    PATCHES[_name] = [(0x060AA87E, bytes([0, _c])), (0x060AA860, bytes([0, 0])), (0x060AA825, bytes([0])), (0x060AA810, bytes([0]))]
