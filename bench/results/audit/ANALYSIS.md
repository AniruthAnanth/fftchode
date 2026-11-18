## Suite geomean speedup over baseline-v0, by thread count

Same T: t(base at T)/t(lib at T), the FFTE-6 convention. vs base T=1: t(base at 1 thread)/t(lib at T). best T: per case each library at its own fastest thread count, over baseline-v0 at its own fastest thread count (and over baseline-v0 at 1 thread). CPU: suite geomean of process CPU time per transform (all threads, spinning included) relative to baseline-v0 at 1 thread and to the same library at 1 thread (>1 = more CPU).

| library | cases | same T=2 | same T=4 | same T=8 | vs base T=1, T=1 | vs base T=1, T=2 | vs base T=1, T=4 | vs base T=1, T=8 | best T | best T vs base T=1 | best-T mode | CPU at T=8 vs base T=1 | CPU at T=8 vs own T=1 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| aocl-est | 128 | 1.46 | 1.46 | 1.53 | 2.09 | 1.66 | 1.91 | 1.39 | 1.93 | 4.96 | 8 | 1.97 | 4.12 |
| aocl-measure | 128 | 2.80 | 2.74 | 2.88 | 3.44 | 3.19 | 3.59 | 2.61 | 2.80 | 7.20 | 1 | 0.94 | 3.29 |
| aocl-pool-measure | 128 | 5.05 | 4.63 | 6.05 | 3.44 | 5.76 | 6.06 | 5.49 | 3.41 | 8.78 | 8 | 0.82 | 2.86 |
| base | 128 | 1.00 | 1.00 | 1.00 | 1.00 | 1.14 | 1.31 | 0.91 | 1.00 | 2.57 | 8 | 3.09 | 3.09 |
| base-aa | 128 | 1.00 | 0.99 | 1.00 | 1.00 | 1.14 | 1.30 | 0.91 | 1.00 | 2.58 | 8 | 3.06 | 3.07 |
| ducc0-native | 128 | 2.56 | 2.46 | 3.34 | 1.52 | 2.91 | 3.23 | 3.03 | 1.40 | 3.60 | 2 | 0.62 | 0.95 |
| ducc0-nofm | 128 | 2.53 | 2.42 | 3.29 | 1.51 | 2.88 | 3.17 | 2.99 | 1.38 | 3.55 | 2 | 0.63 | 0.96 |
| fc-bit | 128 | 2.14 | 2.26 | 3.06 | 1.14 | 2.43 | 2.96 | 2.77 | 1.40 | 3.59 | 8 | 1.26 | 1.44 |
| fc-fast | 128 | 4.83 | 5.02 | 7.45 | 2.77 | 5.50 | 6.57 | 6.76 | 3.09 | 7.94 | 8 | 0.49 | 1.38 |
| fc-fast-measure | 128 | 6.02 | 6.09 | 8.45 | 3.46 | 6.85 | 7.98 | 7.66 | 3.69 | 9.50 | 8 | 0.42 | 1.47 |
| ffte | 21 | 1.46 | 1.31 | 3.19 | 1.16 | 1.16 | 1.16 | 1.16 | 0.51 | 1.16 | 1 | 0.86 | 1.00 |
| fftw-est | 128 | 1.34 | 1.34 | 1.42 | 1.93 | 1.53 | 1.75 | 1.29 | 1.79 | 4.62 | 8 | 2.17 | 4.21 |
| fftw-measure | 128 | 2.46 | 2.31 | 2.41 | 3.31 | 2.80 | 3.02 | 2.19 | 2.65 | 6.83 | 8 | 1.14 | 3.81 |
| fftw-pool-est | 128 | 2.82 | 2.69 | 3.55 | 1.93 | 3.22 | 3.52 | 3.22 | 2.10 | 5.39 | 8 | 1.62 | 3.16 |
| fftw-pool-measure | 128 | 4.72 | 4.18 | 5.40 | 3.31 | 5.37 | 5.48 | 4.90 | 3.18 | 8.18 | 8 | 0.97 | 3.21 |
| kissfft | 98 | 0.58 | 0.53 | 0.90 | 0.53 | 0.53 | 0.53 | 0.53 | 0.24 | 0.53 | 1 | 1.87 | 1.00 |
| mkl | 128 | 4.07 | 4.16 | 5.21 | 2.40 | 4.63 | 5.45 | 4.72 | 2.28 | 5.88 | 4 | 0.45 | 1.09 |
| pffft-native | 50 | 3.37 | 3.14 | 6.39 | 3.09 | 3.09 | 3.09 | 3.09 | 1.41 | 3.09 | 1 | 0.32 | 1.00 |
| pocketfft-native | 128 | 1.64 | 1.47 | 2.13 | 1.21 | 1.87 | 1.92 | 1.93 | 0.95 | 2.44 | 2 | 0.86 | 1.04 |

