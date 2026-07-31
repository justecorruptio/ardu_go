#!/usr/bin/env python3
# Wide-book crawler: extracts a breadth-first opening book from the
# katagobooks.org 9x9 tarball (indexed raw tar, see bookcrawl/ setup).
#
# Pages are JS-constant blobs: board[81], moves[] (policy-ordered
# symmetry-classes with xy equivalents), links{dataPos: childPage},
# linkSyms{dataPos: sym}. A page is read under a view symmetry `sym`;
# data->view is getInvSymPos (transpose first, then flips — matches
# ai.cpp applyInvSym); child view sym = compose(linkSyms[pos], sym).
#
# Expansion order is by REACH PROBABILITY (product of move policies
# along the path): bytes go to the lines opponents actually play,
# wide where play is contested, narrow where it is forced.
#
# Usage: crawl_book.py <node-budget> [depth-cap] [policy-floor]
import heapq
import itertools
import pickle
import re
import sys
import tarfile

BOOKDIR = ("/private/tmp/claude-501/-Users-jay-workspace/"
           "5b06ba38-9a1c-46e2-9ca0-ed5be982489d/scratchpad/bookcrawl")

budget = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
depthCap = int(sys.argv[2]) if len(sys.argv) > 2 else 6
floor = float(sys.argv[3]) if len(sys.argv) > 3 else 0.004

# Parsed-page cache (built by build_book_cache.py): recrawls run from this
# in seconds, no tarball needed. Falls back to the tarball per-page.
import gzip
import os
CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     "bookcache.pkl.gz")
_cache = None
if os.path.exists(CACHE):
    print("loading page cache…", flush=True)
    _cache = pickle.load(gzip.open(CACHE, "rb"))


def get_page(h):
    key = "root" if h is None else h
    if _cache is not None and key in _cache:
        return _cache[key]
    if _cache is not None:
        print(f"cache MISS {key[:12]}… tarball fallback", flush=True)
    _ensure_tar()
    return parse_page(root_html if h is None else read_page(h))


_tar_ready = False


def _ensure_tar():
    # One-time data setup (see build_book_cache.py header for the curl
    # recipe): builds index.pkl on first use, opens the tarball.
    global _tar_ready, index, tar, root_html
    if _tar_ready:
        return
    if not os.path.exists(f"{BOOKDIR}/index.pkl"):
        print("building index (one sequential pass)…", flush=True)
        idx = {}
        hashpat = re.compile(r"/([0-9A-F]{64})\.html$")
        with tarfile.open(f"{BOOKDIR}/book.tar", "r:") as tf:
            for m in tf:
                if m.isfile():
                    hm = hashpat.search(m.name)
                    if hm:
                        idx[hm.group(1)] = (m.offset_data, m.size)
                    elif m.name.endswith("root/root.html"):
                        open(f"{BOOKDIR}/special_1.html", "wb").write(
                            tf.extractfile(m).read())
        pickle.dump(idx, open(f"{BOOKDIR}/index.pkl", "wb"), protocol=4)
    print("loading index…", flush=True)
    index = pickle.load(open(f"{BOOKDIR}/index.pkl", "rb"))
    tar = open(f"{BOOKDIR}/book.tar", "rb")
    root_html = open(f"{BOOKDIR}/special_1.html").read()
    _tar_ready = True


def read_page(h):
    off, size = index[h]
    tar.seek(off)
    return tar.read(size).decode("utf-8", "replace")


def parse_page(html):
    moves = []
    mtext = re.search(r"const moves = \[(.*?)\];", html, re.S).group(1)
    for entry in re.finditer(r"\{(.*?)\},", mtext, re.S):
        e = entry.group(1)
        p = float(re.search(r"'p':([0-9.eE+-]+)", e).group(1))
        xy = re.search(r"'xy':\[\[(\d+),(\d+)\]", e)
        if xy:  # skip 'pass' entries
            moves.append((int(xy.group(1)), int(xy.group(2)), p))
    links = dict(re.findall(r"(\d+):'\.\./..\/([0-9A-F]{64})\.html'",
                            re.search(r"const links = \{(.*?)\};", html,
                                      re.S).group(1)))
    linkSyms = dict(re.findall(
        r"(\d+):(\d+)",
        re.search(r"const linkSyms = \{(.*?)\};", html, re.S).group(1)))
    return moves, links, linkSyms


def data_to_view(x, y, sym):
    # book.js draws board[getInvSymPos(viewPos)] — i.e. InvSym maps
    # view->data, so data->view is getSymPos: flips FIRST, then
    # transpose (this direction was the original crawl's bug too)
    if sym & 1:
        y = 8 - y
    if sym & 2:
        x = 8 - x
    if sym >= 4:
        x, y = y, x
    return x, y


