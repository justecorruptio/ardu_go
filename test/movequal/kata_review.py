#!/usr/bin/env python3
# Feed corpus SGFs to the KataGo analysis engine; emit per-turn
# black-perspective winrate + scoreLead for every game as JSONL.
#   kata_review.py <out.jsonl> <sgf...>
import sys, re, json, subprocess, glob, os
out_path = sys.argv[1]
sgfs = sys.argv[2:]
MODEL = os.environ.get("MODEL", "/tmp/kata_b6.bin.gz")
CFG = os.environ.get("CFG", os.path.join(os.path.dirname(os.path.abspath(__file__)), "kata_analysis.cfg"))
COLS = "ABCDEFGHJ"
def moves_of(path):
    sgf = open(path).read()
    mvs = []
    for c, v in re.findall(r";([BW])\[(..)?\]", sgf):
        if not v: mvs.append([c, "pass"])
        else:
            x = ord(v[0]) - 97; y = ord(v[1]) - 97
            mvs.append([c, f"{COLS[x]}{9-y}"])
    return mvs
proc = subprocess.Popen(["katago", "analysis", "-config", CFG, "-model", MODEL],
                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                        stderr=subprocess.DEVNULL, text=True, bufsize=1)
queries = 0
for path in sgfs:
    g = int(re.search(r"game_(\d+)", path).group(1))
    mvs = moves_of(path)
    q = {"id": str(g), "moves": mvs, "rules": "chinese", "komi": 6.5,
         "boardXSize": 9, "boardYSize": 9,
         "analyzeTurns": list(range(len(mvs) + 1)),
         "maxVisits": 8, "includePolicy": False}
    proc.stdin.write(json.dumps(q) + "\n")
    queries += 1
proc.stdin.close()
results = {}
n = 0
with open(out_path, "w") as f:
    for line in proc.stdout:
        try: r = json.loads(line)
        except: continue
        if "rootInfo" not in r: continue
        f.write(json.dumps({"g": int(r["id"]), "t": r["turnNumber"],
                            "wr": round(r["rootInfo"]["winrate"], 4),
                            "sl": round(r["rootInfo"]["scoreLead"], 2)}) + "\n")
        n += 1
proc.wait()
print(f"{queries} games, {n} positions analyzed")
