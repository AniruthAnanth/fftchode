#!/usr/bin/env bash
# Build baseline-v0 (tag baseline-v0) and fftchode (this checkout) harness binaries for the fairness audit (FFTE-8):
#   base / fc: clang 18 -O2 -ffp-contract=off (the repository's release flags); base-native / fc-native: -O3 -march=znver4 -ffp-contract=off.
#   fc-{bit,fast}-{estimate,measure,patient} and fc-native-* are wrappers that pick the numerics mode and planner level.
# Output in $AUDIT_WORK/bin (default ~/.capy/work/audit/bin). Needs bench/suite/comparators.sh $AUDIT_WORK/c first (long-double reference).
set -euo pipefail
R=$(cd "$(dirname "$0")/../../.." && pwd); W=${AUDIT_WORK:-$HOME/.capy/work/audit}; B=$W/bin; S=$W/src
mkdir -p $B $S/v0 $S/fc
git -C $R show baseline-v0:src/fft.cpp > $S/v0/fft.cpp; git -C $R show baseline-v0:src/fftw3.h > $S/v0/fftw3.h
cp $R/src/fftchode.cpp $R/src/fftchode.h $S/fc/
REFL=$W/c/inst/fftw-ref-l/lib/libfftw3l.a
build() { # name srcdir file flags
  local n=$1 d=$2 f=$3 fl=$4
  clang++ -std=c++17 $fl -pthread -c $d/$f -o $B/$n.o
  INC=$d CC=clang CXX=clang++ $R/bench/suite/build.sh $B/$n $B/$n.o
}
build base $S/v0 fft.cpp "-O2 -ffp-contract=off" &
build base-native $S/v0 fft.cpp "-O3 -march=znver4 -ffp-contract=off" &
build fc $S/fc fftchode.cpp "-O2 -ffp-contract=off" &
build fc-native $S/fc fftchode.cpp "-O3 -march=znver4 -ffp-contract=off" &
wait
clang++ -std=c++17 -O2 -DFFTSUITE_REF -I$S/v0 -c $R/bench/suite/fftsuite.cpp -o $B/base-err.o
clang -O2 -c $R/bench/src/memtrack_stub.c -o $B/stub.o
clang++ $B/base-err.o $B/stub.o $B/base.o $REFL -pthread -lm -o $B/base-err   # baseline-v0 accuracy vs long double (run bench/suite/comparators.sh first)
cp $B/base $B/base-aa
for n in fc fc-native; do
  for p in estimate measure patient; do
    printf '#!/bin/sh\nexec "$(dirname "$0")/%s" "$@" --plan %s\n' $n $p > $B/$n-bit-$p
    printf '#!/bin/sh\nFFTCHODE_NUMERICS=fast exec "$(dirname "$0")/%s" "$@" --plan %s\n' $n $p > $B/$n-fast-$p
  done
done
chmod +x $B/*
sha256sum $B/base $B/base-native $B/fc $B/fc-native $B/*.o > $B/MANIFEST.sha256
echo done
