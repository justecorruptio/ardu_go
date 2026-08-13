#!/bin/bash
# Fresh-games confirmation: s0b vs ship on seeds 2000-2999 (never used in selection)
set +e
for ARM in ship s0b; do
  BIN=/tmp/hbin_newship; [ "$ARM" = "s0b" ] && BIN=/tmp/hbin_s0b
  WD=/tmp/fc_$ARM; rm -rf $WD; mkdir -p $WD
  echo "=== confirm $ARM start $(date '+%H:%M:%S') ==="
  for i in $(seq 0 7); do d="$WD/w$i"; mkdir -p "$d"
    ( cd "$d" && PATH=/tmp/shimbin:$PATH KATAGO_HUMAN=1 KATAGO_RANK=preaz_10k \
      $BIN 125 0 0 1000 1 2 $((2000+i*125)) >out.txt 2>err.txt ) &
  done; wait; mv "$WD"/w*/game_*.sgf "$WD"/ 2>/dev/null
  python3 /tmp/adj4090.py "$WD" /tmp/gpuh_fc_$ARM.json 20
done
python3 - <<'PY'
import json
a = json.load(open('/tmp/gpuh_fc_ship.json')); b = json.load(open('/tmp/gpuh_fc_s0b.json'))
ks = sorted(set(a) & set(b), key=int)
aw = sum(a[k][0] for k in ks); bw = sum(b[k][0] for k in ks)
x = sum(1 for k in ks if not a[k][0] and b[k][0]); y = sum(1 for k in ks if a[k][0] and not b[k][0])
chi = (abs(x-y)-1)**2/(x+y) if x+y else 0
print(f"[FRESH CONFIRM n={len(ks)}] ship {aw} vs s0b {bw} ({100*(bw-aw)/max(len(ks),1):+.1f}pp)  chi2 {chi:.2f} {'SIG' if chi>3.84 else 'n.s.'}")
PY
echo "=== CONFIRM DONE $(date '+%H:%M:%S') ==="
