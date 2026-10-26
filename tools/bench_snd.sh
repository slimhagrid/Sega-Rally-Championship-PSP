#!/bin/bash
# M9: speed of the 68000 interpreter and the mixer on the PC: tools/bench_snd.sh [extra gcc flags]
cd "$(dirname "$0")/.."
gcc -std=gnu99 -O2 "$@" -DSND_PROF -I src tools/test_snd.c src/snd.c src/m68k.c src/scsp.c -lm -o build/host/test_snd_prof 2>&1 | head -3
build/host/test_snd_prof extracted/snd/sc1.ram extracted/snd/sc1.cmds 6000 /dev/null /dev/null | head -1
