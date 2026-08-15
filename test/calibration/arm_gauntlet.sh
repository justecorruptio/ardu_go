#!/bin/bash
# Canonical gauntlet arm: L0 3x1000 (seeds 9000/11000/13000, 8 workers)
# + optional human leg (KataGo human-SL 10k, n=1000, 4090 shim + adj).
#   arm_gauntlet.sh <binary> <tag> [human]
# Requires: gnugo on PATH; for the human leg: /tmp/shimbin + /tmp/pw4090.sh
# (recreate from test/nnretrain/kit/shim_katago_4090.sh + the password
# helper by hand) and kit/adj4090.py. Results append to /tmp/<tag>.log.
set +e
BIN=$1; TAG=$2; HUMAN=$3
LOG=/tmp/$TAG.log
cd "$(dirname "$0")/../.."
T=0
for SB in 9000 11000 13000; do
  WD=/tmp/${TAG}_l0_$SB; rm -rf $WD; mkdir -p $WD
  for i in $(seq 0 7); do d="$WD/w$i"; mkdir -p "$d"
    ( cd "$d" && $BIN 125 0 0 1000 1 2 $((SB + i*125)) >out.txt 2>err.txt ) &
  done; wait
  N=$(grep -ho "AI WIN" $WD/w*/out.txt | wc -l | tr -d ' '); T=$((T+N))
  echo "[$TAG L0 seeds$SB] $N" >> $LOG
done
echo "[$TAG L0 pooled] $T/3000" >> $LOG
if [ "$HUMAN" = "human" ]; then
  WD=/tmp/${TAG}_hu; rm -rf $WD; mkdir -p $WD
  for i in $(seq 0 7); do d="$WD/w$i"; mkdir -p "$d"
    ( cd "$d" && PATH=/tmp/shimbin:$PATH KATAGO_HUMAN=1 KATAGO_RANK=preaz_10k \
      $BIN 125 0 0 1000 1 2 $((4000+i*125)) >out.txt 2>err.txt ) &
  done; wait; mv $WD/w*/game_*.sgf $WD/ 2>/dev/null
  python3 test/nnretrain/kit/adj4090.py $WD /tmp/${TAG}_hu.json 20
  python3 -c "
import json
c = json.load(open('/tmp/${TAG}_hu.json'))
print(f'[$TAG HUMAN n={len(c)}] wins {sum(v[0] for v in c.values())}')" >> $LOG
fi
echo "=== $TAG DONE $(date '+%H:%M:%S') ===" >> $LOG
