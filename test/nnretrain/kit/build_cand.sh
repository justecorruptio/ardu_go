#!/bin/bash
# Host harness build (ship flags). Usage: build_cand.sh <out> [-DFLAG ...]
OUT="$1"; shift
cd /Users/jay/workspace/ardu_go
DEV=/Applications/Xcode.app/Contents/Developer; SDK=$(DEVELOPER_DIR=$DEV xcrun --show-sdk-path 2>/dev/null)
DEVELOPER_DIR=$DEV xcrun clang++ -isysroot "$SDK" -std=c++17 -O2 -Wno-unused-function -Itest \
  -DNNOPEN -DNN_DEVICE_TIER -DNN_CORE_TIER "$@" test/harness.cpp -o "$OUT" 2>/tmp/build.err
rc=$?; [ $rc -ne 0 ] && { echo "BUILD FAILED"; grep -iE 'error' /tmp/build.err|head -5; exit 1; }
echo "built $OUT ${*:+with $*}"
