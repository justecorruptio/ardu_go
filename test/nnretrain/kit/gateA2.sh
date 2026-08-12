#!/bin/bash
set -e
while [ ! -f /tmp/nnret_s0b.npz ]; do sleep 20; done
sleep 5
cd /Users/jay/workspace/ardu_go/test/nnretrain
python3 export_weights.py /tmp/nnret_s0b.npz /tmp/nnret_s0b.h seed0b
cd /Users/jay/workspace/ardu_go
cp nn_open_weights.h /tmp/nn_open_weights_ship_backup.h
cp /tmp/nnret_s0b.h nn_open_weights.h
bash test/nnretrain/kit/build_cand.sh /tmp/hbin_s0b
cp /tmp/nn_open_weights_ship_backup.h nn_open_weights.h
set +e
WD=$(mktemp -d /tmp/gateA2.XXXXXX); echo "workdir $WD"
for i in $(seq 0 7); do d="$WD/w$i"; mkdir -p "$d"
  ( cd "$d" && /tmp/hbin_s0b 125 0 0 1000 1 2 $((i*125)) >out.txt 2>err.txt ) &
done; wait
python3 - "$WD" <<'PY'
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
ks = sorted(set(b) & set(c)); n = len(ks)
bw = sum(b[k] for k in ks); cw = sum(c[k] for k in ks)
x = sum(1 for k in ks if not b[k] and c[k]); y = sum(1 for k in ks if b[k] and not c[k])
chi = (abs(x-y)-1)**2/(x+y) if x+y else 0
print(f"[GATE A2: seed0b vs ship, L0 n={n}] {bw} -> {cw} ({100*(cw-bw)/max(n,1):+.1f}pp)  chi2 {chi:.2f} {'SIG' if chi>3.84 else 'n.s.'}")
PY
echo "=== GATE A2 DONE $(date '+%H:%M:%S') ==="
