"""Flatten the math / collision sections of extracted/oracle/vectors.json into a text file for tools/test_gmath.c (no JSON parser needed on the C side).

    python tools/export_math_vec.py      -> extracted/oracle/math.txt
Lines: "sqrt x out" / "atan x y out" / "coli <course 0-3> x z poly [y20]"   (poly -1 = none; y20 only for floor polygons)
"""
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
v = json.load(open(os.path.join(ROOT, "extracted", "oracle", "vectors.json")))
lines = []
for e in v["math"]:
    if e["fn"] == "sqrt":
        lines.append("sqrt %d %d" % (e["in"][0], e["out"]))
    elif e["fn"] == "atan":
        lines.append("atan %d %d %d" % (e["in"][0], e["in"][1], e["out"]))
for ci, name in enumerate(("DESERT", "FOREST", "MOUNT", "LAKE")):
    for e in v["collision"][name]:
        if e["poly"] < 0:
            lines.append("coli %d %d %d -1" % (ci, e["x"], e["z"]))
        elif "y" in e:
            lines.append("coli %d %d %d %d %d" % (ci, e["x"], e["z"], e["poly"], e["y"]))
        else:
            lines.append("coli %d %d %d %d" % (ci, e["x"], e["z"], e["poly"]))
open(os.path.join(ROOT, "extracted", "oracle", "math.txt"), "w").write("\n".join(lines) + "\n")
print(len(lines), "vectors")
