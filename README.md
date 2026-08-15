# ArduGo

9×9 Go for the [Arduboy](https://arduboy.com) — a full Monte-Carlo tree
search engine, a distilled neural-network opening, learned priors and a
measured seven-rung difficulty ladder, on an ATmega32U4:
**28,672 bytes of flash, 2,560 bytes of RAM, 16 MHz, no OS.**

**v1.0 strength (all numbers from multi-set gauntlets):**

| referee | result |
|---|---|
| GnuGo level 0 | 53.3% (1598/3000) |
| KataGo human-SL 10 kyu | 44.8% (896/2000) |
| KataGo human-SL 15 kyu | 71% |
| even-game rating | ≈ 11 kyu |
| with 3 handicap stones | ≈ even vs 1 kyu |

Mean move: **15.6 s** on real hardware (cycle-exact emulator bench over a
5-position full-game rotation). Flash 28,186/28,672; RAM 2,152/2,560.

---

## The search

**MCTS with UCB1-Tuned, entirely in Q12 integer fixed point.** Software
floats cost milliseconds per node scan on AVR, so everything is integer:

- Win rates in Q6 via a hand-unrolled 7-step restoring divide
  (`winRate6`), with a documented uint16-wrap guard for visit counts
  past 1023.
- `ln N / n` via a reciprocal table (`RECIP_TAB`) plus one correction
  step that makes the floor exact; a proven shortcut pins the
  variance-capped exploration term to its saturation value when the
  confidence bound alone would exceed the cap.
- Square roots via a 192-entry LUT with range normalization.
- The whole selection arithmetic is verified by a **shadow audit**
  (`-DUCB_SHADOW`): every selection recomputed in double precision;
  the fixed-point path disagrees only on genuine near-ties (mean
  exact-value gap ≈ half a Q6 ULP, no bias).

**Budget: 1,000 iterations, shrink-only stopping.** A stable-leader
stop (the visit leader unchallenged for 512 iterations) plus a
probabilistic decided-position stop harvest the easy moves — contested
positions run long, decided ones exit in a third of the time. Measured
lesson behind the constants: the human referee punishes shallow budgets
that GnuGo cannot detect.

**Exploration constant c = ¼** (`bonus >> 2`) — the inherited constant
was tuned in a 400-iteration era and never re-swept; the full 8-point
curve (c = 1 … 1/16) showed L0 rising monotonically toward greedy but
the *human* optimum peaking at ¼. Concentration is also the speed
mechanism: greedier selection fires the stable-stop earlier
(−16% wall-clock at +3.2pp human when shipped).

**Progressive widening with latent batching.** Nodes widen on a visit
schedule (the root has a wider one). Each widen scan yields a batch of
three candidates: the best becomes active immediately, the rest are
stored *latent* (move byte flagged) and activated flag-flip-cheap by
later widen triggers — one scan amortized over three children without
disturbing the visit schedule.

**The tree lives in the screen buffer.** During think() the OLED keeps
showing the last frame while the 1 KB frame buffer becomes the node
pool (137 nodes there + 69 in statics; 6-byte nodes, 8-bit links).
When the pool fills, a reclaim pass evicts latents first, then the
least-visited off-path subtree.

**Root-only RAVE** with the Gelly–Silver β schedule computed from the
*child's* visits (a 32-entry β table covers the hot range), saturating
AMAF tables that halve on overflow, and a placement quirk: the RAVE
tables deliberately own RAM 0x800 (see *Living with the bootloader*).

## The priors

**A learned prior replaced the hand-crafted one.** Every widened
candidate is scored by a 24-feature → 4-hidden → 1 int8 MLP
(~130 bytes of weights) trained with a softmax-expected-cost objective
on KataGo-labeled positions harvested from the engine's own
trajectories. Ships at width 4 because the width ladder (H = 4…12) is
flat in *games* even where it is not offline — one of the project's
recurring lessons (see *Measurement discipline*).

- **Features** are 24 scalars computed in one tactical scan: capture /
  save / atari / doomed-ladder flags, liberty minima, group counts,
  connection and eyespace properties, a learned 3×3 pattern score,
  line/locality geometry. The scan's bound conjunctions (ladder reads,
  sole-connector tests) are preserved as precomputed flag features —
  feature-map sufficiency, not network capacity, is what limits this
  prior (measured: adding keima-geometry and merged-liberty features
  transferred nothing).
