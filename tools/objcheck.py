"""M5 (objects): check my model of the object draw (projection, depth key, order) against the VDP1 command chain the game built for the same frame.

    python tools/objcheck.py [frame name]       (default desert_p330_car; needs extracted/ref/<name>.vram and extracted/psp/frames/<name>.txt)

Walks the chain like the VDP1 does, projects every polygon of every `inst` with the renderer's rules and matches the four screen corners with the commands.
Prints, for each matched polygon: the game's position in the chain, my bucket / key, flags.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import conv_objects  # noqa: E402
import rcmodel  # noqa: E402


def s16(v):
    return v - 65536 if v & 0x8000 else v


def chain(vram):
    def u16(a):
        return (vram[a] << 8) | vram[a + 1]
    i, stack, out = 0, [], []
    for _ in range(4000):
        a = i * 8
        ctrl, link = u16(a), u16(a + 2)
        if ctrl & 0x8000:
            break
        jp = (ctrl >> 12) & 7
        cmd = ctrl & 15
        if not (jp & 4) and cmd in (0, 1, 2, 4, 5, 6):
            pts = [(s16(u16(a + 0xC + 4 * k)), s16(u16(a + 0xE + 4 * k))) for k in range(4)]
            out.append({"idx": i, "cmd": cmd, "pmod": u16(a + 4), "colr": u16(a + 6), "srca": u16(a + 8), "size": u16(a + 0xA), "pts": pts})
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
    return out


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else "desert_p330_car"
    vram = open(os.path.join(ROOT, "extracted", "ref", name + ".vram"), "rb").read()
    cmds = chain(vram)
    names = [l.split()[2] for l in open(os.path.join(ROOT, "extracted", "psp", "course", "object.names"))]
    insts = []
    for line in open(os.path.join(ROOT, "extracted", "psp", "frames", name + ".txt")):
        if line.startswith("inst "):
            p = line.split()
            insts.append((int(p[1]), [float(x) for x in p[4:16]]))          # inst <part> <clip> <kind> m0..m11
    mdl, tables, vram_o, heads = conv_objects.load_objects()
    syms = conv_objects.part_symbols()
    parts = rcmodel.parse_parts(mdl, [a for a, _ in syms], conv_objects.MDL_BASE)
    print("%d commands in the chain, %d instances" % (len(cmds), len(insts)))
    for n, (pi, m) in enumerate(insts):
        part = parts[pi]
        print("inst %d: %s  nverts %d npolys %d" % (n, names[pi], len(part.verts), len(part.polys)))
        vv = []
        for (x, y, z) in part.verts:
            x, y, z = x / 1024.0, y / 1024.0, z / 1024.0
            vv.append((m[0] * x + m[1] * y + m[2] * z + m[3], m[4] * x + m[5] * y + m[6] * z + m[7], m[8] * x + m[9] * y + m[10] * z + m[11]))
        for k, p in enumerate(part.polys):
            idx = list(p.idx)
            tri = p.triangle
            kv = idx + [idx[2]] if tri else idx
            zs = [vv[i][2] for i in kv]
            mode = (p.flags >> 1) & 3
            key = [sum(zs) / 4, min(zs), max(zs), max(zs) + 4][mode]
            behind = any(z < 0.5 for z in zs)
            pts = []
            for i in kv:
                x, y, z = vv[i]
                pts.append((x * 224 / z, -y * 224 / z) if z > 0 else (9999, 9999))
            # match: any command whose four corners are within 1.5 px of the projected ones (any corner order)
            match = None
            for pos, c in enumerate(cmds):
                cp = c["pts"]
                if all(any(abs(a[0] - b[0]) <= 1.01 and abs(a[1] - b[1]) <= 1.01 for b in cp) for a in pts) and                         all(any(abs(a[0] - b[0]) <= 1.01 and abs(a[1] - b[1]) <= 1.01 for a in pts) for b in cp):
                    match = pos
                    break
            vis = not behind and key <= 255
            if vis and (p.flags & 0x10):
                (ax, ay), (bx, by), (dx, dy) = [(vv[i][0] / vv[i][2], -vv[i][1] / vv[i][2]) for i in kv[:3]]
                vis = ((bx - ax) * (dy - ay) - (dx - ax) * (by - ay)) < 0
            bad = "   <== MISMATCH (mine %s, game %s)" % ("drawn" if vis else "culled", "drawn" if match is not None else "absent") if vis != (match is not None) else ""
            if bad or "-v" in sys.argv:
                print("   poly %3d flags %04x key %.3f bucket %4d %s%s -> chain pos %s%s" % (
                    k, p.flags, key, int(key * 32), "BEHIND " if behind else "", "tri" if tri else "", match, bad))


if __name__ == "__main__":
    main()
