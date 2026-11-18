#!/usr/bin/env bash
# E4: AOCL-FFTW built with --enable-amd-app-opt (searches in every planner mode). Pass 1 plans every case cold into a per-thread-count wisdom dir
# (CASE_TIMEOUT=600 s per case) and records the cold plan time; pass 2 times execution with that wisdom in place.
set -euo pipefail
W=${AUDIT_WORK:-$HOME/.capy/work/audit}; B=$W/bin; C=$W/c/bin; S=$(cd "$(dirname "$0")/.." && pwd); OUT=$W/runs/app; mkdir -p $OUT
for T in 1 8; do
  P=$( [ $T = 1 ] && echo 2 || echo 0-7 ); mkdir -p $OUT/wis-t$T
  ( cd $OUT/wis-t$T && CASE_CWD=$OUT/wis-t$T CASE_TIMEOUT=600 taskset -c $P $C/aocl-app-measure --suite $S/frozen.txt --threads $T > $OUT/cold-t$T.txt )
  CASE_CWD=$OUT/wis-t$T CASE_TIMEOUT=120 python3 -I $S/compare.py --suite $S/frozen.txt --threads $T --rounds 3 --out $OUT base=$B/base aocl-app-measure=$C/aocl-app-measure aocl-measure=$C/aocl-noapp-measure fc-fast-measure=$B/fc-fast-measure
done
