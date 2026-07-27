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

# One-time data setup (scratchpad is session-scoped; rebuild at will):
#   mkdir -p $BOOKDIR && cd $BOOKDIR
#   curl -sL -o book.tar.gz \
#     https://katagobooks.org/downloads/book9x9jp-20260226.tar.gz
#   gunzip -kc book.tar.gz > book.tar
#   then run this script — it builds index.pkl itself if missing.
import os
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


root = Node(None, 1.0)
counter = itertools.count()
# heap entries: (-reachP, tiebreak, parentNode, pageHash|None(root), sym, depth)
heap = [(-1.0, next(counter), root, None, 0, 0)]
made = 0
skipped_edge = 0

while heap and made < budget:
    negrp, _, node, h, sym, depth = heapq.heappop(heap)
    if depth >= depthCap:
        continue
    html = root_html if h is None else read_page(h)
    moves, links, linkSyms = parse_page(html)
    for rank, (dx, dy, p) in enumerate(moves):
        dataPos = str(dy * 9 + dx)
        # Only classes with an explored child page are worth storing:
        # a move without a reply is a worthless leaf (matching it
        # just kills the book with no suggestion). NOTE the page
        # lists are VALUE-ordered, not policy-ordered — first entry
        # is the best move (what first-child-best needs), so rank is
        # the site's own quality order and the policy floor is only
        # a backstop against the deep junk tail.
        if dataPos not in links:
            continue
        minKeep = 6 if depth == 0 else 10
        if p < floor and rank >= minKeep:
            continue
        gx, gy = data_to_view(dx, dy, sym)
        if not (1 <= gx <= 7 and 1 <= gy <= 7):
            skipped_edge += 1
            continue  # trie encoding is interior-only
        child = Node((gx, gy), p)
        node.children.append(child)
        made += 1
        csym = compose(int(linkSyms[dataPos]), sym)
        child.page = (links[dataPos], csym, depth + 1)
        heapq.heappush(heap, (negrp * max(p, 1e-4), next(counter),
                              child, links[dataPos], csym, depth + 1))

print(f"main pass: {made} nodes, frontier {len(heap)}")

# ---- reply-fill: a leaf book node is WORTHLESS in play (matching
# the opponent's move against a childless node kills the book with
# no suggestion), so every stored move gets at least its best reply.
def replyfill(node):
    global made
    filled = 0
    for ch in node.children:
        filled += replyfill(ch)
    if not node.children and node.page:
        h, sym, depth = node.page
        if depth < depthCap:
            moves, links, linkSyms = parse_page(read_page(h))
            for dx, dy, p in moves:
                gx, gy = data_to_view(dx, dy, sym)
                if 1 <= gx <= 7 and 1 <= gy <= 7:
                    node.children.append(Node((gx, gy), p))
                    made += 1
                    filled += 1
                    break
    return filled

print("reply-filled", replyfill(root), "leaves;",
      f"total {made} nodes, edge-skipped {skipped_edge}")

# ---- validation: replay every line, stones must land on empty ----
def validate(node, board, color):
    for ch in node.children:
        x, y = ch.move
        assert board.get((x, y)) is None, f"stone collision at {ch.move}"
        board[(x, y)] = color
        validate(ch, board, 3 - color)
        del board[(x, y)]

validate(root, {}, 1)
print("validation: all lines replay onto empty points")

# ---- root sanity: must start with the classic six ----
print("root groups:", [(c.move, round(c.p, 4)) for c in root.children[:8]])

# ---- emit SGF (provenance) and trie header ----
def sgf(node, color, out):
    first = True
    multi = len(node.children) > 1
    for ch in node.children:
        x, y = ch.move
        mv = f";{'BW'[color - 1]}[{chr(97 + x)}{chr(97 + y)}]"
        if multi:
            out.append("(")
        out.append(mv)
        sgf(ch, 3 - color, out)
        if multi:
            out.append(")")
        first = False

out = ["(;GM[1]FF[4]SZ[9]AP[KataGo-wide-crawl]KM[6.5]"]
sgf(root, 1, out)
out.append(")")
open("../opening_book.sgf", "w").write("".join(out))

def emit(node, out):
    n = len(node.children)
    for i, ch in enumerate(node.children):
        x, y = ch.move
        b = (y - 1) * 7 + (x - 1)
        if not ch.children:
            b |= 0x80
        if i == n - 1:
            b |= 0x40
        out.append(b)
        emit(ch, out)

trie = []
emit(root, trie)
weights = [max(1, min(255, round(c.p * 255))) for c in root.children]
print(f"trie {len(trie)} bytes, {len(root.children)} root options, "
      f"weights {weights}")

rows = ",\n    ".join(
    ", ".join(f"0x{b:02X}" for b in trie[i:i + 12])
    for i in range(0, len(trie), 12))
wrow = ", ".join(str(w) for w in weights)

open("../opening_book.h", "w").write(f"""#pragma once
#include <avr/pgmspace.h>

// KataGo 9x9 Japanese-rules opening book, WIDE CRAWL: nodes expanded
// by reach probability (product of policy along the path) from the
// katagobooks.org book, depth <= {depthCap}, policy floor {floor}.
// Wide where play is contested, narrow where it is forced — built to
// answer offbeat human moves, not to follow long engine lines.
// Regenerate with test/crawl_book.py (needs the indexed tarball).
// Trie, DFS order, 1 byte per node:
//   bit7 = leaf (no children), bit6 = last sibling
//   bits5-0 = move index on interior 7x7: (y-1)*7 + (x-1)
// A node's children follow it immediately; children are sorted
// by KataGo policy, so first child = best move.
// {len(trie)} nodes. Root has {len(root.children)} first-move options.

#define OPENING_BOOK_TRIE_SIZE {len(trie)}
#define OPENING_BOOK_ROOT_OPTIONS {len(root.children)}

// Weights for choosing the first move when AI plays Black,
// in root-child order (KataGo policy scaled to 0-255)
PROGMEM const uint8_t OPENING_ROOT_WEIGHTS[] = {{{wrow}}};

PROGMEM const uint8_t OPENING_BOOK_TRIE[] = {{
    {rows}
}};
""")
print("header written")
