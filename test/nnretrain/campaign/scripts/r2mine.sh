#!/bin/bash
# Option-3: mine the r2 family for a both-axes champion.
# Candidates: r2s0 PJ snaps 101/202/303/404, r2s1 (=dg2s1, trained), new seeds 2,3.
# Screen: 3-set paired L0 (ship pooled 1240/3000 = 41.3%); gate >= 1200 (40.0%)
# -> human n=1000 fresh-confirm (gate: beat ship 291 clearly, target >= 340).
set +e
cd /Users/jay/workspace/ardu_go/test/nnretrain
l0run() { mkdir -p "$2"
  for i in $(seq 0 7); do d="$2/w$i"; mkdir -p "$d"
    ( cd "$d" && "$1" 125 0 0 1000 1 2 $(($3 + i*125)) >out.txt 2>err.txt ) &
  done; wait
  grep -ho "AI WIN" "$2"/w*/out.txt | wc -l | tr -d ' '
}
l0screen() { # $1=name $2=binary -> echoes pooled count
  T=0
  for SB in 9000 11000 13000; do
    N=$(l0run "$2" /tmp/mine_l0_$1_$SB $SB); T=$((T+N))
  done
  echo "[MINE L0 $1] pooled $T/3000 (ship 1240)"
}
# --- snaps of r2s0 ---
for PJ in 101 202 303 404; do
  python3 export_weights.py /tmp/nnret_dg2s0.npz /tmp/nnret_r2p$PJ.h r2p$PJ $PJ >/dev/null
  cp /Users/jay/workspace/ardu_go/nn_open_weights.h /tmp/nn_cur_bak.h
  cp /tmp/nnret_r2p$PJ.h /Users/jay/workspace/ardu_go/nn_open_weights.h
  bash kit/build_cand.sh /tmp/hbin_r2p$PJ >/dev/null 2>&1
  cp /tmp/nn_cur_bak.h /Users/jay/workspace/ardu_go/nn_open_weights.h
done
echo "=== snaps built $(date '+%H:%M:%S') ==="
for PJ in 101 202 303 404; do l0screen r2p$PJ /tmp/hbin_r2p$PJ; done
l0screen r2s1 /tmp/hbin_dg2s1
# --- new seeds 2,3 (serial trainers) ---
for S in 2 3; do
  echo "=== train r2s$S $(date '+%H:%M:%S') ==="
  python3 nnret_train_cost.py /tmp/agg2seed /tmp/nnret_r2s$S.npz $S 300 8 128 0.20 > /tmp/nnret_r2s$S.log 2>&1
  grep "epoch 300" /tmp/nnret_r2s$S.log
  cp /tmp/nnret_r2s$S.npz data/nets/ 2>/dev/null
  python3 export_weights.py /tmp/nnret_r2s$S.npz /tmp/nnret_r2s$S.h r2s$S >/dev/null
  cp /Users/jay/workspace/ardu_go/nn_open_weights.h /tmp/nn_cur_bak.h
  cp /tmp/nnret_r2s$S.h /Users/jay/workspace/ardu_go/nn_open_weights.h
  bash kit/build_cand.sh /tmp/hbin_r2s$S >/dev/null 2>&1
  cp /tmp/nn_cur_bak.h /Users/jay/workspace/ardu_go/nn_open_weights.h
  l0screen r2s$S /tmp/hbin_r2s$S
done
echo "=== L0 SCREENS DONE $(date '+%H:%M:%S') — human confirms for gate-passers ==="
# --- human confirms, serial, for candidates >= 1200 pooled ---
for NAME in r2p101 r2p202 r2p303 r2p404 r2s1 r2s2 r2s3; do
  P=$(cat /tmp/mine_l0_${NAME}_*/w*/out.txt 2>/dev/null | grep -c "AI WIN")
  [ "$P" -lt 1200 ] && { echo "[SKIP HUMAN $NAME] L0 pooled $P < 1200"; continue; }
  BIN=/tmp/hbin_$NAME; [ "$NAME" = "r2s1" ] && BIN=/tmp/hbin_dg2s1
  WD=/tmp/fc_$NAME; rm -rf $WD; mkdir -p $WD
  echo "=== human $NAME $(date '+%H:%M:%S') ==="
  for i in $(seq 0 7); do d="$WD/w$i"; mkdir -p "$d"
    ( cd "$d" && PATH=/tmp/shimbin:$PATH KATAGO_HUMAN=1 KATAGO_RANK=preaz_10k \
      "$BIN" 125 0 0 1000 1 2 $((2000+i*125)) >out.txt 2>err.txt ) &
  done; wait; mv "$WD"/w*/game_*.sgf "$WD"/ 2>/dev/null
  python3 /tmp/adj4090.py "$WD" /tmp/gpuh_fc_$NAME.json 20
  python3 - "$NAME" <<'PY'
import json, sys
c = json.load(open(f'/tmp/gpuh_fc_{sys.argv[1]}.json'))
print(f"[MINE HUMAN {sys.argv[1]} n={len(c)}] wins {sum(v[0] for v in c.values())} (ship 291, r2s0 345)")
PY
done
echo "=== R2 MINE DONE $(date '+%H:%M:%S') ==="
