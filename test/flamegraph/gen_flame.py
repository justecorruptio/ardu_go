#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import re, sys, json, os

SCR = os.environ.get("SCR", os.path.dirname(os.path.abspath(__file__)) + "/work")
FULL_MOVE_M   = 272.64   # legacy/unused; per-tab absolute comes from TABS below
BASELINE_MOVE = 422.58   # baseline for the Δ column: b394ae5 (422.58M, 26.4 s)

def clean(f):
    f = re.sub(r'\(.*?\)', '', f)
    f = re.sub(r'\s*\[clone[^\]]*\]', '', f)
    return f.strip()

def parse(path):
    edges, leaves, total = [], {}, 0
    for line in open(path):
        line = line.rstrip('\n')
        if not line.strip():
            continue
        stack, cnt = line.rsplit(' ', 1)
        cnt = int(cnt)
        frames = [clean(x) for x in stack.split(';')]
        # main/setup are the bench harness wrapper — collapse both into the
        # single think() root so builds that do/don't inline setup match.
        frames = [f for f in frames if f and f != '???' and f not in ('main', 'setup')]
        frames = ['think()'] + frames
        edges.append((frames, cnt))
        leaf = frames[-1]
        leaves[leaf] = leaves.get(leaf, 0) + cnt
        total += cnt
    return edges, leaves, total

class Node:
    __slots__ = ('name', 'self', 'total', 'kids')
    def __init__(self, name):
        self.name = name; self.self = 0; self.total = 0; self.kids = {}

def build(edges):
    root = Node('think()')
    for frames, cnt in edges:
        root.total += cnt
        node = root
        for f in frames[1:]:          # skip the root 'think()'
            node = node.kids.setdefault(f, Node(f))
            node.total += cnt
        node.self += cnt              # frames==['think()'] -> root.self
    return root

def color(n):
    if n in ('think()',): return '#5a5a5a'
    if n.startswith('__') or n == 'isqrt32': return '#8a6bd6'          # math
    if n in ('groupLibsCore','hasLiberty','soleLiberty','ladderEscapes','removeGroup'):
        return '#d64b4b'                                              # flood/rules
    if n in ('playoutTry','simPlay','rnd16','raveMark','playout','scoreWinner'):
        return '#e0a93b'                                              # playout
    if n in ('newNode','addChild','allocReady','node','nVisits','nSetStats',
             'selectChild','mctsIterate'):
        return '#4b93d6'                                              # tree/alloc
    return '#e8833a'                                                  # prior/eval

MIN_PCT, LABEL_PCT, MAX_DEPTH = 0.5, 5.0, 4

def layout(root):
    out = []
    def rec(node, depth, left):
        pct = node.total / root.total * 100
        if pct < MIN_PCT or depth > MAX_DEPTH:
            return
        out.append((node, depth, left, pct))
        x = left
        for ch in sorted(node.kids.values(), key=lambda c: -c.total):
            rec(ch, depth + 1, x)
            x += ch.total / root.total * 100
    rec(root, 0, 0.0)
    return out

def frames_html(out):
    s = []
    for node, depth, left, pct in out:
        y = 110 - depth * 22
        absM = pct / 100 * FULL_MOVE_M
        label = node.name if pct >= LABEL_PCT else ''
        syn = id(node) in SYN
        tip = f"{node.name} · {pct:.2f}% · {absM:.1f}M cyc/move" + \
              (" · inlined (addr2line attribution)" if syn else "")
        cls = "f syn" if syn else "f"
        s.append(f'<div class="{cls}" style="left:{left:.3f}%;top:{y}px;width:{pct:.3f}%;'
                 f'--c:{color(node.name)}" data-t="{tip}">{label}</div>')
    return ''.join(s)

