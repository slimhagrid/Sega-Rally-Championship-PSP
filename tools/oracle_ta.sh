#!/bin/bash
# Time attack (game types 4 = three laps, 6 = free run) and the compare with a best run: the oracle traces the driving core (src/drive.c), the camera and the HUD (src/hudgen.c) are checked against.
# python (Windows) and the disc files are needed; the traces go to extracted/oracle/.  From a shell that finds python:  bash tools/oracle_ta.sh   (PYTHON=python.exe if needed)
#   _t4end / _t6end : the race with camera, part list, HUD and the goal screens (--render --hudend), record buffers
#   _t4cmp / _t6cmp / _t2cmp / _t0cmp : the same with the compare option on and the reference list of the _t4end / _t6end / practice / arcade run (--cmp N --ref FILE.rec; test_drive reads <name>.cmp)
# Check: tools/test_drive.sh desert_drive_t4end -map -hud -hude      (tools/test_drive_all.sh does every trace in extracted/oracle)
cd "$(dirname "$0")/.."
PY=${PYTHON:-python}
cat <<'LIST' | xargs -P 4 -I{} bash -c "echo == oracle.py run {}; $PY -I tools/oracle.py run {}"
--course 0 --scenario drive --type 4 --tag _t4end --slots --rec --render --hudend --max 9000
--course 1 --scenario drive --type 4 --tag _t4end --slots --rec --render --hudend --max 9000
--course 2 --scenario drive --type 4 --tag _t4end --slots --rec --render --hudend --max 9000
--course 3 --scenario drive --type 4 --tag _t4end --slots --rec --render --hudend --max 9000
--course 0 --scenario drive --type 6 --tag _t6end --slots --rec --render --hudend --max 9000
--course 1 --scenario drive --type 6 --tag _t6end --slots --rec --render --hudend --max 9000
--course 2 --scenario drive --type 6 --tag _t6end --slots --rec --render --hudend --max 9000
--course 3 --scenario drive --type 6 --tag _t6end --slots --rec --render --hudend --max 9000
LIST
cat <<'LIST' | xargs -P 4 -I{} bash -c "echo == oracle.py run {}; $PY -I tools/oracle.py run {}"
--course 0 --scenario drive --type 4 --car 1 --cmp 1 --ref extracted/oracle/desert_drive_t4end.rec --tag _t4cmp --slots --rec --render --hudend --max 9000
--course 0 --scenario drive --type 6 --car 1 --cmp 2 --ref extracted/oracle/desert_drive_t6end.rec --tag _t6cmp --slots --rec --render --hudend --max 9000
--course 0 --scenario drive --type 2 --car 1 --cmp 1 --ref extracted/oracle/desert_drive_rec.rec --tag _t2cmp --slots --rec --render --hudend --max 9000
--course 0 --scenario drive --type 0 --car 1 --cmp 1 --ref extracted/oracle/desert_drive_t0rec.rec --tag _t0cmp --slots --rec --render --hudend --max 9000
--course 1 --scenario drive --type 4 --car 1 --cmp 1 --ref extracted/oracle/forest_drive_t4end.rec --tag _t4cmp --slots --rec --render --hudend --max 9000
--course 1 --scenario drive --type 6 --car 1 --cmp 2 --ref extracted/oracle/forest_drive_t6end.rec --tag _t6cmp --slots --rec --render --hudend --max 9000
LIST
