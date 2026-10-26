#!/bin/bash
# M8: the menu regression against the recorded traces without recompiling per scenario:  tools/test_menu_fast.sh [scenario...]   (no argument = every scenario of tools/menu_scenarios.txt that has a trace)
# (tools/test_menu_all.ps1 records the traces on the SH-2 oracle first; this only runs the C port against extracted/oracle/menu_<scenario>.mtr)
cd "$(dirname "$0")/.."
mkdir -p build/host
SRC="tools/test_menu.c src/sat.c src/satlib.c src/sat_boot.c src/sat_save.c src/satmenu.c $(ls src/menu_*.c 2>/dev/null | grep -v menu_host.c || true)"
gcc -O1 -g -Wall -Wno-unused-function -fno-strict-aliasing -I src -o build/host/test_menu_fast $SRC -lz || exit 2
if [ $# -eq 0 ]; then set -- $(grep -v '^#' tools/menu_scenarios.txt | awk '{print $1}'); fi
fail=0; n=0
for s in "$@"; do
    [ -f extracted/oracle/menu_$s.mtr ] || continue
    r=$(./build/host/test_menu_fast extracted/oracle/menu_$s.mtr extracted/psp 24 2>&1 | tail -3)
    n=$((n + 1))
    if echo "$r" | grep -q "identical" && ! echo "$r" | grep -qE "FAILED|DIFFERS"; then echo "ok   $s"; else echo "FAIL $s"; echo "$r"; fail=$((fail + 1)); fi
done
echo "$n scenarios, $fail failed"
exit $fail
