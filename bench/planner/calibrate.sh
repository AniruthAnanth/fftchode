#!/usr/bin/env bash
# Refit the fast-mode ESTIMATE cost weights (src/fft.cpp FFTCHODE_FCW_INIT) on this machine.
#   bench/planner/calibrate.sh <outdir> [train.txt]
# Env: CPU=<core to pin to, default 1>, SEEDS=<random-perturbation plans per case, default 16>,
#      ROUNDS=<interleaved timing rounds, default 7>.
# Builds fft.cpp with -DFFTCHODE_PLANNER_CALIBRATE, samples ~26 plans per training case (fast
# ESTIMATE, fast MEASURE and ESTIMATE with randomly perturbed costs), times every distinct plan
# interleaved, and fits the 36 weights by NNLS (fit.py, NEAR=1).  Takes a few hours for the
# shipped 124-case training set.  The training cases exclude every frozen-suite shape
# (gen_train.py); never calibrate on the frozen suite.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$HERE/../.." && pwd)
CXX=${CXX:-clang++}; CC=${CC:-clang}; PY=${PY:-python3}
O=$1; S=${2:-$HERE/train.txt}; CPU=${CPU:-1}; N=${SEEDS:-16}; R=${ROUNDS:-7}
mkdir -p "$O"
$CXX -std=c++17 -O2 -ffp-contract=off -pthread -DFFTCHODE_PLANNER_CALIBRATE -c "$REPO/src/fft.cpp" -o "$O/fft_cal.o"
$CC -O2 -c "$REPO/bench/src/memtrack_stub.c" -o "$O/stub.o"
$CXX -std=c++17 -O2 -I"$REPO/src" "$HERE/retime.cpp" "$O/stub.o" "$O/fft_cal.o" -pthread -lm -o "$O/retime"
export FFTCHODE_NUMERICS=fast
c() { taskset -c "$CPU" "$O/retime" --suite "$S" --collect "$@"; }
c "$O/est.wis"
c "$O/meas.wis" --flags measure
for i in $(seq 1 "$N"); do FFTCHODE_PLANRAND=1.5,$i c "$O/r$i.wis"; done
for i in $(seq 1 8); do FFTCHODE_PLANRAND=0.4,$((100 + i)) c "$O/q$i.wis"; done
taskset -c "$CPU" "$O/retime" --suite "$S" --retime "$R" --tmin 0.005 "$O"/*.wis > "$O/retimed.txt" 2> "$O/retime.log"
NEAR=1 $PY "$HERE/fit.py" "$O/retimed.txt" | tee "$O/fit.txt"
