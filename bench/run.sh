#!/usr/bin/env bash
# One-command benchmark: builds everything, then measures speed, memory and compiled size.
#
#   bench/run.sh [--sizes "2 4 ..."] [--trials N] [--threads "1 4"] [--libs "..."] [--prec "d s"]
#                [--kinds "c r"] [--time-min SEC] [--mem-sizes "..."] [--timeout SEC] [--out DIR]
#                [--force] [--skip-build] [--skip-speed] [--skip-mem] [--skip-size]
#
# libs: fftcpp fftw-scalar fftw-simd pocketfft kissfft ffte   prec: d (double) s (single)
# kinds: c (complex forward, c2c)  r (real forward, r2c)
# Results (CSV) go to <out>/ (default bench/results). Finished measurements are cached under
# bench/build/parts and reused unless parameters change or --force is given.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/versions.sh"
B=${BENCH_BUILD:-$HERE/build}
NPROC=$(nproc)

POW2=""; for e in $(seq 1 20); do POW2="$POW2 $((1<<e))"; done
SIZES="$POW2 6 9 12 15 18 24 36 80 108 210 504 1000 1960 4725 10368 27000 75600 165375 1000003"
SIZES=$(echo $SIZES)
MEM_SIZES="1024 65536 1048576 1000003"
TRIALS=5; TIME_MIN=0.05; TIMEOUT=600
THREADS="1 $NPROC"
LIBS="fftcpp fftw-scalar fftw-simd pocketfft kissfft ffte"
PREC="d s"; KINDS="c r"; OUT=$HERE/results
FORCE=0; DO_BUILD=1; DO_SPEED=1; DO_MEM=1; DO_SIZE=1

while [ $# -gt 0 ]; do
  case $1 in
    --sizes) SIZES=$2; shift 2;;
    --trials) TRIALS=$2; shift 2;;
    --threads) THREADS=$2; shift 2;;
    --libs) LIBS=$2; shift 2;;
    --prec) PREC=$2; shift 2;;
    --kinds) KINDS=$2; shift 2;;
    --time-min) TIME_MIN=$2; shift 2;;
    --mem-sizes) MEM_SIZES=$2; shift 2;;
    --timeout) TIMEOUT=$2; shift 2;;
    --out) OUT=$2; shift 2;;
    --force) FORCE=1; shift;;
    --skip-build) DO_BUILD=0; shift;;
    --skip-speed) DO_SPEED=0; shift;;
    --skip-mem) DO_MEM=0; shift;;
    --skip-size) DO_SIZE=0; shift;;
    -h|--help) sed -n '2,13p' "$0"; exit 0;;
    *) echo "unknown option $1" >&2; exit 2;;
  esac
done

T0=$(date +%s)
if [ $DO_BUILD = 1 ]; then "$HERE/build.sh" all; fi
mkdir -p "$OUT" "$B/parts"
"$HERE/scripts/env.sh" "$B" > "$OUT/ENV.txt"
echo "run options: sizes=[$SIZES] trials=$TRIALS time-min=$TIME_MIN threads=[$THREADS] libs=[$LIBS] prec=[$PREC] kinds=[$KINDS] mem-sizes=[$MEM_SIZES]" >> "$OUT/ENV.txt"
echo "planning:     FFTW_ESTIMATE (fft.cpp and both upstream FFTW builds); placement out-of-place except FFTE (in-place only)" >> "$OUT/ENV.txt"

load=$(cut -d' ' -f1 /proc/loadavg)
echo "[run] 1-min load average before timing: $load (should be ~0 on an idle machine)"

supports_threads() { case $1 in fftcpp|fftw-scalar|fftw-simd|pocketfft) return 0;; *) return 1;; esac; }
threads_for() { if supports_threads "$1"; then echo $THREADS; else echo 1; fi; }

# measure <mode> <csv> <lib list> <prec> <sizes> [extra args]   -- cached per (lib,prec,threads)
run_mode() {
  local mode=$1 csv=$2 sizes=$3 binsfx=$4 libs=$5 lib prec t bin part key tmp first=1 parts=()
  for lib in $libs; do for prec in $PREC; do
    [ -x "$B/bin/$lib-$prec$binsfx" ] || continue
    for t in $(threads_for $lib); do
      key=$(echo "$mode|$sizes|$KINDS|$TRIALS|$TIME_MIN|$TIMEOUT|$lib|$prec|$t|$(sha256sum "$B/bin/$lib-$prec$binsfx" | cut -c1-12)" | cksum | cut -d' ' -f1)
      part=$B/parts/$mode-$lib-$prec-t$t-$key.csv
      if [ $FORCE = 1 ] || [ ! -e "$part" ]; then
        echo "[run] $mode: $lib $prec threads=$t"
        tmp=$part.tmp
        "$B/bin/$lib-$prec$binsfx" --lib $lib --mode $mode --threads $t --sizes "$sizes" --kinds "$KINDS" \
          --trials $TRIALS --time-min $TIME_MIN --timeout $TIMEOUT --header > "$tmp"
        mv "$tmp" "$part"
      fi
      parts+=("$part")
    done
  done; done
  [ ${#parts[@]} -gt 0 ] || { echo "[run] no $mode binaries for the selected libs/precisions" >&2; return 0; }
  head -n 1 "${parts[0]}" > "$csv"
  for part in "${parts[@]}"; do tail -n +2 "$part" >> "$csv"; done
}

if [ $DO_SPEED = 1 ]; then
  TS=$(date +%s)
  run_mode speed "$OUT/speed.csv" "$SIZES" "" "$LIBS"
  echo "[run] speed phase: $(( $(date +%s) - TS )) s"
fi
if [ $DO_MEM = 1 ]; then
  run_mode mem "$OUT/memory.csv" "$MEM_SIZES" -mem "null $LIBS"
fi
if [ $DO_SIZE = 1 ]; then "$HERE/scripts/sizes.sh" "$B" "$OUT/size.csv"; fi
if [ -e "$OUT/speed.csv" ]; then python3 -I "$HERE/scripts/summarize.py" "$OUT" || echo "[run] summary failed" >&2; fi
echo "[run] total wall time: $(( $(date +%s) - T0 )) s" | tee -a "$OUT/ENV.txt"
