#!/usr/bin/env bash
# Build every comparator FFT library and one frozen-suite harness binary per library (FFTE-6 optimization phase).
#
#   bench/suite/comparators.sh <outdir> [step ...]
#
# Steps (default: all, in this order; every step is idempotent and guarded by a stamp in <outdir>/stamps):
#   fetch      pinned sources (git commits / sha256-checked tarballs / pinned pip wheels)
#   fftw       upstream FFTW 3.3.10: simd-all (sse2+avx+avx2+avx512), simd-avx2, long-double reference (gcc, FFTW's own CFLAGS),
#              and fftw-matched: simd-all built with clang $FL_MATCHED like fftchode
#   aocl       AMD AOCL-FFTW 6.0.0 (amd-opt, app-opt layer on and off), threads, d + s; aocl-matched: no app-opt, clang $FL_MATCHED
#   mkl        oneMKL (pip wheels in a venv) static DFTI
#   libs       ducc0, pocketfft, PFFFT, KissFFT, FFTE 7.0 libraries
#   harness    one harness binary per comparator: <outdir>/bin/<id>, <id>-err, <id>-mem (+ run wrappers)
#
# Every binary speaks the fftsuite command line and output lines (see bench/suite/fftsuite.cpp):
#   <bin> --suite FILE --threads N [--mode time|err|mem|hash] [--batches K] [--tmin S] [--only NAME] [--plan estimate|measure|patient]
# so bench/suite/compare.py can interleave them.  FFTW-API libraries (upstream FFTW, AOCL-FFTW) use fftsuite.cpp itself;
# the others use bench/suite/adapters/<lib>.cpp on top of adapters/harness.h (same case parser, geometry, inputs, loop, output).
# Planning level is a run-time option of the FFTW-API binaries: <id>-estimate / -measure / -patient are wrapper scripts
# that exec <id> with --plan X (compare.py takes a single executable per label).
#
# Environment: JOBS (parallel make, default nproc), GCC/GXX/FC (library compilers, default gcc g++ gfortran),
#              HCXX (harness compiler, default clang++), SKIP="mkl aocl ..." to skip steps that cannot run on a machine.
# Results: <outdir>/PROVENANCE.txt (versions, hashes, flags, commands), <outdir>/log/*.log (per step logs).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$HERE/../.." && pwd)
[ $# -ge 1 ] || { sed -n 2,26p "$0"; exit 2; }
OUT=$(mkdir -p "$1" && cd "$1" && pwd); shift
STEPS=${*:-fetch fftw aocl mkl libs harness}
JOBS=${JOBS:-$(nproc)}
. "$REPO/bench/versions.sh"       # FFTW, pocketfft, KissFFT, FFTE pins shared with bench/build.sh
GCC=${GCC:-gcc}; GXX=${GXX:-g++}; HCXX=${HCXX:-clang++}; FC=${FC:-gfortran}

# ---- pins (new for this script; the ones above come from bench/versions.sh) ------------------------------------------
AOCL_URL=https://github.com/amd/amd-fftw
AOCL_TAG=6.0.0
AOCL_COMMIT=08c75e2ee374ce79c37c841462814677bf282a2c
DUCC_URL=https://github.com/mreineck/ducc
DUCC_TAG=ducc0_0_42_0
DUCC_COMMIT=42ce8ab5c5d79e0e2fd9af4a09c2ea4693520e1b
PFFFT_URL=https://github.com/marton78/pffft
PFFFT_COMMIT=efc1dd2de6c3562fc464f905d37d9281f08a20dd
MKL_VERSION=2026.2.1
AOCL_SUBDIRS="support kernel simd-support dft rdft reodft api libbench2 threads"   # built one by one: top-level make also builds doc/, which needs makeinfo

D=$OUT/dl; B=$OUT/build; I=$OUT/inst; BIN=$OUT/bin; LOG=$OUT/log; ST=$OUT/stamps
mkdir -p "$D" "$B" "$I" "$BIN" "$LOG" "$ST"
say() { printf '[comparators] %s\n' "$*"; }
step() { # step <name> <fn> : stamp-guarded, logs to $LOG/<name>.log
  local n=$1; shift
  if [ -e "$ST/$n" ]; then say "$n: up to date"; return 0; fi
  say "$n ..."
  if ( "$@" ) > "$LOG/$n.log" 2>&1; then touch "$ST/$n"; else echo "FAILED: $n (see $LOG/$n.log)" >&2; tail -25 "$LOG/$n.log" >&2; return 1; fi
}
want() { case " $STEPS " in *" $1 "*) case " ${SKIP:-} " in *" $1 "*) return 1;; esac; return 0;; esac; return 1; }
git_pin() { # url commit dir
  [ -d "$3/.git" ] && [ "$(git -C "$3" rev-parse HEAD 2>/dev/null)" = "$2" ] && return 0
  rm -rf "$3"; mkdir -p "$3"
  git -C "$3" init -q && git -C "$3" remote add origin "$1"
  git -C "$3" fetch -q --depth 1 origin "$2" && git -C "$3" checkout -q FETCH_HEAD
  [ "$(git -C "$3" rev-parse HEAD)" = "$2" ]
}
fetch_tar() { # url sha256 file
  [ -f "$3" ] && echo "$2  $3" | sha256sum -c - >/dev/null 2>&1 && return 0
  curl -sSL --retry 3 -o "$3" "$1" && echo "$2  $3" | sha256sum -c -
}
prov() { printf '%s\n' "$*" >> "$OUT/PROVENANCE.txt"; }

