"""M5 (objects): the object gallery.  Every OBJECT.MDL model family is drawn by the game's own `draw_polygon` (software VDP1 = reference) and by the PSP renderer
(frames `obj_g1`..: six parts each, 3 x 2 grid, turned a little so the shape shows), to check the model / texture conversion of all object parts, not only the cars.

    python tools/export_objects.py [group ...]       -> extracted/ref/obj_g<N>.png (+ _vdp1 is the same, no sky) and extracted/psp/frames/obj_g<N>.txt

The frames have `course -1` (no course, no sky); the index file extracted/psp/frames/index_obj.txt lists them (check with tools/check_frames.ps1 -Index index_obj.txt).
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import conv_objects  # noqa: E402
import rcmodel  # noqa: E402
import refframe  # noqa: E402
import vdp1  # noqa: E402
from saturn_env import Race  # noqa: E402

PSP = os.path.join(ROOT, "extracted", "psp", "frames")

GROUPS = [
    ["celica_body_poly", "celica_body_d_poly", "celica_body_s_poly", "celica_tire_l_poly", "celica_kage_poly", "celica_br_on_poly"],
    ["delta_body_poly", "delta_body_d_poly", "delta_body_s_poly", "delta_tire_r_poly", "delta_kage_poly", "delta_br_on_poly"],
    ["stratos_body_poly", "stratos_body_d_poly", "stratos_body_s_poly", "stratos_tire_fl_poly", "stratos_kage_poly", "stratos_br_on_poly"],
    ["enemy1_poly", "enemy2_poly", "enemy3_poly", "enemy4_poly", "enemy5_poly", "enemy6_poly"],
    ["enemy7_poly", "enemy8_poly", "enemy1_s_poly", "enemy3_s_poly", "enemy5_s_poly", "enemy8_s_poly"],
    ["checkpoint_l_poly", "checkpoint_s_poly", "pole_l_poly", "pole_m_poly", "pole_s_poly", "finish_l_poly"],
    ["am3kanban_poly", "megakanban_poly", "startobj_poly", "flag1_00_poly", "flag1_10_poly", "flag2_05_poly"],
    ["kata1_poly", "sohe1_poly", "ken1_poly", "matu1_poly", "oja1_poly", "panel_poly"],
    ["one_poly", "two_poly", "three_poly", "go_poly", "sand_poly", "water_poly"],
    ["bird1_body_poly", "bird1_hane_l1_poly", "swan_poly", "heli1_poly", "heli2_poly", "heli_kage_poly"],
]


def matmul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def main():
    from PIL import Image
    want = [int(x) for x in sys.argv[1:]]
    syms = conv_objects.part_symbols()
    index = {n: i for i, (a, n) in enumerate(syms)}
    addr = {n: a for a, n in syms}
    mdl, tables, vram_o, heads = conv_objects.load_objects()
    parts = rcmodel.parse_parts(mdl, [a for a, _ in syms], conv_objects.MDL_BASE)
    r = Race(0, game_type=2, render=True, rec_buf=0x060FD400)
    r.init_render()
    r.load_game_textures()
    os.makedirs(PSP, exist_ok=True)
    os.makedirs(refframe.REF, exist_ok=True)
    yaw, pitch = math.radians(32), math.radians(14)
    cy, sy_, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    Ry = [[cy, 0, sy_], [0, 1, 0], [-sy_, 0, cy]]
    Rx = [[1, 0, 0], [0, cp, sp], [0, -sp, cp]]
    R = matmul(Rx, Ry)
    cols, rows = (-112, 0, 112), (-52, 52)
    names = []
    for gi, group in enumerate(GROUPS, 1):
        name = "obj_g%d" % gi
        names.append(name)
        if want and gi not in want:
            continue
        insts = []
        for k, pn in enumerate(group):
            part = parts[index[pn]]
            rad = max(math.sqrt(sum((c / 1024.0) ** 2 for c in v)) for v in part.verts)
            d = max(rad * 224.0 / 52.0, rad + 0.8)
            sx, sy = cols[k % 3], rows[k // 3]
            X, Y = sx * d / (224.0 * 1.0625), -sy * d / 224.0
            m = []
            for i in range(3):
                m += [R[i][0] * (1.0625 if i == 0 else 1.0), R[i][1] * (1.0625 if i == 0 else 1.0), R[i][2] * (1.0625 if i == 0 else 1.0), (X, Y, d)[i] * (1.0625 if i == 0 else 1.0)]
            insts.append((index[pn], addr[pn], m))
        vram = r.draw_parts([(a, m) for _, a, m in insts])
        v = vdp1.Vdp1(vram).run(0)
        Image.fromarray(v.image()).save(os.path.join(refframe.REF, name + ".png"))
        open(os.path.join(refframe.REF, name + ".vram"), "wb").write(vram)
        with open(os.path.join(PSP, name + ".txt"), "w") as f:
            f.write("course -1\nview 1 0 0 0 0 1 0 0 0 0 1 0\nparts 0\n")
            for pi, _, m in insts:
                f.write("inst %d 0 0 %s\n" % (pi, " ".join("%.9g" % x for x in m)))
        print("%s: %s -> %d commands" % (name, ", ".join(g.replace("_poly", "") for g in group), v.ncmd))
    with open(os.path.join(PSP, "index_obj.txt"), "w") as f:
        f.write("\n".join(names) + "\n")


if __name__ == "__main__":
    main()
