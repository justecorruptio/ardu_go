#!/bin/bash
# Optimized DAGGER round: dedup -> incremental labels -> warm-start 1-seed -> cheap eval
# Usage: dagger_round.sh <round#> <src_games_dir> <prev_prefix> <prev_npz>
set -e
R=$1; SRC=$2; PREV=$3; PREVNPZ=$4
cd /Users/jay/workspace/ardu_go/test/nnretrain
echo "=== R$R extract $(date '+%H:%M:%S') ==="
python3 extract_positions.py /tmp/r$R $SRC
# cross-round dedup: drop positions whose canonical key already exists in the aggregate
python3 - $R $PREV <<'PY'
import sys, json
R, PREV = sys.argv[1], sys.argv[2]
def keys_from(prefix):
    # replay toks to canonical keys (same logic as extractor)
    def neigh(x, y):
        for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)):
            if 0 <= x+dx < 9 and 0 <= y+dy < 9: yield (x+dx, y+dy)
    def sym(x, y, s):
        if s & 1: x = 8-x
        if s & 2: y = 8-y
        if s & 4: x, y = y, x
        return x, y
    ks = {}
    for line in open(prefix + '_probe.in'):
        p = line.split(); pid = p[0]; toks = p[2:]
        bd = {}; last = None
        for t in toks:
            if t == 'PP': continue
            mv = (int(t[0]), int(t[1])); c = 'B' if (len([x for x in toks[:toks.index(t)+1] if True]) % 2) else 'W'
        # simpler: replay with index parity
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
        ks[pid] = (best, tom)
    return ks
old = set(keys_from(PREV).values())
new = keys_from(f'/tmp/r{R}')
keep = [pid for pid, k in new.items() if k not in old]
print(f"R{R}: {len(new)} extracted, {len(keep)} genuinely new ({100*len(keep)/len(new):.0f}% novel) -- CONVERGENCE METRIC")
keepset = set(keep)
for suf in ('_probe.in',):
    lines = [l for l in open(f'/tmp/r{R}{suf}') if l.split()[0] in keepset]
    open(f'/tmp/r{R}{suf}', 'w').writelines(lines)
rows = [json.loads(l) for l in open(f'/tmp/r{R}_meta.jsonl')]
with open(f'/tmp/r{R}_meta.jsonl', 'w') as f:
    for r in rows:
        if r['id'] in keepset: f.write(json.dumps(r) + '\n')
PY
NN_DUMP=1 FORCE_NN=1 /tmp/nndump < /tmp/r${R}_probe.in > /tmp/r${R}_dump.txt
echo "=== R$R labels $(date '+%H:%M:%S') ==="
python3 label_costs.py /tmp/r$R
echo "=== R$R merge+train $(date '+%H:%M:%S') ==="
python3 - $R $PREV <<'PY'
import sys, json, re
R, PREV = sys.argv[1], sys.argv[2]
pre = f'r{R}x'
for suf in ('_meta.jsonl', '_costs.jsonl'):
    rows = [json.loads(l) for l in open(f'/tmp/r{R}' + suf)]
    with open(f'/tmp/r{R}' + suf, 'w') as f:
        for r in rows:
            r['id'] = pre + r['id']
            f.write(json.dumps(r) + '\n')
dump = re.sub(r'(POS|ENDPOS) p', rf'\1 {pre}p', open(f'/tmp/r{R}_dump.txt').read())
open(f'/tmp/r{R}_dump.txt', 'w').write(dump)
for suf in ('_meta.jsonl', '_costs.jsonl'):
    with open(f'/tmp/agg{R}' + suf, 'w') as f:
        f.write(open(PREV + suf).read()); f.write(open(f'/tmp/r{R}' + suf).read())
open(f'/tmp/agg{R}_dump.txt','w').write(open(PREV + '_dump.txt').read() + open(f'/tmp/r{R}_dump.txt').read())
print("aggregate written")
PY
WARMSTART=$PREVNPZ python3 nnret_train_cost.py /tmp/agg$R /tmp/nnret_r$R.npz 0 100 8 128 0.10 > /tmp/nnret_r$R.log 2>&1
grep "epoch 100" /tmp/nnret_r$R.log
cp /tmp/nnret_r$R.npz data/nets/
echo "=== R$R cheap eval $(date '+%H:%M:%S') ==="
cd /Users/jay/workspace/ardu_go
python3 test/nnretrain/export_weights.py /tmp/nnret_r$R.npz /tmp/nnret_r$R.h r$R >/dev/null
cp nn_open_weights.h /tmp/nn_ship_bak.h; cp /tmp/nnret_r$R.h nn_open_weights.h
bash /tmp/build_cand.sh /tmp/hbin_r$R >/dev/null
cp /tmp/nn_ship_bak.h nn_open_weights.h
WD=$(mktemp -d /tmp/l0s_r$R.XXXXXX)
for i in 0 1 2 3; do d="$WD/w$i"; mkdir -p "$d"
  ( cd "$d" && /tmp/hbin_r$R 125 0 0 1000 1 2 $((i*125)) >out.txt 2>err.txt ) &
done; wait
python3 - "$WD" "$R" <<'PY'
import sys, re, os
def load(root, ws):
    d = {}
    for w in ws:
        p = f"{root}/{w}/out.txt"
        if not os.path.exists(p): continue
        for l in open(p):
            m = re.match(r"game (\d+):.*?(AI WIN|ai loss)", l)
            if m: d[int(m.group(1))] = 1 if m.group(2) == "AI WIN" else 0
    return d
ws = [f"w{i}" for i in range(4)]
b = load("/tmp/ponderl0.C2LWvl/off", ws); c = load(sys.argv[1], ws)
ks = sorted(set(b) & set(c))
print(f"[R{sys.argv[2]} L0SCREEN n={len(ks)}] ship {sum(b[k] for k in ks)} -> {sum(c[k] for k in ks)}")
PY
rm -rf "$WD"
echo "=== R$R DONE $(date '+%H:%M:%S') ==="
