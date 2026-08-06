#!/bin/bash
# Reproducible ArduGo device-flash breakdown -> self-contained HTML artifact.
#
#   ./flashmap.sh                    # build the device ELF, emit flashmap.html
#   ELF=/path/ardu_go.ino.elf ./flashmap.sh   # analyze a prebuilt ELF (skip build)
#
# Method: attribute every 2-byte word of .text to (a) its source FILE and
# (b) its innermost inlined FUNCTION via avr-addr2line -- this is LTO-aware,
# so code the compiler merged into `main` is re-attributed to its real owner
# (Arduino AVR builds with -flto, which dissolves TU boundaries at link).
# PROGMEM data tables (no source line for the fn) are recovered from avr-nm.
#
# Output (beside this script):
#   flashmap.html  page-content HTML -- open locally, or publish via Artifact
#   flashmap.json  raw metrics
# Env: GCC (avr toolchain dir), LIMIT (usable flash, default 28672), ELF.
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"          # ardu_go/
GCC="${GCC:-$HOME/Library/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7}"
LIMIT="${LIMIT:-28672}"                    # ATmega32U4: 32K - 4K bootloader
NM="$GCC/bin/avr-nm"; SIZE="$GCC/bin/avr-size"; A2L="$GCC/bin/avr-addr2line"
W="$DIR/_work"; mkdir -p "$W"

# ---- 1. obtain the ELF ------------------------------------------------------
if [ -z "${ELF:-}" ]; then
  echo "building device ELF (leonardo)..."
  ARD=/Applications/Arduino.app/Contents/Java
  "$ARD/arduino-builder" -compile \
    -hardware "$ARD/hardware" -hardware "$HOME/Library/Arduino15/packages" \
    -tools "$ARD/tools-builder" -tools "$ARD/hardware/tools/avr" -tools "$HOME/Library/Arduino15/packages" \
    -built-in-libraries "$ARD/libraries" -libraries "$HOME/Documents/Arduino/libraries" \
    -fqbn=arduino:avr:leonardo -vid-pid=0X2341_0X8036 -ide-version=10812 -warnings=none \
    -build-path "$W/build" -prefs=build.warn_data_percentage=75 \
    -prefs=runtime.tools.avr-gcc.path=$GCC \
    -prefs=runtime.tools.avr-gcc-7.3.0-atmel3.6.1-arduino7.path=$GCC \
    "$ROOT/ardu_go.ino" > "$W/build.log" 2>&1 \
    || { echo "BUILD FAILED (see $W/build.log)"; tail -6 "$W/build.log"; exit 1; }
  ELF="$W/build/ardu_go.ino.elf"
fi
[ -f "$ELF" ] || { echo "no ELF at $ELF"; exit 1; }
echo "analyzing $ELF"

# ---- 2. sections ------------------------------------------------------------
read TEXT DATA BSS < <("$SIZE" -A "$ELF" | awk '/^\.text/{t=$2}/^\.data/{d=$2}/^\.bss/{b=$2}END{print t+0,d+0,b+0}')
FLASH=$((TEXT+DATA)); FREE=$((LIMIT-FLASH))
COMMIT="$(cd "$ROOT" && git rev-parse --short HEAD 2>/dev/null || echo '?')"
DATESTR="$(date '+%Y-%m-%d %H:%M')"

# ---- 3. per-word attribution (LTO-aware) ------------------------------------
awk -v t=$TEXT 'BEGIN{for(a=0;a<t;a+=2)printf "0x%x\n",a}' > "$W/words.txt"
"$A2L" -e "$ELF"            < "$W/words.txt" > "$W/files.txt"           # file:line / word
"$A2L" -f -C -e "$ELF"      < "$W/words.txt" | awk 'NR%2==1' > "$W/fns.txt"  # fn / word
paste -d'\t' "$W/files.txt" "$W/fns.txt" > "$W/join.txt"                # file<TAB>fn per word

# ---- 4. nm symbols (for PROGMEM data tables + runtime math) -----------------
"$NM" --size-sort --radix=d --print-size --demangle "$ELF" > "$W/syms.txt"

