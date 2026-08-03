#!/usr/bin/env python3
# Corpus-aware book extension: graft gnugo-L0's actual replies onto the
# shipped KataGo book (from book_v2_ref.json), answering each grafted
# reply with KataGo's best move. Pure data change: the on-device walker
# (notifyMove/bookLookup) follows the wider tree as-is.
#
# Diagnosis this serves (KataGo corpus 2026-08): median book exit is
# ply 3 (gnugo deviates off KataGo lines), and the engine's worst moves
# of the whole game are the first post-book plies (t=6: half of moves
# lose >=10% winrate). Position repetition is high (top-50 positions =
# 89% of games at ply 4), so a few hundred grafted nodes cover most
# real games through the catastrophic window.
#
# Usage: graft_book.py [added-bits-budget] [depth-cap] [seeds]
#
# VERDICT (2026-08, paired 1000): the gnugo-graft CEILING arm — near-
# perfect coverage of gnugo-L0's real reply tree to ply 5-6, KataGo
# answers — measured 197 vs 198 (disc 48/49): book coverage converts
# ZERO games vs this referee. Opening equity losses are vs the oracle;
# gnugo cannot cash them, games are decided later. Direction fenced;
# the policy-mode book (+90 nodes, /tmp/opening_book_policy.h) was
# never gauntleted (moot). Kept as tooling for any future opponent
# where opening coverage matters.
#
# GRAFT_MODE=gnugo   branches = gnugo-L0 sampled replies (REFEREE-COUPLED:
#                    a ceiling diagnostic only, never ship — it overfits
#                    the gauntlet metric, Jay 2026-08)
# GRAFT_MODE=policy  branches = KataGo policy top moves (general: covers
#                    what any reasonable opponent plays; the shippable
#                    candidate)
import json, subprocess, sys, os, collections, itertools, heapq

MODE = os.environ.get('GRAFT_MODE', 'policy')
POLICY_FLOOR = 0.04
POLICY_TOPK = 5

HERE = os.path.dirname(os.path.abspath(__file__))
SP = '/private/tmp/claude-501/-Users-jay-workspace/5b06ba38-9a1c-46e2-9ca0-ed5be982489d/scratchpad'
BUDGET_BITS = int(sys.argv[1]) if len(sys.argv) > 1 else 3400   # ~425B
DEPTH_CAP = int(sys.argv[2]) if len(sys.argv) > 2 else 6
NSEEDS = int(sys.argv[3]) if len(sys.argv) > 3 else 16
KEEP = 2                     # keep replies seen >= KEEP of NSEEDS
KATA_VISITS = 200
COLS = "ABCDEFGHJ"

ref = json.load(open(os.path.join(HERE, 'book_v2_ref.json')))
POINTS = ref['points']; WEIGHTS = ref['weights']
PSET = set(POINTS)

class Node:
    __slots__ = ("move", "children", "grafted")
    def __init__(self, move):
        self.move = move          # trie idx 0..48 ((gy-1)*7+(gx-1))
        self.children = []
        self.grafted = False

def load(d):
    n = Node(d['m'])
    n.children = [load(k) for k in d['k']]
    return n
root = Node(-1)
root.children = [load(d) for d in ref['tree']]

