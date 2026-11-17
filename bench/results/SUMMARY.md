# fftchode benchmark summary

Machine: AMD EPYC (Zen 4) cloud VM, 4 cores / 8 threads, AVX-512, 32 GB; Ubuntu 24.04, clang 18.1.3. Every figure is a geometric-mean speedup over baseline-v0 (FFTW 3.3.10 scalar in one file, clang -O2 -ffp-contract=off) measured in the same interleaved run; 95% confidence intervals come from a paired bootstrap over rounds. Suites: bench/suite/frozen.txt (128 cases) and bench/suite/heldout.txt (78 cases, never used for tuning). Planning is FFTW_ESTIMATE unless stated.

## Headline

| build | suite | 1 thread | 8 threads |
|---|---|---|---|
| fftchode baseline mode (bit-identical) | frozen | 1.143 | 2.882 |
| fftchode baseline mode (bit-identical) | heldout | 1.155 | 2.812 |
| fftchode fast mode | frozen | 2.669 | 6.422 |
| fftchode fast mode | heldout | 2.930 | 6.944 |

Confidence intervals for these rows are in the run summaries (docs/fftchode.pdf, Table 2).

## Ablation (frozen suite, 5 rounds): remove one change from the final build

| build | 1 thread | 8 threads |
|---|---|---|
| baseline mode, all changes | 1.147 | 2.852 |
| baseline mode without per-ISA scalar codelets | 1.065 | 2.644 |
| fast mode, all changes | 2.673 | 6.350 |
| fast mode without the cost-model planner (stock ESTIMATE) | 1.933 | 4.916 |
| fast mode without SIMD codelets | 1.237 | 3.117 |
| fast mode without per-ISA scalar codelets | 2.637 | 6.243 |
| fast mode without fftchode's fast solvers (pair/embed/colpack) | 2.620 | 6.179 |

## Against the fastest comparator, 1 thread

Comparators: aocl-app-measure (97 cases, geomean 3.86), aocl-est (128 cases, geomean 2.06), aocl-measure (128 cases, geomean 3.47), ducc0-matched (128 cases, geomean 1.28), ducc0-native (128 cases, geomean 1.59), ffte (21 cases, geomean 1.08), fftw-est (128 cases, geomean 1.88), fftw-measure (128 cases, geomean 3.27), fftw-patient (102 cases, geomean 3.66), kissfft (98 cases, geomean 0.57), mkl (128 cases, geomean 2.49), pffft-matched (50 cases, geomean 2.45), pffft-native (50 cases, geomean 3.13), pocketfft-matched (128 cases, geomean 1.02), pocketfft-native (128 cases, geomean 1.16).

