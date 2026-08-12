#!/usr/bin/env python3
# Replace the CLS section of blunder_hist.html with the finer non-exclusive
# taxonomy from kata_classify2.py (classes2.json). Usage: cls2_inject.py <workdir>
import sys, os, json
WD = sys.argv[1]
P = os.path.join(WD, "blunder_hist.html")
C = json.load(open(os.path.join(WD, "classes2.json")))
html = open(P).read()
T = C["tags"]; total = C["total"]

# campaign context per tag (measured verdicts where they exist)
NOTE = {
 "feed-doomed":  "the midgame-hunt dominant class: playouts believe the save at ~79%; all 4 static classifiers priced dead",
 "missed-save":  "tactical-gate credit for atari answers",
 "missed-capture":"tactical-gate credit for capturing moves",
 "missed-attack":"2-liberty pressure — playout kill-bias territory",
 "ignored-atari":"tactical-gate credit; residue after the rootSelfAtari veto",
 "captured-soon":"fresh stone thrown straight into capture",
 "self-atari":   "rootSelfAtari veto works: near-zero residue",
 "feed-weak":    "endgame-arc: feeding-veto measured harmful (needs ~10x verdict precision)",
 "connect-miss": "patternMatch already scores 3x3-local shapes — probe before adding priors",
 "cut-miss":     "CUT_WEAK prior removed for flash (-11 wins/300, borderline)",
 "crawl":        "NN-crawl probe: real (KataGo -5..-7pt) but the handoff fix measured -8.0% vs L0 (reverted) — KataGo-optimality != L0 win-rate",
 "edge2":        "low-line exemption work shrank it; admission priced neutral",
 "edge1":        "both-colors exemption EL2B1 measured -15/1000",
 "local":        "pattern-table domain: 3x3 priors at the chosen point",
 "tenuki":       "last-move locality credit at the root",
 "eye-fill":     "rare",
 "own-territory":"rare since the settled-territory pass gate",
 "pass":         "FIXED: settled-territory pass gate (51 pre-NN -> 1 now)",
 "game-losing":  "conversion is the wall: half the blunders single-handedly cross won -> lost",
 "serial":       "compounding multiplier — the residue clusters",
}
GROUPS = [
 ("life&death", "Life & death", "reading a group's fate"),
 ("tactics",    "Tactics",      "liberties, connections, cuts"),
 ("direction",  "Direction",    "right fight vs wrong fight"),
 ("waste",      "Wasted moves", "points that gain nothing"),
 ("severity",   "Severity overlays", "orthogonal context, not mechanisms"),
]
maxn = max(v["n"] for v in T.values())
blocks = []
for gkey, gname, gsub in GROUPS:
    rows = []
    for key, a in T.items():
        if a["group"] != gkey or a["n"] == 0: continue
        pct = 100 * a["n"] / total
        avg = round(100 * a["loss"] / a["n"])
        w0 = round(100 * a["wr0"] / a["n"])
        segs = []
        for ph, col, lab in zip(a["ph"], ["var(--good)", "var(--accent)", "var(--bad)"],
                                ["open", "mid", "end"]):
            if ph == 0: continue
            w = 100.0 * ph / maxn
            segs.append(f'<div title="{lab}: {ph}" style="width:{w:.2f}%;background:{col};height:100%"></div>')
        note = NOTE.get(key, "")
        rows.append(
          f'<div style="display:grid;grid-template-columns:170px 1fr;gap:12px;align-items:center;margin:7px 0">'
          f'<div style="text-align:right"><div style="font-weight:600;font-size:13px;font-family:var(--mono)">{key}</div>'
          f'<div style="font:11px var(--mono);color:var(--dim)">{a["n"]:,} · {pct:.1f}% · Ø{avg}% lost · wr {w0}%</div></div>'
          f'<div><div style="display:flex;height:16px;border-radius:4px;overflow:hidden;background:var(--line)">{"".join(segs)}</div>'
          f'<div style="font-size:11.5px;color:var(--dim);margin-top:2px"><b style="color:var(--ink);font-weight:600">{a["desc"]}</b>'
          f'{" — " + note if note else ""}</div></div></div>')
    if rows:
        blocks.append(f'<div style="margin:14px 0 4px;font-weight:600;font-size:13px">{gname} '
                      f'<span style="color:var(--dim);font-weight:400">— {gsub}</span></div>' + "".join(rows))
pairs = ", ".join(f'<span style="font-family:var(--mono)">{a}+{b}</span>&thinsp;{n}'
                  for a, b, n in C["pairs"][:6])
cls_html = f'''<!--CLS--><h1 style="font-size:17px;margin-top:8px">Blunder anatomy — {total} blunders, {sum(1 for v in T.values() if v["n"])} non-exclusive tags</h1>
<div class="note"><b>Finer board-replay taxonomy (2026-08-11, supersedes the 9-class pass):</b> each ≥25% blunder is replayed to its position and tagged by every mechanism that applies — tags are <b>non-exclusive</b>, so a move can be simultaneously a crawl, a missed attack and game-losing; columns sum past 100%. "wr" is the mover's mean winrate <i>before</i> the blunder (how alive the game still was). Bars show volume by phase (<span style="color:var(--good)">■ opening</span> <span style="color:var(--accent)">■ middle</span> <span style="color:var(--bad)">■ endgame</span>), scaled to the largest tag. Untagged residue: {C["untagged"]} moves ({round(100*C["untagged"]/total)}%, down from 21% under the coarse taxonomy). Top co-occurrences: {pairs}.</div>
<div class="phase">{"".join(blocks)}</div><!--/CLS-->'''
i2 = html.index("<!--CLS-->"); i3 = html.index("<!--/CLS-->") + len("<!--/CLS-->")
html = html[:i2] + cls_html + html[i3:]
open(P, "w").write(html)
print(f"CLS2 injected -> {P} ({len(html)} bytes)")
