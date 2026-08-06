# Move Quality by Phase — artifact regeneration

Regenerates the **"ArduGo vs GnuGo L0 — Move Quality by Phase"** artifact
(claude.ai artifact id `b3a6feee-3867-4d99-86b2-d3476071426f` — publish with that
id as the `url` param to update in place).

The artifact grades ArduGo's move quality against **KataGo's true winrate** over a
corpus of games played vs GnuGo L0. Two live sections + two frozen ones:

| Section (HTML marker)      | Source                        | Status |
|----------------------------|-------------------------------|--------|
| Winrate trajectory `TRAJ`  | KataGo per-move winrate        | **live** — regenerated here |
| Per-phase loss `KATA`      | KataGo per-move winrate loss   | **live** — regenerated here |
| Suggestion pass `SUGG`     | KataGo 48-visit best-move pass | frozen / OUTDATED (pre-NN) |
| Attack classes `CLS`       | board-replay blunder classifier| frozen / OUTDATED (pre-NN) |
| ~~GnuGo estimate_score~~   | opendiag gnugo-margin          | **DELETED** (see gotcha #1) |

## Pipeline

### Stage 1 — data (slow, ~20 min for 250 games)
```
cd <workdir>
/tmp/hbin_diag opendiag 250 1000        # 250 ArduGo-vs-GnuGo-L0 games, seed offset 1000
                                        # -> game_1000.sgf ... game_1249.sgf  (+ opendiag.txt)
python3 kata_review.py kata.jsonl game_*.sgf
                                        # KataGo b6c96 @ 8 visits, every turn of every game
                                        # -> kata.jsonl : {"g":game,"t":ply,"wr":black_winrate,"sl":scoreLead}
```
`opendiag` is harness mode in `../harness.cpp` (built to `/tmp/hbin_diag`).
`kata_review.py` hardcodes **MODEL** (`kata_b6.txt.gz`, the KataGo b6c9…/b6c96 net)
and **CFG** (`kata_analysis.cfg`, here) — repoint both to wherever they live.

### Stage 2 — inject + assemble (fast)
```
python3 kata_hist.py     kata.jsonl kata_hist.json      # per-phase winrate-loss histogram
python3 kata_inject.py   kata_hist.json  <html>  final  # rewrites <!--KATA-->…<!--/KATA-->
python3 traj_recompute.py kata.jsonl traj_pct.json      # per-move percentiles, AI perspective
python3 traj_inject.py                                  # rewrites <!--TRAJ-->…<!--/TRAJ-->
python3 assemble.py                                     # structural: sub/note/foot, deletes, banners
```
`kata_inject.py`, `traj_inject.py`, `assemble.py` hardcode **SP** / **P** = the working
`blunder_hist.html`. Start from `base_template.html` here (copy it to the workdir as
`blunder_hist.html`), repoint the SP constant, then publish the result.

`assemble.py` is idempotent-ish but **run it once** on a freshly-injected file: it deletes
the gnugo section + "25 worst moves" block, marks SUGG/CLS OUTDATED, and rewrites the
subtitle/intro-note/footer. Re-running on already-assembled HTML will fail its asserts
(that's the guardrail).

## Gotchas (why the design is what it is)

1. **The gnugo-margin section was deleted, not updated.** ArduGo now opens with its
   learned NN policy, and NN opening moves **bypass opendiag's gnugo-query path** — only
   ~123 of ~250 games' opening moves get a gnugo margin logged. gnugo `estimate_score`
   literally cannot see the NN opening. KataGo (SGF-based, post-hoc) sees every move, so
   it's the only valid evaluator for the current engine. Don't resurrect the gnugo section.

2. **The opening is stochastic** (softmax sampling), so the corpus varies run to run and
   `movecmp` (argmax) is blind to it — always evaluate opening changes on a fresh corpus,
   never by move-compare.

3. **Phase boundaries are by ply index t**: opening `t<=15`, middle `t<=35`, endgame `t>=36`
   (same split in `kata_hist.py` and the `traj_inject.py` phase rules).

4. **AI-perspective winrate**: KataGo reports black-perspective. AI plays black when
   `game_id % 2 == 0`, white otherwise. Convert with `wr if g%2==0 else 1-wr`; count AI
   moves only where `mover_black == ai_black`.

5. **SUGG + CLS need extra passes** not in the stage-1 driver: `kata_suggest.py`
   (48-visit best-move at each ≥25% blunder) and `kata_classify.py` (board-replay tagging).
   Until those are re-run on the NN corpus, both panels stay labelled OUTDATED · PRE-NN.

## Publish
`Artifact` tool, `file_path` = assembled html, `url` = the artifact URL above (updates in
place — omitting it mints a new URL), `title` "ArduGo vs GnuGo — Move Quality by Phase",
`favicon` ⚫.