def compose(s1, s2):
    if s1 & 4:
        s2 = (s2 & 4) | ((s2 & 2) >> 1) | ((s2 & 1) << 1)
    return s1 ^ s2


class Node:
    __slots__ = ("move", "children", "p", "page")

    def __init__(self, move, p):
        self.move = move
        self.p = p
        self.children = []
        self.page = None  # (hash, sym, depth) if a page exists



def crawl_tree(allowed=None):
    """One full crawl (main pass + reply-fill). allowed = optional set of
    trie move indices ((gy-1)*7+(gx-1)); moves outside it are skipped so
    the budget reflows into encodable lines (in-crawl constraint)."""
    root = Node(None, 1.0)
    counter = itertools.count()
    heap = [(-1.0, next(counter), root, None, 0, 0)]
    made = 0
    skipped_edge = 0
    while heap and made < budget:
        negrp, _, node, h, sym, depth = heapq.heappop(heap)
        if depth >= depthCap:
            continue
        moves, links, linkSyms = get_page(h)
        for rank, (dx, dy, p) in enumerate(moves):
            dataPos = str(dy * 9 + dx)
            if dataPos not in links:
                continue
            minKeep = 6 if depth == 0 else 10
            if p < floor and rank >= minKeep:
                continue
            gx, gy = data_to_view(dx, dy, sym)
            if not (1 <= gx <= 7 and 1 <= gy <= 7):
                skipped_edge += 1
                continue
            mv = (gy - 1) * 7 + (gx - 1)
            if allowed is not None and mv not in allowed:
                continue
            child = Node((gx, gy), p)
            node.children.append(child)
            made += 1
            csym = compose(int(linkSyms[dataPos]), sym)
            child.page = (links[dataPos], csym, depth + 1)
            heapq.heappush(heap, (negrp * max(p, 1e-4), next(counter),
                                  child, links[dataPos], csym, depth + 1))
    # reply-fill: every stored move gets at least its best (in-set) reply
    def replyfill(node):
        filled = 0
        for ch in node.children:
            filled += replyfill(ch)
        if not node.children and node.page:
            h, sym, depth = node.page
            if depth < depthCap:
                moves, links, linkSyms = get_page(h)
                for dx, dy, p in moves:
                    gx, gy = data_to_view(dx, dy, sym)
                    if 1 <= gx <= 7 and 1 <= gy <= 7:
                        mv = (gy - 1) * 7 + (gx - 1)
                        if allowed is None or mv in allowed:
                            node.children.append(Node((gx, gy), p))
                            return filled + 1
        return filled
    replyfill(root)
    return root, made, skipped_edge


def validate(node, board, color):
    for ch in node.children:
        x, y = ch.move
        assert board.get((x, y)) is None, f"stone collision at {ch.move}"
        board[(x, y)] = color
        validate(ch, board, 3 - color)
        del board[(x, y)]


def trie_moves(root):
    from collections import Counter as _C
    heat = _C()
    def walk(n):
        for k in n.children:
            heat[(k.move[1] - 1) * 7 + (k.move[0] - 1)] += 1
            walk(k)
    walk(root)
    return heat


# ---- pass 1: probe crawl chooses the 32-point table ----
probe, made1, _ = crawl_tree()
heat = trie_moves(probe)
POINTS = [m for m, _ in heat.most_common(32)]
print(f"probe: {made1} nodes, {len(heat)} distinct points; top-32 chosen "
      f"(tail dropped: {sum(c for m, c in heat.items() if m not in set(POINTS))} uses)")

# ---- pass 2: constrained crawl ----
root, made, skipped_edge = crawl_tree(set(POINTS))
validate(root, {}, 1)
print(f"constrained: {made} nodes made; validation ok")

# ---- anneal the point-table order to minimize gap bits ----
def all_tails(node, isroot, out):
    if len(node.children) > 1 and not isroot:
        out.append([(k.move[1] - 1) * 7 + (k.move[0] - 1)
                    for k in node.children[1:]])
    for k in node.children:
        all_tails(k, False, out)

tails = []
all_tails(root, True, tails)

def gammalen(g):
    return 1 if g == 1 else 3 if g <= 3 else 5 if g <= 7 else 8

def gapcost(perm):
    idx = {m: i for i, m in enumerate(perm)}
    t = 0
    for tl in tails:
        prev = -1
        for s in sorted(idx[m] for m in tl):
            t += gammalen(s - prev)
            prev = s
    return t

