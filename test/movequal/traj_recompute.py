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
    v=bym[t]
    # full decile fan (+quartiles): the SVG draws p10..p90 and the
    # captions read p30/p70 — emit them all so neither can KeyError
    qs=[10,20,25,30,40,50,60,70,75,80,90]
    p=np.percentile(v,qs)
    row={'t':int(t),'n':len(v)}
    for q,val in zip(qs,p): row[f'p{q}']=round(float(val),1)
    out.append(row)
json.dump(out, open(sys.argv[2],'w')); print(f"traj: {len(out)} moves, t0 p50={out[0]['p50']}")
