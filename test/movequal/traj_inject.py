#!/usr/bin/env python3
# Build the <!--TRAJ-->...<!--/TRAJ--> percentile-trajectory section from traj_pct.json.
import json, re

SP = '/private/tmp/claude-501/-Users-jay-workspace/5b06ba38-9a1c-46e2-9ca0-ed5be982489d/scratchpad'
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
svg.append(f'<path d="{band("p10","p90")}" fill="var(--good)" opacity="0.13"/>')
svg.append(f'<path d="{band("p25","p75")}" fill="var(--good)" opacity="0.22"/>')
svg.append(f'<path d="{path("p90")}" fill="none" stroke="var(--good)" stroke-width="1.2" opacity="0.7"/>')
svg.append(f'<path d="{path("p75")}" fill="none" stroke="var(--good)" stroke-width="1.2"/>')
svg.append(f'<path d="{path("p50")}" fill="none" stroke="var(--accent)" stroke-width="2.4"/>')
svg.append(f'<path d="{path("p25")}" fill="none" stroke="var(--bad)" stroke-width="1.2"/>')
svg.append(f'<path d="{path("p10")}" fill="none" stroke="var(--bad)" stroke-width="1.2" opacity="0.7"/>')
lx = X(tmax) + 2
for k, col, lab in (('p90','var(--good)','p90'),('p75','var(--good)','p75'),('p50','var(--accent)','p50'),('p25','var(--bad)','p25'),('p10','var(--bad)','p10')):
    pass
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
<span class="chip"><span style="color:var(--good)">━</span> p75 / p90</span>
<span class="chip"><span style="color:var(--bad)">━</span> p25 / p10</span>
</div></div>
<div class="chartwrap">{''.join(svg)}</div>
<p class="sig">The median game holds even through the opening and peaks at {peak['p50']:.0f}% around move {peak['t']} — the learned NN opening keeps ArduGo level or ahead well into the midgame (the earlier hand-crafted opening peaked near 55% by move 6). The median then slides below 40% by move 26 and to ~{d35['p50']:.0f}% by move 35. The top quartile runs the other way: p75 climbs past {d20['p75']:.0f}% by move 20 and holds there — games that clear the opening are usually already won. The bottom quartile collapses fastest, into single digits (p25 {d20['p25']:.0f}%) by move 20. The whole spread is decided in the moves-10-to-25 window.</p></div><!--/TRAJ-->'''

html = open(SP + '/blunder_hist.html').read()
if '<!--TRAJ-->' in html:
    html = re.sub(r'<!--TRAJ-->.*?<!--/TRAJ-->', lambda m: block, html, flags=re.S)
else:
    html = html.replace('<!--KATA-->', block + '\n<!--KATA-->')
open(SP + '/blunder_hist.html', 'w').write(html)
print('TRAJ section injected')
