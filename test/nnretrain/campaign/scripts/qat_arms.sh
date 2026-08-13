#!/bin/bash
set +e
while ! grep -q "R2 MINE DONE" /tmp/r2mine.log 2>/dev/null; do sleep 180; done
cd /Users/jay/workspace/ardu_go/test/nnretrain
l0run() { mkdir -p "$2"
  for i in $(seq 0 7); do d="$2/w$i"; mkdir -p "$d"
    ( cd "$d" && "$1" 125 0 0 1000 1 2 $(($3 + i*125)) >out.txt 2>err.txt ) &
  done; wait
  grep -ho "AI WIN" "$2"/w*/out.txt | wc -l | tr -d ' '
}
for S in 0 1; do
  echo "=== QAT train s$S $(date '+%H:%M:%S') ==="
  python3 nnret_train_qat.py /tmp/agg2seed /tmp/nnret_qat_s$S.npz $S 300 8 128 0.20 > /tmp/nnret_qat_s$S.log 2>&1
  grep "epoch 300" /tmp/nnret_qat_s$S.log
  cp /tmp/nnret_qat_s$S.npz data/nets/ 2>/dev/null
  python3 export_weights.py /tmp/nnret_qat_s$S.npz /tmp/nnret_qat_s$S.h qat_s$S >/dev/null
  cp /Users/jay/workspace/ardu_go/nn_open_weights.h /tmp/nn_cur_bak.h
  cp /tmp/nnret_qat_s$S.h /Users/jay/workspace/ardu_go/nn_open_weights.h
  bash kit/build_cand.sh /tmp/hbin_qat_s$S >/dev/null 2>&1
  cp /tmp/nn_cur_bak.h /Users/jay/workspace/ardu_go/nn_open_weights.h
  T=0
  for SB in 9000 11000 13000; do
    N=$(l0run /tmp/hbin_qat_s$S /tmp/qat_l0_s${S}_$SB $SB); T=$((T+N))
  done
  echo "[QAT L0 s$S] pooled $T/3000 (ship 1240)"
  if [ "$T" -ge 1200 ]; then
    WD=/tmp/fc_qat_s$S; rm -rf $WD; mkdir -p $WD
    echo "=== QAT human s$S $(date '+%H:%M:%S') ==="
    for i in $(seq 0 7); do d="$WD/w$i"; mkdir -p "$d"
      ( cd "$d" && PATH=/tmp/shimbin:$PATH KATAGO_HUMAN=1 KATAGO_RANK=preaz_10k \
        /tmp/hbin_qat_s$S 125 0 0 1000 1 2 $((2000+i*125)) >out.txt 2>err.txt ) &
    done; wait; mv "$WD"/w*/game_*.sgf "$WD"/ 2>/dev/null
    python3 /tmp/adj4090.py "$WD" /tmp/gpuh_fc_qat_s$S.json 20
    python3 - "$S" <<'PY'
import json, sys
c = json.load(open(f'/tmp/gpuh_fc_qat_s{sys.argv[1]}.json'))
print(f"[QAT HUMAN s{sys.argv[1]} n={len(c)}] wins {sum(v[0] for v in c.values())} (ship 291, r2s0 345)")
PY
  else
    echo "[QAT s$S SKIP HUMAN] L0 $T < 1200"
  fi
done
echo "=== QAT ARMS DONE $(date '+%H:%M:%S') ==="
