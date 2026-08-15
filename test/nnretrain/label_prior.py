#!/usr/bin/env python3
# Learned-prior variant of label_costs.py: labels the widen-context cache
# (extract_prior.py output: records carry their own "moves" list incl. the
# descent path). Same 48-visit moveInfos, chunked + resumable.
#   label_prior.py <cache.jsonl.gz> <out_costs.jsonl>
import sys, os, json, subprocess, threading

import gzip
CACHE, OUTP = sys.argv[1], sys.argv[2]
COLS = "ABCDEFGHJ"
meta = [json.loads(l) for l in gzip.open(CACHE, "rt")]

KD = r"C:\Users\crook\LizzieYZY\katago-v1.15.3-opencl-windows-x64"
MODEL = r"C:\Users\crook\LizzieYZY\katago-networks\kata9x9-b18c384nbt-20231025.bin.gz"
env = dict(os.environ, SSH_ASKPASS="/tmp/pw4090.sh", SSH_ASKPASS_REQUIRE="force", DISPLAY=":0")

done = set()
outp = OUTP
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
            mv = r['moves']
            q = {"id": str(r['id']), "moves": mv, "rules": "chinese", "komi": 6.5,
                 "boardXSize": 9, "boardYSize": 9, "analyzeTurns": [len(mv)],
                 "maxVisits": int(__import__("os").environ.get("KATA_VISITS", "48")), "includePolicy": True}
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
        out.write(json.dumps({"id": int(r["id"]), "wr": mv_wr, "policy": pol}) + "\n")
        got += 1; n += 1
    out.flush()
    proc.kill()
    print(f"chunk {ci//CHUNK}: +{got} (total {n}/{len(meta)})", flush=True)
out.close()
print(f"cost labels done: {n}/{len(meta)} -> {outp}")
