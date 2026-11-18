#!/usr/bin/env bash
# E5: accuracy of every comparator against long-double FFTW 3.3.10 (same inputs), 1 thread, one process per case.
set -uo pipefail
W=${AUDIT_WORK:-$HOME/.capy/work/audit}; B=$W/bin; C=$W/c/bin; S=$(cd "$(dirname "$0")/.." && pwd); OUT=$W/runs/err; mkdir -p $OUT
run() {
  local lab=$1 env=$2 bin=$3; shift 3; : > $OUT/$lab.txt
  while read -r name _; do case "$name" in ""|"#"*) continue;; esac
    env $env timeout 600 $bin --suite $S/frozen.txt --only $name --threads 1 --mode err "$@" >> $OUT/$lab.txt 2>/dev/null || echo "$name FAIL timeout/crash" >> $OUT/$lab.txt
  done < $S/frozen.txt; echo "$lab done"; }
run base X=1 $B/base-err
run fc-bit X=1 $C/fftchode-err
run fc-fast FFTCHODE_NUMERICS=fast $C/fftchode-err
run fc-fast-measure FFTCHODE_NUMERICS=fast $C/fftchode-err --plan measure
run fftw-est X=1 $C/fftw-all-err
run fftw-measure X=1 $C/fftw-all-err --plan measure
run aocl-est X=1 $C/aocl-noapp-err
run aocl-measure X=1 $C/aocl-noapp-err --plan measure
for l in mkl ducc0-nofm ducc0-native ducc0-matched pocketfft-native pocketfft-matched pffft-native pffft-matched kissfft ffte; do run $l X=1 $C/$l-err; done
