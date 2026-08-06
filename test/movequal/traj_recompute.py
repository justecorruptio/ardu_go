import json, sys, collections, numpy as np
rows = collections.defaultdict(dict)
for line in open(sys.argv[1]):
    r = json.loads(line); rows[r['g']][r['t']] = r['wr']
bym = collections.defaultdict(list)
for g, turns in rows.items():
    ai_black = (g % 2 == 0)
    for t, wr in turns.items():
        bym[t].append((wr if ai_black else 1.0-wr)*100)
out=[]
for t in sorted(bym):
    v=bym[t]; p=np.percentile(v,[10,25,50,75,90])
    out.append({'t':int(t),'n':len(v),'p10':round(float(p[0]),1),'p25':round(float(p[1]),1),
                'p50':round(float(p[2]),1),'p75':round(float(p[3]),1),'p90':round(float(p[4]),1)})
json.dump(out, open(sys.argv[2],'w')); print(f"traj: {len(out)} moves, t0 p50={out[0]['p50']}")
