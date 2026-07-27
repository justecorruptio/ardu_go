#!/usr/bin/env python3
# Prune the opening book to a summed policy-rank budget and
# regenerate opening_book.h from opening_book.sgf (child order in the
# SGF is KataGo policy order — first child = best move — which the
# walker relies on, so order is preserved verbatim).
#
# Usage: prune_book.py [rank-budget]   (default 5)
import re
import sys

SGF = "../opening_book.sgf"
HEADER = "../opening_book.h"


class Node:
    __slots__ = ("move", "children")

    def __init__(self, move):
        self.move = move  # (x, y) 0-based, or None for root
        self.children = []


def parse_sgf(text):
    # Recursive descent over ( ; ) tokens. Variations at the same
    # level are ordered children.
    pos = 0

    def parse_seq(parent):
        nonlocal pos
        cur = parent
        while pos < len(text):
            c = text[pos]
            if c == "(":
                pos += 1
                parse_seq(cur)
            elif c == ")":
                pos += 1
                return
            elif c == ";":
                m = re.match(r";([BW])\[([a-i])([a-i])\]", text[pos:])
                if m:
                    x = ord(m.group(2)) - ord("a")
                    y = ord(m.group(3)) - ord("a")
                    node = Node((x, y))
                    cur.children.append(node)
                    cur = node
                    pos += m.end()
                else:
                    # root property node — skip to next structural char
                    m2 = re.match(r";(?:[A-Z]+\[[^\]]*\])+", text[pos:])
                    pos += m2.end() if m2 else 1
            else:
                pos += 1

    root = Node(None)
    parse_seq(root)
    # unwrap: the outermost () wraps the game; root's first child layer
    # may include the property node chain — moves are already attached
    return root


def prune(node, depth, cost, budget):
    # Keep a node while the summed policy rank along its path stays
    # within budget (first child costs 0, k-th child costs k-1), the
    # depth stays <= 10, and the move is interior (the trie encoding
    # cannot express first-line moves; the original book dropped them
    # too). Root options are rank-free: the weight table plays them.
    kept = []
    for i, ch in enumerate(node.children):
        x, y = ch.move
        if not (1 <= x <= 7 and 1 <= y <= 7):
            continue
        ccost = cost + (0 if depth == 0 else i)
        if ccost > budget or depth + 1 > 10:
            continue
        prune(ch, depth + 1, ccost, budget)
        kept.append(ch)
    node.children = kept


def count(node):
    return sum(1 + count(c) for c in node.children)


def emit(node, out):
    n = len(node.children)
    for i, ch in enumerate(node.children):
        x, y = ch.move
        assert 1 <= x <= 7 and 1 <= y <= 7, f"non-interior move {ch.move}"
        b = (y - 1) * 7 + (x - 1)
        if not ch.children:
            b |= 0x80
        if i == n - 1:
            b |= 0x40
        out.append(b)
        emit(ch, out)


def reparse(data):
    # Round-trip check: decode the emitted trie back into a tree
    pos = 0

    def parse_children(parent):
        nonlocal pos
        while True:
            b = data[pos]
            pos += 1
            idx = b & 0x3F
            node = Node((idx % 7 + 1, idx // 7 + 1))
            parent.children.append(node)
            if not (b & 0x80):
                parse_children(node)
            if b & 0x40:
                return

    root = Node(None)
    if data:
        parse_children(root)
    return root


def same(a, b):
    if a.move != b.move or len(a.children) != len(b.children):
        return False
    return all(same(x, y) for x, y in zip(a.children, b.children))


budget = int(sys.argv[1]) if len(sys.argv) > 1 else 5
tree = parse_sgf(open(SGF).read())
full = count(tree)
prune(tree, 0, 0, budget)
kept = count(tree)
assert len(tree.children) == 6, f"root options: {len(tree.children)}"

out = []
emit(tree, out)
assert same(tree, reparse(out)), "round-trip mismatch"
print(f"budget {budget}: {full} -> {kept} nodes ({len(out)} bytes)")

# Preserve the existing root weights verbatim
old = open(HEADER).read()
weights = re.search(r"(PROGMEM const uint8_t OPENING_ROOT_WEIGHTS\[\][^;]+;)",
                    old).group(1)

rows = ",\n    ".join(
    ", ".join(f"0x{b:02X}" for b in out[i:i + 12])
    for i in range(0, len(out), 12))

open(HEADER, "w").write(f"""#pragma once
#include <avr/pgmspace.h>

// KataGo 9x9 Japanese-rules opening book, moves 1-10, PRUNED to a
// summed policy-rank budget of {budget} along each path (first child
// free, k-th child costs k-1): full breadth near the root, junk-rank
// tails cut. Regenerate with test/prune_book.py from opening_book.sgf.
// Trie, DFS order, 1 byte per node:
//   bit7 = leaf (no children), bit6 = last sibling
//   bits5-0 = move index on interior 7x7: (y-1)*7 + (x-1)
// A node's children follow it immediately; children are sorted
// by KataGo policy, so first child = best move.
// {len(out)} nodes. Root has 6 first-move options.

#define OPENING_BOOK_TRIE_SIZE {len(out)}
#define OPENING_BOOK_ROOT_OPTIONS 6

// Weights for choosing the first move when AI plays Black,
// in root-child order (KataGo policy scaled to 0-255)
{weights}

PROGMEM const uint8_t OPENING_BOOK_TRIE[] = {{
    {rows}
}};
""")
print("header written")
