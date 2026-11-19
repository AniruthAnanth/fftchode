# Comparator adapters (FFTE-6)

Every comparator library is driven through the same case-file format and prints the same lines as `bench/suite/fftsuite.cpp`,
so `bench/suite/compare.py` interleaves them with fftchode (`compare.py --suite bench/suite/frozen.txt label=bin ...`).

    bench/suite/comparators.sh <outdir>             # fetch pinned sources, build libraries, build every harness binary (idempotent)
    bench/suite/verify-comparators.sh <outdir>      # harness equivalence + accuracy of every binary vs long-double FFTW
    bench/suite/adapters/mkl_probe.sh <outdir>      # which MKL kernel runs on this CPU and what the MKL_* knobs change

Binaries land in `<outdir>/bin`: `<id>` (time/hash), `<id>-err` (adds `--mode err`), `<id>-mem` (heap accounting through the ld --wrap
memtrack of bench/src). All take `--suite FILE --threads N [--mode time|err|mem|hash] [--batches K] [--tmin S] [--only NAME]`.
Units: seconds per transform, both directions unnormalized. Unsupported case: `<name> <threads> FAIL <reason>` (compare.py skips it).

| id | library | how |
|---|---|---|
| `fftw-all`, `fftw-avx2` | upstream FFTW 3.3.10 (sha256 in bench/versions.sh), gcc 13 stock flags | `--enable-sse2 --enable-avx --enable-avx2 --enable-avx512` / `--enable-avx2`; threads; fftsuite.cpp linked to libfftw3.a + libfftw3f.a |
| `aocl-app`, `aocl-noapp` | AMD AOCL-FFTW 6.0.0 (amd/amd-fftw 08c75e2) | `--enable-amd-opt AMD_ARCH=znver4` + sse2/avx/avx2/avx512, threads, with / without `--enable-amd-app-opt`; fftsuite.cpp |
| `mkl` | oneMKL 2026.2.1 (pip wheels, sha256-pinned in mkl-requirements.txt), static DFTI, intel_thread (libiomp5) | adapters/mkl.cpp |
| `ducc0-matched`, `ducc0-native` | ducc0 0.42.0 (42ce8ab) | adapters/ducc0.cpp |
| `pocketfft-matched`, `pocketfft-native` | pocketfft C++ (bench/versions.sh pin), plan cache 16 | adapters/pocketfft.cpp |
| `pffft-matched`, `pffft-native` | marton78/pffft efc1dd2 | adapters/pffft.cpp |
| `kissfft`, `ffte` | pinned builds of bench/ (matched flags only) | adapters/kissfft*.cpp, adapters/ffte.cpp |
| `fftchode` | this repo's build/fftchode.o in fftsuite.cpp (baseline for verification) | |

*matched* = `-O2 -ffp-contract=off`, no `-m` flags (clang 18, as bench/); *native* = the library's own recommended flags
(ducc0 `-O3 -ffast-math -march=native`, pocketfft and pffft `-O3 -march=native`). FFTW-API binaries have `<id>-estimate|-measure|-patient`
wrapper scripts that exec `<id> --plan X` (planning level is a run-time choice for them; the other libraries ignore `--plan`).

## Adapter contract (harness.h)

`harness.h` is a line-for-line port of fftsuite.cpp's case parser, geometry, Minstd inputs seeded from the case name, restore/iteration/timing loop
and output lines; an adapter implements `adapter_plan()` (double and float), `adapter_init(threads)`, `adapter_cleanup()` and returns a `Plan` with `exec()`.
`adapters/fftw.cpp` is the same on the FFTW API, used by verify-comparators.sh to prove that harness.h and fftsuite.cpp print identical `--mode hash` lines.
Plan time (plan_s): FFTW-API: plan creation; MKL: DftiCreateDescriptor + DftiSetValue + DftiCommitDescriptor; PFFFT: setup;
KissFFT: alloc; pocketfft and ducc0: construction of their 1-D plan objects (the stateless calls build these on a plan-cache miss; the first
execution is untimed so the cache is warm); FFTE: coefficient table.

