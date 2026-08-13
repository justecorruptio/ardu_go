#!/bin/bash
set +e
while ! grep -q "DAGGER TRAINED" /tmp/dagger1.log 2>/dev/null; do sleep 60; done
cd /Users/jay/workspace/ardu_go
for TAG in dg0 dg1; do
  python3 test/nnretrain/export_weights.py /tmp/nnret_$TAG.npz /tmp/nnret_$TAG.h $TAG >/dev/null
  cp nn_open_weights.h /tmp/nn_ship_bak.h
  cp /tmp/nnret_$TAG.h nn_open_weights.h
  bash /tmp/build_cand.sh /tmp/hbin_$TAG >/dev/null
  cp /tmp/nn_ship_bak.h nn_open_weights.h
done
# L0 arms first (local), then human arms (4090), then exit-quality reviews
for TAG in dg0 dg1; do
  WD=$(mktemp -d /tmp/l0x_$TAG.XXXXXX)
  echo "=== $TAG L0 start $(date '+%H:%M:%S') ==="
  for i in $(seq 0 7); do d="$WD/w$i"; mkdir -p "$d"
    ( cd "$d" && /tmp/hbin_$TAG 125 0 0 1000 1 2 $((i*125)) >out.txt 2>err.txt ) &
  done; wait
  python3 - "$WD" "$TAG" <<'PY'
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
print(f"[L0AXIS {sys.argv[2]} n={len(ks)}] ship {sum(b[k] for k in ks)} -> {sum(c[k] for k in ks)}")
PY
  rm -rf "$WD"
done
for TAG in dg0 dg1; do
  WD=$(mktemp -d /tmp/hcg_$TAG.XXXXXX)
  echo "=== $TAG human start $(date '+%H:%M:%S') ==="
  for i in $(seq 0 7); do d="$WD/w$i"; mkdir -p "$d"
    ( cd "$d" && PATH=/tmp/shimbin:$PATH KATAGO_HUMAN=1 KATAGO_RANK=preaz_10k \
      /tmp/hbin_$TAG 125 0 0 1000 1 2 $((1000+i*125)) >out.txt 2>err.txt ) &
  done; wait; mv "$WD"/w*/game_*.sgf "$WD"/ 2>/dev/null
  python3 /tmp/adj4090.py "$WD" /tmp/gpuh_$TAG.json 20
  python3 - "$TAG" <<'PY'
import json, sys
T = sys.argv[1]
base = json.load(open('/tmp/gpuh_puress512.json')); c = json.load(open(f'/tmp/gpuh_{T}.json'))
ks = sorted(set(base) & set(c), key=int)
bw = sum(base[k][0] for k in ks); cw = sum(c[k][0] for k in ks)
b = sum(1 for k in ks if not base[k][0] and c[k][0]); cc = sum(1 for k in ks if base[k][0] and not c[k][0])
chi = (abs(b-cc)-1)**2/(b+cc) if b+cc else 0
print(f"[{T} vs ship, HUMAN n={len(ks)}] {bw} -> {cw} ({100*(cw-bw)/max(len(ks),1):+.1f}pp)  chi2 {chi:.2f} {'SIG' if chi>3.84 else 'n.s.'}")
PY
  echo "$WD" > /tmp/dg_wd_$TAG.txt
done
# exit-quality reviews on the fresh human-arm games
cd /Users/jay/workspace/ardu_go/test/movequal
for TAG in dg0 dg1; do
  D=$(cat /tmp/dg_wd_$TAG.txt)
  ( cd $D && python3 /Users/jay/workspace/ardu_go/test/movequal/kata_review.py kata_ht.jsonl $(ls game_*.sgf | head -200) )
  python3 - "$TAG" "$D" <<'PY'
import json, collections, statistics, sys
tag, d = sys.argv[1], sys.argv[2]
rows = collections.defaultdict(dict)
for l in open(d + "/kata_ht.jsonl"):
    r = json.loads(l); rows[r['g']][r['t']] = r['wr']
at24 = []
for g, ts in rows.items():
    ai_black = (g % 2 == 0)
    t24 = 24 if 24 in ts else max((t for t in ts if t <= 24), default=None)
    if t24 is None: continue
    at24.append(ts[t24] if ai_black else 1 - ts[t24])
print(f"[EXIT {tag}] mean wr@handoff {100*statistics.mean(at24):.1f}%  median {100*statistics.median(at24):.1f}%  n={len(at24)}")
PY
done
echo "=== DG EVAL DONE $(date '+%H:%M:%S') ==="