## fftchode fast mode over each FFTW/AOCL configuration (suite geomean of time ratios, common cases)

| pair | T=1 | T=2 | T=4 | T=8 | best T each |
|---|---|---|---|---|---|
| fc-fast / fftw-est | 1.44 | 3.60 | 3.75 | 5.25 | 1.72 |
| fc-fast / fftw-pool-est | 1.44 | 1.71 | 1.87 | 2.10 | 1.47 |
| fc-fast / fftw-measure | 0.84 | 1.97 | 2.18 | 3.09 | 1.16 |
| fc-fast / fftw-pool-measure | 0.84 | 1.02 | 1.20 | 1.38 | 0.97 |
| fc-fast-measure / fftw-measure | 1.05 | 2.45 | 2.64 | 3.50 | 1.39 |
| fc-fast-measure / fftw-pool-measure | 1.05 | 1.28 | 1.46 | 1.56 | 1.16 |
| fc-fast / aocl-est | 1.32 | 3.31 | 3.44 | 4.87 | 1.60 |
| fc-fast-measure / aocl-measure | 1.00 | 2.15 | 2.23 | 2.94 | 1.32 |
| fc-fast-measure / aocl-pool-measure | 1.00 | 1.19 | 1.32 | 1.40 | 1.08 |
| fc-bit / fftw-est | 0.59 | 1.59 | 1.68 | 2.15 | 0.78 |
| fc-bit / base | 1.14 | 2.14 | 2.26 | 3.06 | 1.40 |

## Ratio to the fastest comparator per cell (fftchode speed / fastest comparator's speed; >1 = fftchode faster)

Comparators that cannot do a cell are absent from that cell. oneMKL is excluded (generic SSE2 kernels on this AMD CPU) and shown separately. ducc0 is the IEEE (no -ffast-math) build.

