#!/bin/bash
# M8: build and run the PSP host's menu loop on the PC:  tools/menu_pc.sh <start vector> <script> <maxticks> <outprefix> [shot ticks...]
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
SRC="tools/menu_pc.c src/sat.c src/satlib.c src/sat_boot.c src/sat_save.c src/satmenu.c src/vdpr.c $(ls src/menu_*.c | grep -v menu_host.c)"
gcc -O1 -g -Wall -Wno-unused-function -fno-strict-aliasing -I src -o build/host/menu_pc $SRC -lz -lm 2>&1 | grep -E "error" || true
timeout "${MENU_PC_TIMEOUT:-120}" ./build/host/menu_pc extracted/psp "$@"