# ---- parse current build + previous published profile
CSS = """<meta name=viewport content="width=device-width,initial-scale=1">
<title>ArduGo Search — On-Device Profile</title>
<style>
:root{--bg:#161311;--panel:#1e1a17;--ink:#ece6df;--dim:#a8a097;--line:#332c26;--accent:#e8833a;--mono:ui-monospace,"SF Mono",Menlo,monospace}
@media(prefers-color-scheme:light){:root{--bg:#faf7f3;--panel:#fff;--ink:#231d18;--dim:#6b625a;--line:#e7ded4}}
:root[data-theme=dark]{--bg:#161311;--panel:#1e1a17;--ink:#ece6df;--dim:#a8a097;--line:#332c26}
:root[data-theme=light]{--bg:#faf7f3;--panel:#fff;--ink:#231d18;--dim:#6b625a;--line:#e7ded4}
*{box-sizing:border-box} body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.5 system-ui,sans-serif}
.wrap{max-width:1060px;margin:0 auto;padding:18px 16px 34px}
h1{font-size:21px;margin:0 0 2px;letter-spacing:-.01em} .sub{color:var(--dim);font-size:12.5px;margin:0 0 14px}
.stats{display:flex;gap:8px;flex-wrap:wrap;margin:0 0 16px}
.stat{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:8px 12px;flex:1;min-width:104px}
.stat b{display:block;font:600 18px/1.1 var(--mono);letter-spacing:-.02em} .stat span{color:var(--dim);font-size:11px;text-transform:uppercase;letter-spacing:.06em}
h2{font-size:13px;text-transform:uppercase;letter-spacing:.07em;color:var(--dim);margin:17px 0 7px;font-weight:600}
.flame{position:relative;height:132px;background:var(--panel);border:1px solid var(--line);border-radius:9px;overflow:hidden}
.f{position:absolute;height:21px;background:var(--c);color:#140f0b;font:600 10px/21px var(--mono);
padding:0 4px;white-space:nowrap;overflow:hidden;border-right:1px solid rgba(0,0,0,.28);cursor:default}
.f:hover{filter:brightness(1.18);box-shadow:inset 0 0 0 1.5px var(--ink)}
.f.syn{background-image:repeating-linear-gradient(45deg,rgba(0,0,0,.18) 0 3px,transparent 3px 6px)}
#tip{position:fixed;pointer-events:none;background:#000;color:#fff;font:11px var(--mono);padding:5px 8px;border-radius:5px;opacity:0;transition:opacity .1s;z-index:9;white-space:nowrap}
table{width:100%;border-collapse:collapse;font-size:12px} th,td{text-align:left;padding:3px 6px;border-bottom:1px solid var(--line)}
th{color:var(--dim);font:600 11px/1 system-ui;text-transform:uppercase;letter-spacing:.05em}
th.n{text-align:right}
td.n{text-align:right;font:600 13px var(--mono)} td.pre{color:var(--dim);font-weight:400} td.up{color:#d64b4b} td.down{color:#3aa06a} td.dim3{color:var(--dim);font-weight:400}
.tblwrap{overflow-x:auto}
code{font:13px var(--mono)} .dot{display:inline-block;width:8px;height:8px;border-radius:2px;margin-right:7px;vertical-align:1px}
.prior{background:#e8833a}.flood{background:#d64b4b}.playout{background:#e0a93b}.math{background:#8a6bd6}.tree{background:#4b93d6}.root{background:#5a5a5a}
.legend{display:flex;gap:16px;flex-wrap:wrap;color:var(--dim);font-size:12px;margin:10px 0 0} .legend span{display:flex;align-items:center;gap:6px}
.note{color:var(--dim);font-size:12px;line-height:1.55;margin-top:10px;border-left:2px solid var(--accent);padding-left:12px}
ol.changelog{margin:8px 0 12px;padding:0 0 0 12px;border-left:2px solid var(--accent);list-style:none;counter-reset:cl;display:flex;flex-direction:column;gap:7px;color:var(--dim);font-size:12px;line-height:1.5}
ol.changelog li{counter-increment:cl;position:relative;padding-left:30px}
ol.changelog li::before{content:counter(cl);position:absolute;left:0;top:0;width:20px;text-align:right;color:var(--accent);font:600 11px var(--mono)}
ol.changelog b{color:var(--ink);font-weight:600} .dlt{color:var(--accent);font:600 11px var(--mono);white-space:nowrap}
.plgrid{display:grid;grid-template-columns:1fr 1fr;gap:8px 14px}@media(max-width:700px){.plgrid{grid-template-columns:1fr}}
.insidegrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:8px 14px}
.inblk{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:7px 9px}
.inln{display:grid;grid-template-columns:72px 42px 1fr;gap:9px;align-items:center;padding:1.5px 0}
.inln .p{text-align:right;font:600 11px var(--mono)} .inln .nm{font:11px var(--mono);color:var(--ink);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.inln .nm i{color:var(--dim);font-style:normal}
.insub{margin:0 0 3px 15px;border-left:1px solid var(--line);padding-left:9px}
.insub .r{display:grid;grid-template-columns:36px 42px 62px 1fr;gap:8px;align-items:center;padding:.5px 0;font:10.5px var(--mono)}
.insub .ex{text-align:right;color:var(--dim)}
.insub .p{text-align:right;color:var(--dim);font-weight:600} .insub .loc{color:var(--dim);white-space:nowrap} .insub .cd{color:var(--ink);opacity:.85;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}

.fnblk{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:7px 9px}
.fnhdr{display:flex;align-items:center;gap:8px;margin-bottom:4px;padding-bottom:4px;border-bottom:1px solid var(--line)}
.fnhdr code{font:600 13px var(--mono)} .fnpct{margin-left:auto;font:600 13px var(--mono);color:var(--dim)}
.ln{display:grid;grid-template-columns:46px 34px 44px 82px 1fr;gap:7px;align-items:center;padding:1px 0}
.lnex{text-align:right;font:10.5px var(--mono);color:var(--dim)}
.lnbarw{background:var(--line);border-radius:3px;height:7px;overflow:hidden}
.lnbar{display:block;height:100%;border-radius:3px}
.lnpct{text-align:right;font:600 11px var(--mono)}
.lnloc{font:11px var(--mono);color:var(--dim);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.lncode{font:11px var(--mono);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.dim2{color:var(--dim);font-style:italic}
</style>"""

