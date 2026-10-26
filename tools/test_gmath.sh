#!/bin/bash
# host test of src/gmath.h (sqrt, atan) and src/coli.c (collision lookup) against the oracle vectors
#   python tools/export_math_vec.py        (once: extracted/oracle/math.txt from vectors.json)
#   wsl -d Ubuntu -- bash tools/test_gmath.sh
cd "$(dirname "$0")/.."
mkdir -p build/host
gcc -O1 -Wall -I src tools/test_gmath.c src/coli.c -lm -o build/host/test_gmath || exit 1
build/host/test_gmath extracted/oracle/math.txt extracted/psp/course
