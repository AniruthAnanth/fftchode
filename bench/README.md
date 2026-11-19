# bench: FFT library benchmark harness

There are two harnesses. The committed results in `bench/results/` come from the frozen-suite harness in `bench/suite/`; `bench/run.sh` (below) is the older benchFFT-style sweep.

## Frozen-suite harness (`bench/suite/`)

`frozen.txt` (128 cases, sha256 3f21c096…) and `heldout.txt` (78 cases, sha256 in `heldout.sha256`, never used for tuning) list the problems. `fftsuite.cpp` runs any FFTW 3-API library on them in `time`, `mem`, `hash` or `err` mode.

    bench/suite/build.sh /tmp/fc build/fftchode.o                    # /tmp/fc and /tmp/fc-mem (REF=libfftw3l.a adds /tmp/fc-err)
    bench/suite/compare.py --suite bench/suite/frozen.txt --threads 1 --rounds 7 --out /tmp/run base=/path/to/baseline fast=/tmp/fc
    bench/suite/summarize.py bench/results                            # regenerates bench/results/SUMMARY.md

`compare.py` interleaves the binaries case by case and round by round, and reports geometric-mean speedups with paired-bootstrap 95% confidence intervals. Set `FFTCHODE_NUMERICS=fast` in a wrapper script to time fast mode, and use `case_timeout.py` for libraries whose planning can hang. `bench/results/` holds the per-case CSVs (`frozen-`, `heldout-`, `ablation-`, `comparators-{estimate,measure,patient}-t{1,8}.csv`, `memory.csv`, `error.csv`, `size.csv`), `ENV.txt` and `SUMMARY.md`.

## Comparators and the fairness audit (`bench/suite/adapters/`, `bench/suite/audit/`)

`bench/suite/comparators.sh <outdir>` fetches and builds every comparator (FFTW 3.3.10 with its own flags and with clang `-O2 -ffp-contract=off`, AOCL-FFTW 6.0.0 with and without app-opt, oneMKL, ducc0, pocketfft, PFFFT, KissFFT, FFTE) and one harness binary per library; `adapters/README.md` describes each adapter. FFTW-API binaries take `--plan estimate|measure|patient` and `--pool spin`, which installs `fftw_threads_set_callback` with a persistent pool using fftchode's wait policy. Every timing line carries process CPU time after the `cpu` marker, and `compare.py` pins N threads to one SMT sibling per core before any second sibling. `--mode plan` prints each case's plan (`fftw_sprint_plan`).

The threading runs (FFTE-9, `bench/results/threads/`) use `bench/suite/audit/run_threads.sh <outdir> base=<baseline-v0 binary> fc-v1=<old fftchode> fc=<new fftchode>`. It runs both numerics modes at 1, 2, 4 and 8 threads on both suites, and then the frozen suite with a CPU hog (`audit/hog.c`) on cpus 4-7. `bench/suite/analyze_threads.py <outdir>/frozen --pairs fc-bit=fc-bit-v1` reports every build against baseline-v0 at 1 thread and at its best thread count, and counts the cells slower than the build's own 1-thread time.

The fairness audit (FFTE-8) is reproduced with:

    bench/suite/comparators.sh ~/.capy/work/audit/c
    bench/suite/audit/build_fftchode.sh                       # baseline-v0 and fftchode, release and -O3 -march=znver4 builds
    for t in 1 2 4 8; do bench/suite/audit/run_sweep.sh $t 3 ~/.capy/work/audit/runs/sweep; done
    bench/suite/audit/run_flags.sh; bench/suite/audit/run_err.sh   # compiler flags, accuracy
    bench/suite/audit/run_patient.sh; bench/suite/audit/run_patient8.sh; bench/suite/audit/run_app.sh   # PATIENT and AOCL app-opt (hours)
    bench/suite/analyze_audit.py ~/.capy/work/audit/runs/sweep --out bench/results/audit/ANALYSIS.md --csv bench/results/audit
    bench/suite/accuracy_table.py ~/.capy/work/audit/runs/err > bench/results/audit/ACCURACY.md

