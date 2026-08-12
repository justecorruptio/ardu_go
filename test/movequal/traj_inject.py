#!/usr/bin/env python3
# Build the <!--TRAJ-->...<!--/TRAJ--> percentile-trajectory section from traj_pct.json.
import json, re

import sys
SP = sys.argv[1] if len(sys.argv) > 1 else '.'
data = json.load(open(SP + '/traj_pct.json'))
data = [d for d in data if d['n'] >= 100]

W, H = 880, 300
x0, x1, ytop, ybot = 44.0, 872.0, 12.0, 268.0
tmax = data[-1]['t']
X = lambda t: x0 + (x1 - x0) * t / tmax
Y = lambda w: ybot - (ybot - ytop) * w / 100.0

def path(key, rev=False):
    pts = [(X(d['t']), Y(d[key])) for d in data]
    if rev: pts = pts[::-1]
    return ' '.join(f"{'L' if i else 'M'}{x:.1f} {y:.1f}" for i, (x, y) in enumerate(pts))

def band(k1, k2):
    return f"{path(k1)} {path(k2, rev=True).replace('M', 'L', 1)} Z"

svg = [f'<svg viewBox="0 0 {W} {H}" width="100%" role="img">']
for w in (0, 25, 50, 75, 100):
    y = Y(w)
    svg.append(f'<line x1="{x0-4}" y1="{y:.1f}" x2="{x1}" y2="{y:.1f}" stroke="var(--line)"'
               + (' stroke-dasharray="3 3"' if w == 50 else '') + '/>'
               f'<text class="axis" x="{x0-8}" y="{y+3.5:.1f}" text-anchor="end">{w}%</text>')
for t in (0, 15, 35):
    if t: svg.append(f'<line x1="{X(t):.1f}" y1="{ytop}" x2="{X(t):.1f}" y2="{ybot}" stroke="var(--dim)" stroke-dasharray="2 4" opacity="0.5"/>')
for t in range(0, tmax + 1, 10):
    svg.append(f'<text class="axis" x="{X(t):.1f}" y="{ybot+16:.1f}" text-anchor="middle">{t}</text>')
svg.append(f'<text class="axis" x="{X(7):.1f}" y="{ytop+10}" text-anchor="middle">opening</text>')
svg.append(f'<text class="axis" x="{X(25):.1f}" y="{ytop+10}" text-anchor="middle">middle</text>')
svg.append(f'<text class="axis" x="{X(47):.1f}" y="{ytop+10}" text-anchor="middle">endgame</text>')
svg.append(f'<path d="{band("p10","p90")}" fill="var(--good)" opacity="0.10"/>')
# one line per decile: p50 emphasized, above-median deciles in the good hue,
# below-median in the bad hue, fading with distance from the median
for q in (90, 80, 70, 60, 40, 30, 20, 10):
    col = 'var(--good)' if q > 50 else 'var(--bad)'
    op = 1.0 - abs(q - 50) / 60.0
    svg.append(f'<path d="{path(f"p{q}")}" fill="none" stroke="{col}" '
               f'stroke-width="1.1" opacity="{op:.2f}"><title>p{q}</title></path>')
svg.append(f'<path d="{path("p50")}" fill="none" stroke="var(--accent)" stroke-width="2.4"><title>p50</title></path>')
# right-edge decile labels, nudged apart when they collide
lab = sorted(((Y(data[-1][f'p{q}']), q) for q in range(10, 100, 10)))
prev = -99
for y, q in lab:
    y = max(y, prev + 11); prev = y
    col = 'var(--accent)' if q == 50 else ('var(--good)' if q > 50 else 'var(--bad)')
    svg.append(f'<text class="axis" x="{x1+4}" y="{y+3.5:.1f}" fill="{col}">p{q}</text>')
svg.append('</svg>')

d15 = next(d for d in data if d['t'] == 15)
d25 = next(d for d in data if d['t'] == 25)
d20 = next(d for d in data if d['t'] == 20)
d35 = next(d for d in data if d['t'] == 35)
NG = data[0]['n']
peak = max((d for d in data if d['t'] <= 20), key=lambda r: r['p50'])
block = f'''<!--TRAJ--><h1 style="font-size:17px;margin-top:8px">Winrate trajectory — percentiles per move, {NG} games</h1>
<div class="phase"><div class="phead"><div class="pname" style="font-size:14px">AI winrate (KataGo, mover-agnostic AI perspective)</div><div class="chips">
<span class="chip"><span style="color:var(--accent)">━</span> p50</span>
<span class="chip"><span style="color:var(--good)">━</span> p60–p90</span>
<span class="chip"><span style="color:var(--bad)">━</span> p10–p40</span>
</div></div>
<div class="chartwrap">{''.join(svg)}</div>
<p class="sig">The median game holds even through the opening and peaks at {peak['p50']:.0f}% around move {peak['t']} — the learned NN opening keeps ArduGo level or ahead well into the midgame (the earlier hand-crafted opening peaked near 55% by move 6). The median then slides below 40% by move 26 and to ~{d35['p50']:.0f}% by move 35. The upper deciles run the other way: p70 climbs past {d20['p70']:.0f}% by move 20 and p90 rides near {d20['p90']:.0f}% — games that clear the opening are usually already won. The lower deciles collapse fastest: p30 is at {d20['p30']:.0f}% and p10 in single digits ({d20['p10']:.0f}%) by move 20. The decile fan makes the shape plain: the distribution splits into a won-band and a lost-band inside the moves-10-to-25 window, with little in between after that.</p></div><!--/TRAJ-->'''

html = open(SP + '/blunder_hist.html').read()  # argv[1] = workdir
if '<!--TRAJ-->' in html:
    html = re.sub(r'<!--TRAJ-->.*?<!--/TRAJ-->', lambda m: block, html, flags=re.S)
else:
    html = html.replace('<!--KATA-->', block + '\n<!--KATA-->')
open(SP + '/blunder_hist.html', 'w').write(html)
print('TRAJ section injected')
