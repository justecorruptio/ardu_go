#!/bin/bash
# Regenerate the "ArduGo Search — On-Device Profile" flamegraph end to end
# from the current working tree.
#   ./run.sh              # both tabs (~5 min: 2 device builds + avrprof sims)
#   SKIP_DATA=1 ./run.sh  # reuse work/*.folded etc.; just re-assemble the HTML
# Output: work/profile_artifact.html  (publish with url=0ad79891-… to update in place)
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
export SCR="${SCR:-$DIR/work}"; mkdir -p "$SCR"
cp "$DIR"/frag_*.html "$SCR"/            # static text fragments (changelog etc.)
if [ "${SKIP_DATA:-0}" != 1 ]; then
  echo "== profiling midgame (400 iters) =="
  TAG=mid  bash "$DIR/gen_profile.sh"; TAG=mid  bash "$DIR/gen_inside.sh"
  # opening tab removed (NN handles the quiet opening; MCTS-opening is a rare handoff)
fi
echo "== assembling profile_artifact.html =="
python3 "$DIR/gen_flame.py"
echo "DONE -> $SCR/profile_artifact.html"
