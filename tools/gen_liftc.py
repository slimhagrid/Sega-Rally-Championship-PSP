"""regenerate extracted/liftc/<module>.c (lifted C skeletons of every module, see tools/lift.py --c)"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import rd, lift
lift.C_MODE = True
ents = []
for ln in open(os.path.join(ROOT, 'extracted', 'analysis', 'modules.tsv')):
    p = ln.rstrip('\n').split('\t')
    if len(p) >= 4 and p[1] in ('P', 'SEGA_P'):
        ents.append((int(p[2], 16), int(p[3], 16), p[0]))
ents.sort()
out = {}
for a, n, m in ents:
    if not (0x06004000 <= a < 0x06035988) or n == 0:
        continue
    try:
        t = lift.to_c(lift.Lift((a, n, "%s_%x" % (m, a), m)).run())
    except Exception as e:
        t = "// lift failed %s" % e
    out.setdefault(m, []).append(t)
os.makedirs(os.path.join(ROOT, 'extracted', 'liftc'), exist_ok=True)
for m, v in out.items():
    open(os.path.join(ROOT, 'extracted', 'liftc', '%s.c' % m), 'w').write("\n\n".join(v) + "\n")
print(len(out), "modules")
