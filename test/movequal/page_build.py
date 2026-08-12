#!/usr/bin/env python3
# Whole-page opponent tabs: rebuild blunder_hist.html so ONE tab bar switches
# every section (trajectory fan, per-phase loss, suggestion pass, blunder
# anatomy) between opponents. Each opponent needs a corpus dir with:
#   kata.jsonl  suggest.jsonl  classes2.json  kata_hist.json  (+ sgfs)
# plus traj percentiles (computed here from kata.jsonl directly).
#   page_build.py <out.html> <base.html> l0=<dir> [15k=<dir>] [10k=<dir>] [5k=<dir>]
# Meta (wins/games) read from <dir>/wins.txt when present (human corpora).
import json, re, os, sys, collections

out_path, base_path = sys.argv[1], sys.argv[2]
CORPORA = []   # (key, label, dir)
LABELS = {'l0': 'vs GnuGo L0', '15k': 'vs KataGo-15k human',
          '10k': 'vs KataGo-10k human', '5k': 'vs KataGo-5k human'}
for a in sys.argv[3:]:
    k, d = a.split('=', 1)
    CORPORA.append((k, LABELS[k], d))

# ---------- shared data helpers ----------
def load_turns(wd):
    rows = collections.defaultdict(dict)
    for line in open(os.path.join(wd, 'kata.jsonl')):
        r = json.loads(line); rows[r['g']][r['t']] = r['wr']
    return rows

def traj_pct(rows):
    bym = collections.defaultdict(list)
    for g, turns in rows.items():
        ai_black = (g % 2 == 0)
        for t, wr in turns.items():
            bym[t].append((wr if ai_black else 1.0 - wr) * 100)
    out = []
    vs_sorted = sorted(bym)
    for t in vs_sorted:
        v = sorted(bym[t]); n = len(v)
        if n < 100: continue
        d = {'t': int(t), 'n': n}
        for q in range(10, 100, 10):
            i = (n - 1) * q / 100.0
            f = int(i); c = min(f + 1, n - 1)
            d[f'p{q}'] = round(v[f] + (v[c] - v[f]) * (i - f), 1)
        out.append(d)
    return out

# ---------- TRAJ fan ----------
def traj_svg(data):
    W, H = 880, 300
    x0, x1, ytop, ybot = 44.0, 872.0, 12.0, 268.0
    tmax = data[-1]['t']
    X = lambda t: x0 + (x1 - x0) * t / tmax
    Y = lambda w: ybot - (ybot - ytop) * w / 100.0
    def path(key, rev=False):
        pts = [(X(d['t']), Y(d[key])) for d in data]
        if rev: pts = pts[::-1]
        return ' '.join(f"{'L' if i else 'M'}{x:.1f} {y:.1f}" for i, (x, y) in enumerate(pts))
    band = f"{path('p10')} {path('p90', rev=True).replace('M', 'L', 1)} Z"
    svg = [f'<svg viewBox="0 0 {W} {H}" width="100%" role="img">']
    for w in (0, 25, 50, 75, 100):
        y = Y(w)
        svg.append(f'<line x1="{x0-4}" y1="{y:.1f}" x2="{x1}" y2="{y:.1f}" stroke="var(--line)"'
                   + (' stroke-dasharray="3 3"' if w == 50 else '') + '/>'
                   f'<text class="axis" x="{x0-8}" y="{y+3.5:.1f}" text-anchor="end">{w}%</text>')
    for t in (15, 35):
        svg.append(f'<line x1="{X(t):.1f}" y1="{ytop}" x2="{X(t):.1f}" y2="{ybot}" stroke="var(--dim)" stroke-dasharray="2 4" opacity="0.5"/>')
    for t in range(0, tmax + 1, 10):
        svg.append(f'<text class="axis" x="{X(t):.1f}" y="{ybot+16:.1f}" text-anchor="middle">{t}</text>')
    for t, lab in ((7, 'opening'), (25, 'middle'), (min(47, tmax - 4), 'endgame')):
        svg.append(f'<text class="axis" x="{X(t):.1f}" y="{ytop+10}" text-anchor="middle">{lab}</text>')
    svg.append(f'<path d="{band}" fill="var(--good)" opacity="0.10"/>')
    for q in (90, 80, 70, 60, 40, 30, 20, 10):
        col = 'var(--good)' if q > 50 else 'var(--bad)'
        op = 1.0 - abs(q - 50) / 60.0
        svg.append(f'<path d="{path(f"p{q}")}" fill="none" stroke="{col}" '
                   f'stroke-width="1.1" opacity="{op:.2f}"><title>p{q}</title></path>')
    svg.append(f'<path d="{path("p50")}" fill="none" stroke="var(--accent)" stroke-width="2.4"><title>p50</title></path>')
    lab = sorted(((Y(data[-1][f'p{q}']), q) for q in range(10, 100, 10)))
    prev = -99
    for y, q in lab:
        y = max(y, prev + 11); prev = y
        col = 'var(--accent)' if q == 50 else ('var(--good)' if q > 50 else 'var(--bad)')
        svg.append(f'<text class="axis" x="{x1+4}" y="{y+3.5:.1f}" fill="{col}">p{q}</text>')
    svg.append('</svg>')
    return ''.join(svg)

