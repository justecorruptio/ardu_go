#!/usr/bin/env python3
# NN retrain: extract unique opening positions from corpus SGFs.
#   extract_positions.py <out_prefix> <sgf_dir> [<sgf_dir> ...]
# Emits:
#   <prefix>_probe.in   — replay lines for nndump_probe (id ntoks toks)
#   <prefix>_meta.jsonl — {id, g, t, sgf, weight, stones, turn}
# Position = board after ply t, to-move = player of ply t+1. Dedupe by
# canonical (8-sym min) board+toMove+last key; weight = occurrence count.
# Filter: 1 <= stones <= 24 (the NN's runtime range), last move exists.
import sys, os, re, glob, json, collections

PREFIX = sys.argv[1]
DIRS = sys.argv[2:]

def sgf_moves(path):
    s = open(path).read()
    return [(c, None if not v else (ord(v[0]) - 97, ord(v[1]) - 97))
            for c, v in re.findall(r";([BW])\[(..)?\]", s)]

def neigh(x, y):
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        if 0 <= x + dx < 9 and 0 <= y + dy < 9:
            yield (x + dx, y + dy)

def sym(x, y, s):
    if s & 1: x = 8 - x
    if s & 2: y = 8 - y
    if s & 4: x, y = y, x
    return x, y

seen = {}
order = []
for d in DIRS:
    for f in sorted(glob.glob(os.path.join(d, "game_*.sgf"))):
        g = int(re.search(r"game_(\d+)", f).group(1))
        mvs = sgf_moves(f)
        bd = {}
        toks = []
        for i, (c, p) in enumerate(mvs):
            if p is None:
                toks.append("PP")
                continue
            bd[p] = c
            # captures
            for nb in list(neigh(*p)):
                if nb in bd and bd[nb] != c:
                    col = bd[nb]; cs = {nb}; st = [nb]; dead = True
                    while st:
                        u = st.pop()
                        for nb2 in neigh(*u):
                            if nb2 not in bd: dead = False
                            elif bd[nb2] == col and nb2 not in cs:
                                cs.add(nb2); st.append(nb2)
                    if dead:
                        for q in cs: del bd[q]
            toks.append(f"{p[0]}{p[1]}")
            t = i + 1                     # position after ply i (1-based count)
            ns = len(bd)
            if not (1 <= ns <= 24): continue
            if t >= len(mvs): continue    # need a next mover
            tomove = 'B' if (t % 2 == 0) else 'W'
            best = None
            for s8 in range(8):
                k = (tuple(sorted((sym(*q, s8), cc) for q, cc in bd.items())),
                     sym(*p, s8))
                if best is None or k < best: best = k
            key = (best, tomove)
            if key in seen:
                seen[key]['weight'] += 1
                continue
            pid = f"p{len(order)}"
            rec = {'id': pid, 'g': g, 't': t, 'sgf': f, 'weight': 1,
                   'stones': ns, 'turn': tomove,
                   'toks': list(toks)}
            seen[key] = rec
            order.append(rec)

with open(PREFIX + "_probe.in", "w") as pf, open(PREFIX + "_meta.jsonl", "w") as mf:
    for r in order:
        pf.write(f"{r['id']} {len(r['toks'])} {' '.join(r['toks'])}\n")
        m = dict(r); del m['toks']
        mf.write(json.dumps(m) + "\n")
w = sum(r['weight'] for r in order)
bl = sum(1 for r in order if r['turn'] == 'B')
print(f"{len(order)} unique positions ({w} occurrences) from {len(DIRS)} dirs; "
      f"to-move B {bl} / W {len(order)-bl}")