# ---------------------------------------------------------------------------------------------------------------------
do_fetch() {
  git_pin "$AOCL_URL" "$AOCL_COMMIT" "$D/amd-fftw"
  git_pin "$DUCC_URL" "$DUCC_COMMIT" "$D/ducc"
  git_pin "$PFFFT_URL" "$PFFFT_COMMIT" "$D/pffft"
  git_pin "$POCKETFFT_URL" "$POCKETFFT_COMMIT" "$D/pocketfft"
  git_pin "$KISSFFT_URL" "$KISSFFT_COMMIT" "$D/kissfft"
  fetch_tar "$FFTW_URL" "$FFTW_SHA256" "$D/fftw-$FFTW_VERSION.tar.gz"
  fetch_tar "$FFTE_URL" "$FFTE_SHA256" "$D/ffte-$FFTE_VERSION.tgz"
  [ -d "$D/fftw-$FFTW_VERSION" ] || tar xzf "$D/fftw-$FFTW_VERSION.tar.gz" -C "$D"
  [ -d "$D/ffte-$FFTE_VERSION" ] || tar xzf "$D/ffte-$FFTE_VERSION.tgz" -C "$D"
}

# upstream FFTW: build_fftw <name> <d|s|l> <configure flags...>   (stock FFTW CFLAGS: gcc -O3 -fomit-frame-pointer ...)
build_fftw() {
  local name=$1 prec=$2; shift 2
  local extra=""; [ "$prec" = s ] && extra=--enable-single; [ "$prec" = l ] && extra=--enable-long-double
  rm -rf "$B/$name-$prec" "$I/$name-$prec"; mkdir -p "$B/$name-$prec"
  ( cd "$B/$name-$prec" && "$D/fftw-$FFTW_VERSION/configure" --prefix="$I/$name-$prec" --enable-threads --with-combined-threads \
      --disable-fortran --disable-shared --enable-static $extra "$@" CC="${LCC:-$GCC}" ${LCFLAGS:+CFLAGS="$LCFLAGS"} > configure.log 2>&1 \
    && make -j"$JOBS" > make.log 2>&1 && make install > install.log 2>&1 ) || return 1
  prov "fftw $name-$prec: $D/fftw-$FFTW_VERSION/configure --prefix=... --enable-threads --with-combined-threads --disable-fortran --disable-shared --enable-static $extra $* CC=${LCC:-$GCC}; CFLAGS=$(grep -m1 '^CFLAGS =' "$B/$name-$prec/Makefile" | cut -d= -f2-)"
}
do_fftw() {
  for p in d s; do
    step fftw-all-$p build_fftw fftw-all $p --enable-sse2 --enable-avx --enable-avx2 --enable-avx512
    step fftw-avx2-$p build_fftw fftw-avx2 $p --enable-avx2
    LCC=$HCC LCFLAGS="$FL_MATCHED" step fftw-matched-$p build_fftw fftw-matched $p --enable-sse2 --enable-avx --enable-avx2 --enable-avx512
  done
  step fftw-ref-l build_fftw fftw-ref l
}

