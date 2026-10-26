#!/bin/bash
# A replay of a recorded time attack run (play mode 2: the result screen's "replay: yes", a record's replay): the oracle traces src/drive.c (mode 2), src/camera.c (the cut-scene cameras of CAMERA.DAT)
# and src/hudgen.c (the replay HUD) are checked against.  Needs the recordings of oracle_ghost.sh (<course>_drive_gr.irec).
# python (Windows) and the disc files are needed; the traces go to extracted/oracle/.  From a shell that finds python:  bash tools/oracle_replay.sh   (PYTHON=python.exe if needed)
#   _rpt : the replay of <course>_drive_gr.irec with --render --hudend (camera, part list, HUD, the end sequence); --rtimes = the run's times as the record's replay start leaves them in the R block
# Check: tools/test_drive.sh desert_drive_rpt -map -hud -hude     (tools/test_drive_all.sh does every trace in extracted/oracle)
cd "$(dirname "$0")/.."
PY=${PYTHON:-python}
T=00024047,00005234,00005412,00005401
cat <<LIST | xargs -P 4 -I{} bash -c "echo == oracle.py run {}; $PY -I tools/oracle.py run {}"
--course 0 --scenario drive --type 4 --replay extracted/oracle/desert_drive_gr.irec --rtimes $T --tag _rpt --slots --rec --render --hudend --max 9000
--course 1 --scenario drive --type 4 --car 1 --mt 1 --setup 1,2,1,3 --replay extracted/oracle/forest_drive_gr.irec --rtimes $T --tag _rpt --slots --rec --render --hudend --max 9000
--course 2 --scenario drive --type 4 --setup 2,3,2,1 --replay extracted/oracle/mount_drive_gr.irec --rtimes $T --tag _rpt --slots --rec --render --hudend --max 9000
--course 3 --scenario drive --type 4 --car 2 --replay extracted/oracle/lake_drive_gr.irec --rtimes $T --tag _rpt --slots --rec --render --hudend --max 9000
LIST
