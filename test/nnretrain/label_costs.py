#!/usr/bin/env python3
# Cost labels for the competitive objective: per-candidate winrate via KataGo
# moveInfos @ 48 visits on the 4090. cost(move) = best_wr - move_wr (mover
# perspective). Moves the search never visited get no entry (trainer decides).
#   label_costs.py <prefix>   (reads _meta/_probe, writes <prefix>_costs.jsonl)
# Chunked + resumable (same lessons as label_positions.py).
import sys, os, json, subprocess, threading

PREFIX = sys.argv[1]
COLS = "ABCDEFGHJ"
meta = [json.loads(l) for l in open(PREFIX + "_meta.jsonl")]
toks = {}
for line in open(PREFIX + "_probe.in"):
    p = line.split()
    toks[p[0]] = p[2:]

KD = r"C:\Users\crook\LizzieYZY\katago-v1.15.3-opencl-windows-x64"
MODEL = r"C:\Users\crook\LizzieYZY\katago-networks\kata9x9-b18c384nbt-20231025.bin.gz"
env = dict(os.environ, SSH_ASKPASS="/tmp/pw4090.sh", SSH_ASKPASS_REQUIRE="force", DISPLAY=":0")

done = set()
outp = PREFIX + "_costs.jsonl"
if os.path.exists(outp):
    for l in open(outp):
        try: done.add(json.loads(l)["id"])
        except ValueError: pass
todo = [r for r in meta if r['id'] not in done]
print(f"{len(done)} already labeled, {len(todo)} to go")
CHUNK = 500
out = open(outp, "a")
n = len(done)

def kv_to_pos(v):
    if v.lower() == "pass": return None
    return (9 - int(v[1:])) * 9 + COLS.index(v[0].upper())

for ci in range(0, len(todo), CHUNK):
    chunk = todo[ci:ci + CHUNK]
    proc = subprocess.Popen(
        ["ssh", "katago@10.0.0.17",
         f'cd /d {KD} && katago.exe analysis -config analysis_example.cfg -model {MODEL} '
         f'-override-config numAnalysisThreads=16'],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=open(f'/tmp/nncost_err_{ci}.log', 'w'),
        text=True, bufsize=1, env=env)
    def feed(rows=chunk, p=proc):
        for r in rows:
            mv = []
            for i, t in enumerate(toks[r['id']]):
                c = "b" if i % 2 == 0 else "w"
                mv.append([c, "pass" if t == "PP" else f"{COLS[int(t[0])]}{9-int(t[1])}"])
            q = {"id": r['id'], "moves": mv, "rules": "chinese", "komi": 6.5,
                 "boardXSize": 9, "boardYSize": 9, "analyzeTurns": [len(mv)],
                 "maxVisits": 48, "includePolicy": True}
            try: p.stdin.write(json.dumps(q) + "\n")
            except BrokenPipeError: return
        try: p.stdin.close()
        except Exception: pass
    threading.Thread(target=feed, daemon=True).start()
    got = 0
    for line in proc.stdout:
        try: r = json.loads(line)
        except ValueError: continue
        if 'moveInfos' not in r: continue
        # winrate is reported for the player to move at the analyzed turn
        mv_wr = {}
        for mi in r['moveInfos']:
            pos = kv_to_pos(mi['move'])
            if pos is not None:
                mv_wr[str(pos)] = round(mi['winrate'], 5)
        pol = {}
        for pos, p2 in enumerate(r.get('policy', [])[:81]):
            if p2 is not None and p2 > 0.002:
                pol[str(pos)] = round(p2, 5)
        out.write(json.dumps({"id": r["id"], "wr": mv_wr, "policy": pol}) + "\n")
        got += 1; n += 1
    out.flush()
    proc.kill()
    print(f"chunk {ci//CHUNK}: +{got} (total {n}/{len(meta)})", flush=True)
out.close()
print(f"cost labels done: {n}/{len(meta)} -> {outp}")
