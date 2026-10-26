#!/bin/bash
# The end-of-race screens (docs/hud.md 7): oracle traces that keep stepping 420 passes after the goal / game over and log the NBG0 name tables, scroll, BGON and the state machine's state per pass
# (<name>.hude, checked by tools/test_drive.sh <name> -hude; tools/test_drive_all.sh does it for every trace that has the file).  Runs 4 at a time; python and the disc files are needed.
#   goal: practice (game type 2) on every stage, arcade on the first stage (the pilot is too slow for the later arcade checkpoints); game over (the clock runs out): arcade on the other stages,
#   no throttle at all in arcade / practice
cd "$(dirname "$0")/.."
cat <<'LIST' | xargs -P 4 -I{} bash -c 'echo "== oracle.py run {}"; python tools/oracle.py run {}'
--course 0 --scenario drive --type 2 --hudend --slots --render --tag _end
--course 1 --scenario drive --type 2 --hudend --slots --render --tag _end
--course 2 --scenario drive --type 2 --hudend --slots --render --tag _end
--course 3 --scenario drive --type 2 --hudend --slots --render --tag _end
--course 0 --scenario drive --type 0 --hudend --slots --render --tag _t0end
--course 1 --scenario drive --type 0 --hudend --slots --render --tag _t0end
--course 2 --scenario drive --type 0 --hudend --slots --render --tag _t0end
--course 3 --scenario drive --type 0 --hudend --slots --render --tag _t0end
--course 0 --scenario idle --type 0 --hudend --slots --render --tag _t0idle
--course 1 --scenario idle --type 0 --hudend --slots --render --tag _t0idle
--course 2 --scenario idle --type 0 --hudend --slots --render --tag _t0idle
--course 3 --scenario idle --type 0 --hudend --slots --render --tag _t0idle
--course 2 --scenario idle --type 2 --hudend --slots --render --tag _idle --max 12000
--course 1 --scenario drive --type 2 --car 1 --mt 1 --hudend --slots --render --tag _mtend
--course 3 --scenario drive --type 2 --car 2 --hudend --slots --render --tag _car2end
LIST
echo finished
