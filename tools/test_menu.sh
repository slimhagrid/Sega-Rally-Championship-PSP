#!/bin/bash
# M8: build and run the menu port against an oracle trace:  tools/test_menu.sh <scenario> [maxdiffs]
# trace: extracted/oracle/menu_<scenario>.mtr, made by `python tools/menu_oracle.py <scenario>` (Windows Python); the binary is build/host/test_menu_<scenario> (parallel runs do not clash)
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
SRC="tools/test_menu.c src/sat.c src/satlib.c src/sat_boot.c src/sat_save.c src/satmenu.c $(ls src/menu_*.c 2>/dev/null | grep -v menu_host.c || true)"
gcc -O1 -g -Wall -Wno-unused-function -fno-strict-aliasing -I src -o build/host/test_menu_$1 $SRC -lz
./build/host/test_menu_$1 extracted/oracle/menu_$1.mtr extracted/psp "${2:-24}"
