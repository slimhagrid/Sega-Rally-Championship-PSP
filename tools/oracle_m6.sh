#!/bin/bash
# M6: the oracle traces the driving core (src/drive.c) and the camera / part list (src/camera.c, src/pmap.c) are checked against, beyond the M4 set (tools/oracle_all.sh).
# python and the disc files are needed; the traces go to extracted/oracle/ (git-ignored).  Runs 4 at a time.
#   drive traces : arcade game type on every stage, the other cars, manual transmission, difficulty levels, tuned setups, mirror mode
#   --render ones: also the camera (view matrix, fly-by, goal orbit) and the visible part list per pass (needs the course model files)
cd "$(dirname "$0")/.."
mkdir -p build/host
cat <<'LIST' | xargs -P 4 -I{} bash -c 'echo "== oracle.py run {}"; python tools/oracle.py run {} --slots'
--course 0 --scenario drive --type 0 --tag _t0
--course 1 --scenario drive --type 0 --tag _t0
--course 2 --scenario drive --type 0 --tag _t0
--course 3 --scenario drive --type 0 --tag _t0
--course 0 --scenario drive --car 1 --tag _car1
--course 3 --scenario drive --car 2 --tag _car2
--course 1 --scenario drive --mt 1 --tag _mt
--course 2 --scenario drive --diff 1 --tag _d1
--course 0 --scenario drive --diff 2 --tag _d2
--course 2 --scenario drive --setup 3,2,1,2 --tag _setup
--course 1 --scenario drive --type 0 --car 2 --setup 1,3,2,0 --tag _t0setup
--course 0 --scenario slalom --mirror 1 --tag _mirror
--course 0 --scenario drive --render --tag _cam
--course 1 --scenario drive --render --tag _cam
--course 2 --scenario drive --render --tag _cam
--course 3 --scenario drive --render --tag _cam
--course 3 --scenario slalom --mirror 1 --render --tag _cammirror
--course 0 --scenario drive --view 2 --toggle 700,1500,2300,3100 --render --tag _camb
--course 1 --scenario drive --view 2 --toggle 900,2000 --render --tag _camb
--course 0 --scenario drive --rec --slots --tag _rec
--course 1 --scenario drive --rec --slots --tag _rec
--course 2 --scenario drive --rec --slots --tag _rec
--course 3 --scenario drive --rec --slots --tag _rec
--course 0 --scenario drive --type 0 --rec --slots --tag _t0rec
--course 1 --scenario drive --type 0 --rec --slots --tag _t0rec
--course 0 --scenario drive --render --sky --tag _sky
--course 1 --scenario drive --render --sky --tag _sky
--course 2 --scenario drive --render --sky --tag _sky
--course 3 --scenario drive --render --sky --tag _sky
--course 2 --scenario slalom --render --sky --tag _skyslalom
--course 3 --scenario slalom --mirror 1 --render --sky --tag _skymirror
--course 0 --scenario drive --render --hud --tag _hud
--course 1 --scenario drive --render --hud --tag _hud
--course 2 --scenario drive --render --hud --tag _hud
--course 3 --scenario drive --render --hud --tag _hud
--course 0 --scenario drive --type 0 --render --hud --tag _t0hud
--course 1 --scenario drive --type 0 --render --hud --tag _t0hud
--course 2 --scenario drive --type 0 --render --hud --tag _t0hud
--course 3 --scenario drive --type 0 --render --hud --tag _t0hud
--course 1 --scenario drive --mt 1 --car 1 --render --hud --tag _mthud
--course 3 --scenario drive --car 2 --render --hud --tag _car2hud
LIST
# spray / dust vectors (tools/spvec.py; test: tools/test_spray.sh): course, game type, passes, car
cat <<'LIST' | xargs -P 4 -L1 bash -c 'echo "== spvec $@"; python tools/spvec.py "$@" > /dev/null' _
0 0 3000
1 0 3000
2 0 3000
3 0 3000
0 2 3000 --car=1
3 0 3000 --car=2
1 2 3000 --car=2
2 2 3000 --car=1
LIST
echo finished
