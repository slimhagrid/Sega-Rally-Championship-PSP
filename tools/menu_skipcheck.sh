#!/bin/bash
# M8: the shortcuts of src/vdpr.c (the layer caches, the dirty VRAM pages, the unchanged picture VdpFrame.same, the table driven composition, the sprite spans, the specialised sprite loops) must not change a
# single picture:  tools/menu_skipcheck.sh [scenario...]   (no argument = every scenario of tools/menu_scenarios.txt;  HI=1: the pictures in two parts at the sprite priority, as a screen with 3D models is drawn)
# runs each scenario twice with tools/test_menu.c -DPROF (PROF_HASH: one hash per picture), once with VDPR_REF + VDPR_NOSKIP + VDPR_NOCACHE (everything rebuilt for every picture, composed dot by dot, the
# straightforward sprite loop) and once as the PSP runs it, and compares the two hash lists.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
BIN=build/host/menu_skip_$$
SRC="tools/test_menu.c src/sat.c src/satlib.c src/sat_boot.c src/sat_save.c src/satmenu.c src/vdpr.c $(ls src/menu_*.c | grep -v menu_host.c)"
gcc -O2 -DPROF -g -Wall -Wno-unused-function -fno-strict-aliasing -I src -o $BIN $SRC -lz -lm 2>&1 | grep -E "error" || true
if [ $# -eq 0 ]; then set -- $(grep -v '^#' tools/menu_scenarios.txt | awk '{print $1}'); fi
if [ -n "$HI" ]; then export VDPR_HI=1; fi
bad=0
for s in "$@"; do
    [ -f extracted/oracle/menu_$s.mtr ] || continue
    VDPR_REF=1 VDPR_NOSKIP=1 VDPR_NOCACHE=1 PROF_HASH=/tmp/skip_ref_$$.txt ./$BIN extracted/oracle/menu_$s.mtr extracted/psp >/dev/null 2>&1 || true
    PROF_HASH=/tmp/skip_new_$$.txt ./$BIN extracted/oracle/menu_$s.mtr extracted/psp > /tmp/skip_out_$$.txt 2>&1 || true
    touch /tmp/skip_ref_$$.txt /tmp/skip_new_$$.txt         # a scenario that draws no picture
    if cmp -s /tmp/skip_ref_$$.txt /tmp/skip_new_$$.txt; then
        printf "%-16s identical  %s\n" "$s" "$(grep -o '[0-9]* pictures unchanged' /tmp/skip_out_$$.txt) of $(wc -l < /tmp/skip_new_$$.txt)"
    else
        printf "%-16s DIFFERENT  first: %s\n" "$s" "$(diff /tmp/skip_ref_$$.txt /tmp/skip_new_$$.txt | head -2 | tr '\n' ' ')"
        bad=1
    fi
done
rm -f /tmp/skip_ref_$$.txt /tmp/skip_new_$$.txt /tmp/skip_out_$$.txt $BIN
exit $bad
