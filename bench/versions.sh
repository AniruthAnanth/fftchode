# Pinned inputs for the benchmark harness (sourced by build.sh and run.sh).
BENCHFFT_URL=https://github.com/FFTW/benchfft
BENCHFFT_COMMIT=fb3a478a65d3280c1b255899179583684bfdd973
POCKETFFT_URL=https://github.com/mreineck/pocketfft
POCKETFFT_BRANCH=cpp
POCKETFFT_COMMIT=4d865cba24359b05b37da4e75aee24498c3b78e7
KISSFFT_URL=https://github.com/mborgerding/kissfft
KISSFFT_COMMIT=57a701785b0a4928081ac302c07be5792a0b3a74
FFTW_VERSION=3.3.10
FFTW_URL=https://www.fftw.org/fftw-3.3.10.tar.gz
FFTW_SHA256=56c932549852cddcfafdab3820b0200c7742675be92179e59e6215b340e26467
FFTE_VERSION=7.0
FFTE_URL=http://www.ffte.jp/ffte-7.0.tgz
FFTE_SHA256=078d5f84a5f2479ca5c4a3bd777ad761fe98addf1642a045bac6602a0cae3da0

# Same compiler family and optimisation level for every library.
CC=${CC:-clang}
CXX=${CXX:-clang++}
FC=${FC:-gfortran}
OPT_C="-O2 -ffp-contract=off"
OPT_F="-O2 -ffp-contract=off"
