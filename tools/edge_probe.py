"""exploration: open edges at a tile seam.  python tools/edge_probe.py [name]
At the boundary k the render polygon edges of the two parts that lie in the boundary plane (both ends within 0.3 m of the plane, a lateral reach of 40 m): those of the part before (exit side) and of
the part after (entry side), as segments (lateral, height above the road) .  An edge that has a partner on the other side (ends within `TOL` m) is closed.  A *tall* segment (its upper end
>= 2 m above the road) without a partner is an open end of a hill / wall: after the join it is a hole.  Stock continuity (exit k against entry k) shows what the original data does."""
import math
import sys
import trackgen_extract as T

PL = 0.3
TOLM = 1.0
TALL = 2.0


def plane_edges(rcp, port, part, side):
    """segments (u0, y0, u1, y1) of the polygon edges of `part` lying in the plane of the boundary, heights above the road; side = -1: the part before, +1: the part after"""
    px, pz, py, h = port["x"], port["z"], port["y"], port["head"]
    fx, fz, rx, rz = math.sin(h), math.cos(h), math.cos(h), -math.sin(h)
    fv, fp, nv, npl, _ = rcp.parts[part % 128]
    out = set()
    for j in range(npl):
        pl = rcp.polys[fp + j]
        if pl[1] == 0xFFFF:
            continue
        V = []
        for c in pl[7:11]:
            x, y, z = rcp.verts[fv + c]
            V.append(((x - px) * fx + (z - pz) * fz, (x - px) * rx + (z - pz) * rz, y - py))
        for i in range(4):
            a, b = V[i], V[(i + 1) & 3]
            if abs(a[0]) <= PL and abs(b[0]) <= PL and (abs(a[1] - b[1]) > 0.05 or abs(a[2] - b[2]) > 0.05) and abs(a[1]) <= 40 and abs(b[1]) <= 40:
                s = ((round(a[1], 2), round(a[2], 2)), (round(b[1], 2), round(b[2], 2)))
                out.add(s if s[0] <= s[1] else (s[1], s[0]))
    return list(out)


def close(a, b):
    return (math.hypot(a[0][0] - b[0][0], a[0][1] - b[0][1]) <= TOLM and math.hypot(a[1][0] - b[1][0], a[1][1] - b[1][1]) <= TOLM)


def open_count(A, B):
    """tall edges of A (and of B) with no partner on the other side"""
    n = 0
    for s, other in ((A, B), (B, A)):
        for e in s:
            if max(e[0][1], e[1][1]) < TALL:
                continue
            if not any(close(e, f) for f in other):
                n += 1
    return n


def edges(rcp, ports):
    ex = [plane_edges(rcp, ports[k], k - 1, -1) if ports[k]["ok"] else None for k in range(128)]
    en = [plane_edges(rcp, ports[k], k, 1) if ports[k]["ok"] else None for k in range(128)]
    return ex, en


if __name__ == "__main__":
    name = sys.argv[1] if len(sys.argv) > 1 else "forest"
    all_rct = {n: T.Rct(n) for n in T.NAMES}
    rct = all_rct[name]
    T.rct_ref[0] = rct
    T.TOL[0] = T.TOLS[name]
    rcp = T.Rcp(name)
    ports = T.ports_of(rct)
    ex, en = edges(rcp, ports)
    sd = sorted(open_count(ex[k], en[k]) for k in range(128) if ex[k] is not None)
    print(name, "stock continuity, open tall edges: median", sd[len(sd) // 2], "p90", sd[int(len(sd) * 0.9)], "max", sd[-1], " edges per plane (median)", sorted(len(e) + len(f) for e, f in zip(ex, en) if e is not None)[60])
    pairs = [(i, j) for i in range(128) for j in range(128) if T.compatible(ports[i], ports[j])]
    cnt = {}
    for i, j in pairs:
        c = open_count(ex[i], en[j])
        cnt[c] = cnt.get(c, 0) + 1
    print("  road compatible pairs:", len(pairs), " by open tall edges:", sorted(cnt.items())[:10])