# AOCL-FFTW: build_aocl <name> <d|s> <extra flags...>
build_aocl() {
  local name=$1 prec=$2; shift 2
  local extra=""; [ "$prec" = s ] && extra=--enable-single
  rm -rf "$B/$name-$prec" "$I/$name-$prec"; mkdir -p "$B/$name-$prec"
  ( cd "$B/$name-$prec" && "$D/amd-fftw/configure" --prefix="$I/$name-$prec" --enable-threads --with-combined-threads \
      --enable-sse2 --enable-avx --enable-avx2 --enable-avx512 --enable-amd-opt \
      --disable-fortran --disable-shared --enable-static $extra "$@" CC="${LCC:-$GCC}" ${LCFLAGS:+CFLAGS="$LCFLAGS"} AMD_ARCH=znver4 > configure.log 2>&1 \
    && for sd in $AOCL_SUBDIRS; do make -j"$JOBS" -C "$sd" || exit 1; done > make.log 2>&1 \
    && make -j"$JOBS" libfftw3$([ "$prec" = s ] && echo f).la >> make.log 2>&1 && make install-libLTLIBRARIES install-pkgconfigDATA > install.log 2>&1 && make -C api install-includeHEADERS >> install.log 2>&1 ) || return 1
  [ -f "$I/$name-$prec/lib/libfftw3$([ "$prec" = s ] && echo f).a" ] || return 1
  prov "aocl $name-$prec: $D/amd-fftw/configure --prefix=... --enable-threads --with-combined-threads --enable-sse2 --enable-avx --enable-avx2 --enable-avx512 --enable-amd-opt --disable-fortran --disable-shared --enable-static $extra $* CC=${LCC:-$GCC} AMD_ARCH=znver4; CFLAGS=$(grep -m1 '^CFLAGS =' "$B/$name-$prec/Makefile" | cut -d= -f2-)"
}
do_aocl() {
  for p in d s; do
    step aocl-app-$p build_aocl aocl-app $p --enable-amd-app-opt
    step aocl-noapp-$p build_aocl aocl-noapp $p
    LCC=$HCC LCFLAGS="$FL_MATCHED" step aocl-matched-$p build_aocl aocl-matched $p
  done
}

# oneMKL from Intel's PyPI wheels (static libraries + headers), sha256-pinned in adapters/mkl-requirements.txt
do_mkl() {
  python3 -m venv "$OUT/mklvenv"
  "$OUT/mklvenv/bin/pip" install -q --upgrade pip
  "$OUT/mklvenv/bin/pip" install -q --require-hashes -r "$HERE/adapters/mkl-requirements.txt"
  MKLROOT=$OUT/mklvenv
  [ -f "$MKLROOT/lib/libmkl_core.a" ] && [ -f "$MKLROOT/include/mkl_dfti.h" ] && [ -f "$MKLROOT/lib/libiomp5.a" ]
  prov "mkl: pip wheels from adapters/mkl-requirements.txt (sha256-pinned), venv $OUT/mklvenv; $("$OUT/mklvenv/bin/pip" list 2>/dev/null | grep -i -E 'mkl|openmp|tbb' | tr '\n' ';')"
}

# ---- libraries built with two flag sets: "matched" (bench/versions.sh: -O2 -ffp-contract=off, no -m flags) and "native" (the library's
# own recommended flags, -march=native).  Compiler: clang family (HCXX / CC) like bench/build.sh.
HCC=${HCC:-clang}
FL_MATCHED="-O2 -ffp-contract=off"
FL_DUCC_NATIVE="-O3 -ffast-math -march=native"      # ducc0's CMakeLists: DUCC0_OPT_FLAGS "-O3 -ffast-math" + DUCC0_ARCH_FLAGS "-march=native"
FL_DUCC_NOFM="-O3 -march=native"                     # ducc0's recommended build minus -ffast-math (IEEE semantics like every other library here)
FL_POCKET_NATIVE="-O3 -march=native"
FL_PFFFT_NATIVE="-O3 -march=native"                   # pffft README: -march=native / haswell enable FMA; CMake default build type Release (-O3)
PF_DEFS="-DPOCKETFFT_CACHE_SIZE=16"                    # same plan-cache size as bench/versions.sh runs (cache disabled by default in pocketfft)

