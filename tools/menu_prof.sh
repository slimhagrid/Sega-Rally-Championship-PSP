#!/bin/bash
# M8: time the pictures of the oracle scenarios on the PC:  tools/menu_prof.sh <scenario>...   (or no argument = every scenario of tools/menu_scenarios.txt)
# runs the scenario's pad script (extracted/oracle/menu_<scenario>.mtr) and draws the PSP's picture (pspfix + software renderer) on every second tick; prints one "prof:" line per scenario.
# The PC is far faster than the PSP (guess: 20-40 times); compare the screens with each other, not with the 33 ms picture budget.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
SRC="tools/test_menu.c src/sat.c src/satlib.c src/sat_boot.c src/sat_save.c src/satmenu.c src/vdpr.c $(ls src/menu_*.c | grep -v menu_host.c)"
gcc -O2 -DPROF -g -Wall -Wno-unused-function -fno-strict-aliasing -I src -o build/host/menu_prof $SRC -lz -lm 2>&1 | grep -E "error" || true
if [ $# -eq 0 ]; then set -- $(grep -v '^#' tools/menu_scenarios.txt | awk '{print $1}'); fi
for s in "$@"; do
    printf "%-14s " "$s"
    ./build/host/menu_prof extracted/oracle/menu_$s.mtr extracted/psp 2>&1 | grep -E "^prof:|FAILED|inflate" | sed '2,$s/^/               /' || echo
done