def m2xy(m):                      # trie idx -> 0-based board (x,y)
    return (m % 7 + 1, m // 7 + 1)
def xy2m(x, y):
    if not (1 <= x <= 7 and 1 <= y <= 7): return None
    return (y - 1) * 7 + (x - 1)
def gtp(m):
    x, y = m2xy(m)
    return f"{COLS[x]}{9 - y}"
def gtp2m(s):
    s = s.strip().upper()
    if s in ('PASS', 'RESIGN'): return None
    x = COLS.index(s[0]); y = 9 - int(s[1:])
    return xy2m(x, y)

def symxy(x, y, s):               # matches ai.cpp applySym on 0..8 coords
    if s & 1: x = 8 - x
    if s & 2: y = 8 - y
    if s & 4: x, y = y, x
    return x, y
def symm(m, s):
    x, y = m2xy(m)
    x, y = symxy(x, y, s)
    return xy2m(x, y)

def stabilizer(path):             # syms mapping the position to itself
    stones = [m2xy(m) for m in path]
    out = []
    for s in range(8):
        ok = True
        for i, (x, y) in enumerate(stones):
            # colors alternate by index; a stabilizer must fix each
            # stone set per color
            pass
        bl = sorted((x, y) for i, (x, y) in enumerate(stones) if i % 2 == 0)
        wh = sorted((x, y) for i, (x, y) in enumerate(stones) if i % 2 == 1)
        bs = sorted(symxy(x, y, s) for x, y in bl)
        ws = sorted(symxy(x, y, s) for x, y in wh)
        if bs == bl and ws == wh: out.append(s)
    return out

# ---- gnugo reply sampler (persistent gtp procs, one per seed) ----
SEEDS = [1001 + i * 1200 // NSEEDS for i in range(NSEEDS)]
procs = {}
def gp(seed):
    if seed not in procs:
        procs[seed] = subprocess.Popen(
            ["gnugo", "--mode", "gtp", "--boardsize", "9", "--komi", "6.5",
             "--level", "0", "--seed", str(seed)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True, bufsize=1)
    return procs[seed]
def gcmd(p, cmd):
    p.stdin.write(cmd + "\n")
    out = []
    while True:
        l = p.stdout.readline()
        if not l: raise RuntimeError("gnugo died")
        if l.strip() == "" and out: break
        if l.strip(): out.append(l.strip())
    return out[-1]
def gnugo_replies(path):
    """Sample gnugo's reply distribution at the position (tree coords).
    Returns {move: prob}; replies below KEEP/NSEEDS are dropped."""
    mover = 'black' if len(path) % 2 == 0 else 'white'
    freq = collections.Counter()
    for seed in SEEDS:
        p = gp(seed)
        gcmd(p, "clear_board")
        for i, m in enumerate(path):
            gcmd(p, f"play {'black' if i % 2 == 0 else 'white'} {gtp(m)}")
        r = gcmd(p, f"genmove {mover}")
        m = gtp2m(r.split()[-1])
        if m is not None: freq[m] += 1
    return {m: c / NSEEDS for m, c in freq.items() if c >= KEEP}

# ---- katago answerer ----
kata = subprocess.Popen(
    ["katago", "analysis", "-config", SP + "/kata_analysis.cfg",
     "-model", SP + "/kata_b6.txt.gz"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
    stderr=subprocess.DEVNULL, text=True, bufsize=1)
qid = itertools.count()
def kata_query(path, visits, want_policy=False):
    moves = [["B" if i % 2 == 0 else "W", gtp(m)] for i, m in enumerate(path)]
    q = {"id": str(next(qid)), "moves": moves, "rules": "chinese",
         "komi": 6.5, "boardXSize": 9, "boardYSize": 9,
         "analyzeTurns": [len(moves)], "maxVisits": visits}
    if want_policy: q["includePolicy"] = True
    kata.stdin.write(json.dumps(q) + "\n")
    while True:
        r = json.loads(kata.stdout.readline())
        if r.get("id") == q["id"]:
            return r

def kata_best(path):
    """KataGo best in-table answer at the position (tree coords)."""
    r = kata_query(path, KATA_VISITS)
    occupied = set(path)
    for mi in r.get("moveInfos", []):
        m = gtp2m(mi["move"]) if mi["move"].upper() != "PASS" else None
        if m is not None and m in PSET and m not in occupied:
            return m
    return None

def policy_replies(path):
    """KataGo policy distribution (interior, in-table) at the position."""
    r = kata_query(path, 8, want_policy=True)
    pol = r.get("policy") or []
    occupied = set(path)
    cand = []
    for pos, p in enumerate(pol[:81]):
        if p is None or p <= 0: continue
        m = xy2m(pos % 9, pos // 9)
        if m is None or m not in PSET or m in occupied: continue
        cand.append((p, m))
    cand.sort(reverse=True)
    cand = [(p, m) for p, m in cand[:POLICY_TOPK] if p >= POLICY_FLOOR]
    tot = sum(p for p, _ in cand) or 1.0
    return {m: p / tot for p, m in cand}

# ---- expansion: heap by reach probability ----
# flow_ai(node): P(position occurs AND engine to move next) -> engine
#   plays node's first child (must be strong).
# flow_opp(node): P(position occurs AND gnugo to move next) -> graft
#   gnugo's replies as children.
added_bits = [0]
added_nodes = [0]
def bits_of(n_children_pos):
    return 7                       # abs node 7 bits; tails ~<=7; use avg

counter = itertools.count()
heap = []
# root: engine-black games pick root child by weights; gnugo-black games
# open with gnugo's move (sampled at empty board).
tw = sum(WEIGHTS)
w_of = {id(k): w for k, w in zip(root.children, WEIGHTS)}
for k, w in zip(root.children, WEIGHTS):
    heapq.heappush(heap, (-(0.5 * w / tw), next(counter), k, [k.move], 'opp'))
replies = gnugo_replies if MODE == 'gnugo' else policy_replies
gfreq = replies([])
kmap0 = {}
for k in root.children:
    for s in range(8):
        sm = symm(k.move, s)
        if sm is not None: kmap0[sm] = k
for m, c in gfreq.items():
    k = kmap0.get(m)
    if k is None:
        cm = min(x for x in (symm(m, s) for s in range(8)) if x is not None)
        if cm not in PSET or added_bits[0] > BUDGET_BITS: continue
        k = Node(cm); k.grafted = True
        root.children.append(k)
        w_of[id(k)] = 1
        for s in range(8):
            sm = symm(cm, s)
            if sm is not None: kmap0[sm] = k
        added_bits[0] += 7; added_nodes[0] += 1
    heapq.heappush(heap, (-(0.5 * c), next(counter), k, [k.move], 'ai'))

processed = 0
while heap and added_bits[0] < BUDGET_BITS:
    negp, _, node, path, side = heapq.heappop(heap)
    p = -negp
    if len(path) >= DEPTH_CAP or p < 0.004: continue
    processed += 1
    if side == 'ai':
        # engine to move: ensure a strong first child exists
        if not node.children:
            best = kata_best(path)
            if best is None: continue
            nk = Node(best); nk.grafted = True
            node.children.append(nk)
            added_bits[0] += 7; added_nodes[0] += 1
        ch = node.children[0]
        heapq.heappush(heap, (-p, next(counter), ch, path + [ch.move], 'opp'))
    else:
        # gnugo to move: graft its sampled replies
        freq = replies(path)
        stab = stabilizer(path)
        canon = collections.Counter()
        for m, c in freq.items():
            cs = [symm(m, s) for s in stab]
            cs = [x for x in cs if x is not None]
            canon[min(cs) if cs else m] += c
        kmap = {}
        for k in node.children:
            for s in stab:
                sm = symm(k.move, s)
                if sm is not None: kmap[sm] = k
        for m, c in canon.items():
            if m not in kmap and c <= 0: continue
            k = kmap.get(m)
            if k is None:
                if m not in PSET or m in path or added_bits[0] > BUDGET_BITS:
                    continue
                k = Node(m); k.grafted = True
                node.children.append(k)
                added_bits[0] += 7; added_nodes[0] += 1
                kmap[m] = k
            heapq.heappush(heap, (-(p * c), next(counter),
                                  k, path + [k.move], 'ai'))
    if processed % 25 == 0:
        print(f"  processed {processed}, added {added_nodes[0]} nodes "
              f"(~{added_bits[0]//8}B)", flush=True)

for p in procs.values(): p.kill()
kata.kill()
print(f"grafting done: +{added_nodes[0]} nodes (~{added_bits[0]//8}B), "
      f"{processed} expansions")

# ---- emit v2 bitstream (same encoding as crawl_book.py) ----
PIDX = {m: i for i, m in enumerate(POINTS)}
bits = []
def put(v, n):
    for k in range(n): bits.append((v >> k) & 1)
def putgap(g):
    if g == 1: put(0, 1)
    elif g <= 3: put(1, 1); put(0, 1); put(g - 2, 1)
    elif g <= 7: put(1, 1); put(1, 1); put(0, 1); put(g - 4, 2)
    else: put(1, 1); put(1, 1); put(1, 1); put(g - 8, 5)
def emit_group(kids, isroot):
    ordered = kids if isroot else [kids[0]] + sorted(kids[1:], key=lambda k: PIDX[k.move])
    prev = -1
    for i, k in enumerate(ordered):
        idx = PIDX[k.move]
        leaf = 0 if k.children else 1
        last = 1 if i == len(ordered) - 1 else 0
        if isroot or i == 0: put(idx, 5)
        else: putgap(idx - prev)
        if not isroot and i >= 1: prev = idx
        put(leaf, 1)
        put(last, 1)
        if k.children: emit_group(k.children, False)

# root is a NORMAL group like the shipped book: first child stays first
# (value-best), tails sorted by table idx; weights follow emitted order
ordered_root = [root.children[0]] + sorted(root.children[1:],
                                           key=lambda k: PIDX[k.move])
root.children = ordered_root
WEIGHTS = [w_of[id(k)] for k in ordered_root]
emit_group(root.children, False)
stream = bytearray((len(bits) + 7) // 8 + 1)
for i, b in enumerate(bits): stream[i >> 3] |= b << (i & 7)

def count_nodes(n):
    return len(n.children) + sum(count_nodes(k) for k in n.children)
total = count_nodes(root)
wrow = ", ".join(str(w) for w in WEIGHTS)
prow = ", ".join(str(m) for m in POINTS)
rows = "\n".join("    " + ", ".join(f"0x{b:02X}" for b in stream[o:o+16]) + ","
                 for o in range(0, len(stream), 16))
open(os.path.join(HERE, "../opening_book.h"), "w").write(f"""#pragma once
#include <avr/pgmspace.h>

// KataGo 9x9 opening book, WIDE CRAWL + GNUGO-REPLY GRAFT, v2 BITSTREAM.
// Base: shipped wide crawl (depth<=8, floor 0.0015, budget 2000).
// Graft mode {MODE}: opponent branches {'sampled from gnugo-L0 (DIAGNOSTIC ONLY)' if MODE=='gnugo' else 'from KataGo policy (general)'},
// answered by KataGo b6c96 @{KATA_VISITS} visits, depth cap {DEPTH_CAP}, +{added_nodes[0]} nodes.
// {total} nodes in {len(stream)} bytes ({8*len(stream)/total:.2f} bits/node).
// Regenerate: test/graft_book.py (needs book_v2_ref.json + gnugo + katago).
//
// Bit order: LSB-first within each byte. Nodes, DFS, children follow
// their parent immediately:
//   absolute node (all root siblings + every group's first child):
//     IDX[5] LEAF[1] LAST[1]           (IDX -> BOOK_POINTS)
//   tail sibling (rest of a group, sorted ascending by IDX; gap from
//   previous tail, first gap counted from -1):
//     gamma gap ('0'=1, '10'+1b=2..3, '110'+2b=4..7, '111'+5b=8..39)
//     then LEAF[1] LAST[1]
// First child = highest-policy move (the sorted tail is match-only).

#define OPENING_BOOK_BITS {len(bits)}
#define OPENING_BOOK_NODES {total}
#define OPENING_BOOK_ROOT_OPTIONS {len(root.children)}

// table index -> interior-7x7 move ((y-1)*7+(x-1)), anneal-ordered
PROGMEM const uint8_t BOOK_POINTS[32] = {{{prow}}};

// Weights for choosing the first move when AI plays Black,
// in root-child order (KataGo policy scaled to 0-255)
PROGMEM const uint8_t OPENING_ROOT_WEIGHTS[] = {{{wrow}}};

PROGMEM const uint8_t OPENING_BOOK_TRIE[] = {{
{rows}
}};
""")
print(f"header written: {total} nodes, {len(stream)} bytes")

def dump(n):
    return {"m": n.move, "k": [dump(c) for c in n.children]}
json.dump({"points": POINTS, "weights": WEIGHTS,
           "tree": [dump(c) for c in root.children]},
          open(os.path.join(HERE, "book_v2_ref.json.new"), "w"))
print("sidecar book_v2_ref.json.new written")
