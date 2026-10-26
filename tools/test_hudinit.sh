#!/bin/bash
# host test of the time attack display set-up (src/hudgen.c hudgen_ta_init) against the original's init_game_disp: python tools/hudinit_vec.py, then
#   wsl -d Ubuntu -- bash tools/test_hudinit.sh
cd "$(dirname "$0")/.."
mkdir -p build/host
gcc -O1 -fwrapv -Wall -I src tools/test_hudinit.c src/hudgen.c src/drive.c src/camera.c src/pmap.c src/coli.c src/sndev.c -lm -o build/host/test_hudinit || exit 1
build/host/test_hudinit extracted/psp/course extracted/oracle/hudinit.vec
