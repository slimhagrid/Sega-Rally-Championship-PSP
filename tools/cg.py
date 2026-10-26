"""M8 tool: call-graph closure from an entry function (static callgraph.tsv + literal pool pointers), printing function, size, module.
    python tools/cg.py adv_init [-x race,mkpoly,...]   (-x: modules not to descend into)"""
import collections
import os
import sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rd  # noqa
cg = collections.defaultdict(set)
for ln in open(os.path.join(rd.AN, "callgraph.tsv")):
    a, an, b, bn, k = ln.rstrip("\n").split("\t")
    cg[int(a, 16)].add(int(b, 16))
args = sys.argv[1:]
skip = set()
if "-x" in args:
    i = args.index("-x"); skip = set(args[i + 1].split(",")); del args[i:i + 2]
roots = [rd.find_func(a)[0] for a in args]
seen = {}
st = list(roots)
while st:
    a = st.pop()
    if a in seen: continue
    f = rd.BYADDR.get(a)
    seen[a] = f
    if f and f[3] in skip: continue
    for b in cg.get(a, ()):
        if b not in seen: st.append(b)
tot = 0
for a in sorted(seen):
    f = seen[a]
    if f is None: print("%08x  ?" % a); continue
    print("%08x %5d %-22s %s%s" % (a, f[1], f[2], f[3], "  (skipped)" if f[3] in skip else ""))
    tot += f[1] if f[3] not in skip else 0
print("total", tot)
