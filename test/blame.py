#!/usr/bin/env python3
# Parse harness eval-mode output and rank the AI's worst moves:
# delta = (gnugo estimate after opponent's reply) - (estimate before
# the AI's move), from the AI's perspective. Big negative = blunder.
import re
import subprocess
import sys

HARNESS = "/Users/jay/workspace/ardu_go/test/harness"


def margin(est, ai_is_black):
    # est like "B+12.3" or "W+4.5"; positive = good for the AI
    m = re.match(r"([BW])\+([0-9.]+)", est)
    if not m:
        return None
    v = float(m.group(2))
    black_margin = v if m.group(1) == "B" else -v
    return black_margin if ai_is_black else -black_margin


def analyze(sgf, ai_color):
    out = subprocess.run([HARNESS, "eval", sgf], capture_output=True,
                        text=True, timeout=600).stdout
    rows = []  # (moveno, color, mv, est)
    for line in out.splitlines():
        m = re.match(r"(\d+)\s+([BW])\s+(\S+)\s+(\S+)", line)
        if m:
            rows.append((int(m.group(1)), m.group(2), m.group(3), m.group(4)))
    ai_is_black = ai_color == "B"
    blunders = []
    for i, (n, c, mv, est) in enumerate(rows):
        if c != ai_color or i == 0 or i + 1 >= len(rows):
            continue
        before = margin(rows[i - 1][3], ai_is_black)
        after = margin(rows[i + 1][3], ai_is_black)
        if before is None or after is None:
            continue
        blunders.append((after - before, n, mv, rows[i - 1][3], rows[i + 1][3]))
    blunders.sort()
    print(f"--- {sgf} (AI={ai_color}) worst moves:")
    for d, n, mv, b, a in blunders[:3]:
        print(f"  mv{n:3d} {mv:4s} delta {d:+6.1f}  ({b} -> {a})")


for arg in sys.argv[1:]:
    sgf, color = arg.split(":")
    analyze(sgf, color)
