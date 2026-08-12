# Measurement kit (rescued from /tmp, 2026-08-12 — /tmp cleanup eats files)
- build_cand.sh — host harness build with ship flags
- shim_katago_4090.sh — PATH shim: `katago gtp` → ssh to the 4090 (10.0.0.17).
  Install as /tmp/shimbin/katago (chmod +x). Needs /tmp/pw4090.sh: a script
  that echoes the katago user's password (see ardu-go-gpu-resume memory);
  SSH_ASKPASS mechanism. Human gauntlets: PATH=/tmp/shimbin:$PATH
  KATAGO_HUMAN=1 KATAGO_RANK=preaz_10k <harness> ...
- adj4090.py — KataGo adjudication of a workdir of SGFs on the 4090
- selfplay5k.py — human-SL self-play SGF generator via the shim
- kata_b6.bin.gz — local KataGo b6c96 model (reviews/labels); also at
  katagoarchive.org/g170/neuralnets/g170-b6c96-s175395328-d26788732.bin.gz
- baselines/gpuh_*.json — per-game human-gauntlet results (seeds 1000-1999),
  keyed by game number; gpuh_puress512.json = SHIP baseline (317/1000 @10k)
