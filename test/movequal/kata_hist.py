#!/usr/bin/env python3
# Per-phase histograms of TRUE winrate loss per AI move (KataGo).
#   kata_hist.py <jsonl> [json-out]
import sys, json, collections
rows = collections.defaultdict(dict)
for line in open(sys.argv[1]):
    r = json.loads(line)
    rows[r["g"]][r["t"]] = (r["wr"], r["sl"])
phases = ["OPENING","MIDDLE","ENDGAME"]
wr_loss = {p: [] for p in range(3)}
sl_loss = {p: [] for p in range(3)}
games = 0
for g, turns in rows.items():
    ai_black = (g % 2 == 0)
    games += 1
    n = max(turns.keys())
    for t in range(n):
        if t not in turns or (t+1) not in turns: continue
        mover_black = (t % 2 == 0)
        if mover_black != ai_black: continue      # AI moves only
        wr0, sl0 = turns[t]; wr1, sl1 = turns[t+1]
        if ai_black: dwr = (wr0 - wr1) * 100; dsl = sl0 - sl1
        else:        dwr = (wr1 - wr0) * 100; dsl = sl1 - sl0
        ph = 0 if t <= 15 else (1 if t <= 35 else 2)
        wr_loss[ph].append(dwr); sl_loss[ph].append(dsl)
out = {"games": games, "moves": sum(len(v) for v in wr_loss.values()), "phases": []}
LO, HI = -10, 40
for p in range(3):
    nb = HI - LO + 1
    counts = [0]*nb
    for d in wr_loss[p]:
        c = max(LO, min(HI, round(d)))
        counts[c - LO] += 1
    n = len(wr_loss[p])
    import statistics
    out["phases"].append({
        "name": phases[p], "n": n,
        "mean": round(sum(wr_loss[p])/n, 2),
        "median": round(statistics.median(wr_loss[p]), 2),
        "counts": counts,
        "pct": [round(100*c/n, 2) for c in counts],
        "big10": round(100*sum(1 for d in wr_loss[p] if d >= 10)/n, 1),
        "big25": round(100*sum(1 for d in wr_loss[p] if d >= 25)/n, 1),
        "slmean": round(sum(sl_loss[p])/n, 2)})
    print(f"{phases[p]}: {n} moves, mean wr-loss {out['phases'][p]['mean']}%, "
          f"median {out['phases'][p]['median']}%, >=10% loss: {out['phases'][p]['big10']}%, "
          f">=25%: {out['phases'][p]['big25']}%, mean scoreLead loss {out['phases'][p]['slmean']}")
if len(sys.argv) > 2:
    out["lo"], out["hi"] = LO, HI
    json.dump(out, open(sys.argv[2], "w"))
