# fftchode

fftchode is a fast FFT library in one C++ file, `src/fftchode.cpp`. Its API is FFTW 3-compatible: `src/fftchode.h` declares the FFTW 3 interface (`fftw_*`, `fftwf_*`, threads included), so code written for FFTW builds against fftchode unchanged. It started as FFTW 3.3.10 and is GPL-2.0-or-later; every original copyright notice is kept.

It has two numerics modes:

- **baseline** (the default) returns results that are bit-identical to scalar FFTW 3.3.10 built without FMA contraction.
- **fast** adds AVX2/AVX-512 SIMD codelets, extra solvers and a cost-model planner. Its results differ in the last bits, and its error is equal to or below FFTW's (relative rms about 2.4e-16 in double precision and 1.3e-7 in single).

## Performance

These are geometric-mean speedups on the 128-case frozen suite, measured on an AMD Zen 4 cloud VM (4 cores with 2 SMT threads each, AVX-512) after a fairness audit of every comparison ([`bench/results/audit/AUDIT.md`](bench/results/audit/AUDIT.md)). The reference is scalar FFTW 3.3.10 (baseline-v0) on 1 thread. Every library uses its own release build, and FFTW and AOCL-FFTW are shown with their stock threading and with a persistent thread pool installed through `fftw_threads_set_callback`. Planning is ESTIMATE unless noted. Peak is the geometric mean of the heap each case needs.

| library | 1 thread | 8 threads | best thread count per case | peak heap |
|---|---|---|---|---|
| FFTW 3.3.10 scalar (baseline-v0, reference) | 1.00 | 0.91 | 1.00 | 237 KiB |
| **fftchode, baseline mode** (bit-identical) | **1.14** | **2.77** | **1.40** | 230 KiB |
| **fftchode, fast mode** | **2.77** | **6.76** | **3.09** | 728 KiB |
| fftchode, fast mode, MEASURE | 3.46 | 7.66 | 3.69 | |
| FFTW 3.3.10 AVX-512, stock threads / pool | 1.93 | 1.29 / 3.22 | 1.79 / 2.10 | 683 KiB |
| FFTW 3.3.10 AVX-512 MEASURE, stock threads / pool | 3.31 | 2.19 / 4.90 | 2.65 / 3.18 | |
| AOCL-FFTW 6.0 MEASURE, stock threads / pool | 3.44 | 2.61 / 5.49 | 2.80 / 3.41 | |
| ducc0 0.42 | 1.51 | 2.99 | 1.38 | |
| oneMKL 2026.2 (generic SSE2 kernels on AMD) | 2.40 | 4.72 | 2.28 | 412 KiB |

The 1- and 8-thread columns are relative to baseline-v0 on 1 thread. The best-thread-count column runs each library and baseline-v0 at their own fastest of 1, 2, 4 and 8 threads per case. Built with `-O3 -march=znver4` instead of the release flags, baseline-v0 gets 1.13x on its own, and baseline mode's 1-thread gain over that build is 1.04x.

At matched planning levels (MKL excluded), fftchode reaches a geometric-mean ratio to the fastest library in each case of 1.00 at 1 thread and 1.23 at 8 threads with ESTIMATE, and 0.91 and 1.05 with MEASURE. With the thread pool, FFTW MEASURE comes within 1.24–1.38x of fftchode fast mode at 8 threads. AOCL-FFTW's app-opt planner, which searches for seconds to hours, is 1.15x faster than fftchode MEASURE at 1 thread. The audit also covers planning time, CPU time and accuracy.

## Build

```sh
make                # build/fftchode.o (clang++ -O2 -ffp-contract=off)
make lib            # build/libfftchode.a
c++ -std=c++17 -O2 -ffp-contract=off -pthread -c src/fftchode.cpp   # or compile it directly
```

The only dependencies are the C++ standard library, libm and pthreads. No `-m` flags are needed: SIMD code is compiled per function and chosen at run time. The file includes itself once per precision, so keep the name `fftchode.cpp`. Optional macros: `FFT_NO_THREADS`, `FFT_NO_SINGLE`, `FFT_NO_DOUBLE`, `FFT_NO_REODFT`, `FFT_NO_SIMD`.

## Usage

Include `fftchode.h` where you would include `fftw3.h`, and link `build/libfftchode.a -pthread`. Select the mode with `fftchode_set_numerics(0)` (baseline) or `fftchode_set_numerics(1)` (fast) before the first plan, or with `FFTCHODE_NUMERICS=baseline|fast` in the environment.

## Tests

- `make test-upstream` builds FFTW 3.3.10 from the checksummed fftw.org tarball with the same flags and checks 4020 cases for identical output bytes and wisdom in baseline mode.
- `make test-fast` checks that 5616 fast-mode transforms agree with baseline mode to within roundoff.
- `make test-matlab GT=<dir>` compares MATLAB-style `fft`/`ifft`/`fft2`/`fftn` against MATLAB ground truth (not included).

## Docs

[`docs/fftchode.pdf`](docs/fftchode.pdf) (source: [`docs/fftchode.tex`](docs/fftchode.tex)) describes the algorithm, each change, and the measurements. [`bench/`](bench/README.md) holds the benchmark harness and raw results.
