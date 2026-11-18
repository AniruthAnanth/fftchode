# Fairness audit of the benchmark comparisons (FFTE-8)

This audit re-measured every comparison behind the FFTE-6 claims on the same machine type: an AMD EPYC Zen 4 KVM guest (lscpu family 25 model 17) with 8 logical CPUs, which are 4 physical cores with 2 SMT threads each (siblings 0/1, 2/3, 4/5, 6/7), AVX-512, 32 GB, Ubuntu 24.04, clang 18.1.3 and gcc 13.3.0. No Intel machine was reachable: a fresh cloud machine also reported AMD EPYC. Per-case data and the scripts are in this directory and in `bench/suite/audit/`. [ANALYSIS.md](ANALYSIS.md) has the full thread-sweep tables and [ACCURACY.md](ACCURACY.md) the error table.

## What changed in the method

- **Thread counts and pinning.** Every library ran at 1, 2, 4 and 8 threads (3, 2, 2 and 3 rounds). The pin set takes one SMT sibling of each core before any second sibling, starting at cpu 2 (1 thread: cpu 2; 2: cpus 2,4; 4: cpus 0,2,4,6; 8: all), and is the same for every library. FFTE-6 pinned 1 thread to cpu 2 and 8 threads to all 8 logical CPUs, so its 8-thread runs always put two threads on each core.
- **Best over thread counts.** Each library is reported at its own fastest thread count per case, beside the fixed-thread figures.
- **FFTW with a persistent pool.** `--pool spin` installs `fftw_threads_set_callback` with a persistent pool that uses fftchode's wait policy: workers spin for 100 µs before sleeping, and the caller spins for 1 ms while joining. Stock FFTW 3.3.10 wakes its workers through semaphores on every parallel loop. The pool gives bit-identical output (128/128 hashes at 8 threads, FFTW and AOCL). It is used for FFTW and AOCL-FFTW.
- **CPU time.** Process CPU time over all threads, spinning included (CLOCK_PROCESS_CPUTIME_ID), is recorded beside wall time for every batch.
- **Planning level.** fftchode is compared with comparators at the same planner level (ESTIMATE with ESTIMATE, MEASURE with MEASURE, PATIENT with PATIENT). Libraries without a planner (ducc0, pocketfft, PFFFT, KissFFT, FFTE) count at every level. AOCL's `--enable-amd-app-opt` layer plans for seconds to hours in every mode, so it is reported as a separate budget class.
- **MKL.** oneMKL 2026.2.1 runs its generic SSE2 kernels on this AMD CPU, and no MKL_* setting changes that (`adapters/mkl_probe.sh`). It is reported but excluded from every per-cell-best ratio.
- **ducc0.** The primary build is the library's recommended release flags without `-ffast-math` (`-O3 -march=native`). The fast-math build is the same speed (1.52 vs 1.51 over baseline-v0 at 1 thread) and slightly more accurate (rms 0.95 vs 0.98 of baseline-v0's). Using the IEEE build costs ducc0 nothing and keeps every library under IEEE semantics.

## Verdict per comparison