- **Feature normalization is compiled away**: the per-feature medians
  (FMID) are folded into the assembly's write sites as compile-time
  constants, so the kernel's inner loop reads differences directly.
- **The kernel is hand-scheduled asm**: all accumulators live in
  register pairs across the feature loop (the rolled loop bounced them
  through the stack), weights walk PROGMEM with `lpm Z+` via a
  dedicated temp (gcc, register-starved, was reloading the pointer per
  element), and the bias row loads as one straight-line `lpm Z+` walk.
  Net effect of the kernel campaign: −18% think, value-identical
  (pool-hash proven) at every step.
- **Prior → seeds**: a candidate's score becomes virtual games —
  positive bonus b seeds b wins in b visits, negative seeds b losses.
  The even-prior ballast under those is the *minimal* expressible
  (2 visits / 1 win): the sweep showed the classical heavy base was
  pure dilution once the ordering was worth trusting, worth +1.8pp L0
  and +4pp human. The score→seed exchange rate (`out >> 8`) sits on a
  measured two-sided optimum.

**A distilled opening network plays the quiet opening outright.**
An int8 policy net (distilled from KataGo, trained on the
expected-winrate-loss objective) picks moves instantly below 24 stones,
with per-candidate 8-symmetry canonicalization and an empty-board
komoku-with-random-symmetry opener for variety. Removing it costs
−13pp vs L0 *and* −6pp vs humans — the single most valuable 1.5 KB in
the image. (Deterministic argmax means mirrored openings can get
non-mirrored replies on exact score ties; measured benign — the model
is indifferent between tied moves.)

## The playouts

Michi-lineage probabilistic heuristics, tuned by gauntlet:

- **Tactical**: capture the opponent's just-moved group in atari
  (classified free via a one-entry liberty cache); squeeze 2-liberty
  groups (what actually kills cut-off stones in rollouts); local
  contact answers with a 5×5 support scan; nakade vital points.
- **Patterns**: a learned 3×3 table, LR-mirror-folded (triangular
  column-pair indexing, −678 bytes lossless).
- **One random draw per move**: a single 16-bit xorshift draw feeds
  every probabilistic gate through disjoint bit slices
  (distribution-exact for mask tests; the 3–5 separate draws it
  replaced were ~2% of think).
- **The throw-in exception**: rollouts may play a lone-stone self-atari
  at a controlled rate — it lets playouts actually kill dead groups,
  which resolves games early through the mercy rule (−10.8% think when
  shipped, and truer life-and-death evals).
- **Termination**: two passes (policy finds nothing acceptable), a
  capture-margin mercy rule, and a hard cap. An "endgame list" mode
  (extract the empty points at ≤16 empties, pick uniformly with
  per-color eye bans) measured −11.4% think but a small human-referee
  cost, and is banked behind a flag rather than shipped — the
  work-removal law again.
- **simPlay**, the board-mutation core, runs a single fused neighbor
  walk (capture check + liberty classification + ko material in one
  pass), a Pachi-style immediate-liberty fast path that skips floods
  for lone stones, and a packed working set (flag bits share one byte;
  the ko point is recomputed in its rare branch) that trimmed the
  callee-saved register bill from 13 to 11 on a 130k-calls/think
  function.

## Scoring and rules

