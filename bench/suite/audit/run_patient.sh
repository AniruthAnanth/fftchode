#!/usr/bin/env bash
# E3 (1 thread; 8 threads: run_patient8.sh): PATIENT planning with a per-case budget (CASE_TIMEOUT s per process, plan + timing); timeouts are reported as absent.
set -euo pipefail
W=${AUDIT_WORK:-$HOME/.capy/work/audit}; B=$W/bin; C=$W/c/bin; S=$(cd "$(dirname "$0")/.." && pwd); OUT=$W/runs/patient; mkdir -p $OUT $W/wrap
mk() { printf '#!/bin/sh\n%sexec env CASE_TIMEOUT=${CASE_TIMEOUT:-180} python3 -I %s/case_timeout.py %s "$@" --plan patient %s\n' "$2" $S $3 "$4" > $W/wrap/$1; chmod +x $W/wrap/$1; }
mk fftw-patient "" $C/fftw-all ""
mk fftw-pool-patient "" $C/fftw-all "--pool spin"
mk aocl-patient "" $C/aocl-noapp ""
mk fc-fast-patient "FFTCHODE_NUMERICS=fast " $B/fc ""
python3 -I $S/compare.py --suite $S/frozen.txt --threads 1 --rounds 2 --out $OUT base=$B/base fc-fast-patient=$W/wrap/fc-fast-patient fftw-patient=$W/wrap/fftw-patient aocl-patient=$W/wrap/aocl-patient