def traj_block(key, data, meta, l0_d20):
    d20 = next(d for d in data if d['t'] == 20)
    d35 = next((d for d in data if d['t'] == 35), data[-1])
    peak = max((d for d in data if d['t'] <= 20), key=lambda r: r['p50'])
    NG = data[0]['n']
    if key == 'l0':
        cap = (f"The median game holds even through the opening and peaks at {peak['p50']:.0f}% around move {peak['t']} — "
               f"the learned NN opening keeps ArduGo level or ahead well into the midgame. The median then slides to "
               f"~{d35['p50']:.0f}% by move 35. The upper deciles run the other way (p70 {d20['p70']:.0f}% by move 20) — "
               f"games that clear the opening are usually already won; the lower deciles collapse into single digits. "
               f"The distribution splits into a won-band and a lost-band inside the moves-10-to-25 window.")
    else:
        wl = meta.get('wins'); gm = meta.get('games')
        wins_txt = f" ArduGo wins {wl}/{gm} ({100.0*wl/gm:.0f}%) at this rank." if wl is not None else ""
        cap = (f"Same engine against the KataGo human-SL <b>{key}</b> profile.{wins_txt} "
               f"The median peaks at {peak['p50']:.0f}% (move {peak['t']}), is {d20['p50']:.0f}% by move 20 and "
               f"~{d35['p50']:.0f}% by move {d35['t']}; p70 sits at {d20['p70']:.0f}% by move 20 "
               f"(vs {l0_d20['p70']:.0f}% against L0). The lost-band forms in the same moves-10-to-25 window — "
               f"what changes with opponent strength is how few games escape into the won-band.")
    return (f'<h1 style="font-size:17px;margin-top:8px">Winrate trajectory — deciles per move ({NG} games)</h1>'
            f'<div class="phase"><div class="phead"><div class="pname" style="font-size:14px">AI winrate (KataGo, mover-agnostic AI perspective)</div><div class="chips">'
            f'<span class="chip"><span style="color:var(--accent)">━</span> p50</span>'
            f'<span class="chip"><span style="color:var(--good)">━</span> p60–p90</span>'
            f'<span class="chip"><span style="color:var(--bad)">━</span> p10–p40</span>'
            f'</div></div><div class="chartwrap">{traj_svg(data)}</div><p class="sig">{cap}</p></div>')

# ---------- KATA per-phase loss ----------
def kata_svg(pcts, counts, lo, hi):
    NB = hi - lo + 1
    W, H = 860, 190
    x0, top, base = 35.0, 8.0, 168.0
    step = (853.0 - x0) / NB
    bw = step - 2.0
    mx = max(pcts) or 1.0
    sc = (base - top) / mx
    out = [f'<svg viewBox="0 0 {W} {H}" width="100%" role="img">']
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
        t = ('≤' if b == lo else '≥' if b == hi else '') + (f'+{b}' if b > 0 and b != hi else str(b))
        out.append(f'<text class="axis" x="{cx:.1f}" y="184" text-anchor="middle">{t}</text>')
    out.append('</svg>')
    return ''.join(out)

PHASE_TITLES = ['Opening — moves 1–15', 'Middle game — moves 16–35', 'Endgame — moves 36+']
def kata_block(d, label):
    parts = [f'<div class="note" style="border-color:var(--good)"><b>KataGo review ({d["games"]:,} games, '
             f'{d["moves"]:,} AI moves, {label}):</b> true winrate loss per AI move (b6c96 net, 8 visits, '
             'black-perspective converted to mover). x = winrate % lost by the move (1%-buckets, edges absorb '
             'overflow); y = % of AI moves. Losses are vs near-optimal play, and winrate saturates once a game is decided.</div>']
    for ph, title in zip(d['phases'], PHASE_TITLES):
        sig = (f"Mean {ph['mean']}% / median {ph['median']}% loss; {ph['big10']}% of moves lose ≥10%, "
               f"{ph['big25']}% lose ≥25%.")
        parts.append(
            f'<div class="phase"><div class="phead">'
            f'<div class="pname">{title} <span style="font-weight:400;color:var(--dim);font-size:12px">· KataGo</span></div><div class="chips">'
            f'<span class="chip"><b>{ph["n"]:,}</b> AI moves</span>'
            f'<span class="chip">mean loss <b>{ph["mean"]}%</b></span>'
            f'<span class="chip">median <b>{ph["median"]}%</b></span>'
            f'<span class="chip">≥10%: <b>{ph["big10"]}%</b></span>'
            f'<span class="chip">≥25%: <b>{ph["big25"]}%</b></span>'
            f'</div></div><div class="chartwrap">{kata_svg(ph["pct"], ph["counts"], d["lo"], d["hi"])}</div>'
            f'<p class="sig">{sig}</p></div>')
    return '\n'.join(parts)

