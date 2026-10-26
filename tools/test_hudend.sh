#!/bin/bash
# host test of the end-of-race HUD (src/hudgen.c) against the synthetic oracle cases: python tools/oracle.py hudsynth, then
#   wsl -d Ubuntu -- bash tools/test_hudend.sh
cd "$(dirname "$0")/.."
mkdir -p build/host
gcc -O1 -fwrapv -Wall -I src -I tools tools/test_hudend.c src/hudgen.c src/drive.c src/camera.c src/pmap.c src/coli.c src/sndev.c -lm -o build/host/test_hudend || exit 1
build/host/test_hudend extracted/psp/course extracted/oracle/hudsynth.bin
