#!/bin/bash
# L0 gates for seeds 1-2 (local only; ssh is down), sequential to be gentle
set +e
for S in s1 s2; do
  WD=$(mktemp -d /tmp/l0_$S.XXXXXX); echo "workdir $WD ($S)"
  for i in $(seq 0 7); do d="$WD/w$i"; mkdir -p "$d"
    ( cd "$d" && /tmp/hbin_$S 125 0 0 1000 1 2 $((i*125)) >out.txt 2>err.txt ) &
  done; wait
  python3 - "$WD" "$S" <<'PY'
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
print(f"[{sys.argv[2]} vs ship, L0 n={n}] {bw} -> {cw} ({100*(cw-bw)/max(n,1):+.1f}pp)")
PY
done
echo "=== L0 S1S2 DONE $(date '+%H:%M:%S') ==="