`bench/results/audit/AUDIT.md` has the verdicts, and `bench/suite/summarize.py` puts them at the top of `bench/results/SUMMARY.md`.

## benchFFT sweep (`bench/run.sh`)

Compares this repository's single-file FFTW (`src/fftchode.cpp`) with upstream FFTW, pocketfft, KissFFT and FFTE under
matched conditions: speed (benchFFT methodology), peak memory, and compiled size. One command builds
everything and writes CSVs plus a summary to `bench/results/`.

    bench/run.sh                                  # everything, defaults
    bench/run.sh --sizes "64 1024 1000003" --trials 3 --threads "1 4" --libs "fftchode fftw-simd" --prec d
    bench/run.sh --skip-build --skip-mem --skip-size --out /tmp/quick
    bench/run.sh --help

`run.sh` writes to `bench/results/` by default (use `--out` to keep the committed results); the run options and wall time are appended to `ENV.txt`. The defaults (5 trials, 0.05 s batches) take about 16 minutes on a 4-vCPU machine.

Options: `--sizes --trials --threads --libs --prec --kinds --time-min --mem-sizes --timeout --out --force --skip-build --skip-speed --skip-mem --skip-size`.
Library ids: `fftchode fftw-scalar fftw-simd pocketfft kissfft ffte`; precisions `d s`; kinds `c` (complex forward) `r` (real forward, r2c).
Requirements: clang/clang++, make, curl, git, python3, and gfortran (build.sh tries `sudo apt-get install gfortran` if it is missing;
without it FFTE is skipped). Network access is needed on the first run. Run on an otherwise idle machine; `run.sh` prints the
load average before timing.

The build is idempotent: sources are fetched at pinned commits into `bench/build/dl`, steps are guarded by stamps in `bench/build/stamps`, and
finished measurements are cached under `bench/build/parts` (keyed by parameters and binary hash; `--force` re-measures). `bench/build/` is git-ignored.

## What is compared

| id | library | how it is built |
|---|---|---|
| fftchode | this repo's `build/fftchode.o` (FFTW 3.3.10 amalgamation, scalar, d+s, pthreads) | repo Makefile: `clang++ -std=c++17 -O2 -ffp-contract=off -pthread -c src/fftchode.cpp`; FFTW API benchee, header `src/fftchode.h` |
| fftw-scalar | upstream FFTW 3.3.10 tarball (sha256 56c93254...467), no SIMD | `configure --enable-threads --with-combined-threads --disable-fortran --disable-shared --enable-static CC=clang CFLAGS="-O2 -ffp-contract=off"` (+ `--enable-single` for float) |
| fftw-simd | same tarball with SIMD | same plus `--enable-sse2 --enable-avx --enable-avx2` (CPU also has AVX-512; not enabled because the brief listed sse2/avx/avx2). configure adds the ISA flags (-mavx, -mavx2 -mfma) for the SIMD codelets only |
| pocketfft | mreineck/pocketfft branch `cpp`, commit 4d865cba24359b05b37da4e75aee24498c3b78e7, header-only | `clang++ -std=c++17 -O2 -ffp-contract=off -DPOCKETFFT_CACHE_SIZE=16 -pthread` |
| kissfft | mborgerding/kissfft commit 57a701785b0a4928081ac302c07be5792a0b3a74 (post-1.3.1 master) | `clang -O2 -ffp-contract=off -Dkiss_fft_scalar=double|float` (kiss_fft.c, kiss_fftr.c) |
| ffte | FFTE 7.0 from http://www.ffte.jp/ffte-7.0.tgz (sha256 078d5f84...3da0), Fortran 77 | `gfortran -O2 -ffp-contract=off -std=legacy -fallow-argument-mismatch` (factor, fft235, kernel, zfft1d) |

