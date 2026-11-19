#!/usr/bin/env bash
# Thread-count runs for the FFTE-9 threading fixes, with the audited protocol (compare.py: interleaved per case and round,
# pinned, A/A control, one reference: baseline-v0 at 1 thread).
#   run_threads.sh <outdir> base=<bin> fc-v1=<bin> fc-steal=<bin> fc=<bin>
# Each fftchode binary runs in both numerics modes. Frozen and held-out suites at 1, 2, 4, 8 threads (3 rounds), then the
# frozen suite at 2, 4, 8 threads (2 rounds) with a CPU hog on cpus 4-7. Analysis: bench/suite/analyze_threads.py.
set -u
OUT=$1; shift; S=$(cd "$(dirname "$0")/.." && pwd); mkdir -p "$OUT/bin"
L=""
for a in "$@"; do
  n=${a%%=*}; b=$(readlink -f "${a#*=}")
  if [ "$n" = base ]; then cp "$b" "$OUT/bin/base-aa"; L="base=$b base-aa=$OUT/bin/base-aa $L"; continue; fi
  printf '#!/bin/sh\nexec "%s" "$@"\n' "$b" > "$OUT/bin/$n-bit"
  printf '#!/bin/sh\nFFTCHODE_NUMERICS=fast exec "%s" "$@"\n' "$b" > "$OUT/bin/$n-fast"
  L="$L ${n/fc/fc-bit}=$OUT/bin/$n-bit ${n/fc/fc-fast}=$OUT/bin/$n-fast"
done
chmod +x "$OUT"/bin/*
cc -O2 -pthread "$S/audit/hog.c" -o "$OUT/bin/hog" -lm
for suite in frozen heldout; do for t in 1 2 4 8; do
  python3 -I "$S/compare.py" --suite "$S/$suite.txt" --threads $t --rounds 3 --out "$OUT/$suite" $L 2>> "$OUT/$suite-log-t$t.txt" > "$OUT/$suite-summary-t$t.txt"
done; done
"$OUT/bin/hog" 4-7 2000 0.999 100000 & HOG=$!
for t in 2 4 8; do
  python3 -I "$S/compare.py" --suite "$S/frozen.txt" --threads $t --rounds 2 --out "$OUT/frozen-hog" $L 2>> "$OUT/frozen-hog-log-t$t.txt" > "$OUT/frozen-hog-summary-t$t.txt"
done
kill $HOG
