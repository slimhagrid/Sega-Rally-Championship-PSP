#!/bin/bash
# M7 oracle traces of the other cars: full races on every stage, arcade (_opp) and practice (_oppp), fast and slow pilots (slow = the player stays among the other cars: contacts, every LOD)
cd "$(dirname "$0")/.."
run() { python -u tools/oracle.py run --opp --render --slots "$@" || exit 1; }
for c in 0 1 2 3; do
  run --course $c --type 0 --tag _opp --max 12000
done
run --course 3 --type 2 --tag _oppp --max 12000
run --course 0 --type 0 --tag _opp_slow --vmax 1.1 --max 12000
run --course 1 --type 0 --tag _opp_slow --vmax 1.3 --max 12000
run --course 2 --type 0 --tag _opp_slow --vmax 1.3 --max 12000
run --course 3 --type 0 --tag _opp_slow --vmax 1.0 --max 12000
# the player parked on the grid: the other cars lap the stage and pass each other near it (car-car contacts)
for c in 0 1 2 3; do
  run --course $c --type 0 --scenario idle --tag _oppidle --max 12000
done
# synthetic car-car contacts: two other cars placed side by side near the parked player (--perturb passes)
run --course 0 --type 0 --scenario idle --tag _oppcc --perturb 200,300,400,600,800,1000,1200,1400,1600 --max 12000
