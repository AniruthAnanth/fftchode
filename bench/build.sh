#!/usr/bin/env bash
# Fetches, builds and links everything the benchmark needs. Idempotent: every step
# is guarded by a stamp (or by make) and can be re-run safely.
#   build.sh [deps|bins|all]     deps = fetch + FFTW builds + fftchode.o, bins = libbench/benchees/binaries
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
. "$HERE/versions.sh"
B=${BENCH_BUILD:-$HERE/build}
DL=$B/dl
JOBS=${JOBS:-$(nproc)}
WHAT=${1:-all}
mkdir -p "$B"/{dl,stamps,bin,lib,obj,log}
LOG=$B/log

say() { printf '[build] %s\n' "$*"; }
need() { command -v "$1" >/dev/null 2>&1 || { echo "missing tool: $1" >&2; return 1; }; }
step() { # step <name> <command...> ; skipped when stamp exists
  local n=$1; shift
  if [ -e "$B/stamps/$n" ]; then say "$n: up to date"; return 0; fi
  say "$n ..."
  ( "$@" ) > "$LOG/$n.log" 2>&1 &
  if wait $!; then touch "$B/stamps/$n"; else echo "FAILED: $n (see $LOG/$n.log)" >&2; tail -20 "$LOG/$n.log" >&2; return 1; fi
}
git_pin() { # git_pin url commit dir
  [ -d "$3/.git" ] && [ "$(git -C "$3" rev-parse HEAD 2>/dev/null)" = "$2" ] && return 0
  rm -rf "$3"; mkdir -p "$3"
  git -C "$3" init -q && git -C "$3" remote add origin "$1"
  git -C "$3" fetch -q --depth 1 origin "$2" && git -C "$3" checkout -q FETCH_HEAD
}
fetch_tar() { # fetch_tar url sha256 file
  [ -f "$3" ] && echo "$2  $3" | sha256sum -c - >/dev/null 2>&1 && return 0
  curl -sSL --retry 3 -o "$3" "$1" && echo "$2  $3" | sha256sum -c -
}

need "$CC"; need "$CXX"; need make; need curl; need git; need python3
HAVE_FC=1
if ! command -v "$FC" >/dev/null 2>&1; then
  say "gfortran not found, trying apt-get"
  (sudo -n apt-get install -y gfortran >"$LOG/apt-gfortran.log" 2>&1 || sudo -n sh -c 'apt-get update && apt-get install -y gfortran' >>"$LOG/apt-gfortran.log" 2>&1) || true
  command -v "$FC" >/dev/null 2>&1 || { say "WARNING: no Fortran compiler, FFTE will be skipped"; HAVE_FC=0; }
fi
echo $HAVE_FC > "$B/have_fortran"

fetch_all() {
  git_pin "$BENCHFFT_URL" "$BENCHFFT_COMMIT" "$DL/benchfft"
  git_pin "$POCKETFFT_URL" "$POCKETFFT_COMMIT" "$DL/pocketfft"
  git_pin "$KISSFFT_URL" "$KISSFFT_COMMIT" "$DL/kissfft"
  fetch_tar "$FFTW_URL" "$FFTW_SHA256" "$DL/fftw-$FFTW_VERSION.tar.gz"
  if [ "$HAVE_FC" = 1 ]; then fetch_tar "$FFTE_URL" "$FFTE_SHA256" "$DL/ffte-$FFTE_VERSION.tgz"; fi
  [ -d "$DL/fftw-$FFTW_VERSION" ] || tar xzf "$DL/fftw-$FFTW_VERSION.tar.gz" -C "$DL"
  if [ "$HAVE_FC" = 1 ] && [ ! -d "$DL/ffte-$FFTE_VERSION" ]; then tar xzf "$DL/ffte-$FFTE_VERSION.tgz" -C "$DL"; fi
}

# upstream FFTW: build_fftw <scalar|simd> <d|s>
build_fftw() {
  local flavor=$1 prec=$2 src=$DL/fftw-$FFTW_VERSION dir=$B/fftw-$1-$2 extra=""
  [ "$prec" = s ] && extra="--enable-single"
  if [ "$flavor" = simd ]; then
    [ "$(uname -m)" = x86_64 ] || { echo "SIMD flavor needs x86_64" >&2; return 1; }
    for f in sse2 avx avx2; do grep -qw "$f" /proc/cpuinfo || { echo "CPU lacks $f" >&2; return 1; }; done
    extra="$extra --enable-sse2 --enable-avx --enable-avx2"
  fi
  rm -rf "$dir"; mkdir -p "$dir/build"
  ( cd "$dir/build" && "$src/configure" --prefix="$dir/inst" --enable-threads --with-combined-threads \
      --disable-fortran --disable-shared --enable-static $extra CC="$CC" CFLAGS="$OPT_C" >configure.log 2>&1 \
    && make -j"$JOBS" >make.log 2>&1 && make install >install.log 2>&1 )
}

build_fftchode() { ( cd "$REPO" && make CXX="$CXX" build/fftchode.o ) ; }

if [ "$WHAT" = deps ] || [ "$WHAT" = all ]; then
  step fetch fetch_all
  # fftchode is rebuilt by make whenever src/fftchode.cpp changes
  say "fftchode -> build/fftchode.o"; build_fftchode > "$LOG/fftchode.log" 2>&1 || { tail -20 "$LOG/fftchode.log" >&2; exit 1; }
  for fl in scalar simd; do for pr in d s; do step "fftw-$fl-$pr" build_fftw $fl $pr; done; done
fi
[ "$WHAT" = deps ] && exit 0

. "$HERE/build_bins.sh"
