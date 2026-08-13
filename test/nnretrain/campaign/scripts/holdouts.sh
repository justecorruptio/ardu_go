#!/bin/bash
set +e
while ! grep -q "ROUND 5 SUMMARY" /tmp/dagger_loop.log 2>/dev/null; do sleep 180; done
cd /Users/jay/workspace/ardu_go
for RANK in preaz_15k preaz_5k; do
  WD=$(mktemp -d /tmp/ho_${RANK}.XXXXXX)
  echo "=== r4s0 holdout $RANK $(date '+%H:%M:%S') ==="
  for i in $(seq 0 7); do d="$WD/w$i"; mkdir -p "$d"
    ( cd "$d" && PATH=/tmp/shimbin:$PATH KATAGO_HUMAN=1 KATAGO_RANK=$RANK \
      /tmp/hbin_r4s0 32 0 0 1000 1 2 $((5000+i*32)) >out.txt 2>err.txt ) &
  done; wait; mv "$WD"/w*/game_*.sgf "$WD"/ 2>/dev/null
  python3 /tmp/adj4090.py "$WD" /tmp/gpuh_ho_${RANK}_r4s0.json 20
  python3 - "$RANK" <<'PY'
import json, sys
c = json.load(open(f'/tmp/gpuh_ho_{sys.argv[1]}_r4s0.json'))
print(f"[r4s0 holdout {sys.argv[1]} n={len(c)}] wins {sum(v[0] for v in c.values())}")
PY
done
echo "ship references: 15k=135/256, 5k=28/256 (same seeds/rank, ship engine)"
echo "=== HOLDOUTS DONE $(date '+%H:%M:%S') ==="
