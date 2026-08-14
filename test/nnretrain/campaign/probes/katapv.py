#!/usr/bin/env python3
# KataGo PVs for the L&D probe positions: query the 4090 analysis engine
# at each (sgf, turn) prefix, capture the top moveInfo's pv.
import sys, os, json, subprocess
sys.path.insert(0, "/Users/jay/.claude/jobs/f7120da0/tmp/nightlog"); import goboard as G
COLS = "ABCDEFGHJ"
kv = lambda p: "pass" if p is None else f"{COLS[p%9]}{9-p//9}"
def parse(s):
    if s.lower() == "pass": return None
    return (9 - int(s[1:])) * 9 + COLS.index(s[0].upper())
items = []
for l in open('/tmp/mqre/ldlist.txt'):
    p = l.split(); items.append((p[0], int(p[1]), int(p[2]), int(p[3])))
KD = r"C:\Users\crook\LizzieYZY\katago-v1.15.3-opencl-windows-x64"
MODEL = r"C:\Users\crook\LizzieYZY\katago-networks\kata9x9-b18c384nbt-20231025.bin.gz"
env = dict(os.environ, SSH_ASKPASS="/tmp/pw4090.sh", SSH_ASKPASS_REQUIRE="force", DISPLAY=":0")
proc = subprocess.Popen(
    ["ssh", "katago@10.0.0.17",
     f'cd /d {KD} && katago.exe analysis -config analysis_example.cfg -model {MODEL} '
     f'-override-config numAnalysisThreads=8,reportAnalysisWinratesAs=BLACK'],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
    text=True, bufsize=1, env=env)
for i, (sgf, turn, played, best) in enumerate(items):
    mvs = G.sgf_moves(sgf)[:turn]
    q = {"id": str(i), "moves": [["b" if c == 1 else "w", kv(mp)] for c, mp in mvs],
         "rules": "chinese", "komi": 6.5, "boardXSize": 9, "boardYSize": 9,
         "analyzeTurns": [turn], "maxVisits": 400}
    proc.stdin.write(json.dumps(q) + "\n")
proc.stdin.close()
pvs = {}
for line in proc.stdout:
    try: r = json.loads(line)
    except: continue
    if "moveInfos" not in r: continue
    mi = max(r["moveInfos"], key=lambda m: m.get("visits", 0))
    pvs[int(r["id"])] = [parse(m) for m in mi.get("pv", [])][:8]
with open('/tmp/mqre/ldlist2.txt', 'w') as out:
    for i, (sgf, turn, played, best) in enumerate(items):
        pv = [p for p in pvs.get(i, []) if p is not None]
        out.write(f"{sgf} {turn} {played} {best} " + " ".join(map(str, pv)) + "\n")
print("wrote", len(items), "with PVs:", sum(1 for i in range(len(items)) if pvs.get(i)))
