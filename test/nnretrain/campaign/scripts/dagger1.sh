#!/bin/bash
set -e
cd /Users/jay/workspace/ardu_go/test/nnretrain
echo "=== DAGGER extract $(date '+%H:%M:%S') ==="
# positions from the cost-family's own gauntlet games (own trajectories vs the 10k human)
python3 extract_positions.py /tmp/dg /tmp/hcg_cn1.3QF18Q /tmp/hcg_cn0.VLlV47 $(ls -d /tmp/hcg_v2b.* 2>/dev/null | head -1)
# dedupe against the existing cache by probe-line identity is imperfect (canonical keys
# differ per run); labeling is resumable and skips already-labeled ids, but ids differ --
# so just label the new set separately.
echo "=== DAGGER dump $(date '+%H:%M:%S') ==="
NN_DUMP=1 FORCE_NN=1 /tmp/nndump < /tmp/dg_probe.in > /tmp/dg_dump.txt
echo "dump: $(grep -c '^POS' /tmp/dg_dump.txt) positions"
echo "=== DAGGER labels $(date '+%H:%M:%S') ==="
python3 label_costs.py /tmp/dg
echo "=== DAGGER splice+train $(date '+%H:%M:%S') ==="
# merged prefix: concatenate cache + dagger (ids disjoint: dg uses p0.. too -> remap)
python3 - <<'PY'
import json
# remap dg ids with a 'd' prefix across all four files
for suf in ('_meta.jsonl', '_labels.jsonl', '_costs.jsonl'):
    try:
        rows = [json.loads(l) for l in open('/tmp/dg' + suf)]
    except FileNotFoundError:
        continue
    with open('/tmp/dg' + suf, 'w') as f:
        for r in rows:
            r['id'] = 'd' + r['id']
            f.write(json.dumps(r) + '\n')
probe = open('/tmp/dg_probe.in').read().replace('\np', '\ndp')
if probe.startswith('p'): probe = 'd' + probe
open('/tmp/dg_probe.in', 'w').write(probe)
dump = open('/tmp/dg_dump.txt').read()
import re
dump = re.sub(r'(POS|ENDPOS) p', r'\1 dp', dump)
open('/tmp/dg_dump.txt', 'w').write(dump)
# merged files
for suf, base in (('_meta.jsonl','/tmp/nncache_meta.jsonl'), ('_costs.jsonl','/tmp/nncache_costs.jsonl')):
    with open('/tmp/mg' + suf, 'w') as f:
        f.write(open(base).read())
        f.write(open('/tmp/dg' + suf).read())
open('/tmp/mg_dump.txt','w').write(open('/tmp/nncache_dump.txt').read() + open('/tmp/dg_dump.txt').read())
open('/tmp/mg_probe.in','w').write(open('/tmp/nncache_probe.in').read() + open('/tmp/dg_probe.in').read())
print("merged cache written (/tmp/mg_*)")
PY
for s in 0 1; do
  echo "=== dagger cn seed $s $(date '+%H:%M:%S') ==="
  python3 nnret_train_cost.py /tmp/mg /tmp/nnret_dg$s.npz $s 300 8 128 0.20 > /tmp/nnret_dg$s.log 2>&1
  grep "epoch 300" /tmp/nnret_dg$s.log
  cp /tmp/nnret_dg$s.npz data/nets/
done
echo "=== DAGGER TRAINED $(date '+%H:%M:%S') ==="
