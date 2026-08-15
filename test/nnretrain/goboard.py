# Minimal 9x9 Go board + SGF replay for the nnretrain pipeline.
# REWRITTEN 2026-08-14: the original lived in /tmp and was eaten (the
# /tmp-eats-files hazard) — this copy is committed. API kept exactly as
# extract_prior.py consumes it:
#   sgf_moves(path) -> [(color 1|2, cell 0..80 | None for pass), ...]
#   replay_to(path, t) -> Board after the first t moves
#   Board.b        -> list[81] of 0 empty / 1 black / 2 white
#   Board.play(mv, color) -> place stone, resolve captures (self-capture
#                            removes the placed group, matching engine rules)
import re

class Board:
    def __init__(self):
        self.b = [0] * 81

    def _group(self, start):
        color = self.b[start]
        seen = {start}
        stack = [start]
        libs = False
        while stack:
            p = stack.pop()
            x, y = p % 9, p // 9
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if not (0 <= nx < 9 and 0 <= ny < 9):
                    continue
                q = ny * 9 + nx
                v = self.b[q]
                if v == 0:
                    libs = True
                elif v == color and q not in seen:
                    seen.add(q)
                    stack.append(q)
        return seen, libs

    def play(self, mv, color):
        self.b[mv] = color
        opp = 3 - color
        x, y = mv % 9, mv // 9
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if not (0 <= nx < 9 and 0 <= ny < 9):
                continue
            q = ny * 9 + nx
            if self.b[q] == opp:
                grp, libs = self._group(q)
                if not libs:
                    for p in grp:
                        self.b[p] = 0
        grp, libs = self._group(mv)
        if not libs:
            for p in grp:
                self.b[p] = 0

_MV = re.compile(r";([BW])\[([a-i]?[a-i]?)\]")

def sgf_moves(path):
    s = open(path).read()
    out = []
    for c, v in _MV.findall(s):
        color = 1 if c == "B" else 2
        if len(v) != 2 or v == "tt":
            out.append((color, None))
        else:
            x = ord(v[0]) - 97
            y = ord(v[1]) - 97
            out.append((color, y * 9 + x))
    return out

def replay_to(path, t):
    bd = Board()
    for color, mv in sgf_moves(path)[:t]:
        if mv is not None:
            bd.play(mv, color)
    return bd
