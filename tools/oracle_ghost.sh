#!/bin/bash
# The ghost ("myline": a stored best run races in slot 1, game type 4) and the recording of the player's input words: the oracle traces src/drive.c is checked against.
# python (Windows) and the disc files are needed; the traces go to extracted/oracle/.  From a shell that finds python:  bash tools/oracle_ghost.sh   (PYTHON=python.exe if needed)
#   _gr : a time attack run that records its inputs (--irec: the input words, 16 bit per pass)
#   _gg : a second run (slower pilot) with that recording as the stored best run: slot 1 (--slots also writes <name>.slot1) races it, the ghost finishes first
# Check: tools/test_drive.sh desert_drive_gg      (compares the player's slot, the ghost's slot, the recorded input words; tools/test_drive_all.sh does every trace in extracted/oracle)
cd "$(dirname "$0")/.."
PY=${PYTHON:-python}
cat <<'LIST' | xargs -P 4 -I{} bash -c "echo == oracle.py run {}; $PY -I tools/oracle.py run {}"
--course 0 --scenario drive --type 4 --tag _gr --slots --irec --rec --max 9000
--course 1 --scenario drive --type 4 --car 1 --mt 1 --setup 1,2,1,3 --tag _gr --slots --irec --rec --max 9000
--course 2 --scenario drive --type 4 --setup 2,3,2,1 --tag _gr --slots --irec --rec --max 9000
--course 3 --scenario drive --type 4 --car 2 --tag _gr --slots --irec --rec --max 9000
LIST
cat <<'LIST' | xargs -P 4 -I{} bash -c "echo == oracle.py run {}; $PY -I tools/oracle.py run {}"
--course 0 --scenario drive --type 4 --vmax 1.9 --tag _gg --ghost extracted/oracle/desert_drive_gr.irec --slots --irec --rec --max 9000
--course 1 --scenario drive --type 4 --car 1 --mt 1 --setup 1,2,1,3 --vmax 1.9 --tag _gg --ghost extracted/oracle/forest_drive_gr.irec --slots --irec --rec --max 9000
--course 2 --scenario drive --type 4 --setup 2,3,2,1 --vmax 1.9 --tag _gg --ghost extracted/oracle/mount_drive_gr.irec --slots --irec --rec --max 9000
--course 3 --scenario drive --type 4 --car 2 --vmax 1.9 --tag _gg --ghost extracted/oracle/lake_drive_gr.irec --slots --irec --rec --max 9000
LIST
