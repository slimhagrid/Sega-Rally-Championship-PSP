#!/bin/bash
# host test of src/opp.c against the oracle's other cars (tools/oracle.py run --opp --render --slots --tag _opp):
#   wsl -d Ubuntu -- bash tools/test_opp.sh desert_drive_opp [more test_drive options]
cd "$(dirname "$0")/.."
mkdir -p build/host
gcc -O1 -fwrapv -Wall -I src tools/test_drive.c src/drive.c src/camera.c src/pmap.c src/coli.c src/hudgen.c src/opp.c src/sndev.c -lm -o build/host/test_drive || exit 1
name=${1:-desert_drive_opp}
shift
build/host/test_drive extracted/psp/course extracted/oracle/$name -mask 364-370 -mask c6-c7 "$@"
