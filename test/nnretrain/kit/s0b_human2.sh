#!/bin/bash
set +e
while [ ! -f /tmp/nnret_s1.npz ] || [ ! -f /tmp/nnret_s2.npz ]; do sleep 30; done
sleep 10
WD=$(mktemp -d /tmp/hcg_s0b2.XXXXXX); echo "workdir $WD"
echo "=== s0b human rerun start $(date '+%H:%M:%S') ==="
for i in $(seq 0 7); do d="$WD/w$i"; mkdir -p "$d"
  ( cd "$d" && PATH=/tmp/shimbin:$PATH KATAGO_HUMAN=1 KATAGO_RANK=preaz_10k \
    /tmp/hbin_s0b 125 0 0 1000 1 2 $((1000+i*125)) >out.txt 2>err.txt ) &
done; wait; mv "$WD"/w*/game_*.sgf "$WD"/ 2>/dev/null
echo "games: $(ls $WD/game_*.sgf | wc -l)"
python3 /tmp/adj4090.py "$WD" /tmp/gpuh_s0b.json 20
python3 - <<'PY'
import json
base = json.load(open('/tmp/gpuh_puress512.json')); c = json.load(open('/tmp/gpuh_s0b.json'))
ks = sorted(set(base) & set(c), key=int)
bw = sum(base[k][0] for k in ks); cw = sum(c[k][0] for k in ks)
b = sum(1 for k in ks if not base[k][0] and c[k][0]); cc = sum(1 for k in ks if base[k][0] and not c[k][0])
chi = (abs(b-cc)-1)**2/(b+cc) if b+cc else 0
print(f"[s0b vs ship, HUMAN n={len(ks)}] {bw} -> {cw} ({100*(cw-bw)/max(len(ks),1):+.1f}pp)  "
      f"McNemar b={b} c={cc} chi2 {chi:.2f} {'SIG' if chi>3.84 else 'n.s.'}")
PY
echo "=== S0B HUMAN2 DONE $(date '+%H:%M:%S') ==="
