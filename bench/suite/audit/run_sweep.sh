#!/usr/bin/env bash
# Thread sweep: every library at T threads, interleaved per case and round (fairness audit, FFTE-8).
#   run_sweep.sh <threads> <rounds> <outdir>
set -euo pipefail
T=$1; R=$2; OUT=$3
W=${AUDIT_WORK:-$HOME/.capy/work/audit}; B=$W/bin; C=$W/c/bin; REPO=$(cd "$(dirname "$0")/../../.." && pwd)
L="base=$B/base base-aa=$B/base-aa fc-bit=$B/fc-bit-estimate fc-fast=$B/fc-fast-estimate fc-fast-measure=$B/fc-fast-measure
 fftw-est=$C/fftw-all-estimate fftw-measure=$C/fftw-all-measure aocl-est=$C/aocl-noapp-estimate aocl-measure=$C/aocl-noapp-measure
 mkl=$C/mkl ducc0-nofm=$C/ducc0-nofm ducc0-native=$C/ducc0-native pocketfft-native=$C/pocketfft-native"
if [ "$T" -gt 1 ]; then L="$L fftw-pool-est=$C/fftw-all-pool-estimate fftw-pool-measure=$C/fftw-all-pool-measure aocl-pool-measure=$C/aocl-noapp-pool-measure"
else L="$L pffft-native=$C/pffft-native kissfft=$C/kissfft ffte=$C/ffte"; fi
python3 -I $REPO/bench/suite/compare.py --suite $REPO/bench/suite/frozen.txt --threads $T --rounds $R --out $OUT $L