| comparison | FFTE-6 | same method, this run | corrected | verdict |
|---|---|---|---|---|
| bit-identical mode, 1 thread, over baseline-v0 | 1.143 | 1.141 (1.129–1.141) | 1.14 with both at the release flags (-O2 -ffp-contract=off); **1.04 (1.02–1.04)** with both at -O3 -march=znver4 | fair at matched flags, but most of the gain is compiler flags: baseline-v0 at -O3 -march=znver4 is itself 1.13x (1.12–1.15) and still bit-identical. FFTE-6's "flags alone ~1.03x" is corrected to 1.13x |
| bit-identical mode, 8 threads | 2.88 | 3.06 (2.72–3.22) | **1.40** at each library's best thread count; 2.77 over single-threaded baseline-v0 | was unfair: the reference is baseline-v0 at 8 threads, which is 0.91x of its own 1-thread speed |
| fast mode, 1 thread | 2.67 | 2.77 (2.73–2.78) | 2.77 at release flags; **2.52 (2.45–2.54)** against baseline-v0 at -O3 -march=znver4 | fair; about 10% of it is flags |
| fast mode, 8 threads | 6.42 | 7.45 (6.00–7.56); a rerun gave 5.88 (5.88–6.00) | **3.09** at best thread count; 6.76 over single-threaded baseline-v0 | was unfair (same reference problem), and unstable between runs |
| ~3x over FFTW at 8 threads | 6.42/2.40 = 2.68 (ESTIMATE vs FFTW MEASURE) | 3.09 | **1.24–1.38** over FFTW MEASURE with a persistent pool (first run 1.38, CI 0.91–1.38; rerun 1.24, CI 1.24–1.28); 1.56 (rerun 1.50) MEASURE vs MEASURE; **0.97** (ESTIMATE vs pooled FFTW MEASURE) and **1.16** (MEASURE vs pooled FFTW MEASURE) at each library's best thread count | was unfair: FFTW's semaphore wake-ups, not its kernels, cost it at 8 threads. FFTW MEASURE goes from 2.41x to 5.40x over baseline-v0 with the pool. The claim does not survive |
| per-cell best, ESTIMATE | 0.62 / 0.82 | not rerun as such | **1.00 / 1.23** at 1 / 8 threads (73 / 91 of 128 cells won), **1.20** at best thread count (93 cells) against ESTIMATE-level comparators | was unfair to fftchode: it compared fftchode ESTIMATE with MEASURE, PATIENT and app-opt planners |
| per-cell best, MEASURE | 0.78 / 0.98 | 0.87 / 0.94 (every level and MKL, no app-opt or PATIENT) | **0.91 / 1.05** at 1 / 8 threads (27 / 68 cells), **0.99** at best thread count (59 cells), MKL excluded | was unfair in both directions: MKL (handicapped) and app-opt/PATIENT (larger planning budgets) were in the pool, and 8-thread comparators ran without a usable thread pool |
| FFTW AVX-512 at 8 threads (comparator row) | ESTIMATE 1.43, MEASURE 2.40 | 1.42 / 2.41 | 3.55 / 5.40 with the pool | was unfair (threading backend) |
| AOCL MEASURE at 8 threads | 2.87 | 2.88 | 6.05 with the pool | was unfair (threading backend) |
| MKL | 2.49 / 5.07 | 2.40 / 5.21 | unchanged numbers, labelled handicapped and excluded from per-cell best | cannot be made fair here: no Intel CPU reachable |
| PATIENT, 1 thread | 3.75 vs FFTW 3.66 (102-case subset) | fftchode PATIENT **1.06x** FFTW PATIENT, **0.99x** AOCL PATIENT (111 common cases, 1 round, 180 s per case) | same | fair; 12–16 cases at 2^20 points or prime 10^6 exceed the budget for each library |
| PATIENT, 8 threads | absent | 1 round with a 120 s per-case budget reached 86 of 128 cases in 6 h; 45–49 cases completed per library: fftchode PATIENT **1.05x** FFTW, **1.00x** FFTW+pool, **1.01x** AOCL | partial | still incomplete: PATIENT planning at 8 threads exceeds 120 s for about 40% of the cases it reached, for every FFTW-API library. The other 42 cases are absent |
| AOCL app-opt (`--enable-amd-app-opt`) | in the MEASURE pool | cold plan: median 1.7 s, p90 84 s, max 436 s, 7 cases > 600 s at 1 thread; 27 of 83 > 600 s at 8 threads | fftchode MEASURE is **0.87x** of it at 1 thread (112 cases), **1.02x** at 8 threads (44 cases it could plan) | reported as its own planning-budget class |

The 95% confidence intervals come from a paired bootstrap over only 2–3 rounds, so they are coarse. The A/A control (baseline-v0 against a copy of itself) gives 0.997 (0.983–1.010), 1.002, 0.993 (0.986–0.996) and 1.004 at 1, 2, 4 and 8 threads, so differences under about 2% are not resolved. At 8 threads the same comparison differs by up to 25% between runs on this VM: hypervisor steal reached 703k ticks in one round, and SMT siblings share cores. Ratios between libraries in the same run are steadier than ratios against baseline-v0 at 8 threads.

## Threading

Each library's threading is set up correctly. FFTW, AOCL-FFTW, fftchode and baseline-v0 call `fftw_init_threads` and `fftw_plan_with_nthreads(N)` for both precisions. MKL links `mkl_intel_thread` with static libiomp5 and calls `mkl_set_num_threads(N)` (MKL_DYNAMIC at its default). ducc0 and pocketfft get the `nthreads` argument on every call. pocketfft and ducc0 do not split a single 1-D transform across threads, and PFFFT, KissFFT and FFTE are single-threaded, so their times at any thread count are their 1-thread times.

FFTW and AOCL are slow at 8 threads because of their threading backend, not their kernels and not their initialisation. In absolute time, FFTW MEASURE at 8 threads runs at 0.66x of its own 1-thread speed (suite geomean), and its best thread count is often 1. With the persistent pool, the same plans run at 1.48x of 1-thread speed. FFTW, AOCL and baseline-v0 all run slower at 8 threads than at 4 (two threads per core), with or without the pool.

