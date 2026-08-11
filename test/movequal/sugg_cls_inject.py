#!/usr/bin/env python3
# Rewrite the SUGG + CLS sections of blunder_hist.html from fresh corpus data
# (drops the OUTDATED badges; updates the top note). Recreated 2026-08-11.
# Usage: sugg_cls_inject.py <workdir>
import sys, os, json
WD = sys.argv[1]
P = os.path.join(WD, "blunder_hist.html")
S = json.load(open(os.path.join(WD, "sugg_stats.json")))
C = json.load(open(os.path.join(WD, "classes.json")))
html = open(P).read()
n = S["n"]

# ---- SUGG section --------------------------------------------------------
# chart 1: line distribution, played vs KataGo (lines 1..5+)
pl, bl = S["pl_lines"][1:], S["bl_lines"][1:]
mx = max(max(pl), max(bl)); W, H, X0, Y0 = 560, 190, 40, 168
bars = []
labels = ["1", "2", "3", "4", "5+"]
for i in range(5):
    gx = X0 + i * 104
    for j, (v, col) in enumerate([(pl[i], "var(--bad)"), (bl[i], "var(--good)")]):
        h = 150.0 * v / mx
        bars.append(f'<rect x="{gx + j*34}" y="{Y0 - h:.1f}" width="30" height="{h:.1f}" rx="2" '
                    f'fill="{col}"><title>line {labels[i]} {"played" if j==0 else "KataGo"}: {v}</title></rect>')
        bars.append(f'<text class="axis" x="{gx + j*34 + 15}" y="{Y0 - h - 4:.1f}" text-anchor="middle">{v}</text>')
    bars.append(f'<text class="axis" x="{gx + 34}" y="{Y0 + 14}" text-anchor="middle">line {labels[i]}</text>')
svg1 = f'<svg viewBox="0 0 {W} {H}" width="100%" role="img">' + "".join(bars) + "</svg>"

# chart 2: best-move winrate histogram (5% buckets)
h = S["hist"]; mx2 = max(h); W2 = 860
bars2 = []
for i, v in enumerate(h):
    x = 36 + i * 41
    bh = 150.0 * v / mx2
    col = "var(--good)" if i >= 10 else "var(--bad)"
    bars2.append(f'<rect x="{x}" y="{168 - bh:.1f}" width="36" height="{bh:.1f}" rx="2" fill="{col}" '
                 f'opacity="0.85"><title>{i*5}-{i*5+5}%: {v}</title></rect>')
bars2.append('<line x1="446" y1="8" x2="446" y2="168" stroke="var(--dim)" stroke-dasharray="3 3" opacity="0.6"/>')
for pct in (0, 25, 50, 75, 100):
    bars2.append(f'<text class="axis" x="{36 + pct*8.2}" y="184" text-anchor="middle">{pct}%</text>')
svg2 = f'<svg viewBox="0 0 {W2} 190" width="100%" role="img">' + "".join(bars2) + "</svg>"

winnable_pct = round(100 * S["winnable"] / n)
adj_pct = round(100 * S["adj"] / n)
pl12, bl12 = sum(pl[:2]), sum(bl[:2])
sugg_html = f'''<!--SUGG--><h1 style="font-size:17px;margin-top:8px">KataGo corrections — every ≥25% blunder re-queried at 48 visits</h1>
<div class="note" style="border-color:var(--good)"><b>Suggestion pass ({n} positions, refreshed on the current-engine corpus):</b> for each move that lost ≥25% winrate, KataGo's preferred move at the same position. <b>{winnable_pct}%</b> of these positions were still winnable (suggested move keeps the mover ≥50%); only <b>{adj_pct}%</b> of corrections are adjacent to the move played — the engine picks the wrong fight, not the wrong local shape. {S["passes"]} blunders were passes (was 51 pre-NN — the settled-territory pass gate shipped in between).</div>
<style>.sgt{{border-collapse:collapse;font:12px var(--mono);width:100%}}.sgt th,.sgt td{{padding:4px 10px;text-align:right;border-bottom:1px solid var(--line);white-space:nowrap}}.sgt th{{color:var(--dim);font-weight:500}}.duo{{display:grid;grid-template-columns:1fr 1fr;gap:22px}}@media(max-width:760px){{.duo{{grid-template-columns:1fr}}}}</style>
<div class="duo">
<div class="phase"><div class="phead"><div class="pname" style="font-size:14px">Where the winning move was</div><div class="chips"><span class="chip"><span style="color:var(--bad)">■</span> played</span><span class="chip"><span style="color:var(--good)">■</span> KataGo</span></div></div>
<div class="chartwrap">{svg1}</div>
<p class="sig">The pre-NN "plays too high" asymmetry is gone: lines 1–2 now get {pl12} played vs {bl12} preferred (was 1,399 vs 1,739 — a 24% gap). The remaining corrections are distributed like the played moves; height is no longer the story.</p></div>
<div class="phase"><div class="phead"><div class="pname" style="font-size:14px">Winrate still available at the blunder</div></div>
<div class="chartwrap">{svg2}</div>
<p class="sig">Mover's winrate had KataGo's move been played (5% buckets). {winnable_pct}% of the mass sits right of the 50% line: these are live games being thrown, not lost causes.</p></div>
</div>
'''

