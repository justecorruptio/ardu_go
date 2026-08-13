#!/bin/bash
set +e
cd /Users/jay/workspace/ardu_go
WD=/tmp/fc_r4s0; rm -rf $WD; mkdir -p $WD
echo "=== r4s0 fresh-confirm $(date '+%H:%M:%S') ==="
for i in $(seq 0 7); do d="$WD/w$i"; mkdir -p "$d"
  ( cd "$d" && PATH=/tmp/shimbin:$PATH KATAGO_HUMAN=1 KATAGO_RANK=preaz_10k \
    /tmp/hbin_r4s0 125 0 0 1000 1 2 $((2000+i*125)) >out.txt 2>err.txt ) &
done; wait; mv "$WD"/w*/game_*.sgf "$WD"/ 2>/dev/null
python3 /tmp/adj4090.py "$WD" /tmp/gpuh_fc_r4s0.json 20
python3 - <<'PY'
import json
a = json.load(open('/tmp/gpuh_fc_ship.json')); b = json.load(open('/tmp/gpuh_fc_r4s0.json'))
ks = sorted(set(a) & set(b), key=int)
aw = sum(a[k][0] for k in ks); bw = sum(b[k][0] for k in ks)
x = sum(1 for k in ks if not a[k][0] and b[k][0]); y = sum(1 for k in ks if a[k][0] and not b[k][0])
chi = (abs(x-y)-1)**2/(x+y) if x+y else 0
print(f"[r4s0 FRESH CONFIRM n={len(ks)}] ship {aw} vs r4s0 {bw} ({100*(bw-aw)/max(len(ks),1):+.1f}pp)  chi2 {chi:.2f} {'SIG' if chi>3.84 else 'n.s.'}")
PY
echo "=== R4 CONFIRM DONE $(date '+%H:%M:%S') ==="
