#!/bin/bash
# M9: src/sndev.c (and the sound calls of src/drive.c) against the oracle's sound logs of every extracted/oracle/*_snd*.snd (made by tools/oracle_snd.sh)
#   wsl -d Ubuntu -- bash tools/test_snd_ev.sh
cd "$(dirname "$0")/.."
mkdir -p build/host
gcc -O1 -fwrapv -Wall -I src tools/test_drive.c src/drive.c src/camera.c src/pmap.c src/coli.c src/hudgen.c src/opp.c src/sndev.c -lm -o build/host/test_drive || exit 1
rc=0
for f in extracted/oracle/*_snd*.snd; do
    n=$(basename "$f" .snd)
    extra=""
    [ -f extracted/oracle/$n.cam ] && extra="-map"
    [ -f extracted/oracle/$n.hud ] && extra="$extra -hud"
    [ -f extracted/oracle/$n.hude ] && extra="$extra -hude"
    out=$(build/host/test_drive extracted/psp/course extracted/oracle/$n -snd $extra | tr "\n" " ")
    echo "$n: $out"
    case "$out" in *" 0 differ"*"sound: 0 passes differ"*) ;; *) rc=1 ;; esac
done
exit $rc
