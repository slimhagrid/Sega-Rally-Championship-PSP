#!/bin/bash
# The ending's replay: the arcade chain's last stage (the lake) replayed from a recording of the player's input words with the rival cars (play mode 2, game type 0): the oracle traces
# src/drive.c (DriveCfg.ending), src/opp.c, the cut-scene camera (src/camera.c) and the part lists the engine of src/ending_engine.c is checked against.
# python (Windows) and the disc files are needed; the traces go to extracted/oracle/.  From a shell that finds python:  bash tools/oracle_ending.sh   (PYTHON=python.exe if needed)  (about 30 minutes)
#   _er : an arcade race on the lake with the other cars that records its inputs (--irec)
#   _ep : the replay of that recording as the ending runs it (--replay, --ending: the presentation 0x060066F8): --opp --render --slots, the camera and the other cars after every pass
# Check: tools/test_drive.sh lake_drive_ep -opp -map      (-cam is implied; the other cars: tools/test_opp.sh lake_drive_ep)
cd "$(dirname "$0")/.."
PY=${PYTHON:-python}
$PY -u tools/oracle.py run --course 3 --type 0 --opp --render --slots --irec --rec --tag _er --max 12000
$PY -u tools/oracle.py run --course 3 --type 0 --opp --render --slots --replay extracted/oracle/lake_drive_er.irec --ending --rec --tag _ep --max 12000