| fftchode variant | comparators | condition | geomean ratio | cells won | cells |
|---|---|---|---|---|---|
| fc-fast | ESTIMATE level | T=1 | 1.00 | 73 | 128 |
| fc-fast | ESTIMATE level | T=2 | 1.19 | 86 | 128 |
| fc-fast | ESTIMATE level | T=4 | 1.20 | 86 | 128 |
| fc-fast | ESTIMATE level | T=8 | 1.23 | 91 | 128 |
| fc-fast | ESTIMATE level | best T each | 1.20 | 93 | 128 |
| fc-fast-measure | MEASURE level | T=1 | 0.91 | 27 | 128 |
| fc-fast-measure | MEASURE level | T=2 | 1.05 | 52 | 128 |
| fc-fast-measure | MEASURE level | T=4 | 1.08 | 58 | 128 |
| fc-fast-measure | MEASURE level | T=8 | 1.05 | 68 | 128 |
| fc-fast-measure | MEASURE level | best T each | 0.99 | 59 | 128 |
| fc-fast | every planning level (FFTE-6 style, minus MKL) | T=1 | 0.72 | 14 | 128 |
| fc-fast | every planning level, MKL included | T=1 | 0.69 | 11 | 128 |
| fc-fast | every planning level (FFTE-6 style, minus MKL) | T=2 | 0.84 | 34 | 128 |
| fc-fast | every planning level, MKL included | T=2 | 0.77 | 31 | 128 |
| fc-fast | every planning level (FFTE-6 style, minus MKL) | T=4 | 0.88 | 41 | 128 |
| fc-fast | every planning level, MKL included | T=4 | 0.79 | 37 | 128 |
| fc-fast | every planning level (FFTE-6 style, minus MKL) | T=8 | 0.92 | 48 | 128 |
| fc-fast | every planning level, MKL included | T=8 | 0.83 | 43 | 128 |
| fc-fast | every planning level (FFTE-6 style, minus MKL) | best T each | 0.83 | 36 | 128 |
| fc-fast | every planning level, MKL included | best T each | 0.79 | 34 | 128 |
| fc-fast-measure | every planning level (FFTE-6 style, minus MKL) | T=1 | 0.90 | 25 | 128 |
| fc-fast-measure | every planning level, MKL included | T=1 | 0.87 | 22 | 128 |
| fc-fast-measure | every planning level (FFTE-6 style, minus MKL) | T=2 | 1.05 | 52 | 128 |
| fc-fast-measure | every planning level, MKL included | T=2 | 0.96 | 45 | 128 |
| fc-fast-measure | every planning level (FFTE-6 style, minus MKL) | T=4 | 1.07 | 58 | 128 |
| fc-fast-measure | every planning level, MKL included | T=4 | 0.96 | 53 | 128 |
| fc-fast-measure | every planning level (FFTE-6 style, minus MKL) | T=8 | 1.04 | 67 | 128 |
| fc-fast-measure | every planning level, MKL included | T=8 | 0.94 | 62 | 128 |
| fc-fast-measure | every planning level (FFTE-6 style, minus MKL) | best T each | 0.99 | 59 | 128 |
| fc-fast-measure | every planning level, MKL included | best T each | 0.94 | 53 | 128 |

## Planning time (seconds, per case: median over rounds; suite median / 90th percentile / max), 1 thread(s) and 8 threads

| library | T | median | p90 | max | suite total |
|---|---|---|---|---|---|
| aocl-est | 1 | 0.000919 | 0.00508 | 0.194 | 0.8 |
| aocl-est | 8 | 0.00125 | 0.00801 | 0.165 | 0.9 |
| aocl-measure | 1 | 0.179 | 7.83 | 85.2 | 548.8 |
| aocl-measure | 8 | 0.59 | 9.27 | 144 | 581.9 |
| aocl-pool-measure | 1 | 0.179 | 7.83 | 85.2 | 548.8 |
| aocl-pool-measure | 8 | 0.408 | 10.3 | 167 | 589.3 |
| base | 1 | 0.000361 | 0.00275 | 0.224 | 0.8 |
| base | 8 | 0.000631 | 0.00419 | 0.17 | 0.6 |
| base-aa | 1 | 0.000348 | 0.00254 | 0.235 | 0.8 |
| base-aa | 8 | 0.000601 | 0.00563 | 0.172 | 0.6 |
| ducc0-native | 1 | 7.15e-05 | 0.000196 | 0.115 | 0.4 |
| ducc0-native | 8 | 6.28e-05 | 0.000199 | 0.0863 | 0.3 |
| ducc0-nofm | 1 | 7.31e-05 | 0.000201 | 0.115 | 0.4 |
| ducc0-nofm | 8 | 6.67e-05 | 0.000191 | 0.101 | 0.4 |
| fc-bit | 1 | 0.000522 | 0.00306 | 0.199 | 0.8 |
| fc-bit | 8 | 0.000872 | 0.00474 | 0.202 | 0.8 |
| fc-fast | 1 | 0.00836 | 0.0637 | 0.178 | 3.0 |
| fc-fast | 8 | 0.00852 | 0.0699 | 0.191 | 3.4 |
| fc-fast-measure | 1 | 0.258 | 8.11 | 131 | 712.6 |
| fc-fast-measure | 8 | 0.258 | 8.11 | 75 | 508.7 |
| ffte | 1 | 6.41e-05 | 0.037 | 0.144 | 0.3 |
| ffte | 8 | 6.41e-05 | 0.037 | 0.144 | 0.3 |
| fftw-est | 1 | 0.000869 | 0.00527 | 0.207 | 0.8 |
| fftw-est | 8 | 0.00134 | 0.00797 | 0.169 | 0.9 |
| fftw-measure | 1 | 0.17 | 7.24 | 108 | 566.4 |
| fftw-measure | 8 | 0.588 | 7.77 | 211 | 639.7 |
| fftw-pool-est | 1 | 0.000869 | 0.00527 | 0.207 | 0.8 |
| fftw-pool-est | 8 | 0.0014 | 0.0102 | 0.177 | 1.0 |
| fftw-pool-measure | 1 | 0.17 | 7.24 | 108 | 566.4 |
| fftw-pool-measure | 8 | 0.392 | 8.73 | 187 | 596.7 |
| kissfft | 1 | 5.67e-05 | 0.0242 | 0.141 | 0.6 |
| kissfft | 8 | 5.67e-05 | 0.0242 | 0.141 | 0.6 |
| mkl | 1 | 0.00184 | 0.014 | 0.179 | 1.2 |
| mkl | 8 | 0.00304 | 0.0116 | 0.177 | 1.2 |
| pffft-native | 1 | 0.00017 | 0.0159 | 0.112 | 0.4 |
| pffft-native | 8 | 0.00017 | 0.0159 | 0.112 | 0.4 |
| pocketfft-native | 1 | 4.29e-05 | 0.00752 | 0.141 | 0.7 |
| pocketfft-native | 8 | 3.98e-05 | 0.00649 | 0.134 | 0.6 |

