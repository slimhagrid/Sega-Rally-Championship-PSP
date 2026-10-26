#!/bin/bash
# M8: function-level check of the record helpers against the original:  python tools/fn_records.py   then   tools/test_rec_fn.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
SRC="tools/test_rec_fn.c src/sat.c src/satlib.c src/sat_boot.c src/sat_save.c src/satmenu.c $(ls src/menu_*.c 2>/dev/null | grep -v menu_host.c || true)"
gcc -O1 -g -Wall -Wno-unused-function -fno-strict-aliasing -I src -o build/host/test_rec_fn $SRC -lz
./build/host/test_rec_fn extracted/oracle/fn_records.txt extracted/psp
