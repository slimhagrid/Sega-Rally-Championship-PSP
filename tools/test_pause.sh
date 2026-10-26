#!/bin/bash
# host test of the pause menu's picture (src/hudgen.c) against the original's routines: python tools/pause_vec.py, then
#   wsl -d Ubuntu -- bash tools/test_pause.sh
cd "$(dirname "$0")/.."
mkdir -p build/host
gcc -O1 -fwrapv -Wall -I src tools/test_pause.c src/hudgen.c src/drive.c src/camera.c src/pmap.c src/coli.c src/sndev.c -lm -o build/host/test_pause || exit 1
build/host/test_pause extracted/psp/course extracted/oracle/pause.vec
