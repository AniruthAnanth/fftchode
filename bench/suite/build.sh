#!/usr/bin/env bash
# Build the frozen-suite harness against one FFTW3-API object or archive set.
#   bench/suite/build.sh <out-prefix> <lib objects/archives...>
# Produces <out-prefix> (time/hash modes) and <out-prefix>-mem (heap accounting via ld --wrap).
# Set REF=<libfftw3l.a> to also build <out-prefix>-err (error vs long-double upstream FFTW).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$HERE/../.." && pwd)
CC=${CC:-clang}; CXX=${CXX:-clang++}; INC=${INC:-$REPO/src}
out=$1; shift
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
$CXX -std=c++17 -O2 -I"$INC" -c "$HERE/fftsuite.cpp" -o "$tmp/s.o"
$CC -O2 -c "$REPO/bench/src/memtrack_stub.c" -o "$tmp/stub.o"
$CC -O2 -c "$REPO/bench/src/memtrack.c" -o "$tmp/mt.o"
$CXX "$tmp/s.o" "$tmp/stub.o" "$@" -pthread -lm -o "$out"
W=""; for f in malloc calloc realloc free posix_memalign aligned_alloc memalign; do W="$W -Wl,--wrap=$f"; done
$CXX "$tmp/s.o" "$tmp/mt.o" "$@" $W -pthread -lm -o "$out-mem"
if [ -n "${REF:-}" ]; then
  $CXX -std=c++17 -O2 -DFFTSUITE_REF -I"$INC" -c "$HERE/fftsuite.cpp" -o "$tmp/se.o"
  $CXX "$tmp/se.o" "$tmp/stub.o" "$@" "$REF" -pthread -lm -o "$out-err"
fi
