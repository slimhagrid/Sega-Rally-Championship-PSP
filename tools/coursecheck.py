"""M5: check the course polygons' depth keys against the chain the game built (same idea as objcheck.py, for the course parts of a frame).

    python tools/coursecheck.py [frame name]

Projects every polygon of the frame's part list with my rules (flags -> key, bucket = key * 32), matches it with the commands of the VDP1 chain and prints the
chain positions whose bucket order disagrees with the game's order (the chain must be sorted far -> near by bucket).
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import objcheck  # noqa: E402

NAMES = ["desert", "forest", "mount", "lake"]


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else "desert_p330_car"
    vram = open(os.path.join(ROOT, "extracted", "ref", name + ".vram"), "rb").read()
    cmds = objcheck.chain(vram)
    txt = open(os.path.join(ROOT, "extracted", "psp", "frames", name + ".txt")).read().split("\n")
    course = int(txt[0].split()[1])
    view = [float(x) for x in txt[1].split()[1:13]]
    plist = [int(x) for x in txt[2].split()[2:]]
    d = open(os.path.join(ROOT, "extracted", "psp", "course", NAMES[course] + ".rcp"), "rb").read()
    h = struct.unpack_from("<10I", d, 4)
    nparts, ntex, npoly, nverts, part_off, tex_off, poly_off, vert_off = h[:8]
    byc = {}
    for pos, c in enumerate(cmds):
        byc.setdefault(tuple(sorted(c["pts"])), []).append(pos)
    rows = []
    allk = {}
    for pi in plist:
        fv, fp, nv, npl, _ = struct.unpack_from("<IIHHI", d, part_off + 16 * pi)
        vv = []
        for i in range(nv):
            x, y, z = struct.unpack_from("<3f", d, vert_off + 12 * (fv + i))
            vv.append((view[0] * x + view[1] * y + view[2] * z + view[3], view[4] * x + view[5] * y + view[6] * z + view[7], view[8] * x + view[9] * y + view[10] * z + view[11]))
        for k in range(npl):
            flags, tex, colr, k0, k1, k2, k3, c0, c1, c2, c3, _ = struct.unpack_from("<HHH4H4HH", d, poly_off + 24 * (fp + k))
            kv = [k0, k1, k2, k3]
            zs = [vv[i][2] for i in kv]
            if min(zs) < 1.25:
                continue
            mode = (flags >> 1) & 3
            key = [sum(zs) / 4, min(zs), max(zs), max(zs) + 4][mode]
            pts = []
            for i in (c0, c1, c2, c3):
                x, y, z = vv[i]
                pts.append((x * 224 / z, -y * 224 / z))
            best = None
            for pos, c in enumerate(cmds):
                cp = c["pts"]
                if all(any(abs(a[0] - b[0]) <= 1.01 and abs(a[1] - b[1]) <= 1.01 for b in cp) for a in pts):
                    e = sum(abs(a[0] - b[0]) + abs(a[1] - b[1]) for a, b in zip(pts, [cp[0], cp[1], cp[2], cp[3]]))      # corner order of the command = sprite corners
                    if best is None or e < best[0]:
                        best = (e, pos)
            if best is not None and best[0] < 6.0:
                rows.append((best[1], int(key * 32), pi, k, flags))
                allk[(pi, k)] = (best[1], flags, zs)
    if "--modes" in sys.argv:
        for mode in range(4):
            items = sorted((pos, zs) for (pos, fl, zs) in allk.values() if (fl >> 1) & 3 == mode)
            res = []
            for nm, fn in (("mean", lambda z: sum(z) / 4), ("min", min), ("max", max), ("min+4", lambda z: min(z) + 4), ("mean3", lambda z: sum(z[:3]) / 3)):
                ks = [fn(z) for _, z in items]
                inv = sum(1 for a, b in zip(ks, ks[1:]) if b > a + 1 / 32.0)
                res.append("%s %d" % (nm, inv))
            print("mode %d (%d polys): inversions between neighbours: %s" % (mode, len(items), ", ".join(res)))
    if "--subsets" in sys.argv:                    # which vertices does the max / min run over?
        import itertools
        for mode in (2, 3):
            items = sorted((pos, zs) for (pos, fl, zs) in allk.values() if (fl >> 1) & 3 == mode)
            out = []
            for sub in ((0, 1, 2, 3), (0, 1, 2), (1, 2, 3), (0, 2), (1, 3), (0, 1), (2, 3)):
                for nm, fn in (("max", max), ("min", min), ("mean", lambda v: sum(v) / len(v))):
                    ks = [fn([z[i] for i in sub]) for _, z in items]
                    inv = sum(1 for a, b in zip(ks, ks[1:]) if b > a + 1 / 32.0)
                    out.append((inv, nm + str(sub)))
            out.sort()
            print("mode %d best: %s" % (mode, out[:5]))
    if "--scan" in sys.argv:                       # which constant c makes `max + c` for mode 3 (and min + c, mean + c) fit the chain best, mixed with modes 1 and 2
        pol = sorted((pos, fl, zs) for (pos, fl, zs) in allk.values())
        for base_name, base in (("max", max), ("min", min), ("mean", lambda z: sum(z) / 4)):
            best = None
            for c100 in range(-800, 801, 10):
                c = c100 / 100.0
                ks = []
                for pos, fl, zs in pol:
                    m = (fl >> 1) & 3
                    ks.append(max(zs) if m == 2 else (min(zs) if m == 1 else (base(zs) + c if m == 3 else sum(zs) / 4)))
                inv = sum(1 for a, b in zip(ks, ks[1:]) if b > a + 1 / 32.0)
                if best is None or inv < best[0]:
                    best = (inv, c)
            print("mode 3 = %s + c: best c %.2f with %d inversions in %d" % (base_name, best[1], best[0], len(pol)))
    rows.sort()
    bad = 0
    mn = 1 << 30
    for pos, bk, pi, k, flags in rows:
        if bk < mn:
            mn = bk
        elif bk - mn > 1:
            bad += 1
            if bad <= 25:
                print("chain pos %3d: part %3d poly %3d flags %04x bucket %5d but an earlier command has the lower bucket %d" % (pos, pi, k, flags, bk, mn))
    print("%d course polygons matched, %d out of order (bucket higher than an earlier command's; the chain runs far -> near)" % (len(rows), bad))
    if rows:
        print("last matched course command: chain pos %d, lowest buckets: %s" % (rows[-1][0], sorted(r[1] for r in rows)[:8]))


if __name__ == "__main__":
    main()