CPU time at 8 threads, relative to the same library at 1 thread (suite geomean, spinning included):

| library | CPU at 8 threads / own CPU at 1 thread |
|---|---|
| fftchode fast (ESTIMATE / MEASURE) | 1.38 / 1.47 |
| fftchode bit-identical | 1.44 |
| baseline-v0 | 3.09 |
| FFTW MEASURE, stock / pool | 3.81 / 3.21 |
| AOCL MEASURE, stock / pool | 3.29 / 2.86 |
| oneMKL | 1.09 |
| ducc0 | 0.96 |

fftchode's spin pool does not buy its speed with extra CPU: it uses less CPU per transform at 8 threads than any FFTW-based comparator.

## Compiler flags

Recorded builds:

| library | release build used in the sweep | matched build |
|---|---|---|
| baseline-v0, fftchode | clang 18 `-O2 -ffp-contract=off`, no -march (the repository Makefile). fftchode compiles its SIMD and per-ISA scalar codelets per function with `target` attributes | `-O3 -march=znver4 -ffp-contract=off` (`base-native`, `fc-native`) |
| FFTW 3.3.10 | gcc 13, FFTW's own CFLAGS `-O3 -fomit-frame-pointer -mtune=native -malign-double -fstrict-aliasing -fno-schedule-insns`, `--enable-sse2 --enable-avx --enable-avx2 --enable-avx512`; SIMD codelets get `-mavx2 -mfma` etc. from configure; gcc's default `-ffp-contract=fast` | clang `-O2 -ffp-contract=off` |
| AOCL-FFTW 6.0.0 | gcc 13, `--enable-amd-opt AMD_ARCH=znver4` (adds `-march=znver4 -mavx2 -mfma`) | clang `-O2 -ffp-contract=off` (+ the AMD_ARCH flags) |
| ducc0 0.42.0 | clang `-O3 -march=native` (recommended minus `-ffast-math`); `-O3 -ffast-math -march=native` as a variant | clang `-O2 -ffp-contract=off` |
| pocketfft C++, PFFFT | clang `-O3 -march=native` | clang `-O2 -ffp-contract=off` |
| KissFFT, FFTE 7.0 | clang / gfortran 13 `-O2 -ffp-contract=off` | same |
| oneMKL 2026.2.1 | Intel's prebuilt static libraries | n/a |

No build uses LTO. Only the ducc0 variant uses `-ffast-math`.

Speedup over baseline-v0 (-O2) at 1 thread, measured in one interleaved run on a separate machine of the same type:

| library | matched | release | release / matched |
|---|---|---|---|
| baseline-v0 | 1.00 | 1.13 (-O3 -march=znver4) | 1.13 |
| fftchode bit-identical | 1.15 | 1.18 | 1.02 |
| fftchode fast, ESTIMATE / MEASURE | 2.84 / 3.57 | 2.85 / 3.66 | 1.00 / 1.03 |
| FFTW ESTIMATE / MEASURE | 1.98 / 3.22 | 1.96 / 3.30 | 0.99 / 1.02 |
| AOCL MEASURE | 3.58 | 3.55 | 0.99 |
| ducc0 (release = IEEE; fast-math 1.57) | 1.28 | 1.58 | 1.23 |
| pocketfft | 1.07 | 1.22 | 1.14 |
| PFFFT (50 cases) | 2.30 | 3.05 | 1.33 |

So the sweep gives every comparator its own release build, which helps pocketfft, ducc0 and PFFFT. FFTW and AOCL run the same speed under either flag set. fftchode gains 0–3% from -march, while baseline-v0 gains 13%: the per-ISA scalar codelets in fftchode's bit-identical mode do roughly what `-march=znver4` does for baseline-v0, without giving up a portable binary.

## Planning budget

Planning time per case (seconds, median over rounds; suite median / 90th percentile / max / suite total), 1 thread:

| library | median | p90 | max | total |
|---|---|---|---|---|
| fftchode fast, ESTIMATE (cost-model search) | 0.0084 | 0.064 | 0.18 | 3.0 |
| FFTW ESTIMATE | 0.00087 | 0.0053 | 0.21 | 0.8 |
| AOCL ESTIMATE | 0.00092 | 0.0051 | 0.19 | 0.8 |
| fftchode fast, MEASURE | 0.26 | 8.1 | 131 | 713 |
| FFTW MEASURE | 0.17 | 7.2 | 108 | 566 |
| AOCL MEASURE | 0.18 | 7.8 | 85 | 549 |
| fftchode / FFTW / AOCL PATIENT | 1.5 / 1.5 / 1.5 | 53 / 61 / 71 | 173 / 175 / 171 (timeouts excluded) | 1499 / 1810 / 1857 |
| AOCL app-opt (cold, any mode) | 1.7 | 84 | 436 (7 cases > 600) | 3439 + |