have() {  # is component $1 built (and not in SKIP)?
  case " ${SKIP:-} " in *" $1 "*) return 1;; esac
  case $1 in
    fftw) [ -f "$I/fftw-all-d/lib/libfftw3.a" ] && [ -f "$I/fftw-ref-l/lib/libfftw3l.a" ];;
    aocl) [ -f "$I/aocl-app-d/lib/libfftw3.a" ] && [ -f "$I/aocl-noapp-s/lib/libfftw3f.a" ];;
    mkl) [ -f "$OUT/mklvenv/lib/libmkl_core.a" ];;
    libs) [ -f "$B/ffte/zfft1d.o" ];;
  esac
}
pall() { local pids=() p rc=0; for c in "$@"; do eval "$c" & pids+=($!); done; for p in "${pids[@]}"; do wait "$p" || rc=1; done; return $rc; }

build_ducc() { # <variant> <flags>
  local v=$1 fl=$2 o=$B/ducc0-$1 cmds=() f
  rm -rf "$o"; mkdir -p "$o"
  for f in fft/fft_inst1 fft/fft_inst2 infra/threading infra/string_utils infra/mav; do
    cmds+=("$HCXX -std=c++17 $fl -I$D/ducc/src -c $D/ducc/src/ducc0/$f.cc -o $o/$(basename $f).o")
  done
  pall "${cmds[@]}"
  prov "ducc0 $v: $HCXX -std=c++17 $fl (sources ducc0/fft/fft_inst1.cc fft_inst2.cc infra/threading.cc string_utils.cc mav.cc at $DUCC_COMMIT = $DUCC_TAG)"
}
build_pffft() { # <variant> <flags>
  local v=$1 fl=$2 o=$B/pffft-$1 f
  rm -rf "$o"; mkdir -p "$o"
  for f in pffft pffft_common pffft_double; do $HCC -std=gnu11 $fl -I"$D/pffft/include/pffft" -I"$D/pffft/include" -I"$D/pffft/src" -c "$D/pffft/src/$f.c" -o "$o/$f.o"; done
  prov "pffft $v: $HCC $fl (src/pffft.c pffft_common.c pffft_double.c at $PFFFT_COMMIT)"
}
build_kiss() {
  local o=$B/kissfft f
  rm -rf "$o"; mkdir -p "$o"
  for f in kiss_fft kiss_fftr kiss_fftnd kiss_fftndr; do
    $HCC $FL_MATCHED -Dkiss_fft_scalar=double -DKISS_DOUBLE -I"$D/kissfft" -c "$D/kissfft/$f.c" -o "$o/$f-d.o"
    $HCC $FL_MATCHED -Dkiss_fft_scalar=float -include "$HERE/adapters/kissf_rename.h" -I"$D/kissfft" -c "$D/kissfft/$f.c" -o "$o/$f-s.o"
  done
  prov "kissfft: $HCC $FL_MATCHED -Dkiss_fft_scalar=double|float (float build renames symbols with adapters/kissf_rename.h) at $KISSFFT_COMMIT"
}
build_ffte() {
  local o=$B/ffte f src=$D/ffte-$FFTE_VERSION
  rm -rf "$o"; mkdir -p "$o"
  for f in factor fft235 kernel zfft1d; do $FC $FL_MATCHED -std=legacy -fallow-argument-mismatch -I"$src" -c "$src/$f.f" -o "$o/$f.o"; done
  prov "ffte $FFTE_VERSION: $FC $FL_MATCHED -std=legacy -fallow-argument-mismatch (factor fft235 kernel zfft1d; gfortran $($FC -dumpversion))"
}
do_libs() {
  step ducc0-matched build_ducc matched "$FL_MATCHED"
  step ducc0-native build_ducc native "$FL_DUCC_NATIVE"
  step ducc0-nofm build_ducc nofm "$FL_DUCC_NOFM"
  step pffft-matched build_pffft matched "$FL_MATCHED"
  step pffft-native build_pffft native "$FL_PFFFT_NATIVE"
  step kissfft build_kiss
  step ffte build_ffte
}

