"""exploration: how different are the two sides of a tile seam?  python tools/side_probe.py [name]
For every block boundary k the ground surface beside the road 2 m before (parts k-1, k-2) and 2 m after (parts k, k+1) the boundary line: heights above the road at lateral offsets of 2 m (the render
polygons that are not vertical, the highest one under the point; None = nothing there).  Stock continuity (before / after the same boundary) gives the natural level of the difference; a join
of unrelated blocks is compared the same way."""
import math
import sys
import trackgen_extract as T

U = [u * 2.0 for u in range(-24, 25)]
NY = 0.3


def surfaces(rcp, parts):
    out = []
    for part in parts:
        fv, fp, nv, npl, _ = rcp.parts[part % 128]
        for j in range(npl):
            pl = rcp.polys[fp + j]
            if pl[1] == 0xFFFF:
                continue
            V = [rcp.verts[fv + c] for c in pl[7:11]]
            a = [V[1][i] - V[0][i] for i in range(3)]
            b = [V[2][i] - V[1][i] for i in range(3)]
            N = [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
            nl = math.sqrt(sum(t * t for t in N))
            if nl < 1e-6 or abs(N[1]) / nl < NY:
                continue
            xs, zs = [v[0] for v in V], [v[2] for v in V]
            out.append((min(xs), max(xs), min(zs), max(zs), V, N))
    return out


def inside(V, x, z):
    neg = pos = False
    for i in range(4):
        j = (i + 1) & 3
        c = (V[i][0] - x) * (V[j][2] - z) - (V[j][0] - x) * (V[i][2] - z)
        if c < 0:
            neg = True
        elif c > 0:
            pos = True
    return not (neg and pos)


def sample(surf, x, z):
    best = None
    for x0, x1, z0, z1, V, N in surf:
        if x < x0 or x > x1 or z < z0 or z > z1:
            continue
        if not inside(V, x, z):
            continue
        y = V[0][1] - (N[0] * (x - V[0][0]) + N[2] * (z - V[0][2])) / N[1]
        if best is None or y > best:
            best = y
    return best


def profile(rcp, port, parts, along):
    px, pz, py, h = port["x"], port["z"], port["y"], port["head"]
    fx, fz, rx, rz = math.sin(h), math.cos(h), math.cos(h), -math.sin(h)
    surf = surfaces(rcp, parts)
    out = []
    for u in U:
        x, z = px + fx * along + rx * u, pz + fz * along + rz * u
        y = sample(surf, x, z)
        out.append(None if y is None else y - py)
    return out


def diff(a, b, umax=30.0, tall=3.0):
    """worst height difference over |u| <= umax: both have a surface -> the difference; only one has -> its height when it is a visible obstacle (>= `tall` m above the road), else nothing"""
    worst = 0.0
    for u, x, y in zip(U, a, b):
        if abs(u) > umax:
            continue
        if x is None and y is None:
            continue
        if x is None or y is None:
            h = x if y is None else y
            d = h if h >= tall else 0.0
        else:
            d = abs(x - y)
        worst = max(worst, d)
    return worst


def profiles(rcp, ports):
    ex = [profile(rcp, ports[k], [k - 1, k - 2], -0.4) if ports[k]["ok"] else None for k in range(128)]
    en = [profile(rcp, ports[k], [k, k + 1], 0.4) if ports[k]["ok"] else None for k in range(128)]
    return ex, en


if __name__ == "__main__":
    name = sys.argv[1] if len(sys.argv) > 1 else "forest"
    all_rct = {n: T.Rct(n) for n in T.NAMES}
    rct = all_rct[name]
    T.rct_ref[0] = rct
    T.TOL[0] = T.TOLS[name]
    rcp = T.Rcp(name)
    ports = T.ports_of(rct)
    ex, en = profiles(rcp, ports)
    sd = sorted(diff(ex[k], en[k]) for k in range(128) if ex[k])
    print(name, "stock continuity: median %.2f p75 %.2f p90 %.2f max %.2f" % (sd[len(sd) // 2], sd[int(len(sd) * 0.75)], sd[int(len(sd) * 0.9)], sd[-1]))
    pairs = [(i, j) for i in range(128) for j in range(128) if T.compatible(ports[i], ports[j])]
    print("  road compatible pairs:", len(pairs), " ".join("tol %.1f: %d" % (t, sum(1 for i, j in pairs if diff(ex[i], en[j]) <= t)) for t in (1.0, 1.5, 2, 3, 4, 6)))
