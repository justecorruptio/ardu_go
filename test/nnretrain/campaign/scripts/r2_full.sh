#!/bin/bash
set +e
cd /Users/jay/workspace/ardu_go
WD=/tmp/fc_r2s0; rm -rf $WD; mkdir -p $WD
echo "=== r2s0 human fresh-confirm $(date '+%H:%M:%S') ==="
for i in $(seq 0 7); do d="$WD/w$i"; mkdir -p "$d"
  ( cd "$d" && PATH=/tmp/shimbin:$PATH KATAGO_HUMAN=1 KATAGO_RANK=preaz_10k \
    /tmp/hbin_dg2s0 125 0 0 1000 1 2 $((2000+i*125)) >out.txt 2>err.txt ) &
done; wait; mv "$WD"/w*/game_*.sgf "$WD"/ 2>/dev/null
python3 /tmp/adj4090.py "$WD" /tmp/gpuh_fc_r2s0.json 20
python3 - <<'PY'
import json
a = json.load(open('/tmp/gpuh_fc_ship.json')); b = json.load(open('/tmp/gpuh_fc_r2s0.json'))
ks = sorted(set(a) & set(b), key=int)
aw = sum(a[k][0] for k in ks); bw = sum(b[k][0] for k in ks)
x = sum(1 for k in ks if not a[k][0] and b[k][0]); y = sum(1 for k in ks if a[k][0] and not b[k][0])
chi = (abs(x-y)-1)**2/(x+y) if x+y else 0
print(f"[r2s0 FRESH CONFIRM n={len(ks)}] ship {aw} vs r2s0 {bw} ({100*(bw-aw)/max(len(ks),1):+.1f}pp)  chi2 {chi:.2f} {'SIG' if chi>3.84 else 'n.s.'}")
PY
for RANK in preaz_15k preaz_5k; do
  WD=$(mktemp -d /tmp/ho2_${RANK}.XXXXXX)
  echo "=== r2s0 holdout $RANK $(date '+%H:%M:%S') ==="
  for i in $(seq 0 7); do d="$WD/w$i"; mkdir -p "$d"
    ( cd "$d" && PATH=/tmp/shimbin:$PATH KATAGO_HUMAN=1 KATAGO_RANK=$RANK \
      /tmp/hbin_dg2s0 32 0 0 1000 1 2 $((5000+i*32)) >out.txt 2>err.txt ) &
  done; wait; mv "$WD"/w*/game_*.sgf "$WD"/ 2>/dev/null
  python3 /tmp/adj4090.py "$WD" /tmp/gpuh_ho_${RANK}_r2s0.json 20
  python3 - "$RANK" <<'PY'
import json, sys
c = json.load(open(f'/tmp/gpuh_ho_{sys.argv[1]}_r2s0.json'))
print(f"[r2s0 holdout {sys.argv[1]} n={len(c)}] wins {sum(v[0] for v in c.values())}")
PY
done
echo "refs: human ship 291 (r4s0 358); 15k ship 135, r4s0 157; 5k ship 28, r4s0 25"
echo "=== R2 FULL DONE $(date '+%H:%M:%S') ==="
