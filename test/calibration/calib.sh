#!/bin/bash
# Difficulty-ladder calibration point runner.
#   calib.sh <rank> <handi> <ngames> <tag>
set +e
RANK=$1; HANDI=$2; N=$3; TAG=$4
PER=$((N / 4))
WD=/tmp/cal_$TAG; rm -rf $WD; mkdir -p $WD
cd /Users/jay/workspace/ardu_go
for i in 0 1 2 3; do d="$WD/w$i"; mkdir -p "$d"
  ( cd "$d" && PATH=/tmp/shimbin:$PATH KATAGO_HUMAN=1 KATAGO_RANK=$RANK \
    ARDU_HANDI=$HANDI /tmp/hbin_cal $PER 0 0 1000 1 2 $((50000 + i*PER)) >out.txt 2>err.txt ) &
done; wait
W=$(grep -ho "AI WIN" $WD/w*/out.txt | wc -l | tr -d ' ')
G=$(grep -hc "AI WIN\|ai loss" $WD/w*/out.txt | paste -sd+ - | bc)
echo "[calib $TAG] $RANK handi=$HANDI: $W/$G wins" >> /tmp/calib.log
echo "=== CALIB $TAG DONE $(date '+%H:%M:%S') ===" >> /tmp/calib.log