# ---- harness binaries --------------------------------------------------------------------------------------------------
# hbuild <id> <adapter.cpp> "<compile flags>" "<link inputs>"  ->  $BIN/<id>, <id>-mem, <id>-err
hbuild() {
  local id=$1 src=$2 cf=$3 ld=$4 o=$B/obj/$1 W="" f
  mkdir -p "$o"
  $HCXX -std=c++17 $cf -I"$HERE/adapters" -c "$HERE/adapters/$src" -o "$o/a.o"
  $HCXX -std=c++17 $cf -DFFTSUITE_REF -I"$HERE/adapters" -I"$I/fftw-ref-l/include" -c "$HERE/adapters/$src" -o "$o/a-err.o"
  local base="-static-libstdc++ -static-libgcc -pthread -lm -ldl"
  $HCXX "$o/a.o" "$B/obj/memtrack_stub.o" $ld $base -o "$BIN/$id"
  $HCXX "$o/a-err.o" "$B/obj/memtrack_stub.o" $ld "$I/fftw-ref-l/lib/libfftw3l.a" $base -o "$BIN/$id-err"
  for f in malloc calloc realloc free posix_memalign aligned_alloc memalign; do W="$W -Wl,--wrap=$f"; done
  $HCXX "$o/a.o" "$B/obj/memtrack.o" $ld $W $base -o "$BIN/$id-mem"
  echo "$id" >> "$B/ids.txt"
}
# FFTW-API libraries: fftsuite.cpp itself (bench/suite/build.sh) + <id>-estimate|measure|patient wrappers
fftw_api() { # <id> <inst-d> <inst-s> [extra link flags]
  local id=$1 d=$2 s=$3 x=${4:-} f
  INC="$I/$d/include" REF="$I/fftw-ref-l/lib/libfftw3l.a" CC=$HCC CXX=$HCXX "$HERE/build.sh" "$BIN/$id" "$I/$d/lib/libfftw3.a" "$I/$s/lib/libfftw3f.a" $x
  for f in estimate measure patient; do
    case $id in
      aocl-app) printf '#!/bin/sh\nexec python3 -I "%s/case_timeout.py" "$(dirname "$0")/%s" "$@" --plan %s\n' "$HERE" "$id" "$f" > "$BIN/$id-$f";;  # per-case time limit, see case_timeout.py
      *) printf '#!/bin/sh\nexec "$(dirname "$0")/%s" "$@" --plan %s\n' "$id" "$f" > "$BIN/$id-$f"
         printf '#!/bin/sh\nexec "$(dirname "$0")/%s" "$@" --plan %s --pool spin\n' "$id" "$f" > "$BIN/$id-pool-$f"; chmod +x "$BIN/$id-pool-$f";;
    esac
    chmod +x "$BIN/$id-$f"
  done
  echo "$id (FFTW API; -estimate|-measure|-patient wrappers)" >> "$B/ids.txt"
}
do_harness() {
  mkdir -p "$B/obj"; : > "$B/ids.txt"
  $HCC -O2 -c "$REPO/bench/src/memtrack_stub.c" -o "$B/obj/memtrack_stub.o"
  $HCC -O2 -c "$REPO/bench/src/memtrack.c" -o "$B/obj/memtrack.o"
  ( cd "$REPO" && make CXX="$HCXX" build/fftchode.o >/dev/null )
  # baseline = this repository's fftchode.o (FFTW API): the fftsuite harness and the adapter harness on the same object prove the two harnesses agree
  INC="$REPO/src" REF="$I/fftw-ref-l/lib/libfftw3l.a" CC=$HCC CXX=$HCXX "$HERE/build.sh" "$BIN/fftchode" "$REPO/build/fftchode.o"
  $HCXX -std=c++17 -O2 -I"$HERE/adapters" -I"$REPO/src" -c "$HERE/adapters/fftw.cpp" -o "$B/obj/fftw-adapter.o"
  $HCXX "$B/obj/fftw-adapter.o" "$B/obj/memtrack_stub.o" "$REPO/build/fftchode.o" -pthread -lm -o "$BIN/fftchode-harness"
  have fftw || { echo "fftw step must run first" >&2; return 1; }
  fftw_api fftw-all fftw-all-d fftw-all-s; fftw_api fftw-avx2 fftw-avx2-d fftw-avx2-s; fftw_api fftw-matched fftw-matched-d fftw-matched-s
  # AOCL's libfftw3.a and libfftw3f.a both define the un-prefixed global wisdom_write_set (a flag; wisdom is unused here): allow the duplicate
  have aocl && { fftw_api aocl-app aocl-app-d aocl-app-s -Wl,--allow-multiple-definition; fftw_api aocl-noapp aocl-noapp-d aocl-noapp-s -Wl,--allow-multiple-definition; fftw_api aocl-matched aocl-matched-d aocl-matched-s -Wl,--allow-multiple-definition; }
  if have mkl; then
    local M=$OUT/mklvenv
    hbuild mkl mkl.cpp "-O2 -I$M/include" "-Wl,--start-group $M/lib/libmkl_intel_lp64.a $M/lib/libmkl_intel_thread.a $M/lib/libmkl_core.a -Wl,--end-group $M/lib/libiomp5.a"
  fi
  if have libs; then
    local v
    for v in matched native; do
      hbuild pocketfft-$v pocketfft.cpp "$([ $v = matched ] && echo $FL_MATCHED || echo $FL_POCKET_NATIVE) $PF_DEFS -I$D/pocketfft" ""
      hbuild ducc0-$v ducc0.cpp "$([ $v = matched ] && echo $FL_MATCHED || echo $FL_DUCC_NATIVE) -I$D/ducc/src" "$B/ducc0-$v/*.o"
      if [ $v = native ]; then hbuild ducc0-nofm ducc0.cpp "$FL_DUCC_NOFM -I$D/ducc/src" "$B/ducc0-nofm/*.o"; fi
      hbuild pffft-$v pffft.cpp "$([ $v = matched ] && echo $FL_MATCHED || echo $FL_PFFFT_NATIVE) -I$D/pffft/include -I$D/pffft/include/pffft" "$B/pffft-$v/*.o"
    done
    local K=$B/kissfft cf="$FL_MATCHED -I$D/kissfft"
    $HCXX -std=c++17 $cf -DKISS_PREC_D -Dkiss_fft_scalar=double -DKISS_DOUBLE -I"$HERE/adapters" -c "$HERE/adapters/kissfft_plan.cpp" -o "$K/plan-d.o"
    $HCXX -std=c++17 $cf -DKISS_PREC_S -Dkiss_fft_scalar=float -include "$HERE/adapters/kissf_rename.h" -I"$HERE/adapters" -c "$HERE/adapters/kissfft_plan.cpp" -o "$K/plan-s.o"
    hbuild kissfft kissfft.cpp "$FL_MATCHED" "$K/plan-d.o $K/plan-s.o $K/kiss_fft-d.o $K/kiss_fftr-d.o $K/kiss_fftnd-d.o $K/kiss_fftndr-d.o $K/kiss_fft-s.o $K/kiss_fftr-s.o $K/kiss_fftnd-s.o $K/kiss_fftndr-s.o"
    hbuild ffte ffte.cpp "$FL_MATCHED" "$B/ffte/*.o -lgfortran -lquadmath"
  fi
  {
    echo "# comparator binaries (all take: --suite FILE --threads N [--mode time|err|mem|hash] [--batches K] [--tmin S] [--only NAME]); sha256 + size"
    ( cd "$BIN" && for f in $(ls | grep -v -E -- '-(estimate|measure|patient)$'); do printf '%s  %s\n' "$(sha256sum "$f" | cut -c1-16)" "$f"; done )
  } > "$OUT/bin-manifest.txt"
}

