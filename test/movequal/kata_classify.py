#!/usr/bin/env python3
# Attack-class tagger (recreated 2026-08-11): board-replay classification of
# >=25% blunders, non-exclusive tags, faithful to the published taxonomy.
# Usage: kata_classify.py <workdir> <suggest.jsonl> <out.json>
import sys, os, json
sys.path.insert(0, "/Users/jay/.claude/jobs/f7120da0/tmp/nightlog"); import goboard as G
WD, SUGG, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
COLS = "ABCDEFGHJ"
def parse_kv(s):
    if s is None or s.lower() == "pass": return None
    x = COLS.index(s[0].upper()); y = 9 - int(s[1:])
    return y * 9 + x
def nbrs(p):
    x, y = p % 9, p // 9; r = []
    if x > 0: r.append(p - 1)
    if x < 8: r.append(p + 1)
    if y > 0: r.append(p - 9)
    if y < 8: r.append(p + 9)
    return r
def chain(b, c):
    g = {c}; st = [c]
    while st:
        u = st.pop()
        for q in nbrs(u):
            if b.b[q] == b.b[c] and q not in g: g.add(q); st.append(q)
    return g
def libs_of(b, g):
    return set(q for u in g for q in nbrs(u) if b.b[q] == 0)
def line_of(p):
    x, y = p % 9, p // 9
    return 1 + min(x, 8 - x, y, 8 - y)
def cheb(a, b):
    return max(abs(a % 9 - b % 9), abs(a // 9 - b // 9))

rows = [json.loads(l) for l in open(os.path.join(WD, "kata.jsonl"))]
byg = {}
for r in rows: byg.setdefault(r["g"], {})[r["t"]] = r
def loss_of(g, t):
    ts = byg[g]; ai_black = (g % 2 == 0)
    wr0 = ts[t]["wr"] if ai_black else 1 - ts[t]["wr"]
    wr1 = ts[t+1]["wr"] if ai_black else 1 - ts[t+1]["wr"]
    return max(0.0, wr0 - wr1)

sugg = [json.loads(l) for l in open(SUGG)]
bl_by_game = {}
for s in sugg: bl_by_game.setdefault(s["g"], []).append(s["t"])
CLASSES = ["serial", "edge2", "local", "tenuki", "fed", "edge1",
           "atari", "capture", "pass"]
agg = {c: {"n": 0, "ph": [0, 0, 0], "loss": 0.0} for c in CLASSES}
untagged = 0; games_with = set(); games_3plus = {}
for s in sugg:
    g, t = s["g"], s["t"]
    played = s["played"]; best = parse_kv(s["best"]); last = s["last"]
    bd = G.replay_to(os.path.join(WD, f"game_{g:03d}.sgf"), t)
    mover = 1 if (t % 2 == 0) else 2; opp = 3 - mover
    tags = []
    if any(abs(t2 - t) <= 4 and t2 != t for t2 in bl_by_game[g]): tags.append("serial")
    if best is not None and played is not None:
        if line_of(best) == 2 and line_of(played) >= 3: tags.append("edge2")
        if line_of(best) == 1 and line_of(played) >= 3: tags.append("edge1")
        if cheb(best, played) == 1: tags.append("local")
    if (best is not None and played is not None and last is not None and
        cheb(played, last) >= 3 and cheb(best, last) <= 2): tags.append("tenuki")
    if played is not None:
        b2 = G.replay_to(os.path.join(WD, f"game_{g:03d}.sgf"), t + 1)
        if b2.b[played] == mover and len(libs_of(b2, chain(b2, played))) <= 2:
            tags.append("fed")
    # ataris on the pre-move board
    seen = set(); own_atari_fix = set(); enemy_atari_cap = set()
    for c in range(81):
        if bd.b[c] == 0 or c in seen: continue
        ch = chain(bd, c); seen |= ch
        ls = libs_of(bd, ch)
        if len(ls) == 1:
            (l,) = ls
            if bd.b[c] == mover: own_atari_fix.add(l)
            else: enemy_atari_cap.add(l)
    if own_atari_fix and best is not None and best in (own_atari_fix | enemy_atari_cap) \
       and played not in (own_atari_fix | enemy_atari_cap): tags.append("atari")
    if enemy_atari_cap and best in enemy_atari_cap and played != best \
       and best is not None: tags.append("capture")
    if played is None:
        live = any(bd.b[q] == 0 and
                   any(bd.b[x] == 1 for x in nbrs(q)) and
                   any(bd.b[x] == 2 for x in nbrs(q)) for q in range(81))
        if live: tags.append("pass")
    if not tags: untagged += 1
    games_with.add(g); games_3plus[g] = games_3plus.get(g, 0) + 1
    ph = 0 if t <= 15 else (1 if t <= 35 else 2)
    L = loss_of(g, t)
    for c in tags:
        agg[c]["n"] += 1; agg[c]["ph"][ph] += 1; agg[c]["loss"] += L
res = {"classes": agg, "total": len(sugg), "untagged": untagged,
       "games_with": len(games_with),
       "games_3plus": sum(1 for v in games_3plus.values() if v >= 3),
       "ngames": len(set(r["g"] for r in rows))}
json.dump(res, open(OUT, "w"), indent=1)
for c in CLASSES:
    a = agg[c]
    print(f"{c:8s} n={a['n']:4d} ({100*a['n']/len(sugg):.0f}%)  ph={a['ph']}  "
          f"avg loss {100*a['loss']/max(a['n'],1):.0f}%")
print(f"untagged {untagged} ({100*untagged/len(sugg):.0f}%)  "
      f"games w/ blunder {len(games_with)}  3+ {res['games_3plus']}")
