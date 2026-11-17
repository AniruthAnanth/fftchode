# ffte

`src/fft.cpp` is FFTW 3.3.10 in a single C++ translation unit. It has the scalar codelets, the double (`fftw_`) and single (`fftwf_`) libraries, and POSIX threads. The public API is FFTW's own, declared in `src/fftw3.h`, which is the unmodified upstream header. FFTW is GPL-2.0-or-later, and every original copyright notice is kept in the file.

## Build

```sh
make                       # build/fft.o with clang++ -O2 -ffp-contract=off (the fidelity configuration)
c++ -std=c++17 -O2 -ffp-contract=off -pthread -c src/fft.cpp      # or compile it directly
```

The only dependencies are the C++ standard library, libm and pthreads. The file includes itself once per precision, so keep the name `fft.cpp`. It compiles warning-free with clang++ and g++.

| macro | effect |
|---|---|
| `FFT_NO_THREADS` | no `fftw_init_threads` and no threaded solvers |
| `FFT_NO_SINGLE` / `FFT_NO_DOUBLE` | drop one precision |
| `FFT_NO_REODFT` | drop the DCT/DST (REDFT/RODFT) solvers |

## Tests

- `make test-upstream` downloads the fftw.org 3.3.10 tarball and checks its sha256. It then builds upstream FFTW with the same compiler and flags (scalar, combined threads) and runs `tests/fft_dump.cpp` against both libraries. That battery has 4020 cases: c2c, r2c and c2r in both precisions, many sizes, batched, strided and in-place layouts, rank 1–5, guru64 along every dimension, all r2r kinds, and 1–10 threads. For every case the output bytes and the exported plan wisdom must be identical.
- `make test-matlab GT=<dir>` compares MATLAB-style `fft`/`ifft`/`fft2`/`fftn` results, built on this library by `tests/mlfft.cpp`, against a directory of MATLAB ground-truth arrays. That data is not part of this repository.

## Benchmarks

`bench/` holds the benchFFT-based comparison against upstream FFTW (scalar and SIMD), pocketfft, KissFFT and FFTE, with speed, memory and compiled-size measurements. Results are in `bench/results/`.

## Docs

`docs/MATH.md` explains the algorithms in the file.
