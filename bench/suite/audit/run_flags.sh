#!/usr/bin/env bash
# E2: compiler-flag study. matched = clang 18 -O2 -ffp-contract=off (fftchode's release flags); recommended = each library's own.
set -euo pipefail
W=${AUDIT_WORK:-$HOME/.capy/work/audit}; B=$W/bin; C=$W/c/bin; S=$(cd "$(dirname "$0")/.." && pwd); OUT=$W/runs/flags; mkdir -p $OUT
python3 -I $S/compare.py --suite $S/frozen.txt --threads 1 --rounds 3 --out $OUT \
 base=$B/base base-native=$B/base-native fc-bit=$B/fc-bit-estimate fc-native-bit=$B/fc-native-bit-estimate \
 fc-fast=$B/fc-fast-estimate fc-native-fast=$B/fc-native-fast-estimate fc-fast-measure=$B/fc-fast-measure fc-native-fast-measure=$B/fc-native-fast-measure \
 fftw-matched-est=$C/fftw-matched-estimate fftw-est=$C/fftw-all-estimate fftw-matched-measure=$C/fftw-matched-measure fftw-measure=$C/fftw-all-measure \
 aocl-matched-measure=$C/aocl-matched-measure aocl-measure=$C/aocl-noapp-measure \
 ducc0-matched=$C/ducc0-matched ducc0-nofm=$C/ducc0-nofm ducc0-native=$C/ducc0-native \
 pocketfft-matched=$C/pocketfft-matched pocketfft-native=$C/pocketfft-native pffft-matched=$C/pffft-matched pffft-native=$C/pffft-native
python3 -I $S/compare.py --suite $S/frozen.txt --threads 8 --rounds 3 --out $OUT \
 base=$B/base base-native=$B/base-native fc-bit=$B/fc-bit-estimate fc-native-bit=$B/fc-native-bit-estimate fc-fast=$B/fc-fast-estimate fc-native-fast=$B/fc-native-fast-estimate