## Planning budget: break-even execution counts

Per case, A = the fftchode variant, B = the comparator. When one side plans longer but runs faster, it pays off after n* = (extra plan time) / (time saved per execution) executions. Columns: cases where A is faster to plan and to run; cases where B is; cases where A plans longer and runs faster (median and 90th-percentile n*); cases where B plans longer and runs faster (same).

| A vs B | T | A dominates | B dominates | A plans longer, runs faster: cases / median n* / p90 n* | B plans longer, runs faster: cases / median n* / p90 n* | cases |
|---|---|---|---|---|---|---|
| fc-fast vs fftw-est | 1 | 2 | 18 | 108 / 199 / 1.44e+04 | 0 / - / - | 128 |
| fc-fast vs fftw-est | 8 | 2 | 12 | 114 / 77.2 / 514 | 0 / - / - | 128 |
| fc-fast vs aocl-est | 1 | 2 | 27 | 98 / 506 / 2.13e+04 | 1 / 0.171 / 0.171 | 128 |
| fc-fast vs aocl-est | 8 | 1 | 18 | 109 / 82.5 / 647 | 0 / - / - | 128 |
| fc-fast vs fftw-measure | 1 | 21 | 3 | 5 / 70.3 / 286 | 99 / 1.01e+04 / 1e+06 | 128 |
| fc-fast vs fftw-measure | 8 | 93 | 4 | 4 / 2.14e+03 / 4.27e+03 | 27 / 4.18e+03 / 7.01e+04 | 128 |
| fc-fast vs aocl-measure | 1 | 23 | 3 | 4 / 134 / 265 | 98 / 9.39e+03 / 6.86e+05 | 128 |
| fc-fast vs aocl-measure | 8 | 93 | 2 | 5 / 521 / 5.19e+03 | 28 / 3.95e+03 / 4.87e+04 | 128 |
| fc-fast-measure vs fftw-measure | 1 | 11 | 60 | 47 / 4.36e+03 / 1.99e+05 | 10 / 7.28e+03 / 9.75e+04 | 128 |
| fc-fast-measure vs fftw-measure | 8 | 57 | 14 | 47 / 831 / 5.27e+04 | 10 / 1.15e+03 / 1.09e+04 | 128 |
| fc-fast-measure vs aocl-measure | 1 | 12 | 55 | 36 / 1.64e+03 / 5.48e+04 | 25 / 3.63e+04 / 7.6e+04 | 128 |
| fc-fast-measure vs aocl-measure | 8 | 70 | 16 | 30 / 568 / 6.97e+03 | 12 / 2.62e+03 / 4.35e+03 | 128 |
