#!/bin/bash
# The record box of the time attack's goal (retime_etc_display bit 2 -> record_sub 0x060118FC): a time attack run with a stored course record, so the goal's new best total sets the flag byte of R+0x70 and the box is drawn.
# python (Windows) and the disc files are needed; the traces go to extracted/oracle/.  From a shell that finds python:  bash tools/oracle_recbox.sh   (PYTHON=python.exe if needed)
#   _rb0 : total < [0x060AA184] (flag 0), _rb1 : < [0x060AA18C] (flag 0x80), _rb2 : < [0x060AA188] (flag 0x40); R+0x24 = a stored best total of 99:00.00 (always beaten)
# Check: tools/test_drive.sh desert_drive_rb0 -map -hud -hude     (every flag: the HUD name tables after every pass, including the box)
cd "$(dirname "$0")/.."
PY=${PYTHON:-python}
cat <<'LIST' | xargs -P 3 -I{} bash -c "echo == oracle.py run {}; $PY -I tools/oracle.py run {}"
--course 0 --scenario drive --type 4 --tag _rb0 --slots --rec --render --hudend --max 9000 --recs 990000,0,0,990000
--course 0 --scenario drive --type 4 --tag _rb1 --slots --rec --render --hudend --max 9000 --recs 0,0,990000,990000
--course 1 --scenario drive --type 4 --tag _rb2 --slots --rec --render --hudend --max 9000 --recs 0,990000,0,990000
LIST
