#!/usr/bin/env python3
# Learned-prior arc: extract widen-context feature dumps into a training
# cache. Joins WP records (root-board FNV hash + descent path) to boards
# by replaying each worker's SGFs and hashing every position identically.
# Dedupes by (node-board canonical), subsamples for labeling.
#   extract_prior.py <corpusdir> <outprefix> [maxlabel=18000]
import sys, os, glob, json, random, gzip

sys.path.insert(0, "/Users/jay/.claude/jobs/f7120da0/tmp/nightlog")
import goboard as G

CORP, OUT = sys.argv[1], sys.argv[2]
MAXL = int(sys.argv[3]) if len(sys.argv) > 3 else 18000
FNV_OFF, FNV_PRIME = 2166136261, 16777619
M32 = 0xFFFFFFFF

def fnv(bd, turn):
    h = FNV_OFF
    for i in range(81):
        h = ((h ^ bd.b[i]) * FNV_PRIME) & M32
    return h ^ ((turn & 0xFF) << 24)

COLS = "ABCDEFGHJ"
kv = lambda p: "pass" if p is None else f"{COLS[p%9]}{9-p//9}"

recs = []            # (workdir, hash, stones, depth, toMove, path, [(cell, feats, hand)])
for wd in sorted(glob.glob(os.path.join(CORP, "*_w*"))):
    dump = os.path.join(wd, "dump.txt")
    if not os.path.exists(dump): continue
    cur = None
    for line in open(dump, errors="replace"):
        p = line.split()
        if not p: continue
        if p[0] == "WP" and len(p) >= 6:
            # WP <seq> <roothash-hex8> <stones> <depth> <toMove> <path...>
            cur = (wd, int(p[2], 16), int(p[3]), int(p[4]), int(p[5]),
                   [int(x) for x in p[6:]], [])
        elif p[0] == "WC" and cur is not None and len(p) >= 27:
            cell = int(p[1]); feats = [int(x) for x in p[2:26]]; hand = int(p[26])
            cur[6].append((cell, feats, hand))
        elif p[0] == "WEND" and cur is not None:
            if cur[6]: recs.append(cur)
            cur = None
print(f"{len(recs)} sampled widens parsed")

# hash -> (workdir, moves-prefix) index from SGF replays
posix = {}
for wd in sorted(glob.glob(os.path.join(CORP, "*_w*"))):
    for sgf in sorted(glob.glob(os.path.join(wd, "game_*.sgf"))):
        mvs = G.sgf_moves(sgf)
        for t in range(len(mvs) + 1):
            bd = G.replay_to(sgf, t)
            turn = 1 if t % 2 == 0 else 2
            posix[(wd, fnv(bd, turn))] = (sgf, t)
print(f"{len(posix)} root positions indexed")

# join + build node boards
out, misses, seen = [], 0, set()
for wd, h, stones, depth, tomove, path, cands in recs:
    key = (wd, h)
    if key not in posix: misses += 1; continue
    sgf, t = posix[key]
    bd = G.replay_to(sgf, t)
    mvs = G.sgf_moves(sgf)[:t]
    node_moves = [m for _, m in mvs] + path
    # apply descent path with captures (goboard Board.play handles them)
    ok = True
    color = 1 if t % 2 == 0 else 2
    pathmoves = []
    for mv in path:
        if mv >= 81 or bd.b[mv] != 0: ok = False; break
        bd.play(mv, color)
        pathmoves.append((color, mv))
        color = 3 - color
    if not ok: misses += 1; continue
    if color != tomove: misses += 1; continue   # parity sanity
    ck = (tuple(bd.b), tomove)
    if ck in seen: continue
    seen.add(ck)
    out.append({"id": len(out), "sgf": sgf, "t": t, "path": path,
                "toMove": tomove, "stones": stones, "depth": depth,
                "moves": [["b" if c == 1 else "w", kv(m)] for c, m in mvs] +
                         [["b" if c == 1 else "w", kv(m)] for c, m in pathmoves],
                "cands": [{"c": c, "f": f, "hand": hd} for c, f, hd in cands]})
print(f"joined {len(out)} unique node positions ({misses} misses)")

random.seed(7)
if len(out) > MAXL:
    out = random.sample(out, MAXL)
    for i, r in enumerate(out): r["id"] = i
with gzip.open(OUT + "_prior.jsonl.gz", "wt") as f:
    for r in out: f.write(json.dumps(r) + "\n")
print(f"wrote {len(out)} -> {OUT}_prior.jsonl.gz")
