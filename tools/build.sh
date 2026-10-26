#!/bin/bash
# Build EBOOT.PBP and stage the game folder with the converted data:
#
#   build/  ->  dist/SegaRallyPSP/      EBOOT.PBP + README.txt + data/  (what goes to ms0:/PSP/GAME/)
#
# Run on Linux / inside WSL:  bash tools/build.sh.  From PowerShell: wsl -d Ubuntu -- bash /mnt/<drive>/path/to/this/folder/tools/build.sh
# (not from Git Bash: it rewrites /mnt/... paths).  PSPDEV points at the PSP toolchain (default ~/pspdev).  Run  python tools/make_data.py  first: the game's data is copied from extracted/psp.
# SNDLOG=1 also puts an empty data/sndlog (the audio timing log) and the test switches data/*.off into the game folder.
set -e
export PSPDEV="${PSPDEV:-$HOME/pspdev}"
export PATH="$PATH:$PSPDEV/bin"
cd "$(dirname "$0")/.."
ROOT=$PWD
B=$ROOT/build
D=$ROOT/dist/SegaRallyPSP

mkdir -p "$B" "$D"
cd "$B"
psp-cmake "$ROOT" >/dev/null          # always re-configure: picks up pbp/ICON0.png, PIC1.png ... added or changed since the last build
make -j"$(nproc)"
cp EBOOT.PBP "$D/EBOOT.PBP"
[ -f "$ROOT/pbp/README.txt" ] && cp "$ROOT/pbp/README.txt" "$D/README.txt"

# Converted game data (tools/make_data.py writes extracted/psp/...): mirrored into data/
if [ -d "$ROOT/extracted/psp" ]; then
    mkdir -p "$D/data"
    if command -v rsync >/dev/null; then
        rsync -a --delete "$ROOT/extracted/psp/" "$D/data/"
    else                                            # no rsync: start from an empty data/ and copy
        rm -rf "$D/data"; mkdir -p "$D/data"; cp -a "$ROOT/extracted/psp/." "$D/data/"
    fi
else
    echo "extracted/psp is missing: run  python tools/make_data.py CUE  first (the EBOOT alone does not run)"
fi
echo 0 > "$D/data/menu.txt"                         # main.c starts the ported menus when data/menu.txt exists (the start vector in hex; 0 = what the power-on leaves: the title / attract screen; 06022894 starts at the mode select for development)
if [ "${SNDLOG:-0}" = 1 ]; then : > "$D/data/sndlog"; : > "$D/data/nocd.off"; echo 60 > "$D/data/sndvol.off"; echo 70 > "$D/data/cdvol.off"; fi      # the audio timing log (data/sndlog.txt) and the test switches: renaming nocd.off to nocd turns the CD music off, sndvol.off to sndvol sets the output level, cdvol.off to cdvol the music level, to the percentage in them
echo "dist/SegaRallyPSP: $(du -sh "$D" | cut -f1)"
ls -la "$B/EBOOT.PBP"