Ratio = fftchode speedup / best comparator speedup in that cell (>1: fftchode faster). 'ESTIMATE' is fftchode fast mode with ESTIMATE planning (MATLAB's mode); 'MEASURE' is fftchode fast mode with FFTW_MEASURE.

| precision | type | class | cells | ESTIMATE | MEASURE | cells won (MEASURE) | fastest comparator most often |
|---|---|---|---|---|---|---|---|
| d | c2c | 2-D | 5 | 0.54 | 0.74 | 0 | aocl-app-measure |
| d | c2c | 3-D | 3 | 0.57 | 0.79 | 0 | aocl-measure |
| d | c2c | batched | 4 | 0.83 | 0.87 | 0 | aocl-measure |
| d | c2c | large prime factor | 2 | 0.51 | 0.61 | 0 | fftw-patient |
| d | c2c | mixed radix | 6 | 0.76 | 0.87 | 1 | fftw-patient |
| d | c2c | pow2 large | 7 | 0.62 | 0.89 | 1 | aocl-measure |
| d | c2c | pow2 small | 7 | 0.75 | 0.82 | 0 | aocl-measure |
| d | c2c | prime | 5 | 0.66 | 0.86 | 1 | aocl-measure |
| d | c2c | strided | 3 | 0.39 | 0.72 | 0 | ducc0-native |
| d | c2r | 2-D | 1 | 0.63 | 0.80 | 0 | fftw-patient |
| d | c2r | mixed radix | 1 | 0.86 | 0.78 | 0 | fftw-patient |
| d | c2r | pow2 large | 2 | 0.56 | 0.88 | 0 | fftw-patient |
| d | c2r | pow2 small | 1 | 0.91 | 0.96 | 0 | fftw-patient |
| d | r2c | 2-D | 2 | 0.68 | 0.84 | 0 | aocl-app-measure |
| d | r2c | 3-D | 1 | 0.76 | 0.82 | 0 | fftw-patient |
| d | r2c | batched | 2 | 0.74 | 0.73 | 0 | ducc0-native |
| d | r2c | large prime factor | 1 | 0.35 | 0.35 | 0 | aocl-app-measure |
| d | r2c | mixed radix | 2 | 0.91 | 0.94 | 0 | fftw-patient |
| d | r2c | pow2 large | 2 | 0.63 | 0.86 | 0 | aocl-measure |
| d | r2c | pow2 small | 4 | 0.78 | 0.80 | 0 | fftw-measure |
| d | r2c | prime | 2 | 0.77 | 1.14 | 2 | mkl |
| d | r2c | strided | 1 | 0.48 | 0.64 | 0 | pocketfft-native |
| s | c2c | 2-D | 5 | 0.49 | 0.70 | 0 | aocl-app-measure |
| s | c2c | 3-D | 3 | 0.73 | 0.77 | 0 | aocl-measure |
| s | c2c | batched | 4 | 0.82 | 0.92 | 1 | aocl-measure |
| s | c2c | large prime factor | 2 | 0.66 | 0.74 | 0 | fftw-patient |
| s | c2c | mixed radix | 6 | 0.69 | 0.77 | 0 | fftw-patient |
| s | c2c | pow2 large | 7 | 0.36 | 0.60 | 0 | pffft-native |
| s | c2c | pow2 small | 7 | 0.64 | 0.81 | 0 | aocl-measure |
| s | c2c | prime | 5 | 0.55 | 0.76 | 2 | mkl |
| s | c2c | strided | 3 | 0.32 | 0.88 | 1 | ducc0-native |
| s | c2r | 2-D | 1 | 0.67 | 0.81 | 0 | aocl-app-measure |
| s | c2r | mixed radix | 1 | 0.88 | 0.69 | 0 | aocl-app-measure |
| s | c2r | pow2 large | 2 | 0.58 | 0.73 | 0 | aocl-app-measure |
| s | c2r | pow2 small | 1 | 0.87 | 0.89 | 0 | fftw-patient |
| s | r2c | 2-D | 2 | 0.55 | 0.87 | 0 | aocl-measure |
| s | r2c | 3-D | 1 | 0.72 | 0.79 | 0 | aocl-app-measure |
| s | r2c | batched | 2 | 0.78 | 0.78 | 0 | mkl |
| s | r2c | large prime factor | 1 | 0.56 | 0.53 | 0 | aocl-app-measure |
| s | r2c | mixed radix | 2 | 0.91 | 0.81 | 0 | aocl-measure |
| s | r2c | pow2 large | 2 | 0.55 | 0.82 | 0 | fftw-patient |
| s | r2c | pow2 small | 4 | 0.74 | 0.73 | 0 | aocl-app-measure |
| s | r2c | prime | 2 | 0.54 | 0.89 | 1 | mkl |
| s | r2c | strided | 1 | 0.35 | 0.41 | 0 | ducc0-native |
| all | | | 128 | 0.62 | 0.78 | 10 | |

Cells where fftchode (MEASURE) is at least as fast as every comparator: 10 of 128; with ESTIMATE: 2.


## Against the fastest comparator, 8 threads

Comparators: aocl-app-measure (63 cases, geomean 12.97), aocl-est (128 cases, geomean 1.53), aocl-measure (128 cases, geomean 2.87), ducc0-matched (128 cases, geomean 2.67), ducc0-native (128 cases, geomean 3.17), ffte (21 cases, geomean 2.40), fftw-est (128 cases, geomean 1.43), fftw-measure (128 cases, geomean 2.40), kissfft (98 cases, geomean 1.12), mkl (128 cases, geomean 5.07), pffft-matched (50 cases, geomean 4.72), pffft-native (50 cases, geomean 5.96), pocketfft-matched (128 cases, geomean 1.64), pocketfft-native (128 cases, geomean 1.80).

Ratio = fftchode speedup / best comparator speedup in that cell (>1: fftchode faster). 'ESTIMATE' is fftchode fast mode with ESTIMATE planning (MATLAB's mode); 'MEASURE' is fftchode fast mode with FFTW_MEASURE.

| precision | type | class | cells | ESTIMATE | MEASURE | cells won (MEASURE) | fastest comparator most often |
|---|---|---|---|---|---|---|---|
| d | c2c | 2-D | 5 | 0.79 | 0.81 | 2 | aocl-measure |
| d | c2c | 3-D | 3 | 0.82 | 1.10 | 1 | aocl-measure |
| d | c2c | batched | 4 | 1.29 | 1.34 | 4 | aocl-measure |
| d | c2c | large prime factor | 2 | 0.63 | 1.37 | 1 | aocl-measure |
| d | c2c | mixed radix | 6 | 0.90 | 1.07 | 3 | aocl-app-measure |
| d | c2c | pow2 large | 7 | 0.79 | 1.02 | 3 | aocl-measure |
| d | c2c | pow2 small | 7 | 0.81 | 0.93 | 2 | aocl-app-measure |
| d | c2c | prime | 5 | 0.61 | 0.78 | 0 | aocl-measure |
| d | c2c | strided | 3 | 0.56 | 0.87 | 2 | ducc0-native |
| d | c2r | 2-D | 1 | 1.12 | 0.93 | 0 | aocl-measure |
| d | c2r | mixed radix | 1 | 1.19 | 1.13 | 1 | aocl-app-measure |
| d | c2r | pow2 large | 2 | 0.57 | 0.55 | 0 | fftw-measure |
| d | c2r | pow2 small | 1 | 1.03 | 1.02 | 1 | aocl-app-measure |
| d | r2c | 2-D | 2 | 1.06 | 1.11 | 2 | fftw-measure |
| d | r2c | 3-D | 1 | 1.50 | 1.08 | 1 | aocl-app-measure |
| d | r2c | batched | 2 | 1.61 | 1.56 | 2 | aocl-measure |
| d | r2c | large prime factor | 1 | 0.34 | 1.24 | 1 | ducc0-native |
| d | r2c | mixed radix | 2 | 0.93 | 1.18 | 2 | fftw-measure |
| d | r2c | pow2 large | 2 | 0.96 | 1.19 | 2 | fftw-measure |
| d | r2c | pow2 small | 4 | 0.76 | 0.78 | 1 | pffft-native |
| d | r2c | prime | 2 | 1.07 | 1.44 | 1 | mkl |
| d | r2c | strided | 1 | 0.78 | 0.99 | 0 | ducc0-native |
| s | c2c | 2-D | 5 | 0.80 | 0.92 | 1 | aocl-measure |
| s | c2c | 3-D | 3 | 1.02 | 0.84 | 1 | aocl-measure |
| s | c2c | batched | 4 | 1.32 | 1.33 | 4 | mkl |
| s | c2c | large prime factor | 2 | 0.74 | 1.35 | 1 | mkl |
| s | c2c | mixed radix | 6 | 1.14 | 1.06 | 2 | aocl-app-measure |
| s | c2c | pow2 large | 7 | 0.71 | 1.08 | 3 | aocl-measure |
| s | c2c | pow2 small | 7 | 0.74 | 0.91 | 2 | aocl-app-measure |
| s | c2c | prime | 5 | 0.53 | 0.71 | 2 | aocl-measure |
| s | c2c | strided | 3 | 0.54 | 1.12 | 2 | ducc0-native |
| s | c2r | 2-D | 1 | 1.11 | 1.38 | 1 | mkl |
| s | c2r | mixed radix | 1 | 0.99 | 0.87 | 0 | mkl |
| s | c2r | pow2 large | 2 | 0.60 | 0.60 | 0 | mkl |
| s | c2r | pow2 small | 1 | 0.91 | 0.94 | 0 | aocl-app-measure |
| s | r2c | 2-D | 2 | 0.97 | 0.90 | 0 | fftw-measure |
| s | r2c | 3-D | 1 | 1.26 | 1.21 | 1 | aocl-measure |
| s | r2c | batched | 2 | 1.13 | 1.17 | 1 | mkl |
| s | r2c | large prime factor | 1 | 0.29 | 0.36 | 0 | ducc0-native |
| s | r2c | mixed radix | 2 | 0.84 | 0.98 | 1 | mkl |
| s | r2c | pow2 large | 2 | 1.06 | 1.20 | 1 | fftw-measure |
| s | r2c | pow2 small | 4 | 0.85 | 0.84 | 1 | aocl-app-measure |
| s | r2c | prime | 2 | 0.59 | 1.12 | 1 | mkl |
| s | r2c | strided | 1 | 0.66 | 0.89 | 0 | ducc0-matched |
| all | | | 128 | 0.82 | 0.98 | 57 | |

Cells where fftchode (MEASURE) is at least as fast as every comparator: 57 of 128; with ESTIMATE: 41.

## Peak heap (frozen suite, bytes above the caller's buffers, plan + one execution)

| library | threads | geomean peak (KiB) | suite total peak (MiB) |
|---|---|---|---|
| baseline-v0 | 1 | 236.8 | 287.7 |
| baseline-v0 | 8 | 236.8 | 287.7 |
| ducc0-native | 1 | 140.4 | 658.7 |
| fftchode-baseline | 1 | 229.7 | 287.5 |
| fftchode-baseline | 8 | 229.7 | 287.5 |
| fftchode-fast | 1 | 728.0 | 388.4 |
| fftchode-fast | 8 | 728.0 | 388.4 |
| fftw-simd-estimate | 1 | 682.8 | 342.5 |
| mkl | 1 | 412.0 | 631.7 |
| pocketfft-matched | 1 | 98.8 | 1054.5 |

## Accuracy (relative rms error vs long-double FFTW, frozen suite)

| library | double geomean | single geomean | worst double | worst single |
|---|---|---|---|---|
| baseline-v0 | 2.53e-16 | 1.39e-07 | 7.19e-16 | 3.5e-07 |
| fftchode-fast | 2.43e-16 | 1.34e-07 | 6.54e-16 | 3.23e-07 |
| mkl | 2.83e-16 | 1.36e-07 | 7.57e-16 | 3.15e-07 |
| fftw-simd-estimate | 2.47e-16 | 1.33e-07 | 6.95e-16 | 3.5e-07 |
| aocl-estimate | 2.41e-16 | 1.31e-07 | 6.52e-16 | 3.22e-07 |
| ducc0-native | 2.48e-16 | 1.29e-07 | 8.86e-16 | 3.45e-07 |
| ducc0-matched | 2.64e-16 | 1.33e-07 | 8.99e-16 | 3.64e-07 |
| pocketfft-matched | 2.64e-16 | 1.32e-07 | 9.78e-16 | 3.6e-07 |
| pffft-native | 3.12e-16 | 1.57e-07 | 6.65e-16 | 2.9e-07 |
| kissfft | 3.89e-16 | 1.29e-07 | 1.1e-15 | 5.78e-07 |
| ffte | 3.26e-16 | nan | 6.87e-16 | nan |

## Compiled size

| object | text bytes | data bytes |
|---|---|---|
| baseline-v0 | 2192063 | 44432 |
| fftchode | 10194887 | 893320 |
| fftchode-g++ | 8753940 | 930468 |
| fftw-scalar-double | 988802 | 22728 |
| fftw-scalar-single | 932437 | 22728 |
| fftw-simd-avx512-double | 3297762 | 119848 |
| fftw-simd-avx512-single | 3561790 | 119848 |
