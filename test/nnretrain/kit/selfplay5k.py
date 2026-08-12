#!/usr/bin/env python3
# 5k-vs-5k human-SL self-play via the 4090 GTP shim: one katago process plays
# both colors. Writes harness-format SGFs (game_<n>.sgf).
#   selfplay5k.py <outdir> <first_game_no> <n_games> [profile]
import subprocess, sys, os, re, time

OUT, G0, N = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
PROFILE = sys.argv[4] if len(sys.argv) > 4 else "preaz_5k"
os.makedirs(OUT, exist_ok=True)

def gtp(proc, cmd):
    proc.stdin.write(cmd + "\n"); proc.stdin.flush()
    out = []
    while True:
        line = proc.stdout.readline()
        if not line: raise RuntimeError(f"engine died on {cmd!r}")
        line = line.rstrip("\n")
        if line == "" and out: break
        if line: out.append(line)
    r = " ".join(out)
    if not r.startswith("="): raise RuntimeError(f"{cmd!r} -> {r!r}")
    return r[1:].strip()

COLS = "ABCDEFGHJ"
def to_sgf(v):
    if v.lower() == "pass": return ""
    x = COLS.index(v[0].upper()); y = 9 - int(v[1:])
    return chr(97 + x) + chr(97 + y)

for g in range(G0, G0 + N):
    ovr = (f"maxVisits=1,humanSLProfile={PROFILE},humanSLChosenMoveProp=1.0,"
           "humanSLCpuctExploration=0.50,chosenMoveTemperature=0.70,"
           "chosenMoveTemperatureEarly=0.85,chosenMoveTemperatureHalflife=80,"
           "chosenMoveTemperatureOnlyBelowProb=0.01,rules=chinese,"
           f"allowResignation=false,numSearchThreads=1,searchRandSeed={g}")
    proc = subprocess.Popen(["/tmp/shimbin/katago", "gtp", "-override-config", ovr],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, text=True, bufsize=1)
    try:
        gtp(proc, "boardsize 9"); gtp(proc, "komi 6.5"); gtp(proc, "clear_board")
        moves = []
        passes = 0
        color = "b"
        while passes < 2 and len(moves) < 120:
            mv = gtp(proc, f"genmove {color}")
            if mv.lower() == "resign":
                break
            moves.append((color.upper(), to_sgf(mv)))
            passes = passes + 1 if mv.lower() == "pass" else 0
            color = "w" if color == "b" else "b"
        sgf = (f"(;GM[1]FF[4]SZ[9]KM[6.5]PB[{PROFILE}]PW[{PROFILE}]"
               + "".join(f";{c}[{v}]" for c, v in moves) + ")")
        open(os.path.join(OUT, f"game_{g}.sgf"), "w").write(sgf)
        print(f"game {g}: {len(moves)} moves", flush=True)
    finally:
        try: gtp(proc, "quit")
        except Exception: pass
        proc.kill()
