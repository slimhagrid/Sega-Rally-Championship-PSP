#!/bin/bash
# M8: get_camera_angle on the port against the original:  python tools/fn_ending.py; bash tools/test_ending_fn.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
SRC="tools/test_ending_fn.c src/sat.c src/satlib.c src/sat_boot.c src/sat_save.c src/satmenu.c $(ls src/menu_*.c | grep -v 'menu_host.c\|menu_ending.c')"
gcc -O1 -g -Wno-unused-function -fno-strict-aliasing -I src -o build/host/test_ending_fn $SRC -lz -lm
./build/host/test_ending_fn
