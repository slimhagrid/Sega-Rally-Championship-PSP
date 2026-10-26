#!/bin/bash
# M4: regenerate every oracle trace (about 15 minutes of Python).  Run from the project root:  bash tools/oracle_all.sh
set -e
cd "$(dirname "$0")/.."
for s in accel brake slalom coast; do
    python -I tools/oracle.py run --course 0 --scenario $s --slots | tail -1
done
for c in 0 1 2 3; do
    python -I tools/oracle.py run --course $c --scenario drive --max 14000 --slots | tail -1
done
python -I tools/oracle_fn.py | tail -6
echo all oracle traces done
