#!/bin/bash
# line coverage of src/drive.c over the oracle traces (which branches of the port do the traces exercise?)
#   wsl -d Ubuntu -- bash tools/cov_drive.sh [first_line last_line]   (prints the lines never executed)
cd "$(dirname "$0")/.."
rm -rf build/host/cov && mkdir -p build/host/cov
gcc -O0 --coverage -fwrapv -I src tools/test_drive.c src/drive.c src/camera.c src/pmap.c src/coli.c src/hudgen.c -lm -o build/host/cov/test_drive || exit 1
for f in extracted/oracle/*.slots; do
  n=${f%.slots}
  build/host/cov/test_drive extracted/psp/course $n -mask 364-370 -mask c6-c7 > /dev/null
done
gcov -o build/host/cov build/host/cov/test_drive-drive.gcda > /dev/null 2>&1
mv drive.c.gcov build/host/cov/ 2>/dev/null
lo=${1:-1}; hi=${2:-99999}
awk -F: -v lo=$lo -v hi=$hi '$2+0>=lo && $2+0<=hi && $1 ~ /#####/ {print $2 ":" $3}' build/host/cov/drive.c.gcov
