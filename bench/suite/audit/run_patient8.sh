#!/usr/bin/env bash
# E3 (8 threads): PATIENT planning, one round, per-case budget CASE_TIMEOUT=120 s per process (plan + timing); a timeout is reported as absent.
set -euo pipefail
W=${AUDIT_WORK:-$HOME/.capy/work/audit}; B=$W/bin; C=$W/c/bin; S=$(cd "$(dirname "$0")/.." && pwd); OUT=$W/runs/patient8; mkdir -p $OUT $W/wrap
mk() { printf '#!/bin/sh\n%sexec env CASE_TIMEOUT=${CASE_TIMEOUT:-120} python3 -I %s/case_timeout.py %s "$@" --plan patient %s\n' "$2" $S $3 "$4" > $W/wrap/$1; chmod +x $W/wrap/$1; }
mk fftw-patient "" $C/fftw-all ""
mk fftw-pool-patient "" $C/fftw-all "--pool spin"
mk aocl-patient "" $C/aocl-noapp ""
mk fc-fast-patient "FFTCHODE_NUMERICS=fast " $B/fc ""
python3 -I $S/compare.py --suite $S/frozen.txt --threads 8 --rounds 1 --out $OUT base=$B/base fc-fast-patient=$W/wrap/fc-fast-patient fftw-patient=$W/wrap/fftw-patient fftw-pool-patient=$W/wrap/fftw-pool-patient aocl-patient=$W/wrap/aocl-patient
