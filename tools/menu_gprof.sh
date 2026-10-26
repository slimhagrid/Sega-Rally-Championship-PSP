#!/bin/bash
# M8: where the PC spends the time of a menu scenario (the PSP costs follow the instruction counts, not the PC's cache behaviour):  tools/menu_gprof.sh <scenario> [lines]
# builds tools/test_menu.c with -DPROF -pg (renders every second tick like the PSP) and prints gprof's flat profile.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
SRC="tools/test_menu.c src/sat.c src/satlib.c src/sat_boot.c src/sat_save.c src/satmenu.c src/vdpr.c $(ls src/menu_*.c | grep -v menu_host.c)"
gcc -O2 -DPROF -pg -fno-inline-functions -fno-strict-aliasing -Wno-unused-function -I src -o build/host/menu_gprof $SRC -lz -lm
cd build/host
./menu_gprof ../../extracted/oracle/menu_$1.mtr ../../extracted/psp > /dev/null 2>&1 || true
gprof -b -p menu_gprof gmon.out 2>/dev/null | head -${2:-25}
rm -f gmon.out