# ---------------------------------------------------------------------------------------------------------------------
write_provenance_header() {
  {
    echo "# Comparator build provenance ($(date -u +%FT%TZ)); one line per library build is appended by the build steps"
    echo "host: $(uname -srm); cpu: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | xargs); cores: $(nproc)"
    echo "compilers: $($GCC --version | head -1); $($HCXX --version | head -1); $($FC --version | head -1)"
    echo "pins: AOCL-FFTW $AOCL_TAG $AOCL_COMMIT ($AOCL_URL); ducc0 $DUCC_TAG $DUCC_COMMIT ($DUCC_URL); PFFFT $PFFFT_COMMIT ($PFFFT_URL)"
    echo "      FFTW $FFTW_VERSION sha256 $FFTW_SHA256; pocketfft $POCKETFFT_COMMIT; KissFFT $KISSFFT_COMMIT; FFTE $FFTE_VERSION sha256 $FFTE_SHA256; oneMKL $MKL_VERSION (adapters/mkl-requirements.txt)"
    echo "      repo commit of this script: $(git -C "$REPO" rev-parse HEAD 2>/dev/null || echo unknown)"
  } > "$OUT/PROVENANCE.header"
}
for s in $STEPS; do
  case $s in
    fetch) step fetch do_fetch;;
    fftw) want fftw && do_fftw;;
    aocl) want aocl && do_aocl;;
    mkl) want mkl && step mkl do_mkl;;
    libs) want libs && do_libs;;
    harness) want harness && do_harness;;
    *) echo "unknown step $s" >&2; exit 2;;
  esac
done
write_provenance_header
say "done. binaries: $BIN  (list: $OUT/bin-manifest.txt, provenance: $OUT/PROVENANCE.header + $OUT/PROVENANCE.txt)"
