#!/usr/bin/env bash
# Compiled-size table. Usage: sizes.sh <build dir> <out csv>
# Per library and precision: size(1) berkeley totals (text,data,bss) and file size of the library
# object/archive, and file size of the stripped static benchmark binary (plus the same binary
# built with a do-nothing benchee, so the library's own contribution is the difference).
B=$1; OUT=$2
REPO=$(cd "$(dirname "$0")/../.." && pwd)
echo "lib,precision,artifact,artifact_file_bytes,text,data,bss,stripped_binary_bytes,null_binary_bytes,binary_minus_null_bytes" > "$OUT"
row() { # lib prec artifact-path
  local lib=$1 prec=$2 art=$3 t d b fb bin nb
  [ -e "$art" ] || return 0
  read -r t d b _ < <(size -t "$art" | tail -1)
  fb=$(stat -c %s "$art")
  bin=$B/bin/$lib-$prec.stripped
  [ -e "$bin" ] || return 0
  nb=$(stat -c %s "$B/bin/null-$prec.stripped")
  echo "$lib,$([ $prec = d ] && echo double || echo single),$(basename "$art"),$fb,$t,$d,$b,$(stat -c %s "$bin"),$nb,$(( $(stat -c %s "$bin") - nb ))" >> "$OUT"
}
for p in d s; do
  row fftchode $p "$REPO/build/fftchode.o"
  row fftw-scalar $p "$B/fftw-scalar-$p/inst/lib/libfftw3$([ $p = s ] && echo f).a"
  row fftw-simd $p "$B/fftw-simd-$p/inst/lib/libfftw3$([ $p = s ] && echo f).a"
  row pocketfft $p "$B/obj/bn-pocketfft-$p.o"
  row kissfft $p "$B/lib/libkissfft-$p.a"
done
row ffte d "$B/lib/libffte.a"