Compilers: Ubuntu clang 18.1.3 for all C/C++; GNU Fortran 13.3.0 for FFTE (clang has no Fortran front end;
the same -O2 / -ffp-contract=off flags are passed). `results/ENV.txt` records the exact versions, flags, configure lines and the machine.
Every binary is linked statically (`-static`). No `-march` flags are used anywhere, so only FFTW-SIMD (via its own runtime-dispatched codelets)
uses AVX/SSE2 beyond the x86-64 baseline.

Planning: `FFTW_ESTIMATE` for fftchode and both upstream FFTW builds (MATLAB's mode). Buffers are 128-byte aligned (libbench's `memalign`) for every library, so the
SIMD codelets are eligible. Transforms are out-of-place (input preserved) except FFTE, which only offers in-place
(the `placement` column says which). Forward transforms only. Real transforms are r2c with N/2+1 complex output
(pocketfft `r2c`, KissFFT `kiss_fftr`, FFTW `plan_dft_r2c`); KissFFT needs even N for r2c and FFTE 7.0 has no 1-D real routine.

## Pinned sources and how benchFFT is used

- benchFFT: https://github.com/FFTW/benchfft commit fb3a478a65d3280c1b255899179583684bfdd973 (2025-03-18), fetched by `build.sh`.
  That repository contains `libbench/` (the benchFFT core, the ancestor of FFTW's `libbench2`) and the `benchees/` tree; it has no `libbench2`
  directory (libbench2 exists only inside the FFTW tarball and has a different benchee API), so the benchees' own core is used.
  From it we use the problem parser/allocator, the benchee interface (`can_do/setup/doit/done/useropt`), the copy routines, `mflops()` and the
  `verify()` self-test. libbench's `bench-main/speed/timer/report` are replaced by `src/driver.c` (per-trial CSV output, separate plan timing, forked cells,
  memory mode) and `bench_malloc` is libbench's own.
