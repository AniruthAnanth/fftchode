#!/usr/bin/env bash
# E4, 8 threads only (see run_app.sh). In FFTE-8 the cold pass hit the 6 h limit after 83 cases (27 over 600 s); execution was then timed
# on the 56 planned cases only: a case file built from the non-FAIL lines of cold-t8.txt, passed to compare.py as --suite.
set -euo pipefail
W=${AUDIT_WORK:-$HOME/.capy/work/audit}; B=$W/bin; C=$W/c/bin; S=$(cd "$(dirname "$0")/.." && pwd); OUT=$W/runs/app; mkdir -p $OUT/wis-t8
( cd $OUT/wis-t8 && CASE_CWD=$OUT/wis-t8 CASE_TIMEOUT=600 taskset -c 0-7 $C/aocl-app-measure --suite $S/frozen.txt --threads 8 > $OUT/cold-t8.txt )
CASE_CWD=$OUT/wis-t8 CASE_TIMEOUT=120 python3 -I $S/compare.py --suite $S/frozen.txt --threads 8 --rounds 2 --out $OUT base=$B/base aocl-app-measure=$C/aocl-app-measure aocl-measure=$C/aocl-noapp-measure fc-fast-measure=$B/fc-fast-measure
