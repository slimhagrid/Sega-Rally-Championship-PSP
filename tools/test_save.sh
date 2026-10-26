#!/bin/bash
# M8: build and run the unit test of the save data layer (src/sat_save.c, src/menu_bup.c):  tools/test_save.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host build/host/save_tmp
SRC="tools/test_save.c src/sat.c src/satlib.c src/sat_boot.c src/sat_save.c src/satmenu.c $(ls src/menu_*.c 2>/dev/null | grep -v menu_host.c || true)"
gcc -O1 -g -Wall -Wno-unused-function -fno-strict-aliasing -I src -o build/host/test_save $SRC -lz
./build/host/test_save extracted/psp build/host/save_tmp
