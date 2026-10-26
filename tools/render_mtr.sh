#!/bin/bash
# M8: build and run the trace renderer:  tools/render_mtr.sh <scenario> <tick> <out.ppm>
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
gcc -O1 -g -Wall -Wno-unused-function -fno-strict-aliasing -I src -o build/host/render_mtr tools/render_mtr.c src/sat.c src/satlib.c src/vdpr.c src/menu_pspfix.c src/menu_padcfg.c -lz -lm 2>&1 | head -30
./build/host/render_mtr extracted/oracle/menu_$1.mtr $2 $3
