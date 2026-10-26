#!/bin/bash
# host test of src/spray.c against the oracle vectors (tools/spvec.py writes extracted/oracle/sp_<course>_<type>.vec)
#   wsl -d Ubuntu -- bash tools/test_spray.sh [vector names ...]
cd "$(dirname "$0")/.."
mkdir -p build/host
gcc -O1 -fwrapv -Wall -I src tools/test_spray.c src/spray.c src/drive.c src/coli.c src/sndev.c -lm -o build/host/test_spray || exit 1
[ $# -eq 0 ] && set -- $(ls extracted/oracle/sp_*.vec | sed 's#.*/##; s#\.vec##')
for n in "$@"; do
  echo -n "$n: "
  build/host/test_spray extracted/psp/course extracted/oracle/$n.vec
done