# ================= per-tab build =================
TABS = [
  # (tag, label, cycles_M, secs, iters, desc, prev_folded_for_delta)
  ("ship1000", "Ship 1000", 198.75, 12.4, 1000,
   "The shipped engine: pure-1000 &times; stable-stop-512 on the bench board (contested positions run longer; corpus mean ~18.5s).",
   None),
  ("incr2", "HEAD +perf", 192.02, 12.0, 1000,
   "Ship 1000 plus tonight's perf pair: widenNode O2 (&minus;1.09%) and the incremental near-mask (&minus;2.32%, root-mask copy + descent-stone stamps with capture fallback). Net &minus;3.39%.",
   "profile_folded_ship1000.folded"),
  ("v2prior", "NN prior", 300.44, 18.8, 1000,
   "THE SHIPPED ENGINE, profiled over the NEW 5-position bench (opening&rarr;endgame rotation; mean reproduces the ~18.5s field move time — single-board numbers compare via -DBENCH_SINGLE). Kernel arc &minus;18.4% shipped; the 24&rarr;8&rarr;1 int8 MLP replaces the hand prior: L0 +4.4pp, human flat; candidatePrior = 14.3% of the full-game move (the old midgame-only profile overweighted it at 24%). Flash 27,796 (876 free).",
   "profile_folded_ship1000.folded"),
]

