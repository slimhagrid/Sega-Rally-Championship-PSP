"""M4: function-level golden vectors from the ORIGINAL routines (unit tests for the C port).

    python tools/oracle_fn.py            -> extracted/oracle/vectors.json

Sections
  math       sin / cos / r_sin / r_cos / sqrt / atan / r_atan: [{"fn", "in": [r4, r5], "out": r0}]            (raw 32-bit values, signed)
  collision  per course: get_coli_area + get_y / get_d for sample points: input (x, z) in 11.20 units (metres * 2^20), output
             polygon index (-1 = none), polygon flags, y (16.16 track units, floor polygons) or d (walls)
  block      per course: get_block for car positions along the guide line: segment (+0x60), previous (+0x62), checkpoint byte (+0xC4),
             block id (+0xF0), selector (+0xF1), flag (+0xF7), direction (+0xB8, +0xBC)
Addresses: docs/physics.md 3.13, 6.1.  Everything is deterministic (fixed seed).
"""
import json
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from saturn_env import Race, CAR0, COURSES  # noqa: E402
from sh2 import s32  # noqa: E402
from oracle import GUIDE, NGUIDE, OUT  # noqa: E402

FN = {"sin": 0x0601B76C, "cos": 0x0601B784, "r_sin": 0x0601B764, "r_cos": 0x0601B77C, "sqrt": 0x0601B8CC, "atan": 0x0601B7C0, "r_atan": 0x0601B794}
GET_COLI_AREA, GET_Y, GET_D, GET_BLOCK = 0x06008008, 0x0600802C, 0x06008060, 0x06007EAC
POLY0, POLYSZ = 0x00248000, 0x34


def math_vectors(r):
    rnd = random.Random(1234)
    out = []
    pi2 = 0x6487F
    specials = [0, 1, -1, 0x7FFFFFFF, -0x80000000, 0x10000, -0x10000, 0x8000, 0x3243F, 0x6487F, -0x3243F, 0xFFFF, 0x12345678, -0x12345678]
    for name in ("sin", "cos", "r_sin", "r_cos"):
        for v in specials + [rnd.randint(-1 << 31, (1 << 31) - 1) for _ in range(200)] + [rnd.randint(-4 * pi2, 4 * pi2) for _ in range(300)]:
            r.call(FN[name], r4=v & 0xFFFFFFFF)
            out.append({"fn": name, "in": [v], "out": s32(r.cpu.r[0])})
    for v in specials + [rnd.randint(0, (1 << 31) - 1) for _ in range(300)] + [rnd.randint(0, 100 << 16) for _ in range(300)] + [rnd.randint(-100, -1) for _ in range(5)]:
        r.call(FN["sqrt"], r4=v & 0xFFFFFFFF)
        out.append({"fn": "sqrt", "in": [v], "out": s32(r.cpu.r[0])})
    pts = [(0, 0), (1, 0), (0, 1), (-1, 0), (0, -1), (0x10000, 0x10000), (-0x10000, 0x10000), (0x10000, -0x10000), (-0x10000, -0x10000)]
    pts += [(rnd.randint(-1 << 24, 1 << 24), rnd.randint(-1 << 24, 1 << 24)) for _ in range(500)]
    pts += [(rnd.randint(-1 << 30, (1 << 30)), rnd.randint(-1 << 30, 1 << 30)) for _ in range(100)]
    for name in ("atan", "r_atan"):
        for a, b in pts:
            r.call(FN[name], r4=a & 0xFFFFFFFF, r5=b & 0xFFFFFFFF)
            out.append({"fn": name, "in": [a, b], "out": s32(r.cpu.r[0])})
    return out


def cell_nonempty(m, x, z):
    """the grid cell get_coli_area (0x06008008) selects exists and holds a list"""
    col = ((x & 0xFFFFFFFF) + 0x40000000 & 0xFFFFFFFF) >> 25
    row = (((0x40000000 - z) & 0xFFFFFFFF) >> 25)
    if col >= 64 or row >= 64:
        return False
    return m.r32(0x00240000 + 4 * (row * 64 + col)) != 0


