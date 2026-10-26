#!/bin/bash
# host test of src/drive.c against the oracle traces (tools/oracle.py run --slots writes extracted/oracle/<course>_<scenario>.{inp,slots})
#   wsl -d Ubuntu -- bash tools/test_drive.sh desert_drive [more test_drive options]
cd "$(dirname "$0")/.."
mkdir -p build/host
gcc -O1 -fwrapv -Wall -I src tools/test_drive.c src/drive.c src/camera.c src/pmap.c src/coli.c src/hudgen.c src/opp.c src/sndev.c -lm -o build/host/test_drive || exit 1
name=${1:-desert_drive}
shift
build/host/test_drive extracted/psp/course extracted/oracle/$name "$@"
