#!/bin/bash
set -e
cd /Users/jay/workspace/ardu_go/test/nnretrain
echo "=== R2 extract $(date '+%H:%M:%S') ==="
python3 extract_positions.py /tmp/dg2 /tmp/hcg_dg0.dTde41 /tmp/hcg_dg1.ciia7E
NN_DUMP=1 FORCE_NN=1 /tmp/nndump < /tmp/dg2_probe.in > /tmp/dg2_dump.txt
echo "dump: $(grep -c '^POS' /tmp/dg2_dump.txt)"
python3 - <<'PY'
# coverage saturation: how many R2 positions are already in the aggregate?
probe_new = {l.split()[0]: l for l in open('/tmp/dg2_probe.in')}
# canonical containment via replay keys is what extract already deduped internally;
# cross-file check: compare against merged probe by token-sequence identity is weak.
# Use position-key files instead: recompute canonical keys for both.
print(len(probe_new), "R2 unique positions (in-file dedup)")
PY
echo "=== R2 labels $(date '+%H:%M:%S') ==="
python3 label_costs.py /tmp/dg2
echo "=== R2 merge+train $(date '+%H:%M:%S') ==="
python3 - <<'PY'
import json, re
for suf in ('_meta.jsonl', '_costs.jsonl'):
    rows = [json.loads(l) for l in open('/tmp/dg2' + suf)]
    with open('/tmp/dg2' + suf, 'w') as f:
        for r in rows:
            r['id'] = 'e' + r['id']
            f.write(json.dumps(r) + '\n')
probe = open('/tmp/dg2_probe.in').read().replace('\np', '\nep')
if probe.startswith('p'): probe = 'e' + probe
open('/tmp/dg2_probe.in', 'w').write(probe)
dump = re.sub(r'(POS|ENDPOS) p', r'\1 ep', open('/tmp/dg2_dump.txt').read())
open('/tmp/dg2_dump.txt', 'w').write(dump)
for suf in ('_meta.jsonl', '_costs.jsonl'):
    with open('/tmp/mg2' + suf, 'w') as f:
        f.write(open('/tmp/mg' + suf).read())
        f.write(open('/tmp/dg2' + suf).read())
open('/tmp/mg2_dump.txt','w').write(open('/tmp/mg_dump.txt').read() + open('/tmp/dg2_dump.txt').read())
print("R2 aggregate written")
PY
for s in 0 1; do
  echo "=== R2 seed $s $(date '+%H:%M:%S') ==="
  python3 nnret_train_cost.py /tmp/mg2 /tmp/nnret_dg2s$s.npz $s 300 8 128 0.20 > /tmp/nnret_dg2s$s.log 2>&1
  grep "epoch 300" /tmp/nnret_dg2s$s.log
  cp /tmp/nnret_dg2s$s.npz data/nets/
done
echo "=== DAGGER R2 TRAINED $(date '+%H:%M:%S') ==="
