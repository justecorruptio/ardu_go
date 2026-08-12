#!/usr/bin/env python3
# Finer-grained blunder tagger (2026-08-11, supersedes kata_classify.py's
# 9-class pass): non-exclusive board-replay tags over the >=25% blunders.
# Every tag is board-verifiable (replay) or KataGo-data-derived; a blunder
# can carry any number of tags. Usage:
#   kata_classify2.py <workdir> <suggest.jsonl> <out.json>
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
def adj_chains(b, p, color):
    """Distinct chains of `color` orthogonally adjacent to empty point p."""
    seen, out = set(), []
    for q in nbrs(p):
        if b.b[q] == color and q not in seen:
            g = chain(b, q); seen |= g; out.append(g)
    return out
def enclosed_region(b, p, color):
    """Empty region containing p; True if its stone border is all `color`."""
    reg = {p}; st = [p]; ok = True
    while st:
        u = st.pop()
        for q in nbrs(u):
            if b.b[q] == 0:
                if q not in reg: reg.add(q); st.append(q)
            elif b.b[q] != color: ok = False
    return ok, reg

rows = [json.loads(l) for l in open(os.path.join(WD, "kata.jsonl"))]
byg = {}
for r in rows: byg.setdefault(r["g"], {})[r["t"]] = r
def wr_pair(g, t):
    ts = byg[g]; ai_black = (g % 2 == 0)
    wr0 = ts[t]["wr"] if ai_black else 1 - ts[t]["wr"]
    wr1 = ts[t+1]["wr"] if ai_black else 1 - ts[t+1]["wr"]
    return wr0, wr1

sugg = [json.loads(l) for l in open(SUGG)]
bl_by_game = {}
for s in sugg: bl_by_game.setdefault(s["g"], []).append(s["t"])
finals = {}   # game -> final board (replay once)