import random as _rnd
_rnd.seed(7)
cur = POINTS[:]
cC = gapcost(cur)
bC, best = cC, cur[:]
T = cC * 0.002
for _ in range(30000):
    i, j = _rnd.randrange(32), _rnd.randrange(32)
    cur[i], cur[j] = cur[j], cur[i]
    c = gapcost(cur)
    import math as _m
    if c < cC or _rnd.random() < _m.exp((cC - c) / max(T, 1e-9)):
        cC = c
        if c < bC:
            bC, best = c, cur[:]
    else:
        cur[i], cur[j] = cur[j], cur[i]
    T *= 0.9997
POINTS = best
PIDX = {m: i for i, m in enumerate(POINTS)}
print(f"anneal: gap bits {bC}")

# ---- emit the v2 bitstream ----
# Bit order: LSB-first within each byte. Node encodings:
#   absolute (root siblings + every group's first child):
#       IDX[5] LEAF[1] LAST[1]
#   tail sibling (sorted ascending by table idx, gap from prev, start -1):
#       gamma gap: '0'=1 | '1''0'x = 2+x | '1''1''0'xx = 4+xx |
#                  '1''1''1'xxxxx = 8+val   (bits read LSB-first, flags after)
#       then LEAF[1] LAST[1]
# A node's children follow immediately (first child absolute).
bits = []
def put(v, n):
    for k in range(n):
        bits.append((v >> k) & 1)

def putgap(g):
    if g == 1:
        put(0, 1)
    elif g <= 3:
        put(1, 1); put(0, 1); put(g - 2, 1)
    elif g <= 7:
        put(1, 1); put(1, 1); put(0, 1); put(g - 4, 2)
    else:
        put(1, 1); put(1, 1); put(1, 1); put(g - 8, 5)

def emit_group(kids, isroot):
    # first child (or all root children) absolute; tails sorted+gap-coded
    ordered = kids if isroot else         [kids[0]] + sorted(kids[1:], key=lambda k: PIDX[(k.move[1]-1)*7+(k.move[0]-1)])
    prev = -1
    for i, k in enumerate(ordered):
        idx = PIDX[(k.move[1]-1)*7+(k.move[0]-1)]
        leaf = 0 if k.children else 1
        last = 1 if i == len(ordered) - 1 else 0
        if isroot or i == 0:
            put(idx, 5)
        else:
            putgap(idx - prev)
        if not isroot and i >= 1:
            prev = idx
        elif not isroot and i == 0:
            prev = -1     # first child not part of the sorted chain
        put(leaf, 1)
        put(last, 1)
        if k.children:
            emit_group(k.children, False)

# root is a NORMAL group (first=value-best, tails sorted): the weights
# array is emitted in the same order, so no special root layout is needed
emit_group(root.children, False)
stream = bytearray((len(bits) + 7) // 8 + 1)   # +1 pad: walker reads 2 bytes
for i, b in enumerate(bits):
    stream[i >> 3] |= b << (i & 7)

# root weights in EMITTED sibling order (first, then table-sorted tails)
_rk = root.children
_ordered = [_rk[0]] + sorted(_rk[1:],
    key=lambda k: PIDX[(k.move[1]-1)*7+(k.move[0]-1)])
ps = [c.p for c in _ordered]
# shipped semantics: raw policy x255 (NOT max-normalized) -- keeps the
# historical pick distribution bit-for-bit ({51,23,5,2,1,1} today)
weights = [max(1, int(round(p * 255))) for p in ps]
wrow = ", ".join(str(w) for w in weights)
prow = ", ".join(str(m) for m in POINTS)
rows = "\n".join("    " + ", ".join(f"0x{b:02X}" for b in stream[o:o+16]) + ","
                 for o in range(0, len(stream), 16))

def count_nodes(n):
    return len(n.children) + sum(count_nodes(k) for k in n.children)
total = count_nodes(root)

open("../opening_book.h", "w").write(f"""#pragma once
#include <avr/pgmspace.h>

// KataGo 9x9 Japanese-rules opening book, WIDE CRAWL, v2 BITSTREAM.
// depth <= {depthCap}, policy floor {floor}, budget {budget}, 32-point
// in-crawl constraint. {total} nodes in {len(stream)} bytes
// ({8*len(stream)/total:.2f} bits/node). Regenerate: test/crawl_book.py.
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
print(f"header written: {total} nodes, {len(stream)} bytes "
      f"({8*len(stream)/total:.2f} bits/node)")

# ---- JSON sidecar for walker verification ----
import json
def dump(n):
    return {"m": (n.move[1]-1)*7+(n.move[0]-1) if n.move else -1,
            "k": [dump(c) for c in n.children]}
json.dump({"points": POINTS, "weights": weights,
           "tree": [dump(c) for c in _ordered]},
          open("book_v2_ref.json", "w"))
print("sidecar book_v2_ref.json written")