Chinese (area) scoring. Game end triggers a **settle vote**: playout
consensus classifies dead stones, which are removed before territory
counting; eyespace region analysis (lazily cached per-region codes in
the empty cells' chain bytes) drives both scoring and the vital-point
priors. A closed-territory guard prevents premature pass-and-settle in
open positions.

## Living with the bootloader (AVR engineering)

RAM 0x800–0x801 is the Caterina bootloader's *magic key*, and USB
hardware can stomp it at any time — any engine state there is silently
corruptible, and a sketch that writes it can break upload auto-reset.
The discipline that grew around this:

- **Layout invariants, enforced twice**: `test/checkmagic.sh` asserts
  from the ELF that the node pool, game state, flood scratch and every
  carry-free pointer bound clear the hazard (plus that the size flags
  and LTO actually engaged — a Boards Manager update once silently
  deleted them); a runtime boot guard re-checks on device and halts
  loudly. A `magicPad[]` shim absorbs layout drift and is re-swept on
  every static-byte change (the guard has caught real hazards twice).
- **Tolerant data on the hazard**: the RAVE tables own 0x800 by
  design — a stomped AMAF count is noise, a stomped node is a crash.
- **Vendored, stripped USB**: sketch-level copies of the Arduino USB
  core shadow the originals at link time, with the Serial data plane,
  string descriptors and LED pulse code removed (≈1.2 KB saved) while
  keeping enumeration and the 1200-baud auto-reset — which parks the
  CPU in a spin after arming the watchdog so the engine can't stomp
  the key mid-upload.
- **Pointer-arithmetic tricks with proofs**: carry-free `boardAt`
  (array base's low byte bounded so index adds never carry),
  carry-correct chain pointers, all asserted by checkmagic.
- Everything hot honors the measured **register-ceiling law**: big
  bodies don't inline into register-saturated callers (spills convert
  prologue cost into per-access cost); `-Os` + LTO +
  `-mcall-prologues`, with `noinline` used to give hot leaves their
  own register file — which is what makes the asm kernels possible.

## Fitting in 28 KB (the flash campaign)

The image lives a few hundred bytes under a hard 28,672-byte ceiling;
every feature above was paid for by a measured reclaim somewhere else.
The techniques that survived (numbers are shipped deltas):

**Data representation**
- **Symmetry-fold learned tables**: the 3×3 pattern table is
  LR-mirror-folded through a triangular column-pair index — −678 B,
  lossless, ~+1% pattern-lookup cost. (The second symmetry doesn't
  fold: measured, no.)
- **Board state packs 2 bits per point** (`packedGet` walks packed
  bytes with constant shifts that unroll flat); repacking game.cpp's
  state cost nothing and saved 136 B.
- **Dense offset classes over sparse tables**: the opening net's
  pairwise features clamp offsets to ±3 with sign preserved — 98 table
  rows replacing a sparse 128.
- **Prompts as glyph indices**: the large-font dialog strings are
  stored as 1-based glyph numbers, skipping the character-set lookup.
- **Right-sized networks**: the width ladder proved H=4 carries H=8's
  strength — −284 B of weights and kernel.

**Code structure**
- **`noinline` as deduplication**: marking `candidatePrior` noinline
  saved 784 B *and* ran faster (one copy, own register file). The
  inverse is measured too: noinline on single-call-site functions costs
  bytes for nothing (+50 in one experiment). The dedupe only pays at
  multiple call sites.
- **Stack frames over 64 B are poison**: avr-gcc's large-frame
  addressing bloats every access; restructuring one function under the
  limit saved 316 B. Audit any function with big locals.
- **Shared label tables**: UI text positioned via PROGMEM
  `{x, y, string}` tables walked by one loop (−294 B in the score
  screen alone), `\n`-merged label columns as single strings (−384 B),
  and one shared number-printing method replacing six hand-rolled
  call sites (−92 B).
- **Let the compiler dedupe divides**: replacing libgcc's division
  helpers with reciprocal tricks *grew* the image (+2 to +120 B) —
  at `-Os` the call-vs-inline decisions are already size-optimal, and
  a shared helper only loses when call sites would shrink too.
  Hand-optimize hot-path divides for *speed*, never for size.
- **Per-function O2 is a two-way trade**: some hot functions buy speed
  with `optimize("O2")` (+flash); reverting one such promotion bought
  556 B back for +1% think when flash was the binding constraint.
  Every such flag is a measured, revisitable line item.

**Toolchain**
- **LTO with a value-identity proof**: enabling `-flto` saved 302 B;
  the emulator pool-hash confirmed byte-identical search behavior, and
  `checkmagic.sh` now asserts the GIMPLE fingerprint so a toolchain
  update can't silently drop it.
- **`-mcall-prologues`**: shared register save/restore helpers
  (~1 KB saved across the image) at a measured ~2.5% think cost —
  the standing size-for-speed trade.
- **Vendored, stripped USB**: shadowing the Arduino core's USB objects
  with trimmed sketch-level copies (no Serial data plane, no string
  descriptors, no LED pulses) freed ~1.26 KB while keeping enumeration
  and upload auto-reset. Full removal (a further −1.28 KB) is built
  and flag-gated but unused — it costs one-button upload.

**Discipline**
- **Verify outputs, not flags**: every claimed saving is an
  `avr-size` delta on the real device build, and behavioral claims
  ride on the pool-hash/fingerprint instruments. Build flags have
  silently failed to engage more than once.
- **Emulator-bench every deletion**: the one flash trick shipped
  without a bench — removing a 164-byte β table — cost +7.9% think
  and had to be re-discovered by profiling. Data that looks cold
  often isn't.

## Measurement discipline

The project's real engine. Everything above shipped through it:

- **Two referees, always**: GnuGo L0 (3×1000-game seed sets — single
  sets lie by ±50) and KataGo human-SL profiles via a GPU shim, with
  resignations re-adjudicated by a stronger net. The recurring
  finding: **the cheap referee's optimum is not the human optimum**
  (the exploration-constant curve peaked at different values per
  referee; offline pick-cost improvements repeatedly failed to convert
  to games). Human legs replicate on fresh seed blocks before any
  ship.
- **Cycle-exact benching**: a bench firmware runs think() in a
  ProjectABE-based emulator with a cycle counter, over five real
  positions spanning opening→endgame (single-position benches swing
  20% on stop-timing alone); the 5-position mean reproduces the field
  move-time.
- **Value-identity proofs**: refactors claiming "same values" must
  produce byte-identical node pools in the emulator (pool hash per
  bench position) and identical host fingerprints. Flag-gated features
  must build byte-identical with the flag off.
- **Profiling to the source line**: a flamegraph artifact built from
  basic-block cycle histograms with addr2line attribution, composite
  per-region breakdowns of hot functions, and a companion
  KataGo-graded move-quality artifact (win-rate trajectories vs every
  referee rank).

## The difficulty ladder

Seven rungs, every label backed by calibration games (engine vs
KataGo human-SL, 100 games per point, handicap and komi as listed):

| label | setup (human side) | anchor |
|---|---|---|
| 25 KYU | Black + 3 stones | opp+3 ≈ even far below 20k (measured 10% vs 20k) |
| 18 KYU | Black + 2 stones | measured 32% vs 17k |
| 13 KYU | Black, no komi | measured 45% vs 13k |
| 11 KYU | even | measured 44.8% vs 10k |
| 9 KYU | White, no komi | komi ≈ 1.5 ranks |
| 5 KYU | White, engine +2 stones | measured 43–44% vs 4k/6k |
| 1 KYU | White, engine +3 stones | measured 44–46% vs 1k/2k |

The calibration exposed a real 9×9 property: **ranks-per-stone grows
toward the weak end** (a stone ≈ 3 ranks among single-digit kyu, far
more among double-digit), which is why the ladder is not symmetric
around even. Handicap games place hoshi stones, set komi to 0.5, and
give White the first move.

## Building

Arduino IDE (classic 1.8.x) or arduino-cli, FQBN `arduino:avr:leonardo`,
Arduboy2 ≥ 5.2.1. The leonardo defaults provide the required
`-Os -flto -mcall-prologues -mrelax`; `test/checkmagic.sh` verifies the
layout *and* that those flags actually engaged. Run it after any change
that moves a byte of static RAM.

Host-side development: `test/harness.cpp` builds a native binary with
gauntlet drivers (GnuGo GTP, the KataGo human shim, handicap/komi
calibration modes), probes (`priorprobe`, `symprobe`, `peepseed`), and
the instrumentation flags referenced throughout (`UCB_SHADOW`,
`PLAYOUT_STATS`, `ENDLIST_STATS`, `PRIOR_DUMP`).

## Provenance

Engine constants, feature contracts and every shipped strength change
trace to logged experiments — the gauntlet logs, sweep curves and
graveyards (things measured and *rejected*: exploration extremes,
heavier priors, playout work-removal, wider networks, alternative
scales) are as much a part of this project as the code. If you change
something load-bearing: two referees, three seed sets, replicate the
human leg, and bench on all five positions. The constants you are
tempted to tune were probably measured; check the logs first.

## License

MIT — see [LICENSE](LICENSE).