# tag -> (group, description) ; groups order the artifact table
TAGS = {
 "feed-doomed":  ("life&death", "adds a stone to an own group that is dead at game end"),
 "missed-save":  ("life&death", "own group in atari, KataGo saves it, ArduGo plays elsewhere"),
 "missed-capture":("life&death", "enemy group in atari, KataGo takes it, ArduGo plays elsewhere"),
 "missed-attack":("life&death", "enemy group at 2 libs, KataGo presses it, ArduGo plays elsewhere"),
 "ignored-atari":("life&death", "own group in atari, KataGo addresses it, ArduGo does neither"),
 "captured-soon":("life&death", "the played stone is gone within 4 plies"),
 "self-atari":   ("tactics", "played stone's chain ends with exactly 1 liberty"),
 "feed-weak":    ("tactics", "played stone's chain ends with exactly 2 liberties"),
 "connect-miss": ("tactics", "KataGo's move connects 2+ own chains, ArduGo plays elsewhere"),
 "cut-miss":     ("tactics", "KataGo's move cuts/wedges 2+ enemy chains, ArduGo plays elsewhere"),
 "crawl":        ("direction", "plays line 1-2 while KataGo plays line 3+ (low crawl)"),
 "edge2":        ("direction", "KataGo plays line 2, ArduGo plays line 3+ (edge blindness)"),
 "edge1":        ("direction", "KataGo plays line 1, ArduGo plays line 3+"),
 "local":        ("direction", "played adjacent to KataGo's point (right area, wrong point)"),
 "tenuki":       ("direction", "abandons the hot area KataGo stays in"),
 "eye-fill":     ("waste", "fills a point whose neighbors are all own stones"),
 "own-territory":("waste", "plays inside a region bordered only by own stones"),
 "pass":         ("waste", "passes while contested boundaries remain"),
 "game-losing":  ("severity", "this single move crosses the game from won to lost"),
 "serial":       ("severity", "within 4 plies of another >=25% blunder"),
}
agg = {c: {"n": 0, "ph": [0, 0, 0], "loss": 0.0, "wr0": 0.0} for c in TAGS}
pair = {}
untagged = 0; per_move_tags = []
for s in sugg:
    g, t = s["g"], s["t"]
    played = s["played"]; best = parse_kv(s["best"]); last = s["last"]
    sgf = os.path.join(WD, f"game_{g:03d}.sgf")
    bd = G.replay_to(sgf, t)
    mover = 1 if (t % 2 == 0) else 2; opp = 3 - mover
    tags = []
    wr0, wr1 = wr_pair(g, t)
    if wr0 >= 0.5 and wr1 < 0.5: tags.append("game-losing")
    if any(abs(t2 - t) <= 4 and t2 != t for t2 in bl_by_game[g]): tags.append("serial")
    # --- direction (needs both points) ---
    if best is not None and played is not None:
        if line_of(played) <= 2 and line_of(best) >= 3: tags.append("crawl")
        if line_of(best) == 2 and line_of(played) >= 3: tags.append("edge2")
        if line_of(best) == 1 and line_of(played) >= 3: tags.append("edge1")
        if cheb(best, played) == 1: tags.append("local")
        if (last is not None and cheb(played, last) >= 3 and
            cheb(best, last) <= 2): tags.append("tenuki")
    # --- pre-move atari / 2-lib scans ---
    own_fix, enemy_cap, enemy_press = set(), set(), set()
    seen = set()
    for c in range(81):
        if bd.b[c] == 0 or c in seen: continue
        ch = chain(bd, c); seen |= ch
        ls = libs_of(bd, ch)
        if bd.b[c] == mover and len(ls) == 1: own_fix |= ls
        elif bd.b[c] == opp and len(ls) == 1: enemy_cap |= ls
        elif bd.b[c] == opp and len(ls) == 2: enemy_press |= ls
    if best is not None and played != best:
        if own_fix and best in own_fix: tags.append("missed-save")
        if enemy_cap and best in enemy_cap: tags.append("missed-capture")
        if enemy_press and best in enemy_press: tags.append("missed-attack")
    if own_fix and best is not None and best in (own_fix | enemy_cap) \
       and played is not None and played not in (own_fix | enemy_cap):
        tags.append("ignored-atari")
    # --- played-stone consequences ---
    if played is not None:
        joined = [q for q in nbrs(played) if bd.b[q] == mover]
        b2 = G.replay_to(sgf, t + 1)
        if b2.b[played] == mover:
            mych = chain(b2, played); nl = len(libs_of(b2, mych))
            if nl == 1: tags.append("self-atari")
            elif nl == 2: tags.append("feed-weak")
            if joined:
                fb = finals.get(g)
                if fb is None: fb = finals[g] = G.replay_to(sgf, 10**9)
                if all(fb.b[q] != mover for q in mych): tags.append("feed-doomed")
        b6 = G.replay_to(sgf, t + 5)
        if b6.b[played] != mover: tags.append("captured-soon")
        if bd.b[played] == 0:
            if all(bd.b[q] == mover for q in nbrs(played)): tags.append("eye-fill")
            ok, reg = enclosed_region(bd, played, mover)
            if ok and len(reg) <= 8: tags.append("own-territory")
    # --- best-move structure ---
    if best is not None and played != best and bd.b[best] == 0:
        if len(adj_chains(bd, best, mover)) >= 2: tags.append("connect-miss")
        if len(adj_chains(bd, best, opp)) >= 2: tags.append("cut-miss")
    if played is None:
        live = any(bd.b[q] == 0 and
                   any(bd.b[x] == 1 for x in nbrs(q)) and
                   any(bd.b[x] == 2 for x in nbrs(q)) for q in range(81))
        if live: tags.append("pass")
    if not tags: untagged += 1
    ph = 0 if t <= 15 else (1 if t <= 35 else 2)
    L = max(0.0, wr0 - wr1)
    for c in tags:
        agg[c]["n"] += 1; agg[c]["ph"][ph] += 1
        agg[c]["loss"] += L; agg[c]["wr0"] += wr0
    for a in tags:
        for b in tags:
            if a < b: pair[(a, b)] = pair.get((a, b), 0) + 1
    per_move_tags.append({"g": g, "t": t, "tags": tags, "loss": round(L, 3)})

top_pairs = sorted(pair.items(), key=lambda kv: -kv[1])[:12]
res = {"tags": {c: {"group": TAGS[c][0], "desc": TAGS[c][1], **agg[c]} for c in TAGS},
       "total": len(sugg), "untagged": untagged,
       "pairs": [[a, b, n] for (a, b), n in top_pairs],
       "moves": per_move_tags}
json.dump(res, open(OUT, "w"), indent=1)
grp_order = ["life&death", "tactics", "direction", "waste", "severity"]
for grp in grp_order:
    print(f"-- {grp}")
    for c in TAGS:
        if TAGS[c][0] != grp: continue
        a = agg[c]
        print(f"  {c:14s} n={a['n']:4d} ({100*a['n']/len(sugg):4.1f}%)  ph={a['ph']}  "
              f"loss {100*a['loss']/max(a['n'],1):3.0f}%  wr0 {100*a['wr0']/max(a['n'],1):3.0f}%")
print(f"untagged {untagged} ({100*untagged/len(sugg):.0f}%)")
print("top pairs:", ", ".join(f"{a}+{b}={n}" for (a, b), n in top_pairs[:6]))
