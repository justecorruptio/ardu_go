#!/usr/bin/env python3
# Regenerate the <!--KATA-->...<!--/KATA--> section of blunder_hist.html from kata_hist.json
import json, sys, re

SP = '/private/tmp/claude-501/-Users-jay-workspace/5b06ba38-9a1c-46e2-9ca0-ed5be982489d/scratchpad'
HIST = sys.argv[1] if len(sys.argv) > 1 else SP + '/kata_hist.json'
HTML = sys.argv[2] if len(sys.argv) > 2 else SP + '/blunder_hist.html'
STATUS = sys.argv[3] if len(sys.argv) > 3 else 'partial'  # 'partial' or 'final'

d = json.load(open(HIST))
lo, hi = d['lo'], d['hi']          # bucket range, e.g. -10..40
NB = hi - lo + 1

PHASE_META = [
    ('Opening — moves 1–15',),
    ('Middle game — moves 16–35',),
    ('Endgame — moves 36+',),
]
def sigs(phases):
    op, mid, end = phases
    return [
        f'Not mild slippage after all: {op["big10"]}% of opening moves lose ≥10% winrate — gnugo’s estimator could not see opening quality.',
        f'Heavy tails: 1 in {round(100/mid["big25"])} moves loses ≥25% winrate — the phase where games flip.',
        'Median ~0: by now most games are decided and winrate has saturated; losses concentrate in the few still-live games.',
    ]

def svg(pcts, counts):
    W, H = 860, 190
    x0, top, base = 35.0, 8.0, 168.0
    step = (853.0 - x0) / NB
    bw = step - 2.0
    mx = max(pcts) or 1.0
    sc = (base - top) / mx
    out = [f'<svg viewBox="0 0 {W} {H}" width="100%" role="img">']
    # gridlines every 5%
    g = 5
    while g < mx:
        y = base - g * sc
        out.append(f'<line x1="34" y1="{y:.1f}" x2="856" y2="{y:.1f}" stroke="var(--line)"/>'
                   f'<text class="axis" x="29" y="{y+3.5:.1f}" text-anchor="end">{g}%</text>')
        g += 5
    for i, (p, c) in enumerate(zip(pcts, counts)):
        b = lo + i
        h = max(0.5, p * sc)
        x = x0 + i * step
        col = 'var(--good)' if b < 0 else 'var(--par)' if b <= 1 else 'var(--bad)' if b < 25 else 'var(--worst)'
        lbl = ('≤' if b == lo else '≥' if b == hi else '') + (f'+{b}' if b > 0 else str(b))
        out.append(f'<rect x="{x:.1f}" y="{base-h:.1f}" width="{bw:.1f}" height="{h:.1f}" rx="1.5" fill="{col}">'
                   f'<title>{lbl}%: {round(p,2)}% ({c})</title></rect>')
    zx = x0 + (0 - lo) * step + bw / 2
    out.append(f'<line x1="{zx:.1f}" y1="8" x2="{zx:.1f}" y2="168" stroke="var(--dim)" stroke-dasharray="3 3" opacity="0.6"/>')
    for b in range(lo, hi + 1, 5):
        cx = x0 + (b - lo) * step + bw / 2
        t = ('≤' if b == lo else '≥' if b == hi else '') + (f'+{b}' if b > 0 and b != hi else str(b) if b <= 0 else str(b))
        out.append(f'<text class="axis" x="{cx:.1f}" y="184" text-anchor="middle">{t}</text>')
    out.append('</svg>')
    return ''.join(out)

hdr = (f'PARTIAL — {d["games"]} games so far, updating' if STATUS == 'partial'
       else f'{d["games"]:,} games, {d["moves"]:,} AI moves')
parts = [f'<!--KATA--><div class="note" style="border-color:var(--good)"><b>KataGo review ({hdr}):</b> '
         'true winrate loss per AI move (b6c96 net, 8 visits, black-perspective converted to mover). '
         'No drift correction needed — a real evaluator cannot be “gained” against. '
         'x = winrate % lost by the move (1%-buckets, edges absorb overflow); y = % of AI moves. '
         'Caveat: losses are vs near-optimal play, and winrate saturates once a game is decided.</div>']
for ph, (title,), sig in zip(d['phases'], PHASE_META, sigs(d['phases'])):
    parts.append(
        f'<div class="phase"><div class="phead">\n'
        f'<div class="pname">{title} <span style="font-weight:400;color:var(--dim);font-size:12px">· KataGo</span></div><div class="chips">\n'
        f'<span class="chip"><b>{ph["n"]:,}</b> AI moves</span>\n'
        f'<span class="chip">mean loss <b>{ph["mean"]}%</b></span>\n'
        f'<span class="chip">median <b>{ph["median"]}%</b></span>\n'
        f'<span class="chip">≥10%: <b>{ph["big10"]}%</b></span>\n'
        f'<span class="chip">≥25%: <b>{ph["big25"]}%</b></span>\n'
        f'</div></div>\n'
        f'<div class="chartwrap">{svg(ph["pct"], ph["counts"])}</div>\n'
        f'<p class="sig">{sig}</p></div>')
block = '\n'.join(parts) + '<!--/KATA-->'

html = open(HTML).read()
new = re.sub(r'<!--KATA-->.*?<!--/KATA-->', lambda m: block, html, flags=re.S)
assert new != html or block in html, 'KATA markers not found'
open(HTML, 'w').write(new)
print(f'injected: {d["games"]} games, {d["moves"]} moves, status={STATUS}')
