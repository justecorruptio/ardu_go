#!/bin/bash
# Learned-prior arc: overnight corpus. 300 games vs L0 + 300 vs the 10k
# human referee, PRIOR_DUMP on; dumps land per-worker for extraction.
set +e
cd /tmp
mkdir -p /tmp/pcorp
echo "=== L0 corpus $(date '+%H:%M:%S') ==="
for i in $(seq 0 5); do d=/tmp/pcorp/l0_w$i; mkdir -p $d
  ( cd $d && PRIOR_DUMP_ON=1 /tmp/hbin_pd 50 0 0 1000 1 2 $((20000 + i*50)) >out.txt 2>dump.txt ) &
done; wait
echo "=== 10k corpus $(date '+%H:%M:%S') ==="
for i in $(seq 0 5); do d=/tmp/pcorp/hk_w$i; mkdir -p $d
  ( cd $d && PATH=/tmp/shimbin:$PATH KATAGO_HUMAN=1 KATAGO_RANK=preaz_10k \
    PRIOR_DUMP_ON=1 /tmp/hbin_pd 50 0 0 1000 1 2 $((21000 + i*50)) >out.txt 2>dump.txt ) &
done; wait
echo "dumps: $(du -sh /tmp/pcorp | cut -f1)"
tar czf /Users/jay/workspace/ardu_go/test/nnretrain/campaign/priorcorpus_dumps.tgz -C /tmp pcorp
echo "=== PRIOR CORPUS DONE $(date '+%H:%M:%S') ==="