# ---- CLS section ---------------------------------------------------------
META = [
 ("serial",  "Serial collapse",        "another ≥25% blunder within 4 turns in the same game",
   "not a move class — a compounding multiplier; now 65% of blunders (was 53%): the residue clusters"),
 ("local",   "Local shape",            "right area, wrong point (correction adjacent to played)",
   "pattern-table domain: 3×3 priors at the chosen point"),
 ("edge2",   "Edge blindness — line 2","winning move on the second line, we played line 3+",
   "shrunk 21%→15% since the low-line exemption work; admission priced neutral"),
 ("tenuki",  "Left the hot area",      "tenuki ≥3 from the last move; correction answers it",
   "last-move locality credit at the root"),
 ("fed",     "Fed a weak group",       "played stone ends with ≤2 liberties",
   "endgame-arc 2026-08-11: feeding-veto measured harmful (needs ~10× verdict precision); playout-probe class dead"),
 ("edge1",   "Edge blindness — line 1","winning move on the first line, we played line 3+",
   "admission priced: both-colors exemption EL2B1 measured −15/1000"),
 ("atari",   "Ignored an atari",       "a chain sat in atari; correction addresses it, we did not",
   "tactical-gate credit for atari answers"),
 ("capture", "Missed a capture",       "enemy chain in atari; correction takes it",
   "tactical-gate credit for capturing moves"),
 ("pass",    "Premature pass",         "passed with live boundaries on the board",
   "FIXED: settled-territory pass gate shipped (6dfe150) — 51 pre-NN → 2 now"),
]
cls = C["classes"]; total = C["total"]
maxn = max(cls[k]["n"] for k, *_ in META)
rows = []
for key, name, mech, attack in META:
    a = cls[key]
    if a["n"] == 0: continue
    pct = round(100 * a["n"] / total)
    avg = round(100 * a["loss"] / max(a["n"], 1))
    segs = []
    for ph, col, lab in zip(a["ph"], ["var(--good)", "var(--accent)", "var(--bad)"],
                            ["open", "mid", "end"]):
        if ph == 0: continue
        w = 100.0 * ph / maxn
        segs.append(f'<div title="{lab}: {ph}" style="width:{w:.2f}%;background:{col};height:100%"></div>')
    rows.append(
      f'<div style="display:grid;grid-template-columns:170px 1fr;gap:12px;align-items:center;margin:7px 0">'
      f'<div style="text-align:right"><div style="font-weight:600;font-size:13px">{name}</div>'
      f'<div style="font:11px var(--mono);color:var(--dim)">{a["n"]:,} · {pct}% · Ø{avg}% lost</div></div>'
      f'<div><div style="display:flex;height:16px;border-radius:4px;overflow:hidden;background:var(--line)">{"".join(segs)}</div>'
      f'<div style="font-size:11.5px;color:var(--dim);margin-top:2px"><b style="color:var(--ink);font-weight:600">{mech}</b> — attack: {attack}</div></div></div>')
cls_html = f'''<!--CLS--><h1 style="font-size:17px;margin-top:8px">Attack classes — {total} blunders, non-exclusive</h1>
<div class="note"><b>Board-replay classification (refreshed on the current-engine corpus):</b> each ≥25% blunder replayed to its position and tagged by mechanism (a move can carry several tags). Bars show volume split by phase (<span style="color:var(--good)">■ opening</span> <span style="color:var(--accent)">■ middle</span> <span style="color:var(--bad)">■ endgame</span>), scaled to the largest class. {C["untagged"]} moves ({round(100*C["untagged"]/total)}%) carry no tactical tag — the whole-board calibration residue. {C["games_with"]} of {C["ngames"]} games contain at least one ≥25% blunder; {C["games_3plus"]} games contain three or more.</div>
<div class="phase">{"".join(rows)}</div><!--/CLS-->'''

# ---- splice --------------------------------------------------------------
i1 = html.index("<!--SUGG-->"); i2 = html.index("<!--CLS-->")
i3 = html.index("<!--/CLS-->") + len("<!--/CLS-->")
html = html[:i1] + sugg_html + cls_html + html[i3:]
old_note = ("The two panels at the bottom (suggestion pass, attack classes) are "
            "retained from the earlier pre-NN corpus and are labelled outdated.")
new_note = ("The two panels at the bottom (suggestion pass, attack classes) are "
            "refreshed on this same corpus.")
html = html.replace(old_note, new_note)
open(P, "w").write(html)
print(f"SUGG+CLS injected -> {P} ({len(html)} bytes)")
