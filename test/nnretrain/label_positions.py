#!/usr/bin/env python3
# NN retrain: label extracted positions with the 9x9-specialist KataGo policy
# (b18 on the 4090, includePolicy @ maxVisits=2 — the wgnet-arc teacher).
#   label_positions.py <prefix>   (reads <prefix>_meta.jsonl, writes <prefix>_labels.jsonl)
import sys, os, json, subprocess, threading

PREFIX = sys.argv[1]
COLS = "ABCDEFGHJ"
meta = [json.loads(l) for l in open(PREFIX + "_meta.jsonl")]
# replay tokens live in _probe.in (meta drops them); re-read
toks = {}
for line in open(PREFIX + "_probe.in"):
    p = line.split()
    toks[p[0]] = p[2:]

KD = r"C:\Users\crook\LizzieYZY\katago-v1.15.3-opencl-windows-x64"
MODEL = r"C:\Users\crook\LizzieYZY\katago-networks\kata9x9-b18c384nbt-20231025.bin.gz"
env = dict(os.environ, SSH_ASKPASS="/tmp/pw4090.sh", SSH_ASKPASS_REQUIRE="force", DISPLAY=":0")

# resumable: skip ids already labeled; chunked sessions (the single 29k-query
# stream died mid-flight on the remote side)
done = set()
outp = PREFIX + "_labels.jsonl"
if os.path.exists(outp):
    for l in open(outp):
        try: done.add(json.loads(l)["id"])
        except ValueError: pass
todo = [r for r in meta if r['id'] not in done]
print(f"{len(done)} already labeled, {len(todo)} to go")
CHUNK = 500
out = open(outp, "a")
n = len(done)
for ci in range(0, len(todo), CHUNK):
    chunk = todo[ci:ci + CHUNK]
    proc = subprocess.Popen(
        ["ssh", "katago@10.0.0.17",
         f'cd /d {KD} && katago.exe analysis -config analysis_example.cfg -model {MODEL} '
         f'-override-config numAnalysisThreads=32'],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=open(f'/tmp/nnlabel_err_{ci}.log','w'),
        text=True, bufsize=1, env=env)
    def feed(rows=chunk, p=proc):
        for r in rows:
            mv = []
            for i, t in enumerate(toks[r['id']]):
                c = "b" if i % 2 == 0 else "w"
                mv.append([c, "pass" if t == "PP" else f"{COLS[int(t[0])]}{9-int(t[1])}"])
            q = {"id": r['id'], "moves": mv, "rules": "chinese", "komi": 6.5,
                 "boardXSize": 9, "boardYSize": 9, "analyzeTurns": [len(mv)],
                 "maxVisits": 2, "includePolicy": True}
            try: p.stdin.write(json.dumps(q) + "\n")
            except BrokenPipeError: return
        try: p.stdin.close()
        except Exception: pass
    threading.Thread(target=feed, daemon=True).start()
    got = 0
    for line in proc.stdout:
        try: r = json.loads(line)
        except ValueError: continue
        if 'policy' not in r: continue
        pol = {}
        for pos, p2 in enumerate(r['policy'][:81]):
            if p2 is not None and p2 > 0.002:
                pol[str(pos)] = round(p2, 5)
        out.write(json.dumps({"id": r["id"], "policy": pol}) + "\n")
        got += 1; n += 1
    out.flush()
    proc.kill()
    print(f"chunk {ci//CHUNK}: +{got} (total {n}/{len(meta)})", flush=True)
out.close()
print(f"labels done: {n}/{len(meta)} -> {outp}")
