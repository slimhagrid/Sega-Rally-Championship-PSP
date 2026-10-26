#!/bin/bash
# M9: the oracle traces with the sound-library calls (tools/oracle.py run --snd -> <name>.snd) that src/sndev.c is checked against:  bash tools/oracle_snd.sh   then
#   wsl -d Ubuntu -- bash tools/test_snd_ev.sh        (tools/test_drive.c -snd over every *_snd*.snd trace)
# Every trace also has the car slots (--slots), so the sound bytes of the car (+0xC6, +0x364 ..) are compared like all the others.  Runs 6 at a time.
cd "$(dirname "$0")/.."
cat <<'LIST' | xargs -P 6 -I{} bash -c 'echo "== oracle.py run {}"; python tools/oracle.py run {} --slots --snd | tail -1'
--course 0 --scenario drive --max 14000 --tag _snd
--course 1 --scenario drive --max 14000 --tag _snd
--course 2 --scenario drive --max 14000 --tag _snd
--course 3 --scenario drive --max 14000 --tag _snd
--course 0 --scenario drive --type 0 --tag _sndt0
--course 1 --scenario drive --type 0 --tag _sndt0
--course 2 --scenario drive --type 0 --tag _sndt0
--course 3 --scenario drive --type 0 --tag _sndt0
--course 0 --scenario drive --car 1 --tag _sndcar1
--course 3 --scenario drive --car 2 --tag _sndcar2
--course 1 --scenario drive --mt 1 --tag _sndmt
--course 0 --scenario slalom --tag _sndslalom
--course 0 --scenario coast --tag _sndcoast
--course 0 --scenario brake --tag _sndbrake
--course 2 --scenario drive --diff 1 --tag _sndd1
LIST
echo done
