#!/usr/bin/env python3
# adj.py ported to the 4090: KataGo adjudication of a workdir of SGFs over ssh
# (same kata9x9 net as local -> consistent verdicts; ~seconds instead of ~10 min).
# Usage: adj4090.py <workdir> <out.json> [maxVisits=20]
import sys, os, re, json, glob, subprocess
sys.path.insert(0, "/Users/jay/.claude/jobs/f7120da0/tmp/nightlog"); import goboard as G
COLS = "ABCDEFGHJ"; kv = lambda p: "pass" if p is None else f"{COLS[p%9]}{9-p//9}"
WD = sys.argv[1]; OUT = sys.argv[2]; MV = int(sys.argv[3]) if len(sys.argv) > 3 else 20
KD = r"C:\Users\crook\LizzieYZY\katago-v1.15.3-opencl-windows-x64"
MODEL = r"C:\Users\crook\LizzieYZY\katago-networks\kata9x9-b18c384nbt-20231025.bin.gz"
env = dict(os.environ, SSH_ASKPASS="/tmp/pw4090.sh", SSH_ASKPASS_REQUIRE="force", DISPLAY=":0")
games = sorted(glob.glob(os.path.join(WD, "game_*.sgf")))
proc = subprocess.Popen(
    ["ssh", "katago@10.0.0.17",
     f'cd /d {KD} && katago.exe analysis -config analysis_example.cfg -model {MODEL} '
     f'-override-config numAnalysisThreads=32,reportAnalysisWinratesAs=BLACK'],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
    text=True, bufsize=1, env=env)
meta = {}
for i, p in enumerate(games):
    g = int(re.search(r"game_(\d+)", p).group(1)); mvs = G.sgf_moves(p); ab = (g % 2 == 0)
    meta[i] = (g, ab)
    q = {"id": str(i), "moves": [["b" if c == 1 else "w", kv(mp)] for c, mp in mvs],
         "rules": "chinese", "komi": 6.5, "boardXSize": 9, "boardYSize": 9,
         "analyzeTurns": [len(mvs)], "maxVisits": MV}
    proc.stdin.write(json.dumps(q) + "\n")
proc.stdin.close()
out = {}
for line in proc.stdout:
    try: r = json.loads(line)
    except Exception: continue
    if "rootInfo" in r:
        out[int(r["id"])] = (r["rootInfo"]["winrate"], r["rootInfo"]["scoreLead"])
        if len(out) >= len(meta): break
try: proc.terminate()
except Exception: pass
res = {}; aw = 0; n = 0; sls = []
for i, (g, ab) in meta.items():
    if i not in out: continue
    wr, sl = out[i]; ai_sl = sl if ab else -sl; won = (wr if ab else 1 - wr) >= 0.5
    res[g] = (1 if won else 0, round(ai_sl, 1)); aw += won; n += 1; sls.append(ai_sl)
json.dump(res, open(OUT, "w"))
sls.sort(); med = sls[len(sls)//2] if sls else 0
print(f"WINRATE {aw}/{n} = {100*aw/max(n,1):.0f}%  median_scoreLead={med:+.1f}")
