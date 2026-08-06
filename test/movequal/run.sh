#!/bin/bash
# Regenerate the "Move Quality by Phase" artifact end to end.
#   MODEL=/path/kata_b6.txt.gz ./run.sh
# Env:
#   WORK      working dir (default ./mqwork)
#   MODEL     KataGo net (b6c96) -- REQUIRED for stage 1
#   CFG       analysis cfg (default: kata_analysis.cfg beside this script)
#   NGAMES    game count (default 250)      SEED  seed offset (default 1000)
#   SKIP_DATA =1 to reuse an existing $WORK/kata.jsonl and skip stage 1
# Output: $WORK/blunder_hist.html  (publish with url=b3a6feee-… to update in place)
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
WORK="${WORK:-$PWD/mqwork}"
CFG="${CFG:-$DIR/kata_analysis.cfg}"
NGAMES="${NGAMES:-250}"; SEED="${SEED:-1000}"
mkdir -p "$WORK"; cd "$WORK"

# runtime copies of the scripts with hardcoded paths repointed at $WORK
sed -e "s#^MODEL = .*#MODEL = \"${MODEL:-UNSET}\"#" -e "s#^CFG = .*#CFG = \"$CFG\"#" \
    "$DIR/kata_review.py"  > "$WORK/_kata_review.py"
sed -e "s#^SP = .*#SP = '$WORK'#" "$DIR/kata_inject.py" > "$WORK/_kata_inject.py"
sed -e "s#^SP = .*#SP = '$WORK'#" "$DIR/traj_inject.py" > "$WORK/_traj_inject.py"
sed -e "s#^P = .*#P = '$WORK/blunder_hist.html'#" "$DIR/assemble.py" > "$WORK/_assemble.py"

# stage 1 -- corpus + KataGo review (slow; ~20 min for 250 games)
if [ "${SKIP_DATA:-0}" != 1 ]; then
  [ "${MODEL:-}" ] || { echo "set MODEL=/path/to/kata_b6.txt.gz (or SKIP_DATA=1)"; exit 1; }
  rm -f game_*.sgf opendiag.txt
  /tmp/hbin_diag opendiag "$NGAMES" "$SEED"
  python3 "$WORK/_kata_review.py" kata.jsonl game_*.sgf
fi

# stage 2 -- inject live sections, then structural assemble
cp "$DIR/base_template.html" "$WORK/blunder_hist.html"
python3 "$DIR/kata_hist.py"      kata.jsonl "$WORK/kata_hist.json"
python3 "$WORK/_kata_inject.py"  "$WORK/kata_hist.json" "$WORK/blunder_hist.html" final
python3 "$DIR/traj_recompute.py" kata.jsonl "$WORK/traj_pct.json"
python3 "$WORK/_traj_inject.py"
python3 "$WORK/_assemble.py"
echo "DONE -> $WORK/blunder_hist.html"
