#!/bin/bash
# host test of src/event.c against the oracle vectors (tools/evvec.py writes extracted/oracle/ev_<course>_<type>.vec)
#   wsl -d Ubuntu -- bash tools/test_event.sh [vector names, default all]
cd "$(dirname "$0")/.."
mkdir -p build/host
gcc -O1 -Wall -I src tools/test_event.c src/event.c src/coli.c -lm -o build/host/test_event || exit 1
names="$@"
[ -z "$names" ] && names="ev_0_0 ev_1_0 ev_2_0 ev_3_0 ev_0_2 ev_0_0_heli ev_0_2_heli ev_0_0_startobj"
for v in $names; do
    echo "== $v"
    rct=""
    case $v in ev_0_*) rct=extracted/psp/course/desert.rct;; esac
    EV_RCT=$rct build/host/test_event extracted/psp/course/events.dat extracted/psp/course/object.names extracted/oracle/$v.vec | tail -${LINES_TAIL:-8}
done