- Benchees (`benchees/`): `fftw3.c` and `kissfft.c` are adapted from benchFFT's fftw3 and kissfft benchees (threads option, ESTIMATE, 1-D only, n/a reasons); `pocketfft.cc`
  is new (benchFFT's bundled pocketfft_cxx is an older copy that uses `r2r_fftpack`); `ffte.c` wraps benchFFT's unmodified-except-for-one-line `doit-ffte.c` (FFTE 7.0 needs a
  2N-complex work array, benchFFT's FFTE 6.0 glue allocates N, which corrupts the heap; `build_ffte_lib` patches that single expression and fails if the patch does not apply).
  benchFFT's `doit-ffte.c` also references FFTE's 2-D/3-D routines; `ffte_stubs.c` satisfies them so the binary and archive contain 1-D code only.
- Every cell (library, precision, kind, threads, size) runs `verify()` (benchFFT's linearity, impulse and shift tests, tolerance 1e-10 double / 1e-3 single, 3 rounds)
  in a separate process before it is timed; a failure is recorded as `verify-fail` and the cell is not timed. This catches glue bugs and library defects.

## Methodology

Speed (`--mode speed`): benchFFT's scheme. The iteration count is doubled until one batch takes at least `--time-min` (default 0.05 s), then `--trials`
(default 5) batches of that many executions are timed with `clock_gettime(CLOCK_MONOTONIC)`; per-transform time t = batch time / iterations;
mflops = 5 N log2(N) / t (complex) or 2.5 N log2(N) / t (real), t in microseconds, i.e. benchFFT's formula, not a real operation count. The CSV has
the median over trials (`mflops_median`, the headline), the best trial (`mflops_best` = benchFFT's min-time convention) and the worst (`mflops_worst`),
plus all trial times. Input is uniform random data in [-0.5, 0.5). Each cell runs in its own forked process, so a crash or a 600 s timeout only loses that cell.
Plan time: `plan_cold_s` is the first `setup()` in that fresh process; `plan_warm_s` is the median of three further plan/destroy cycles. Plan and execution are timed separately.
Threads: `threads=1` and `threads=nproc` (4 here) for FFTW (`fftw_init_threads` + `fftw_plan_with_nthreads`), fftchode (same API) and pocketfft (the `nthreads` argument).
KissFFT and FFTE are single-threaded.

Memory (`--mode mem`): one plan + one execution in a freshly exec'd process per cell (`memory.csv`).
- Peak heap: the "-mem" binaries are linked with `-Wl,--wrap` over malloc/calloc/realloc/free/posix_memalign/aligned_alloc/memalign (`src/memtrack.c`); live bytes
  are the sum of `malloc_usable_size` over live blocks, tracked with atomics. The counter is read after the input/output buffers are allocated
  (`heap_before_plan_bytes`); `heap_plan_live_bytes` is what remains allocated after planning, `heap_peak_plan_bytes` / `heap_peak_plan_exec_bytes` are the peaks
  above the baseline during planning and during planning+execution. Not counted: thread stacks, static/bss data (FFTE keeps its work tables in static arrays), direct mmap, and libc-internal allocations.
- Peak RSS: `ru_maxrss` (also `VmHWM`) of that process. ru_maxrss of a forked child starts at the parent driver's RSS (~0.5 MB), identical for all libraries; `SUMMARY.md` therefore subtracts the same
  measurement for a do-nothing benchee (`null`) at the same size. The kernel batches per-task RSS counters, so values below ~256 KiB are not resolved; use the large sizes for conclusions.

Compiled size (`size.csv`): `size -t` totals (text/data/bss) and file size of the library object or archive, and the size of the stripped static benchmark binary
(driver + libbench + libc + library) together with the same binary using a do-nothing benchee; the difference is the library's own contribution. fftchode.o holds both precisions and threads in
one object; the FFTW archives are per precision, so the summary also sums them. pocketfft is header-only: its "artifact" is the object compiled from the benchee that instantiates c2c/r2c.
Binaries are not linked with `--gc-sections`.

## Output files (`bench/results/`)

- `speed.csv`: one row per (lib, precision, kind, threads, size). `status` is `ok`, `n/a`, `verify-fail`, `timeout`, `crash`; `note` gives the reason for non-ok rows.
- `memory.csv`, `size.csv`: as described above. `SUMMARY.md` is generated by `scripts/summarize.py`. `ENV.txt`: CPU, cores, compilers, flags, pinned versions, run options, total wall time.

## Caveats

- Absolute mflops are not operation counts and only compare libraries under the same formula. Cloud VM (KVM guest, AMD EPYC, 4 vCPUs that are SMT siblings of 2 cores): timings are noisier than bare metal, and threads=4 uses hyperthreads. No CPU pinning or frequency control.
- FFTW_ESTIMATE does not account for threading overhead, so threaded FFTW plans for small N are often much slower than threads=1 (upstream FFTW shows the same; fftchode matches it).
- pocketfft parallelises across independent 1-D transforms; a single 1-D transform ignores `nthreads`, so its threads=4 column equals threads=1. Its stateless API reuses plans through the plan cache enabled here (16 entries); `setup()` runs one transform, so its plan time includes one execution and the warm plan time is not a plan time.
- In-place transforms (FFTE) are timed repeatedly on their own output, so values grow by about sqrt(N) per call and can overflow to inf/NaN; modern x86 does not slow down for that, but it is not identical data to the out-of-place runs.
- KissFFT uses an O(N p) generic butterfly for a prime factor p; N with a prime factor above 1024 (here 1000003) is reported n/a rather than run. KissFFT's real transform needs even N.
- FFTE 7.0 supports N = 2^a 3^b 5^c only, has no 1-D real routine and fails benchFFT verification at N=2 and N=18 (those cells are recorded as `verify-fail`); it is double precision only. FFTE's Fortran compile (gfortran) is a different compiler front end/back end than the C/C++ libraries (clang).
- fftchode is built by the repository's own Makefile target (`build/fftchode.o`), so `bench/run.sh` benchmarks whatever `src/fftchode.cpp` currently contains; `ENV.txt` records its sha256.
- Results are specific to this machine and toolchain; do not compare them with timings from another machine (e.g. the MATLAB timings recorded on a Mac).