def build_tab(tag, full_move_m, prev_path):
    edges, leaves, total = parse(f"{SCR}/profile_folded_{tag}.folded")
    root = build(edges)
    global SYN
    SYN = set()
    _inside = json.load(open(f"{SCR}/inside_{tag}.json"))
    def inject(node, parts):
        for pr in parts:
            nm = pr['fn']
            if '(own' in nm or 'small inlined' in nm: continue
            cyc = pr['pct'] / 100.0 * root.total
            if cyc < root.total * 0.004: continue
            k = node.kids.get(nm)
            if k is None:
                k = Node(nm); node.kids[nm] = k; SYN.add(id(k))
            k.total += cyc
    inject(root, _inside['think']['parts'])
    _wn = root.kids.get('widenNode')
    if _wn: inject(_wn, _inside['widenNode']['parts'])
    if 'candidatePrior' in _inside and _wn:
        _cp = _wn.kids.get('candidatePrior')
        if _cp: inject(_cp, _inside['candidatePrior']['parts'])
    flame = frames_html(layout(root))

    newp = {k: v / total * 100 for k, v in leaves.items()}
    oldp = {}
    if prev_path:
        try:
            _, ol, ot = parse(prev_path)
            oldp = {k: v / ot * 100 for k, v in ol.items()}
        except FileNotFoundError: pass
    calls = {}
    try:
        for line in open(f"{SCR}/profile_calls_{tag}.txt"):
            line = line.strip()
            if not line: continue
            nm, c = line.rsplit(' ', 1)
            calls[clean(nm)] = calls.get(clean(nm), 0) + int(c)
    except FileNotFoundError: pass
    rows = []
    for fn in sorted(newp, key=lambda k: -newp[k])[:20]:
        npv = newp[fn]; nowM = npv / 100 * full_move_m
        ov = oldp.get(fn); d = DOT[color(fn)]
        was = f"{ov:.1f}%" if ov is not None else "&mdash;"
        cc = calls.get(fn)
        callstr = fmtcalls(cc) if cc else "&mdash;"
        cpc = f"{nowM*1e6/cc:,.0f}" if cc else "&mdash;"
        if ov is None: dtxt, cls = ("&mdash;" if not prev_path else "new"), ""
        else:
            dd = nowM - ov / 100 * BASELINE_MOVE
            cls = "up" if dd > 3 else ("down" if dd < -3 else "")
            dtxt = f"{dd:+.0f}M"
        rows.append(f'<tr><td><span class="dot {d}"></span><code>{fn}</code></td>'
                    f'<td class="n pre">{was}</td>'
                    f'<td class="n">{npv:.1f}%</td><td class="n">{nowM:.0f}M</td>'
                    f'<td class="n">{callstr}</td><td class="n dim3">{cpc}</td>'
                    f'<td class="n {cls}">{dtxt}</td></tr>')
    table = ''.join(rows)

    perline = json.load(open(f"{SCR}/perline_{tag}.json"))
    maxln = max((ln['pct'] for fn in perline for ln in fn['lines']), default=1)
    pl = []
    for fn in perline:
        c = color(fn['fn'])
        pl.append(f'<div class="fnblk"><div class="fnhdr"><span class="dot" style="background:{c}">'
                  f'</span><code>{esc(fn["fn"])}</code><span class="fnpct">{fn["pct"]}%</span></div>')
        for ln in fn['lines']:
            w = ln['pct'] / maxln * 100
            code = esc(ln['text']) if ln['text'] else '<span class="dim2">inlined helper / macro</span>'
            pl.append(f'<div class="ln"><span class="lnbarw"><span class="lnbar" '
                      f'style="width:{w:.1f}%;background:{c}"></span></span>'
                      f'<span class="lnpct">{ln["pct"]:.2f}</span>'
                      f'<span class="lnex">&times;{fmtex(ln.get("ex", 0))}</span>'
                      f'<span class="lnloc">{esc(ln["loc"])}</span>'
                      f'<code class="lncode">{code}</code></div>')
        pl.append('</div>')
    perline_html = ''.join(pl)

    inside = json.load(open(f"{SCR}/inside_{tag}.json"))
    def inside_card(key, title):
        d = inside[key]
        out = [f'<div class="inblk"><div class="fnhdr"><code>inside {esc(title)}</code>'
               f'<span class="fnpct">{d["total"]}%</span></div>']
        for p in d['parts']:
            w = p['pct'] / d['total'] * 100
            c = color(p['fn'].split(' ')[0])
            nm = (esc(p['fn']).replace('(own code)', '<i>(own code)</i>')
                  .replace('(small inlined)', '<i>(small inlined)</i>'))
            out.append(f'<div class="inln"><span class="lnbarw"><span class="lnbar" '
                       f'style="width:{w:.0f}%;background:{c}"></span></span>'
                       f'<span class="p">{p["pct"]:.2f}</span><span class="nm">{nm}</span></div>')
            if p.get('lines'):
                out.append('<div class="insub">')
                for ln in p['lines']:
                    code = esc(ln['text']) if ln['text'] else '<span class="dim2">&mdash;</span>'
                    out.append(f'<div class="r"><span class="p">{ln["pct"]:.2f}</span>'
                               f'<span class="ex">&times;{fmtex(ln.get("ex", 0))}</span>'
                               f'<span class="loc">{esc(ln["loc"])}</span>'
                               f'<span class="cd">{code}</span></div>')
                out.append('</div>')
        out.append('</div>')
        return ''.join(out)
    inside_html = ('<div class="insidegrid">' + inside_card('think', 'think()')
                   + inside_card('widenNode', 'widenNode')
                   + (inside_card('candidatePrior', 'candidatePrior')
                      if 'candidatePrior' in inside else '')
                   + '</div>')
    return flame, table, perline_html, inside_html

def fmtex(n):
    if not n: return ''
    if n >= 1e6: return f'{n/1e6:.1f}M'
    if n >= 1e4: return f'{n/1e3:.0f}K'
    if n >= 1e3: return f'{n/1e3:.1f}K'
    return str(n)

def esc(sx):
    return sx.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')

DOT = {'#5a5a5a':'root','#8a6bd6':'math','#d64b4b':'flood','#e0a93b':'playout',
       '#4b93d6':'tree','#e8833a':'prior'}
def fmtcalls(n):
    return f"{n/1000:.1f}K" if n >= 1000 else str(n)

SUB = open(f"{SCR}/frag_sub.html").read()
CHANGELOG = open(f"{SCR}/frag_changelog.html").read()
NETNOTE = open(f"{SCR}/frag_net.html").read()
VERNOTE = open(f"{SCR}/frag_ver.html").read()