fftchode's ESTIMATE is not ESTIMATE-equivalent in cost: its median plan takes about 10x as long as FFTW ESTIMATE's (4x the suite total). It is still 20x below MEASURE at the median. The largest ESTIMATE plan in the frozen suite was 0.18 s at 1 thread and 0.19 s at 8 threads; the 1.8 s worst case cited in FFTE-6 did not occur here. Against FFTW ESTIMATE, fftchode's extra planning pays for itself after a median of 199 executions at 1 thread (90th percentile 14,400) and 77 at 8 threads (p90 514). FFTW ESTIMATE is faster to plan and to run in 18 of 128 cells at 1 thread. Against FFTW MEASURE at 1 thread, FFTW runs faster in 99 cells but needs a median of 10,100 executions (p90 10^6) to recover its longer plan; fftchode ESTIMATE is faster to plan and to run in 21 cells (93 at 8 threads). fftchode MEASURE plans 26% longer in total than FFTW MEASURE. [ANALYSIS.md](ANALYSIS.md) has the full break-even table.

## Coverage, transform semantics and measurement

- **Coverage.** A library that cannot do a cell is absent from that cell's best. FFTW, AOCL, ducc0 and pocketfft cover all 128 cells, so every cell has at least four comparators at every planning level. PFFFT covers 50 cells (1-D, N = 2^a 3^b 5^c, N % 16 or 32), KissFFT 98 and FFTE 21.
- **Transform semantics.** Every library gets the same case (precision, rank, lengths, strides, howmany, distances) through one parser and one geometry routine. `adapters/harness.h` is a line-for-line port of `fftsuite.cpp`, and the two print identical output hashes on the same library. All transforms are unnormalised in both directions, and out-of-place except FFTE, which is in-place only (the adapter copies input before each timed batch, outside the timing). One asymmetry remains: FFTW-API libraries (FFTW, AOCL, fftchode, baseline-v0) may destroy the input of a c2r transform (FFTW's default without FFTW_PRESERVE_INPUT), while ducc0 and pocketfft preserve it. That affects the 10 c2r cells. Buffers are 64-byte aligned for every SIMD library.
- **Measurement.** One timing loop serves every library: per case a fresh process plans, runs once untimed, doubles the iteration count until a batch takes 20 ms, and times 3 batches on warm caches. Cases are interleaved library by library with the order rotating per case and round. The statistic is the median over rounds of each round's fastest batch. DAZ/FTZ is set for every library.
- **Accuracy.** Every comparator was run against the same long-double FFTW 3.3.10 reference on identical inputs (1 thread). Relative rms error, geomean over the cases each library supports, against baseline-v0 on common cases: fftchode fast 0.96x (MEASURE 0.95x), FFTW 0.96x (MEASURE 0.93x), AOCL 0.95x (0.92x), MKL 1.05x, ducc0 0.98x (fast-math 0.95x), pocketfft 0.95x, PFFFT 1.30x, KissFFT 1.31x, FFTE 1.49x. Two fftchode MEASURE cases (d_rf1000003 and s_rf1000003) exceeded the 600 s limit of the accuracy run.

## Bugs found in the benchmark path

fftchode's own benchmark path had no bug that changed a number. The harness had three problems, all fixed here:

- The comparator harness (`bench/suite/adapters/`, `comparators.sh`, `verify-comparators.sh`) existed only on the `opt/comparators` branch. The published comparator CSVs could not be reproduced from main.
- `fftsuite.cpp` on main only built against `fftchode.h`, so it could not build against upstream FFTW or AOCL.
- `compare.py` wrote raw data only at the end of each round, so a run that hit a time limit lost the whole round (this is what happened to the FFTE-6 8-thread PATIENT run). It now writes every case as it finishes. In `summarize.py`, the "fastest comparator most often" column broke ties in hash order and changed between runs; the tie-break is now deterministic.

## What remains a limitation

- MKL could only be measured with its SSE2 fallback (no Intel machine).
- The 8-thread PATIENT comparison is partial (about 47 of 128 cases per library, 1 round, small and mid sizes only). AOCL app-opt at 8 threads was timed on 44 cases only.
- Only 2–3 rounds per condition; 8-thread results vary up to 25% between runs on this VM.
- The selection of the best thread count per case picks the minimum of four noisy estimates, which favours libraries with more usable thread counts slightly. Single-threaded libraries get no such pick.
- The c2r input-destruction asymmetry described above.
- The held-out suite was not rerun.
