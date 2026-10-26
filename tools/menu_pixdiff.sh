#!/bin/bash
# M8: which pixels of picture N differ between the straightforward renderer and the PSP's:  [HI=1] tools/menu_pixdiff.sh <scenario> <picture n>
# (HI=1: the picture in two parts; prints the differing pixels of the picture and of its upper part, with the bounding box, and compares the two parts put together with the one-part picture of the reference)
cd "$(dirname "$0")/.."
SRC="tools/test_menu.c src/sat.c src/satlib.c src/sat_boot.c src/sat_save.c src/satmenu.c src/vdpr.c $(ls src/menu_*.c | grep -v menu_host.c)"
gcc -O2 -DPROF -g -Wno-unused-function -fno-strict-aliasing -I src -o build/host/menu_dbg $SRC -lz -lm || exit 2
export PROF_SHOT=$2
VDPR_REF=1 VDPR_NOSKIP=1 VDPR_NOCACHE=1 PROF_HASH=/tmp/dflat.txt ./build/host/menu_dbg extracted/oracle/menu_$1.mtr extracted/psp >/dev/null 2>&1
if [ -n "$HI" ]; then export VDPR_HI=1; fi
VDPR_REF=1 VDPR_NOSKIP=1 VDPR_NOCACHE=1 PROF_HASH=/tmp/dref.txt ./build/host/menu_dbg extracted/oracle/menu_$1.mtr extracted/psp >/dev/null 2>&1
PROF_HASH=/tmp/dnew.txt ./build/host/menu_dbg extracted/oracle/menu_$1.mtr extracted/psp >/dev/null 2>&1
python3 - <<'EOF'
import struct
def rd(p):
    d = open(p, "rb").read()
    return struct.unpack("<%dI" % (len(d) // 4), d)
w = 352
flat = rd("/tmp/dflat.txt.fr.raw")
def comp(fr, hi):
    return [h if (h >> 24) else f for f, h in zip(fr, hi)]
def report(name, a, b):
    diffs = [(i // w, i % w, x, y) for i, (x, y) in enumerate(zip(a, b)) if x != y]
    print(name, len(diffs), "differing pixels")
    for d in diffs[:6]:
        print("   y %d x %d  %08x  %08x" % d)
    if diffs:
        ys = [d[0] for d in diffs]; xs = [d[1] for d in diffs]
        print("   bbox x %d..%d y %d..%d" % (min(xs), max(xs), min(ys), max(ys)))
rf, rh = rd("/tmp/dref.txt.fr.raw"), rd("/tmp/dref.txt.hi.raw")
nf, nh = rd("/tmp/dnew.txt.fr.raw"), rd("/tmp/dnew.txt.hi.raw")
report("flat reference vs reference two-part put together", flat, comp(rf, rh))
report("flat reference vs PSP two-part put together", flat, comp(nf, nh))
report("reference lower part vs PSP lower part", rf, nf)
report("reference upper part vs PSP upper part", rh, nh)
EOF