# ---------- SUGG ----------
COLS = "ABCDEFGHJ"
def parse_kv(s):
    if s is None or str(s).lower() == 'pass': return None
    return (9 - int(s[1:])) * 9 + COLS.index(s[0].upper())
def line_of(p):
    x, y = p % 9, p // 9
    return 1 + min(x, 8 - x, y, 8 - y)
def sugg_stats(wd):
    sugg = [json.loads(l) for l in open(os.path.join(wd, 'suggest.jsonl'))]
    pl = [0] * 6; bl = [0] * 6; hist = [0] * 20
    winnable = adj = passes = 0
    for s in sugg:
        p, b = s['played'], parse_kv(s['best'])
        if p is None: passes += 1
        else: pl[min(line_of(p), 5)] += 1
        if b is not None: bl[min(line_of(b), 5)] += 1
        w = s['best_wr']
        hist[min(int(w * 20), 19)] += 1
        if w >= 0.5: winnable += 1
        if p is not None and b is not None and max(abs(p % 9 - b % 9), abs(p // 9 - b // 9)) <= 1: adj += 1
    return {'n': len(sugg), 'pl_lines': pl, 'bl_lines': bl, 'hist': hist,
            'winnable': winnable, 'adj': adj, 'passes': passes}

def sugg_block(S):
    n = S['n']
    pl, bl = S['pl_lines'][1:], S['bl_lines'][1:]
    mx = max(max(pl), max(bl)) or 1
    W, H, X0, Y0 = 560, 190, 40, 168
    bars = []
    labels = ['1', '2', '3', '4', '5+']
    for i in range(5):
        gx = X0 + i * 104
        for j, (v, col) in enumerate([(pl[i], 'var(--bad)'), (bl[i], 'var(--good)')]):
            h = 150.0 * v / mx
            bars.append(f'<rect x="{gx + j*34}" y="{Y0 - h:.1f}" width="30" height="{h:.1f}" rx="2" '
                        f'fill="{col}"><title>line {labels[i]} {"played" if j==0 else "KataGo"}: {v}</title></rect>')
            bars.append(f'<text class="axis" x="{gx + j*34 + 15}" y="{Y0 - h - 4:.1f}" text-anchor="middle">{v}</text>')
        bars.append(f'<text class="axis" x="{gx + 34}" y="{Y0 + 14}" text-anchor="middle">line {labels[i]}</text>')
    svg1 = f'<svg viewBox="0 0 {W} {H}" width="100%" role="img">' + ''.join(bars) + '</svg>'
    h = S['hist']; mx2 = max(h) or 1
    bars2 = []
    for i, v in enumerate(h):
        x = 36 + i * 41
        bh = 150.0 * v / mx2
        col = 'var(--good)' if i >= 10 else 'var(--bad)'
        bars2.append(f'<rect x="{x}" y="{168 - bh:.1f}" width="36" height="{bh:.1f}" rx="2" fill="{col}" '
                     f'opacity="0.85"><title>{i*5}-{i*5+5}%: {v}</title></rect>')
    bars2.append('<line x1="446" y1="8" x2="446" y2="168" stroke="var(--dim)" stroke-dasharray="3 3" opacity="0.6"/>')
    for pct in (0, 25, 50, 75, 100):
        bars2.append(f'<text class="axis" x="{36 + pct*8.2}" y="184" text-anchor="middle">{pct}%</text>')
    svg2 = f'<svg viewBox="0 0 860 190" width="100%" role="img">' + ''.join(bars2) + '</svg>'
    winnable_pct = round(100 * S['winnable'] / n)
    adj_pct = round(100 * S['adj'] / n)
    return f'''<h1 style="font-size:17px;margin-top:8px">KataGo corrections — every ≥25% blunder re-queried at 48 visits</h1>
<div class="note" style="border-color:var(--good)"><b>Suggestion pass ({n} positions):</b> for each move that lost ≥25% winrate, KataGo's preferred move at the same position. <b>{winnable_pct}%</b> of these positions were still winnable (suggested move keeps the mover ≥50%); <b>{adj_pct}%</b> of corrections are adjacent to the move played. {S['passes']} blunders were passes.</div>
<style>.duo{{display:grid;grid-template-columns:1fr 1fr;gap:22px}}@media(max-width:760px){{.duo{{grid-template-columns:1fr}}}}</style>
<div class="duo">
<div class="phase"><div class="phead"><div class="pname" style="font-size:14px">Where the winning move was</div><div class="chips"><span class="chip"><span style="color:var(--bad)">■</span> played</span><span class="chip"><span style="color:var(--good)">■</span> KataGo</span></div></div>
<div class="chartwrap">{svg1}</div>
<p class="sig">Line distribution of played moves vs KataGo's corrections at the blunder positions.</p></div>
<div class="phase"><div class="phead"><div class="pname" style="font-size:14px">Winrate still available at the blunder</div></div>
<div class="chartwrap">{svg2}</div>
<p class="sig">Mover's winrate had KataGo's move been played (5% buckets). {winnable_pct}% of the mass sits right of the 50% line: live games being thrown, not lost causes.</p></div>
</div>'''

# ---------- CLS (blunder anatomy) ----------
CLS_NOTE = {
 'feed-doomed':  'the midgame-hunt dominant class: playouts believe the save at ~79%; all 4 static classifiers priced dead',
 'missed-save':  'tactical-gate credit for atari answers',
 'missed-capture': 'tactical-gate credit for capturing moves',
 'missed-attack': '2-liberty pressure — playout kill-bias territory',
 'ignored-atari': 'tactical-gate credit; residue after the rootSelfAtari veto',
 'captured-soon': 'fresh stone thrown straight into capture',
 'self-atari':   'rootSelfAtari veto works: near-zero residue',
 'feed-weak':    'endgame-arc: feeding-veto measured harmful (needs ~10x verdict precision)',
 'connect-miss': 'patternMatch already scores 3x3-local shapes — probe before adding priors',
 'cut-miss':     'CUT_WEAK prior removed for flash (-11 wins/300, borderline)',
 'crawl':        'D1 probe: 79% seed-luck at near-ties; pick-time tie-break measured negative on both opponents — KataGo-optimality != win-rate',
 'edge2':        'low-line exemption work shrank it; admission priced neutral',
 'edge1':        'both-colors exemption EL2B1 measured -15/1000',
 'local':        'pattern-table domain: 3x3 priors at the chosen point',
 'tenuki':       'last-move locality credit at the root',
 'eye-fill':     'rare',
 'own-territory': 'rare since the settled-territory pass gate',
 'pass':         'settled-territory pass gate shipped',
 'game-losing':  'conversion is the wall',
 'serial':       'compounding multiplier — the residue clusters',
}
CLS_GROUPS = [('life&death', 'Life & death', "reading a group's fate"),
              ('tactics', 'Tactics', 'liberties, connections, cuts'),
              ('direction', 'Direction', 'right fight vs wrong fight'),
              ('waste', 'Wasted moves', 'points that gain nothing'),
              ('severity', 'Severity overlays', 'orthogonal context, not mechanisms')]
def cls_block(C):
    T = C['tags']; total = C['total']
    maxn = max(v['n'] for v in T.values()) or 1
    blocks = []
    for gkey, gname, gsub in CLS_GROUPS:
        rows = []
        for key, a in T.items():
            if a['group'] != gkey or a['n'] == 0: continue
            pct = 100 * a['n'] / total
            avg = round(100 * a['loss'] / a['n'])
            w0 = round(100 * a['wr0'] / a['n'])
            segs = []
            for ph, col, lab in zip(a['ph'], ['var(--good)', 'var(--accent)', 'var(--bad)'],
                                    ['open', 'mid', 'end']):
                if ph == 0: continue
                w = 100.0 * ph / maxn
                segs.append(f'<div title="{lab}: {ph}" style="width:{w:.2f}%;background:{col};height:100%"></div>')
            note = CLS_NOTE.get(key, '')
            rows.append(
              f'<div style="display:grid;grid-template-columns:170px 1fr;gap:12px;align-items:center;margin:7px 0">'
              f'<div style="text-align:right"><div style="font-weight:600;font-size:13px;font-family:var(--mono)">{key}</div>'
              f'<div style="font:11px var(--mono);color:var(--dim)">{a["n"]:,} · {pct:.1f}% · Ø{avg}% lost · wr {w0}%</div></div>'
              f'<div><div style="display:flex;height:16px;border-radius:4px;overflow:hidden;background:var(--line)">{"".join(segs)}</div>'
              f'<div style="font-size:11.5px;color:var(--dim);margin-top:2px"><b style="color:var(--ink);font-weight:600">{a["desc"]}</b>'
              f'{" — " + note if note else ""}</div></div></div>')
        if rows:
            blocks.append(f'<div style="margin:14px 0 4px;font-weight:600;font-size:13px">{gname} '
                          f'<span style="color:var(--dim);font-weight:400">— {gsub}</span></div>' + ''.join(rows))
    pairs = ', '.join(f'<span style="font-family:var(--mono)">{a}+{b}</span>&thinsp;{n}'
                      for a, b, n in C['pairs'][:6])
    return f'''<h1 style="font-size:17px;margin-top:8px">Blunder anatomy — {total} blunders, {sum(1 for v in T.values() if v["n"])} non-exclusive tags</h1>
<div class="note"><b>Finer board-replay taxonomy:</b> each ≥25% blunder is replayed to its position and tagged by every mechanism that applies — tags are <b>non-exclusive</b>, so columns sum past 100%. "wr" is the mover's mean winrate <i>before</i> the blunder. Bars show volume by phase (<span style="color:var(--good)">■ opening</span> <span style="color:var(--accent)">■ middle</span> <span style="color:var(--bad)">■ endgame</span>), scaled to the largest tag. Untagged residue: {C['untagged']} ({round(100*C['untagged']/total)}%). Top co-occurrences: {pairs}.</div>
<div class="phase">{''.join(blocks)}</div>'''

# ---------- assemble ----------
base = open(base_path).read()
prefix = base[:base.index('<!--TRAJ-->')]
foot_note = ('Corpora: harness games vs GnuGo L0 (250, seeds 1000–1249) and vs KataGo human-SL '
             'profiles 15k/10k/5k (256 each, GPU-served opponent), every turn reviewed post-hoc by '
             'KataGo (b6c96, 8 visits); blunders re-queried at 48 visits. Engine: pure-1000 × ss512 (918e0e9).')

l0_dir = dict((k, d) for k, _, d in CORPORA)['l0']
l0_traj = traj_pct(load_turns(l0_dir))
l0_d20 = next(d for d in l0_traj if d['t'] == 20)

btns, panes = [], []
for i, (key, label, wd) in enumerate(CORPORA):
    rows = load_turns(wd)
    tdata = traj_pct(rows)
    meta = {}
    wt = os.path.join(wd, 'wins.txt')
    if os.path.exists(wt):
        meta = {'wins': int(open(wt).read().strip()), 'games': len(rows)}
    sections = [traj_block(key, tdata, meta, l0_d20)]
    kh = os.path.join(wd, 'kata_hist.json')
    if os.path.exists(kh): sections.append(kata_block(json.load(open(kh)), label))
    if os.path.exists(os.path.join(wd, 'suggest.jsonl')):
        sections.append(sugg_block(sugg_stats(wd)))
    c2 = os.path.join(wd, 'classes2.json')
    if os.path.exists(c2): sections.append(cls_block(json.load(open(c2))))
    on = 'true' if i == 0 else 'false'
    btns.append(f'<button class="otab" data-o="{key}" aria-selected="{on}" onclick="opTab(this)">{label}</button>')
    panes.append(f'<div class="opane" id="op-{key}" style="display:{"block" if i==0 else "none"}">'
                 + '\n'.join(sections) + '</div>')

tab_ui = ('<style>.otab{font:13px var(--mono);padding:7px 14px;border:1px solid var(--line);'
          'background:none;color:var(--dim);border-radius:8px;cursor:pointer;margin-right:8px}'
          '.otab[aria-selected="true"]{color:var(--ink);border-color:var(--accent);font-weight:600}</style>'
          '<script>function opTab(b){document.querySelectorAll(".otab").forEach('
          'function(x){x.setAttribute("aria-selected","false")});b.setAttribute("aria-selected","true");'
          'document.querySelectorAll(".opane").forEach(function(p){p.style.display="none"});'
          'document.getElementById("op-"+b.dataset.o).style.display="block"}</script>'
          f'<div style="margin:14px 0 18px">{"".join(btns)}</div>')

html = (prefix + tab_ui + '\n'.join(panes)
        + f'\n<div class="foot">{foot_note}</div>\n')
open(out_path, 'w').write(html)
print(f'built {out_path}: {len(CORPORA)} opponent tab(s), {len(html)} bytes')
