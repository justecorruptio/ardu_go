#!/usr/bin/env python3
# Structural transform on blunder_hist.html (post inject):
#  - update subtitle + top note (gnugo-margin references are gone)
#  - delete "The 25 worst moves of the corpus" block (SUGG)
#  - delete the whole "GnuGo estimate_score version" section + old footer, add new footer
#  - mark SUGG + CLS panels OUTDATED (pre-NN corpus)
import re, sys

P = '/private/tmp/claude-501/-Users-jay-workspace/5b06ba38-9a1c-46e2-9ca0-ed5be982489d/scratchpad/blunder_hist.html'
s = open(P).read()
n0 = len(s)

CHIP = ('<span style="font:600 10px var(--mono);color:var(--bad);border:1px solid var(--bad);'
        'border-radius:4px;padding:1px 6px;margin-left:8px;vertical-align:middle;'
        'letter-spacing:0.5px">OUTDATED · PRE-NN</span>')

# 1) Subtitle ---------------------------------------------------------------
old_sub = ('<div class="sub">1,200 games · 27,264 AI moves · 1-point buckets, '
           'drift-centered per phase</div>')
new_sub = ('<div class="sub">250 games vs GnuGo L0 · 6,546 AI moves · scored by KataGo true '
           'winrate · ArduGo now opens with a learned NN policy</div>')
assert old_sub in s, 'subtitle not found'
s = s.replace(old_sub, new_sub)

# 2) Top note (described the deleted gnugo-margin reading) -------------------
new_note = ('<div class="note"><b>What changed:</b> ArduGo gained a learned neural-net opening '
            'since the last measurement, so this is a re-run. Move quality is now scored by '
            'KataGo’s true winrate — a real evaluator that cannot be “gained” against — '
            'replacing the earlier gnugo-margin proxy, which could not see opening quality and has '
            'been dropped. The two panels at the bottom (suggestion pass, attack classes) are '
            'retained from the earlier pre-NN corpus and are labelled outdated.</div>')
s, k = re.subn(r'<div class="note"><b>Reading the charts:</b>.*?</div>', new_note, s, flags=re.S)
assert k == 1, f'top note replace matched {k}'

# 3) Delete "25 worst moves" block (up to the CLS marker) -------------------
s, k = re.subn(
    r'<div class="phase"><div class="phead"><div class="pname" style="font-size:14px">'
    r'The 25 worst moves of the corpus.*?(?=<!--CLS-->)', '', s, flags=re.S)
assert k == 1, f'25-worst delete matched {k}'

# 4) Mark SUGG outdated: chip in the h1 + red banner after it ---------------
sugg_banner = ('\n<div class="note" style="border-color:var(--bad)"><b>Pre-NN corpus.</b> '
               'This suggestion pass is from the earlier 1,200-game run, before ArduGo had a '
               'learned opening. The 2,962-blunder count and the line distribution predate the NN '
               '— the mechanism holds, but the magnitudes shift once re-run on the current engine.</div>')
old_sugg_h1 = 'blunder re-queried at 48 visits</h1>'
assert old_sugg_h1 in s, 'SUGG h1 not found'
s = s.replace(old_sugg_h1, f'blunder re-queried at 48 visits{CHIP}</h1>{sugg_banner}')

# 5) Mark CLS outdated ------------------------------------------------------
cls_banner = ('\n<div class="note" style="border-color:var(--bad)"><b>Pre-NN corpus.</b> '
              'Attack-class volumes are from the earlier 1,200-game run. The NN opening most changes '
              'the opening-phase bars — read this as the mechanism breakdown, not current counts.</div>')
old_cls_h1 = 'Attack classes — 2,962 blunders, non-exclusive</h1>'
assert old_cls_h1 in s, 'CLS h1 not found'
s = s.replace(old_cls_h1, f'Attack classes — 2,962 blunders, non-exclusive{CHIP}</h1>{cls_banner}')

# 6) Delete the GnuGo estimate_score section + old footer; add new footer ---
new_foot = ('<div class="foot">Corpus: 250 harness self-play games vs GnuGo level 0 (reproducible '
            'seeds, offsets 1000–1249), each reviewed post-hoc by KataGo (b6c96 net, 8 visits). '
            'ArduGo now opens with its learned NN policy, so the opening phase reflects real NN play '
            '— unlike the earlier gnugo-margin measurement, which could not evaluate the opening at '
            'all and has been removed. Winrate is shown from the mover’s perspective; ≥25% loss = a '
            'blunder. y-scales are per-phase.</div></div>')
s, k = re.subn(
    r'<h1 style="font-size:17px;margin-top:8px">GnuGo estimate_score version \(drift-centered points\)</h1>.*$',
    lambda m: new_foot, s, flags=re.S)
assert k == 1, f'gnugo section delete matched {k}'

# sanity: exactly one wrap close at EOF, no stray gnugo/25-worst text
assert 'estimate_score' not in s, 'gnugo text残留'
assert '25 worst' not in s, '25-worst text残留'
assert s.rstrip().endswith('</div></div>'), 'wrap close missing'

open(P, 'w').write(s)
print(f'assemble: {n0} -> {len(s)} bytes (delta {len(s)-n0})')
