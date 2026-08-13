#!/bin/bash
# Self-driving DAGGER loop, rounds 4+ until convergence.
# Per round: extract prev round's own games -> dedup (novelty%) -> label new ->
# aggregate -> cold 300ep train (seed 0) -> full L0 + human arms.
# Stop: novelty < 25% OR both axes below running best for 2 consecutive rounds.
set +e
while ! grep -q "R3B DONE" /tmp/r3b.log 2>/dev/null; do sleep 120; done
cp /tmp/agg2seed_probe.in /tmp/agg3_probe.in
cat /tmp/r3_probe.in >> /tmp/agg3_probe.in
BEST_L0=365; BEST_HU=381; DECLINES=0
PREV_NPZ=/tmp/nnret_r3b.npz
for R in 4 5 6 7 8; do
  P=$((R-1))
  SRC=$(ls -dt /tmp/hcg_r${P}b.* /tmp/hcg_r${P}s0.* 2>/dev/null | head -1)
  [ -z "$SRC" ] && SRC=$(ls -dt /tmp/hcg_*.* | head -1)
  echo "=== ROUND $R from $SRC $(date '+%H:%M:%S') ==="
  cd /Users/jay/workspace/ardu_go/test/nnretrain
  python3 extract_positions.py /tmp/r$R "$SRC"
  python3 - $R <<'PY'
import sys, json
R = sys.argv[1]
def neigh(x, y):
    for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)):
        if 0 <= x+dx < 9 and 0 <= y+dy < 9: yield (x+dx, y+dy)
def sym(x, y, s):
    if s & 1: x = 8-x
    if s & 2: y = 8-y
    if s & 4: x, y = y, x
    return x, y
def key_of(toks):
    bd = {}; last = None
    for i, t in enumerate(toks):
        if t == 'PP': continue
        mv = (int(t[0]), int(t[1])); c = 'B' if i % 2 == 0 else 'W'
        bd[mv] = c
        for nb in list(neigh(*mv)):
            if nb in bd and bd[nb] != c:
                col = bd[nb]; cs = {nb}; st = [nb]; dead = True
                while st:
                    u = st.pop()
                    for nb2 in neigh(*u):
                        if nb2 not in bd: dead = False
                        elif bd[nb2] == col and nb2 not in cs: cs.add(nb2); st.append(nb2)
                if dead:
                    for q in cs: del bd[q]
        last = mv
    tom = 'B' if len(toks) % 2 == 0 else 'W'
    best = None
    for s8 in range(8):
        k = (tuple(sorted((sym(*q, s8), cc) for q, cc in bd.items())), sym(*last, s8))
        if best is None or k < best: best = k
    return (best, tom)
old = set()
for l in open(f'/tmp/agg{int(R)-1}_probe.in'):
    p = l.split(); old.add(key_of(p[2:]))
keep = set(); total = 0
lines = []
for l in open(f'/tmp/r{R}_probe.in'):
    p = l.split(); total += 1
    if key_of(p[2:]) not in old:
        keep.add(p[0]); lines.append(l)
print(f"NOVELTY R{R}: {len(keep)}/{total} = {100*len(keep)/max(total,1):.0f}%")
open(f'/tmp/r{R}_probe.in', 'w').writelines(lines)
rows = [json.loads(l) for l in open(f'/tmp/r{R}_meta.jsonl')]
with open(f'/tmp/r{R}_meta.jsonl', 'w') as f:
    for r in rows:
        if r['id'] in keep: f.write(json.dumps(r) + '\n')
PY
  NOV=$(grep "NOVELTY R$R" /tmp/dagger_loop.log | tail -1 | grep -o '[0-9]*%' | tr -d '%')
  NN_DUMP=1 FORCE_NN=1 /tmp/nndump < /tmp/r${R}_probe.in > /tmp/r${R}_dump.txt
  python3 label_costs.py /tmp/r$R
  python3 - $R <<'PY'
import sys, json, re
R = sys.argv[1]; pre = f'r{R}x'
for suf in ('_meta.jsonl', '_costs.jsonl'):
    rows = [json.loads(l) for l in open(f'/tmp/r{R}' + suf)]
    with open(f'/tmp/r{R}' + suf, 'w') as f:
        for r in rows:
            r['id'] = pre + r['id']
            f.write(json.dumps(r) + '\n')
dump = re.sub(r'(POS|ENDPOS) p', rf'\1 {pre}p', open(f'/tmp/r{R}_dump.txt').read())
open(f'/tmp/r{R}_dump.txt', 'w').write(dump)
P = int(R) - 1
for suf in ('_meta.jsonl', '_costs.jsonl'):
    with open(f'/tmp/agg{R}' + suf, 'w') as f:
        f.write(open(f'/tmp/agg{P}' + suf if suf != '_meta.jsonl' or P != 3 else '/tmp/agg3_meta.jsonl').read() if False else '')
