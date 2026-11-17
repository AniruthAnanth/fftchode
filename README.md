# fftchode

fftchode is a fast FFT library in one C++ file, \`src/fftchode.cpp\`. Its API is FFTW 3-compatible: \`src/fftchode.h\` declares the FFTW 3 interface (\`fftw_*\`, \`fftwf_*\`, threads included), so code written for FFTW builds against fftchode unchanged. It started as FFTW 3.3.10 and is GPL-2.0-or-later; every original copyright notice is kept.

It has two numerics modes:

- **baseline** (the default) returns results that are bit-identical to scalar FFTW 3.3.10 built without FMA contraction.
- **fast** adds AVX2/AVX-512 SIMD codelets, extra solvers and a cost-model planner. Its results differ in the last bits, and its error is equal to or below FFTW's (relative rms about 2.4e-16 in double precision and 1.3e-7 in single).

## Performance

These are geometric-mean speedups over scalar FFTW 3.3.10 on the 128-case frozen suite, measured on an AMD Zen 4 cloud VM (4 cores, 8 threads, AVX-512). All runs use ESTIMATE planning unless noted. Peak is the geometric mean of the heap each case needs. Confidence intervals, the held-out suite, comparators and ablations are in [\`docs/fftchode.pdf\`](docs/fftchode.pdf) and [\`bench/results/SUMMARY.md\`](bench/results/SUMMARY.md).

| library | 1 thread | 8 threads | peak heap |
|---|---|---|---|
| FFTW 3.3.10 scalar (reference) | 1.00 | 1.00 | 237 KiB |
| **fftchode, baseline mode** | **1.14** | **2.88** | 230 KiB |
| **fftchode, fast mode** | **2.67** | **6.42** | 728 KiB |
| fftchode, fast mode, MEASURE | 3.43 | 7.62 | |
| FFTW 3.3.10 AVX-512 | 1.88 | 1.43 | 683 KiB |
| FFTW 3.3.10 AVX-512, MEASURE | 3.27 | 2.40 | |
| fastest other library (AOCL-FFTW MEASURE / oneMKL) | 3.47 | 5.07 | 412 KiB (MKL) |

fftchode does not win every case. Against the fastest library in each case, it reaches a geometric-mean ratio of 0.78 at 1 thread and 0.98 at 8 threads with MEASURE. The paper says where it loses and why.

## Build

\`\`\`sh
make                # build/fftchode.o (clang++ -O2 -ffp-contract=off)
make lib            # build/libfftchode.a
c++ -std=c++17 -O2 -ffp-contract=off -pthread -c src/fftchode.cpp   # or compile it directly
\`\`\`

The only dependencies are the C++ standard library, libm and pthreads. No \`-m\` flags are needed: SIMD code is compiled per function and chosen at run time. The file includes itself once per precision, so keep the name \`fftchode.cpp\`. Optional macros: \`FFT_NO_THREADS\`, \`FFT_NO_SINGLE\`, \`FFT_NO_DOUBLE\`, \`FFT_NO_REODFT\`, \`FFT_NO_SIMD\`.

## Usage

Include \`fftchode.h\` where you would include \`fftw3.h\`, and link \`build/libfftchode.a -pthread\`. Select the mode with \`fftchode_set_numerics(0)\` (baseline) or \`fftchode_set_numerics(1)\` (fast) before the first plan, or with \`FFTCHODE_NUMERICS=baseline|fast\` in the environment.

## Tests

- \`make test-upstream\` builds FFTW 3.3.10 from the checksummed fftw.org tarball with the same flags and checks 4020 cases for identical output bytes and wisdom in baseline mode.
- \`make test-fast\` checks that 5616 fast-mode transforms agree with baseline mode to within roundoff.
- \`make test-matlab GT=<dir>\` compares MATLAB-style \`fft\`/\`ifft\`/\`fft2\`/\`fftn\` against MATLAB ground truth (not included).

## Docs

[\`docs/fftchode.pdf\`](docs/fftchode.pdf) (source: [\`docs/fftchode.tex\`](docs/fftchode.tex)) describes the algorithm, each change, and the measurements. [\`bench/\`](bench/README.md) holds the benchmark harness and raw results.
