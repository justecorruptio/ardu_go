# On-Device Profile — flamegraph regeneration

Regenerates the **"ArduGo Search — On-Device Profile"** artifact
(claude.ai id `0ad79891-a0f1-497c-9640-abb295f5af27` — publish with that id as the
`url` param to update in place, favicon 🔥).

A two-tab (Opening / Midgame) cycle-accurate flamegraph of `think()`: flame widths =
share of think cycles, a self-time table, per-line hot spots, and the "inside the
composite blocks" inlined-origin breakdown. Everything is derived from `avrprof.js`
simulating the real AVR firmware.

## Pipeline
`./run.sh` (with a working `arduino-cli` + the avr-gcc toolchain) does it all:
```
gen_profile.sh   compile bench_avr @400 iters -> avrprof -> profile_folded_$TAG.folded
                 + profile_calls_$TAG.txt, and lineprof2 -> perline_$TAG.json
gen_inside.sh    same lineprof2 trace -> inside_$TAG.json (origin breakdown of
                 think()/widenNode, by inlined function + hot lines)
gen_flame.py     assembles the two tabs + the static text fragments -> profile_artifact.html
```
Output lands in `work/`. `SKIP_DATA=1 ./run.sh` reuses `work/*.folded` and just re-assembles.

Depends on the durable JS profilers in `../emu/`: `avrprof.js` (call-stack cycle
profiler), `lineprof2.js` (per-line), `avrbench.js` (absolute cyc/move). Tabs:
midgame = default bench position; opening = `-DBENCH_OPENING` (8-stone, 600-iter boost).

## The absolute numbers are hand-set (measured separately)
`gen_profile.sh` forces `-DMCTS_ITERATIONS=400` for **both** tabs so the flame
*proportions* are comparable. The headline **Mcyc/move** per tab (and the gstats bar
+ closing note) are the real shipped numbers from the emulator bench
(`../emu/run.sh`, 400 iters mid / 600 iters open) and live as literals in `gen_flame.py`:
`TABS[...]` (`cycles_M`, `secs`), the `gstats` `<div>`, and the closing figures inside
`frag_changelog.html`. **Re-measure with `../emu/run.sh` and update those by hand when
the engine speed changes.** Current: mid 196.95M/12.3 s, open 248.14M/15.5 s, flash 27,760 (912 B free) -- commit `fae9794` (after the -5.9% MCTS speed grind; the Opening tab is the NN-handoff worst case, not the typical instant NN opening).

## Fragments
`frag_changelog.html` holds the entire post-tabs region (intro note + the numbered
changelog + the closing method/summary notes). `frag_net.html` / `frag_ver.html` /
`frag_sub.html` are empty on purpose — `gen_flame.py` concatenates all four, so the
split doesn't matter; everything was merged into the changelog fragment. Edit the
changelog text there (add an `<li>`), not in `gen_flame.py`.

## Provenance note
The original `gen_profile.sh` / `gen_inside.sh` / fragment files were lost to a
`/private/tmp` cleanup on 2026-08-06 and reconstructed here (gen_profile from a full
reading, gen_inside from its head + the JSON schema + an end-to-end validation run).
The `profile_prev.folded` b394ae5 delta baseline was not recoverable, so the midgame Δ
column is retired (both tabs now show absolutes only).

## Lesson embedded in the changelog
Changelog #137 documents `BETA_TAB` as a −6.9% win; a later flash trim removed it as
"tiny root-only" and it cost +7.9% (caught by re-profiling, fixed with a 32-entry
table). See `../../` commit `4977e5f` and memory `ardu-go-flash-campaign`.

## Publish
`Artifact` tool, `file_path` = `work/profile_artifact.html`, `url` = the id above,
`title` "ArduGo Search — On-Device Profile", `favicon` 🔥.
