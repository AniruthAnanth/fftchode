#!/usr/bin/env bash
# MKL code-path study on the current CPU (FFTE-6 comparators).
#
#   bench/suite/adapters/mkl_probe.sh <outdir>      (needs comparators.sh <outdir> mkl harness first; uses gdb/strace if installed)
#
# Writes <outdir>/mkl-codepath.txt: (1) version and the dispatch queries of adapters/mkl_probe.cpp, (2) the DFTI kernel that is actually
# executing (sampled program counter -> symbol, mkl_dft_<isa>_*) under each environment setting, (3) smoke timings of bin/mkl per setting.
# Settings: default, MKL_ENABLE_INSTRUCTIONS=AVX512|AVX2|SSE4_2, MKL_DEBUG_CPU_TYPE=5, MKL_CBWR=AVX2|AVX512|COMPATIBLE.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(cd "$1" && pwd); M=$OUT/mklvenv; BIN=$OUT/bin; W=$OUT/mkl-probe; REP=$OUT/mkl-codepath.txt
mkdir -p "$W"
S="$M/lib/libmkl_intel_lp64.a $M/lib/libmkl_intel_thread.a $M/lib/libmkl_core.a"
clang++ -O2 -I"$M/include" "$HERE/mkl_probe.cpp" -o "$W/probe-static" -Wl,--start-group $S -Wl,--end-group "$M/lib/libiomp5.a" -lpthread -ldl -lm
SETTINGS=("X=default" "MKL_ENABLE_INSTRUCTIONS=AVX512" "MKL_ENABLE_INSTRUCTIONS=AVX2" "MKL_ENABLE_INSTRUCTIONS=SSE4_2" "MKL_DEBUG_CPU_TYPE=5" "MKL_CBWR=AVX2" "MKL_CBWR=AVX512" "MKL_CBWR=COMPATIBLE")
printf 'd_cf1024 d cf 1024 1 contig oop\ns_cf1024 s cf 1024 1 contig oop\nd_rf1024 d rf 1024 1 contig oop\nd_cf1000 d cf 1000 1 contig oop\nd_cf512x512 d cf 512x512 1 contig oop\n' > "$W/sample.txt"
CASES="d_cf1024 d_cf65536 d_cf1048576 s_cf65536 d_rf65536 d_cf1024h256 d_cf512x512 d_cf1000"
grep -E "^($(echo $CASES | tr ' ' '|')) " "$HERE/../frozen.txt" > "$W/timing.txt"
{
echo "== host: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | xargs), family $(grep -m1 'cpu family' /proc/cpuinfo | cut -d: -f2 | xargs) model $(grep -m1 '^model' /proc/cpuinfo | cut -d: -f2 | xargs); $(date -u +%FT%TZ)"
echo "== probe (static link)"; "$W/probe-static"
echo; echo "== MKL_VERBOSE=1 first line (bin/mkl, one DFTI call)"
MKL_VERBOSE=1 "$BIN/mkl" --suite "$W/sample.txt" --batches 1 --tmin 0.001 2>&1 | grep -m2 -E '^MKL_VERBOSE (oneMKL|DFTI)' || true
echo; echo "== shared objects opened by a dynamically linked bin/mkl (strace -e openat; libmkl_def = generic path, libmkl_avx2/avx512 = ISA paths)"
if command -v strace >/dev/null; then
  clang++ -std=c++17 -O2 -I"$HERE" -I"$M/include" "$HERE/mkl.cpp" "$OUT/build/obj/memtrack_stub.o" -o "$W/mkl-dyn" -Wl,-rpath,"$M/lib" "$M/lib/libmkl_intel_lp64.so.3" "$M/lib/libmkl_intel_thread.so.3" "$M/lib/libmkl_core.so.3" "$M/lib/libiomp5.so" -lpthread -ldl -lm 2>/dev/null
  strace -f -e trace=openat "$W/mkl-dyn" --suite "$W/sample.txt" --batches 1 --tmin 0.001 2>&1 | grep -o 'libmkl_[a-z0-9_]*\.so[.0-9]*' | sort -u | tr '\n' ' '; echo
fi
echo; echo "== kernel executing in DFTI (gdb PC samples, 6 per setting; symbol prefix names the ISA: mkl_dft_def_ / _avx2_ / _avx512_ ...)"
if command -v gdb >/dev/null; then
  for c in d_cf1024 d_rf1024 d_cf1000 d_cf512x512 s_cf1024; do
    grep -E "^$c " "$W/sample.txt" > "$W/one.txt"
    for st in "${SETTINGS[@]}"; do
      env "$st" taskset -c 3 "$BIN/mkl" --suite "$W/one.txt" --tmin 6 --batches 1 > /dev/null & sleep 1.5
      pid=$(pgrep -n -x mkl || true)
      top=$(for i in 1 2 3 4 5 6; do gdb -p "$pid" -batch -ex 'info symbol $pc' 2>/dev/null | grep ' in section' | sed 's/ + [0-9]* in section.*//'; sleep 0.2; done | sort | uniq -c | sort -rn | head -1)
      pkill -x mkl || true; wait 2>/dev/null || true
      printf '%-12s %-34s %s\n' "$c" "$st" "$top"
    done
  done
fi
echo; echo "== smoke timings, seconds per transform (median of 3 batches, 1 thread, pinned cpu 2); NOT official numbers"
python3 -I - "$BIN/mkl" "$W/timing.txt" "$(IFS=,; echo "${SETTINGS[*]}")" <<'EOF'
import sys, subprocess, os, statistics
b, suite, sets = sys.argv[1], sys.argv[2], sys.argv[3].split(",")
res = {}
for st in sets:
    k, v = st.split("=", 1); env = dict(os.environ); env[k] = v
    out = subprocess.run(["taskset", "-c", "2", b, "--suite", suite, "--threads", "1", "--batches", "3", "--tmin", "0.05"], env=env, capture_output=True, text=True).stdout
    for l in out.splitlines():
        f = l.split()
        if len(f) >= 5 and f[2] != "FAIL": res.setdefault(f[0], {})[st] = statistics.median(float(x) for x in f[4:])
print("%-14s" % "case" + "".join("%-34s" % s[:33] for s in sets))
for c in res: print("%-14s" % c + "".join("%-34.3e" % res[c].get(s, float("nan")) for s in sets))
EOF
} 2>&1 | tee "$REP"