def collision_vectors(r, n=1500):
    """sample points: jittered centroids of the real collision polygons plus uniform points over the track area"""
    m = r.mem
    rnd = random.Random(99)
    npoly = 0
    while npoly < 8000:                                   # count: polygons are contiguous, flags of a valid record are nonzero or the vertices are
        if all(m.r32(POLY0 + npoly * POLYSZ + 4 * k) == 0 for k in range(13)):
            break
        npoly += 1
    pts = []
    for _ in range(n):
        i = rnd.randrange(npoly)
        base = POLY0 + i * POLYSZ
        xs = [s32(m.r32(base + 4 + 8 * k)) for k in range(4)]
        zs = [s32(m.r32(base + 8 + 8 * k)) for k in range(4)]
        cx, cz = sum(xs) // 4, sum(zs) // 4
        pts.append((cx + rnd.randint(-3 << 20, 3 << 20), cz + rnd.randint(-3 << 20, 3 << 20)))
    for _ in range(n // 4):
        pts.append((rnd.randint(-1000 << 20, 1000 << 20), rnd.randint(-1000 << 20, 1000 << 20)))
    out = []
    for x, z in pts:
        cpu = r.cpu
        if not cell_nonempty(m, x, z):          # the original walks garbage here (never queried in play): not a test vector
            continue
        # trapa #9 (no polygon) is the original's debug trap: record it as "none"
        try:
            r.call(GET_COLI_AREA, r4=x & 0xFFFFFFFF, r5=z & 0xFFFFFFFF)
            ptr = cpu.r[6]
        except Exception:
            ptr = 0
        if ptr < POLY0 or ptr >= POLY0 + npoly * POLYSZ or (ptr - POLY0) % POLYSZ:
            out.append({"x": x, "z": z, "poly": -1})
            continue
        idx = (ptr - POLY0) // POLYSZ
        flags = m.r32(ptr)
        rec = {"x": x, "z": z, "poly": idx, "flags": flags}
        dst = 0x060F4000
        if (flags >> 16) != 0:
            r.call(GET_Y, r4=x & 0xFFFFFFFF, r5=z & 0xFFFFFFFF, r6=ptr, r7=dst)
            rec["y"] = s32(m.r32(0xFFFFFF1C))
        else:
            r.call(GET_D, r4=x & 0xFFFFFFFF, r5=z & 0xFFFFFFFF, r6=ptr, r7=dst)
            rec["d"] = s32(cpu.r[0])
        out.append(rec)
    return out


def block_vectors(r, course):
    m = r.mem
    g = GUIDE[course]
    out = []
    for k in range(0, NGUIDE, 2):
        x = m.r32(g + 16 * k)
        y = m.r32(g + 16 * k + 4)
        z = m.r32(g + 16 * k + 8)
        for dx, dz in ((0, 0), (1 << 17, 0), (0, 1 << 17)):          # on the line and 2 m to either side
            m.w32(CAR0 + 0x18, x + dx)
            m.w32(CAR0 + 0x1C, y)
            m.w32(CAR0 + 0x20, z + dz)
            try:
                r.call(GET_BLOCK)
            except Exception:
                continue
            rec = {"x": s32(x + dx), "z": s32(z + dz),
                   "seg": m.r16(CAR0 + 0x60), "prev": m.r16(CAR0 + 0x62), "ck": m.r8(CAR0 + 0xC4), "block": m.r8(CAR0 + 0xF0), "sel": m.r8(CAR0 + 0xF1),
                   "flag": m.r8(CAR0 + 0xF7), "dirx": s32(m.r32(CAR0 + 0xB8)), "dirz": s32(m.r32(CAR0 + 0xBC))}
            out.append(rec)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    result = {"math": None, "collision": {}, "block": {}}
    r0 = Race(0)
    result["math"] = math_vectors(r0)
    print("math vectors:", len(result["math"]))
    for c in range(4):
        r = Race(c)

        result["collision"][COURSES[c]] = collision_vectors(r)
        r = Race(c)
        result["block"][COURSES[c]] = block_vectors(r, c)
        print(COURSES[c], "collision", len(result["collision"][COURSES[c]]), "block", len(result["block"][COURSES[c]]))
    with open(os.path.join(OUT, "vectors.json"), "w") as f:
        json.dump(result, f)
    print("-> extracted/oracle/vectors.json")


if __name__ == "__main__":
    main()
