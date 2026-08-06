# flashmap — device flash breakdown → artifact

Reproducible byte-level breakdown of ArduGo's ATmega32U4 program flash, rendered
as a self-contained HTML artifact.

## Run

```sh
./flashmap.sh                          # builds the leonardo ELF, emits flashmap.html
ELF=/path/ardu_go.ino.elf ./flashmap.sh   # analyze a prebuilt ELF, skip the build
```

Env: `GCC` (avr toolchain dir), `LIMIT` (usable flash, default 28672 = 32K − 4K
bootloader), `ELF` (skip the build).

Outputs (git-ignored, regenerate on demand):
- `flashmap.html` — the artifact (page-content HTML: opens locally in a browser, or publish via the Artifact tool)
- `flashmap.json` — the raw metrics
- `_work/` — build + intermediate attribution files

## Publish / update the artifact

The live artifact is **https://claude.ai/code/artifact/5f2509cf-7e2f-4843-9653-448bdc01d173**.
To refresh it in place after a code change, regenerate and republish with that URL:

```
Artifact(file_path='test/flashmap/flashmap.html',
         url='https://claude.ai/code/artifact/5f2509cf-7e2f-4843-9653-448bdc01d173')
```

Omitting the `url` mints a *new* artifact instead of updating this one.

## Method

Arduino AVR builds with `-flto`, so the linker merges code across translation
units and much of the hot path ends up inside `main`. Symbol-table sizes
(`avr-nm`) therefore mis-attribute inlined code. Instead this tool walks **every
2-byte word of `.text`** and asks `avr-addr2line` for that address's innermost
inlined function and source file — DWARF tracks inlining, so each byte is charged
to its real owner. PROGMEM data tables (which have no owning function) are
recovered from `avr-nm` by symbol name. The module view sums to `.text` exactly.

Views produced:
- **modules** — file-level attribution (exact, sums to `.text`)
- **subsystems** — ai.cpp *code* bucketed by function (MCTS / NN / playout / …)
- **tables** — PROGMEM data (`PAT3W_BITS`, `NN_E/W/F`, `NEIGHBOR_TABLE`, …)
- **functions** — largest functions, inlined copies folded back in
- **runtime** — libgcc software math (AVR has no hardware divide)

Toolchain: `avr-size`, `avr-nm`, `avr-addr2line` from the Arduino avr-gcc 7.3.0.