## Per-library coverage and caveats

- MKL: rank, lengths, strides, NUMBER_OF_TRANSFORMS and distances (DFTI_FWD_/BWD_STRIDES and _DISTANCE), in/out-of-place, real transforms in CCE
  storage. Threads via `mkl_set_num_threads(N)`; MKL_DYNAMIC stays at its default (may use fewer threads on small problems).
- ducc0 / pocketfft: everything in the suite (batches become a leading axis excluded from `axes`; col layout is a stride).
- PFFFT: 1-D, unit stride, N = 2^a 3^b 5^c with N % 16 == 0 (complex) / % 32 == 0 (real); everything else FAIL. SIMD = SSE for float, SSE2 (AVX only with -mavx) for double; no AVX-512 backend.
  Real transforms convert pffft's packed Nyquist to CCE with O(1) element moves per transform (in the timing). c2r overwrites in[1] (Im X_0). Batched real output
  needs the batch stride aligned to the SIMD vector (32 B for double) so d_rf1024h256 / s_rf1024h256 are FAIL.
- KissFFT: contiguous only, even last dimension for real, any dimension with a prime factor > 1024 is not run (bench/README rule); 1-D uses kiss_fft/kiss_fftr, N-D kiss_fftnd/kiss_fftndr.
  The float library is built with symbol renames (kissf_rename.h) so both precisions live in one executable.
- FFTE 7.0: 1-D complex double only, N = 2^a 3^b 5^c, in place: out-of-place cases transform the output buffer in place (the input is copied in before each timed batch,
  not inside it; values grow within a batch, harmless on x86). FFTE's inverse divides by N, the adapter multiplies by N again (one extra O(N) pass in cb cases).
- AOCL-FFTW: both archives (libfftw3.a, libfftw3f.a) define the un-prefixed global `wisdom_write_set`; the harness links with `-Wl,--allow-multiple-definition` (a flag; harmless).
  `aocl-app` (`--enable-amd-app-opt`) plans with a search in every planner mode, including FFTW_ESTIMATE: seconds for the first plan of a problem class at 256-16384 points,
  minutes to hours from 64k points up (37 of 128 frozen cases did not finish in 20 s). Its `-estimate|-measure|-patient` wrappers therefore run each case in its own process through
  `bench/suite/case_timeout.py` (CASE_TIMEOUT seconds per case, default 120; a timeout prints `FAIL timeout>Ns`); plan_s is then the cold first-plan cost of each case. It also reads/writes FFTW wisdom in `wis.dat` in the current directory (so plan_s depends on a leftover file; the wrapper uses a fresh temporary directory per case, `CASE_CWD=DIR` keeps wisdom). Its execution is
  clearly faster than FFTW/aocl-noapp where it plans (d_cf1024 1.2 us vs 1.9 us, d_cf16384 35 us vs 65 us in a loaded smoke run), so decide the time limit knowingly.
- MKL on this CPU (AMD EPYC Zen 4, family 25): MKL 2026.2.1 (also 2024.2.2 and 2022.2.1) runs its generic SSE2 DFTI kernels (`mkl_dft_def_*`, xmm registers), MKL_VERBOSE prints "Intel(R) Architecture
  processors", and none of MKL_ENABLE_INSTRUCTIONS=AVX2|AVX512|SSE4_2, MKL_DEBUG_CPU_TYPE=5, MKL_CBWR=AVX2|AVX512|COMPATIBLE or mkl_enable_instructions() changes the kernel or the speed
  (reproduce with adapters/mkl_probe.sh). MKL numbers are therefore SSE2-class on the target machine.
- pocketfft / ducc0 `--mode mem`: plan caches are filled by the first execution, so "live after plan" excludes them; the peak column includes them.