# simpler: build from prefixes
PY
  # aggregates (bash-side concat; base for R=4 is agg3 built from mg2+r3 during r3b prep)
  if [ "$R" = "4" ]; then
    for suf in _meta.jsonl _costs.jsonl _dump.txt; do
      cat /tmp/agg3$suf > /tmp/agg4_pre$suf 2>/dev/null || cat /tmp/mg2$suf /tmp/r3$suf > /tmp/agg4_pre$suf
    done
  fi
  for suf in _meta.jsonl _costs.jsonl _dump.txt; do
    if [ -f /tmp/agg$((R-1))$suf ]; then cat /tmp/agg$((R-1))$suf /tmp/r$R$suf > /tmp/agg$R$suf
    else cat /tmp/agg4_pre$suf /tmp/r$R$suf > /tmp/agg$R$suf; fi
  done
  cat /tmp/agg$((R-1))_probe.in /tmp/r${R}_probe.in > /tmp/agg${R}_probe.in
  echo "=== R$R train $(date '+%H:%M:%S') ==="
  python3 nnret_train_cost.py /tmp/agg$R /tmp/nnret_r${R}s0.npz 0 300 8 128 0.20 > /tmp/nnret_r${R}s0.log 2>&1
  grep "epoch 300" /tmp/nnret_r${R}s0.log
  cp /tmp/nnret_r${R}s0.npz data/nets/
  cd /Users/jay/workspace/ardu_go
  python3 test/nnretrain/export_weights.py /tmp/nnret_r${R}s0.npz /tmp/nnret_r${R}s0.h r${R}s0 >/dev/null
  cp nn_open_weights.h /tmp/nn_ship_bak.h; cp /tmp/nnret_r${R}s0.h nn_open_weights.h
  bash /tmp/build_cand.sh /tmp/hbin_r${R}s0 >/dev/null
  cp /tmp/nn_ship_bak.h nn_open_weights.h
  WD=$(mktemp -d /tmp/l0x_r${R}s0.XXXXXX)
  echo "=== R$R L0 $(date '+%H:%M:%S') ==="
  for i in $(seq 0 7); do d="$WD/w$i"; mkdir -p "$d"
    ( cd "$d" && /tmp/hbin_r${R}s0 125 0 0 1000 1 2 $((i*125)) >out.txt 2>err.txt ) &
  done; wait
  L0=$(python3 - "$WD" <<'PY'
import sys, re, os
def load(root):
    d = {}
    for w in sorted(os.listdir(root)):
        p = f"{root}/{w}/out.txt"
        if not os.path.exists(p): continue
        for l in open(p):
            m = re.match(r"game (\d+):.*?(AI WIN|ai loss)", l)
            if m: d[int(m.group(1))] = 1 if m.group(2) == "AI WIN" else 0
    return d
b = load("/tmp/ponderl0.C2LWvl/off"); c = load(sys.argv[1])
ks = sorted(set(b) & set(c))
print(sum(c[k] for k in ks))
PY
)
  rm -rf "$WD"
  echo "[R${R}s0 L0 n=1000] $L0 (best $BEST_L0, ship 401)"
  WD=$(mktemp -d /tmp/hcg_r${R}s0.XXXXXX)
  echo "=== R$R human $(date '+%H:%M:%S') ==="
  for i in $(seq 0 7); do d="$WD/w$i"; mkdir -p "$d"
    ( cd "$d" && PATH=/tmp/shimbin:$PATH KATAGO_HUMAN=1 KATAGO_RANK=preaz_10k \
      /tmp/hbin_r${R}s0 125 0 0 1000 1 2 $((1000+i*125)) >out.txt 2>err.txt ) &
  done; wait; mv "$WD"/w*/game_*.sgf "$WD"/ 2>/dev/null
  python3 /tmp/adj4090.py "$WD" /tmp/gpuh_r${R}s0.json 20
  HU=$(python3 - <<PY
import json
c = json.load(open('/tmp/gpuh_r${R}s0.json'))
print(sum(v[0] for v in c.values()))
PY
)
  echo "[R${R}s0 HUMAN n=1000] $HU (best $BEST_HU, ship 317)"
  IMPROVED=0
  [ "$L0" -gt "$BEST_L0" ] && { BEST_L0=$L0; IMPROVED=1; }
  [ "$HU" -gt "$BEST_HU" ] && { BEST_HU=$HU; IMPROVED=1; }
  if [ "$IMPROVED" = "0" ]; then DECLINES=$((DECLINES+1)); else DECLINES=0; fi
  echo "ROUND $R SUMMARY: L0=$L0 HU=$HU best=($BEST_L0,$BEST_HU) declines=$DECLINES"
  if [ "$DECLINES" -ge 2 ]; then echo "=== CONVERGED (2 non-improving rounds) ==="; break; fi
done
echo "=== DAGGER LOOP DONE $(date '+%H:%M:%S') ==="