tab_contents = []
tab_bar = []
tab_radios = []
tab_css_rules = []
for i, (tag, label, mcyc, secs, iters, desc, prev) in enumerate(TABS):
    FULL_MOVE_M = mcyc   # per-tab absolute for the flame tooltips
    flame, table, perline_html, inside_html = build_tab(tag, mcyc, prev)
    checked = ' checked' if i == 0 else ''
    tab_radios.append(f'<input type="radio" name="tab" id="tab-{tag}" class="tabr"{checked}>')
    tab_bar.append(f'<label for="tab-{tag}">{label} &middot; {iters} iters &middot; {secs}&nbsp;s</label>')
    tab_css_rules.append(f'#tab-{tag}:checked ~ .tabc-{tag}{{display:block}}')
    tab_css_rules.append(f'#tab-{tag}:checked ~ .tabbar label[for="tab-{tag}"]{{background:var(--accent);color:#140f0b;border-color:var(--accent)}}')
    delta_note = '' if prev else '<p class="note" style="border-color:var(--dim);margin-top:8px">No b394ae5 column: the opening bench position is new with this two-tab profile; deltas are tracked on the midgame tab.</p>'
    tab_contents.append(f"""<div class="tabc tabc-{tag}">
<p class="note" style="margin-top:0">{desc}</p>
<div class="stats">
<div class="stat"><b>{mcyc:.0f}M</b><span>cycles / think</span></div>
<div class="stat"><b>{secs}s</b><span>per move @16MHz</span></div>
<div class="stat"><b>{iters}</b><span>iterations</span></div>
</div>
<h2>Flamegraph — width = share of think cycles</h2>
<div class="flame">{flame}</div>
<div class="legend"><span><i class="dot prior"></i>prior / shape eval</span><span><i class="dot" style="background:repeating-linear-gradient(45deg,rgba(0,0,0,.35) 0 2px,#888 2px 4px)"></i>hatched = inlined (addr2line)</span><span><i class="dot flood"></i>liberty floods</span><span><i class="dot playout"></i>playout</span><span><i class="dot math"></i>fixed-point math</span><span><i class="dot tree"></i>tree/alloc</span></div>
<h2>Self-time — cycles per think</h2>
<div class="tblwrap"><table><thead><tr><th>function</th><th class="n">b394ae5</th><th class="n">now</th><th class="n">Mcyc</th><th class="n">calls</th><th class="n">cyc/call</th><th class="n">&Delta;</th></tr></thead><tbody>{table}</tbody></table></div>
{delta_note}
<h2>Inside the composite blocks</h2>
{inside_html}
<h2>Hot lines</h2>
<div class="plgrid">{perline_html}</div>
</div>""")

TAB_CSS = ('<style>.gstats{display:flex;gap:34px;flex-wrap:wrap;margin:6px 0 18px}.gstats span{display:flex;flex-direction:column}.gstats b{font:700 22px/1.15 var(--mono);letter-spacing:-.02em;color:var(--accent)}.gstats i{font-style:normal;color:var(--dim);font-size:10.5px;text-transform:uppercase;letter-spacing:.07em;margin-top:2px}.tabr{display:none}.tabc{display:none}'
           '.tabbar{display:flex;gap:8px;margin:0 0 14px}'
           '.tabbar label{cursor:pointer;padding:7px 14px;border:1px solid var(--line);'
           'border-radius:8px;font:600 12.5px system-ui;color:var(--dim);background:var(--panel)}'
           + ''.join(tab_css_rules) + '</style>')

HTML = CSS + TAB_CSS + f"""
<div class="wrap">
<h1>ArduGo Search — On-Device Profile</h1>
<div class="gstats"><span><b>&minus;53.4%</b><i>midgame vs b394ae5</i></span><span><b>&minus;56.7%</b><i>all-time think</i></span><span><b>27.8K</b><i>flash &middot; 824&nbsp;B free</i></span><span><b>2,147</b><i>RAM &middot; think stack 200&nbsp;B</i></span></div>
{''.join(tab_radios)}
{('<div class="tabbar">'+''.join(tab_bar)+'</div>') if len(TABS)>1 else ''}
{''.join(tab_contents)}
{CHANGELOG}
{NETNOTE}
{VERNOTE}
</div>
<div id="tip"></div>
<script>
var tip=document.getElementById('tip');
document.querySelectorAll('.f').forEach(function(e){{
 e.onmousemove=function(ev){{tip.textContent=e.dataset.t;tip.style.opacity=1;tip.style.left=(ev.clientX+12)+'px';tip.style.top=(ev.clientY+12)+'px';}};
 e.onmouseleave=function(){{tip.style.opacity=0;}};
}});
</script>"""

open(f"{SCR}/profile_artifact.html","w").write(HTML)
print("wrote profile_artifact.html", len(HTML), "bytes")
