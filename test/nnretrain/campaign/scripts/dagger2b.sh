#!/bin/bash
set -e
cd /Users/jay/workspace/ardu_go/test/nnretrain
for s in 0 1; do
  echo "=== R2 seed $s $(date '+%H:%M:%S') ==="
  python3 nnret_train_cost.py /tmp/mg2 /tmp/nnret_dg2s$s.npz $s 300 8 128 0.20 > /tmp/nnret_dg2s$s.log 2>&1
  grep "epoch 300" /tmp/nnret_dg2s$s.log
  cp /tmp/nnret_dg2s$s.npz data/nets/
done
echo "=== DAGGER R2 TRAINED $(date '+%H:%M:%S') ==="
