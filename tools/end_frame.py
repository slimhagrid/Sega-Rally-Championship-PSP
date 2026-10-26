"""M6 (HUD, end of the race): the reference picture of one pass of an oracle trace, with the goal / game-over screen (VDP2 NBG0) over the VDP1 / RBG0 picture.

    python tools/end_frame.py extracted/oracle/desert_drive_end 5774 [out.png]

Replays the trace's input file (.inp, settings in its header) in the oracle with the HUD (`game_display` after every pass), builds the frame of the given pass with the game's own polygon pipeline
(software VDP1, tools/vdp1.py), the sky (RBG0, tools/vdp2.py) and the HUD layers (tools/hud.py render: NBG2 / NBG3 while racing, NBG0 after the goal).  Writes extracted/ref/<name>_<pass>.png.
Compare with a PPSSPP screenshot of the PSP port (`tools/run_ppsspp.ps1 -Play "course N;type T;view 2;replay <name>.inp;hold <pass>"`, tools/compare_frames.py).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import oracle  # noqa: E402
import hud  # noqa: E402
import vdp1  # noqa: E402
import vdp2  # noqa: E402
from saturn_env import Race  # noqa: E402


def frame(prefix, npass, view=2):
    """-> (rgb image (224, 352, 3), race)"""
    st, inp = oracle.read_inputs(prefix + ".inp")
    setup = [int(x) for x in st["setup"].split(",")] if st.get("setup") else None
    r = Race(int(st.get("course", 0)), game_type=int(st.get("type", 2)), mt=int(st.get("mt", 0)), cartype=int(st.get("car", 0)), difficulty=int(st.get("diff", 0)), setup=setup,
             mirror=int(st.get("mirror", 0)), render=True)
    hs = oracle.hud_start(r, [])
    r.mem.w8(0x060AA01B, view)
    vram = None
    for i, t in enumerate(inp[:npass + 1]):
        a, b, ax = t[:3]
        sh = t[3] if len(t) > 3 else 0
        vw = t[4] if len(t) > 4 else 0
        r.step(bool(a), bool(b), (ax - 127) / 128.0, sh, vw)
        oracle.hud_pass(r, hs, [])
        if i == npass:                                        # the frame of this pass: the game's own course drawing (the car is not drawn: the goal presentation's slave jobs do not run headless)
            vram = r.draw_frame()
    from PIL import Image
    v = vdp1.Vdp1(vram).run(0)
    stt = vdp2.build_state(r)
    bg, _ = vdp2.render_background(stt)
    chars = hud.end_chars(r) if r.mem.r16(0x060AA8E6) & 1 else None
    img = vdp2.compose_hud(vdp2.compose(v.image(), v.drawn, bg), hud.render(r, chars))
    return Image.fromarray(img), r


def main():
    a = sys.argv[1:]
    if len(a) < 2:
        raise SystemExit(__doc__)
    prefix, npass = a[0], int(a[1])
    out = a[2] if len(a) > 2 else os.path.join(ROOT, "extracted", "ref", "%s_%d.png" % (os.path.basename(prefix), npass))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    im, r = frame(prefix, npass)
    im.save(out)
    m = r.mem
    print("%s pass %d: state %d timer %d bgon %04x scroll %d %d -> %s" % (os.path.basename(prefix), npass, m.r32(0x060AA178), m.r32(0x060AA17C), m.r16(0x060AA8E6), m.r16(0x060AA864) & 0x3FF,
                                                                        m.r16(0x060AA868) & 0x3FF, out))


if __name__ == "__main__":
    main()