# ---- 5. build the JSON payload ---------------------------------------------
{
echo "const DATA = {"
echo "\"limit\":$LIMIT,\"text\":$TEXT,\"data\":$DATA,\"bss\":$BSS,\"flash\":$FLASH,\"free\":$FREE,"
echo "\"commit\":\"$COMMIT\",\"date\":\"$DATESTR\","

# modules: file-level attribution (2 bytes/word), accurate & exact-summing
printf '"modules":['
awk -F'\t' '{f=$1;sub(/:.*/,"",f);n=split(f,a,"/");b=a[n];if(b=="??"||b=="")b="runtime/libgcc";s[b]+=2}
  END{for(k in s)print s[k]"\t"k}' "$W/join.txt" | sort -rn | \
  awk -F'\t' 'BEGIN{ORS=""}{if(NR>1)print",";printf "[\"%s\",%d]",$2,$1}'
echo "],"

# engine code subsystems: ai.cpp code only (fn != ??), bucketed by name
printf '"subsystems":['
awk -F'\t' '{f=$1;sub(/:.*/,"",f);n=split(f,a,"/");file=a[n];fn=$2;
  if(file!="ai.cpp")next; if(fn=="??")next;   # data tables handled separately
  b="misc engine";
  if(fn~/^nn|nnOpeningMove|nnChain|nnBucket|nnSymPos|nnAddRow|nnRd|nnDistMap/)b="NN opening";
  else if(fn~/playout|simPlay/)b="playout";
  else if(fn~/pattern|PAT3W/)b="patterns";
  else if(fn~/think|mctsIterate|selectChild|widenNode|bestMove|candidatePrior|expandNode|addChild|buildChainMap|buildNearMask|raceWin|latentBest|childCount|newNode|reclaim|freeSubtree|allocReady|isqrt32|lnQ12|winRate6|raveRatio|nBump|stash|adoptStash|chooseMove/)b="MCTS search";
  else if(fn~/regionVital|nakadeVital|settledRegion|isOwnEye|soleConnector|scoreWinner|vKomiWinner|ownVote|settleVote|scoreDead|^ld/)b="eval / scoring / L&D";
  else if(fn~/ladder|looseLadder|groupLibsList|emptyDeg|llMoveAt/)b="ladder reader";
  else if(fn~/hasLiberty|groupLibs|removeGroup|soleLiberty|boardAt|posXY|newMark|regionVitalCell|lpmNext/)b="board / groups / libs";
  else if(fn~/rnd/)b="RNG";
  s[b]+=2}
  END{for(k in s)print s[k]"\t"k}' "$W/join.txt" | sort -rn | \
  awk -F'\t' 'BEGIN{ORS=""}{if(NR>1)print",";printf "[\"%s\",%d]",$2,$1}'
echo "],"

# PROGMEM data tables: ALL_CAPS nm symbols with size (accurate)
printf '"tables":['
awk '{sz=$2;nm=$4; if(nm ~ /^[A-Z][A-Z0-9_]+$/ && sz>0) print sz"\t"nm}' "$W/syms.txt" | sort -rn | \
  awk -F'\t' 'BEGIN{ORS=""}{if(NR>1)print",";printf "[\"%s\",%d]",$2,$1}'
echo "],"

# top functions by DWARF-attributed size (inlined-aware), args stripped
printf '"functions":['
awk '{fn=$0; sub(/\(.*/,"",fn); sub(/ \[clone.*/,"",fn); sub(/\.constprop.*/,"",fn);
     if(fn=="??"||fn~/^__/||fn~/trampolines/)next; s[fn]+=2}
  END{for(k in s)print s[k]"\t"k}' "$W/fns.txt" | sort -rn | head -30 | \
  awk -F'\t' 'BEGIN{ORS=""}{if(NR>1)print",";gsub(/"/,"",$2);printf "[\"%s\",%d]",$2,$1}'
echo "],"

# runtime / libgcc software-math (AVR has no HW divide; UCB math dominates)
printf '"runtime":['
awk '{sz=$2;nm=$4; if(nm ~ /^__(udivmod|divmod|umulh|mulh|muls|div|mul|adddf|muldf|fixun|float|negdf|ashr|lshr|ashl)/ && sz>0) print sz"\t"nm}' "$W/syms.txt" | sort -rn | \
  awk -F'\t' 'BEGIN{ORS=""}{if(NR>1)print",";printf "[\"%s\",%d]",$2,$1}'
echo "]"
echo "};"
} > "$W/data.js"

# ---- 6. inject into template ------------------------------------------------
awk -v df="$W/data.js" '/__FLASHMAP_DATA__/{while((getline l<df)>0)print l;next}1' \
  "$DIR/flashmap.template.html" > "$DIR/flashmap.html"
# also drop the raw json (strip the JS wrapper) for scripting
sed -e 's/^const DATA = //' -e 's/;$//' "$W/data.js" > "$DIR/flashmap.json"

PCT=$(awk -v f=$FLASH -v l=$LIMIT 'BEGIN{printf "%.1f",f*100/l}')
echo "flash: $FLASH / $LIMIT B  (${PCT}%),  $FREE free"
echo "wrote $DIR/flashmap.html  and  $DIR/flashmap.json"
# Publish/update the artifact (keeps the same URL when the id is passed):
#   Artifact(file_path='.../flashmap.html',
#            url='https://claude.ai/code/artifact/5f2509cf-7e2f-4843-9653-448bdc01d173')
echo "publish:  Artifact(file_path='$DIR/flashmap.html', url='https://claude.ai/code/artifact/5f2509cf-7e2f-4843-9653-448bdc01d173')"