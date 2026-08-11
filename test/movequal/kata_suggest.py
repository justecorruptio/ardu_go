#!/usr/bin/env python3
# Suggestion pass (recreated 2026-08-11; original lost with the old scratchpad):
# for every AI move that lost >=25% winrate (per kata.jsonl), re-query KataGo at
# 48 visits on the pre-move position -> best move + its winrate.
# Usage: kata_suggest.py <workdir> <out.jsonl>   (MODEL/CFG hardcoded below)
import sys, os, json, subprocess, threading
sys.path.insert(0, "/Users/jay/.claude/jobs/f7120da0/tmp/nightlog"); import goboard as G
MODEL = "/tmp/kata_b6.bin.gz"
CFG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "kata_analysis.cfg")
WD, OUT = sys.argv[1], sys.argv[2]
COLS = "ABCDEFGHJ"; kv = lambda p: "pass" if p is None else f"{COLS[p%9]}{9-p//9}"
rows = [json.loads(l) for l in open(os.path.join(WD, "kata.jsonl"))]
byg = {}
for r in rows: byg.setdefault(r["g"], {})[r["t"]] = r
blunders = []
for g, ts in byg.items():
    ai_black = (g % 2 == 0)
    for t in sorted(ts):
        if t + 1 not in ts: continue
        if (t % 2 == 0) != ai_black: continue
        wr0 = ts[t]["wr"] if ai_black else 1 - ts[t]["wr"]
        wr1 = ts[t + 1]["wr"] if ai_black else 1 - ts[t + 1]["wr"]
        if max(0.0, wr0 - wr1) >= 0.25: blunders.append((g, t))
print(f"{len(blunders)} blunders (>=25%) to re-query at 48 visits", flush=True)
proc = subprocess.Popen(["katago", "analysis", "-config", CFG, "-model", MODEL,
    "-override-config", "numAnalysisThreads=12,numSearchThreadsPerAnalysisThread=1"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1)
meta = {}; queries = []
for g, t in blunders:
    mvs = G.sgf_moves(os.path.join(WD, f"game_{g:03d}.sgf"))
    meta[f"{g}_{t}"] = (g, t, mvs[t][1], (mvs[t-1][1] if t else None))
    queries.append(json.dumps({"id": f"{g}_{t}",
        "moves": [["b" if c == 1 else "w", kv(mp)] for c, mp in mvs[:t]],
        "rules": "chinese", "komi": 6.5, "boardXSize": 9, "boardYSize": 9,
        "analyzeTurns": [t], "maxVisits": 48}))
def feed():
    for q in queries: proc.stdin.write(q + "\n")
    proc.stdin.close()
threading.Thread(target=feed, daemon=True).start()
out = open(OUT, "w"); got = 0
for line in proc.stdout:
    try: r = json.loads(line)
    except Exception: continue
    if "moveInfos" not in r or not r["moveInfos"]: continue
    g, t, played, last = meta[r["id"]]
    best = max(r["moveInfos"], key=lambda m: -m.get("order", 99))
    best = r["moveInfos"][0] if r["moveInfos"][0].get("order", 0) == 0 else best
    mover_black = (t % 2 == 0)
    wr = best["winrate"] if mover_black else 1 - best["winrate"]   # cfg: BLACK persp
    out.write(json.dumps({"g": g, "t": t, "played": played, "last": last,
                          "best": best["move"], "best_wr": round(wr, 4)}) + "\n")
    got += 1
    if got % 200 == 0: print(f"  {got}/{len(blunders)}", flush=True)
    if got >= len(queries): break
try: proc.terminate()
except Exception: pass
out.close()
print(f"DONE suggest: {got} -> {OUT}", flush=True)
