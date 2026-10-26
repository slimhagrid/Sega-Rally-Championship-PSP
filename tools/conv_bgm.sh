#!/bin/bash
# M9: the songs of the menu screens as streams for the PSP (src/bgm.c):  wsl -d Ubuntu -- bash tools/conv_bgm.sh
# Each song is rendered by the original sound driver (the C core, tools/bgm_render.c) for both settings of the stereo option, the loop is cut (one period of the sequence, found with
# tools/bgm_loop.py: the lag with the smallest waveform error; the second period is used so that the notes of the end of the first are ringing) and encoded (tools/bgm_enc.c).
# Output: extracted/psp/snd/<name>_0.bgm (stereo option off: two channels) / <name>_1.bgm (on: the driver plays the same sound on both sides: one channel) and bgm.idx
# ("set value name": the sound set of select_set and the id table value of the song).
cd "$(dirname "$0")/.."
set -e
SRC="src/sat.c src/satlib.c src/sat_boot.c src/sat_save.c src/satmenu.c src/vdpr.c $(ls src/menu_*.c | grep -v menu_host.c) src/snd_lib.c src/bgm.c src/snd.c src/m68k.c src/scsp.c tools/cdda_stub.c"
mkdir -p build/host extracted/psp/snd
gcc -std=gnu99 -O2 -Wall -Wno-unused-function -fno-strict-aliasing -I src tools/bgm_render.c $SRC -lz -lm -o build/host/bgm_render 2>&1 | grep -E "error|undefined" || true
gcc -O2 -Wall tools/bgm_enc.c -lm -o build/host/bgm_enc
OUT=extracted/psp/snd
: > $OUT/bgm.idx
XF=2205
# set  id  value     name  kind  period-or-end (driver ticks of 89 frames)
songs="
0 40 00000000 sel  loop 27071
2 60 00000000 res  loop 27433
3 70 00000000 org0 loop 38425
3 71 00010000 org1 loop 76850
4 80 00000000 end  once 34936
5 82 00000000 cere once 10573
"
echo "$songs" | while read set id value name kind ticks; do
    [ -z "$set" ] && continue
    if [ "$kind" = loop ]; then secs=$(( ticks * 2 * 89 / 44100 + 8 )); else secs=$(( ticks * 89 / 44100 + 4 )); fi
    for v in 0 1; do
        if [ $v = 1 ]; then x=""; else x="mono"; fi      # (bgm_render's "mono" clears the driver's flag bit: variant 0)
        build/host/bgm_render extracted/psp $set $id $secs build/host/bgm_$name$v.raw $x > /dev/null &
    done
    wait
    for v in 0 1; do
        m=""; [ $v = 1 ] && m="mono"
        if [ "$kind" = loop ]; then
            build/host/bgm_enc build/host/bgm_$name$v.raw $OUT/${name}_$v.bgm $((ticks * 89)) $((ticks * 89)) $XF $m
        else
            build/host/bgm_enc build/host/bgm_$name$v.raw $OUT/${name}_$v.bgm 0 $((ticks * 89)) 0 $m
        fi
    done
    echo "$set $value $name" >> $OUT/bgm.idx
    rm -f build/host/bgm_$name?.raw
done
ls -la $OUT
