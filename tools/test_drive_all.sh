#!/bin/bash
# host test of src/drive.c (+ camera / part list where the trace has them) against every oracle trace in extracted/oracle/ (4 at a time).
#   wsl -d Ubuntu -- bash tools/test_drive_all.sh
# (the sound-state bytes +0x364..0x36F and +0xC6 are compared since M9: src/sndev.c).  *.hude traces (tools/oracle_hudend.sh) also check the end-of-race screens (NBG0 name tables, scroll, BGON ...); tools/test_hudend.sh the synthetic cases.
cd "$(dirname "$0")/.."
mkdir -p build/host
gcc -O1 -fwrapv -Wall -I src tools/test_drive.c src/drive.c src/camera.c src/pmap.c src/coli.c src/hudgen.c src/opp.c src/sndev.c -lm -o build/host/test_drive || exit 1
ls extracted/oracle/*.slots | sed 's#.*/##; s#\.slots##' | xargs -P 4 -I{} bash -c '
  n={}
  extra=""
  [ -f extracted/oracle/$n.cam ] && extra="-map"
  [ -f extracted/oracle/$n.sky ] && extra="$extra -sky"
  [ -f extracted/oracle/$n.hud ] && extra="$extra -hud"
  [ -f extracted/oracle/$n.hude ] && extra="$extra -hude"
  out=$(build/host/test_drive extracted/psp/course extracted/oracle/$n $extra | tr "\n" " ")
  echo "$n: $out"'
