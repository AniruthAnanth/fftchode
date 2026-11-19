# Threading fixes (FFTE-9): block stealing and a work-based thread cap

Two changes to fftchode's thread pool and planner, measured with the fairness audit's protocol (`bench/suite/compare.py`: one process per case and binary, interleaved case by case with rotating order, pinned as in the audit, 3 rounds per thread count, the median over rounds of each round's best batch, paired-bootstrap 95% CIs over rounds, and an A/A copy of baseline-v0). The reference is always baseline-v0 at 1 thread. Scripts: `bench/suite/audit/run_threads.sh` and `bench/suite/analyze_threads.py`. Full tables: [frozen-analysis.md](frozen-analysis.md), [heldout-analysis.md](heldout-analysis.md), [frozen-hog-analysis.md](frozen-hog-analysis.md).

Machine: a fresh AMD EPYC Zen 4 KVM guest of the audit's type (8 logical CPUs = 4 cores x 2 SMT, AVX-512, 31 GiB, clang 18.1.3, THP madvise). It is not the audit's machine instance and runs these cases about 2.4x faster in absolute time, with almost no hypervisor steal (at most 265 ticks per run, against up to 703k in the audit), so compare ratios only within one run. The A/A control is 0.994-1.014 in every condition here.

Builds: `v1` = audit/fairness (src/fftchode.cpp sha256 14de8d82...), `steal` = v1 + block stealing only, `new` = this branch (sha256 2db47248...).

## What changed

- **Block stealing (both modes, bit-identical by construction).** A threaded loop still splits into FFTW's blocks, and the work-based lane count is unchanged. Each lane, the caller included, now claims the blocks of its own range one at a time and then any left in the other lanes' ranges. The caller does not wait for a worker that never took its lane: it revokes the hand-off. Before, a lane was a fixed block range, so one worker on a CPU that was busy elsewhere stalled the whole loop for a scheduler time slice. The blocks, the child plans and the order of operations inside each block are unchanged.
- **Work-based thread cap per plan (both modes).** A problem with rough work `W = c * n log2 n * howmany` flops (c = 5 complex, 2.5 real) is planned for at most `cap = 2^floor(log2(W / 32768))` threads (at least 1; `FFTCHODE_THREAD_WORK` sets the 32768, 0 turns it off).
  - Fast mode plans at `min(T, cap)` threads directly, besides the existing serial thresholds.
  - Baseline mode first plans at the requested T and blesses that plan, so the exported wisdom is FFTW's at T. Then it plans at the cap and uses that plan only if it has the same canonical form as the T plan. The canonical form drops vector lengths, maps each threaded solver to its serial counterpart with repeated block children merged, maps rank-0 copies to one token, and drops vector loops that have a single kind of child. Two plans with the same form run the same codelets on the same elements, so the output bits stay those of FFTW at T. When the forms differ, the T plan is kept. In the frozen suite that happens for d_rf256 and s_rf256 at 8 threads, where the 8-thread plan uses radix 16 and the serial one radix 32, so their bits differ, and for d/s_cf100x37 at 8 threads (capped to 4). The capped plan is not blessed, so wisdom is unchanged.

## Bit-identity and gates

- `make test-upstream`: 4020/4020 cases with identical output and wisdom, including 8- and 10-thread cases where the cap applies.
- Output hashes equal baseline-v0 at 1, 2, 3, 4, 6 and 8 threads on the frozen (128) and held-out (78) suites, in baseline mode.
- `make test-fast`: 5616 cases, 0 outside tolerance.
- Peak heap (`-mem` binaries, 1 and 8 threads, both modes, both suites): no case is worse than v1. Baseline mode: frozen geomean 229.7 -> 226.9 KiB, total 287.54 -> 287.29 MiB; held-out 184.7 -> 181.9 KiB. Fast mode: 728.0 -> 726.9 KiB. The small drop comes from freeing the planning copy of the plan before the final one is awakened.

## Speed, no contention (geomean over baseline-v0 at 1 thread, 95% CI)

Frozen suite, 128 cases:

| build | 1 thread | 2 | 4 | 8 | best T per case | best T vs baseline-v0 best T |
|---|---|---|---|---|---|---|
| baseline-v0 | 1.000 | 0.707 | 0.901 | 0.909 | 2.179 | 1.000 |
| bit-identical, v1 | 1.188 | 1.622 | 2.261 | 2.953 | 3.100 | 1.423 |
| bit-identical, steal | 1.189 | 1.629 | 2.264 | 2.934 | 3.080 | 1.414 |
| **bit-identical, new** | **1.186** (1.183-1.193) | **1.665** (1.602-1.769) | **2.325** (2.254-2.387) | **3.033** (2.933-3.097) | **3.087** (3.006-3.164) | **1.417** (1.402-1.428) |
| fast, v1 | 3.016 | 4.013 | 5.255 | 6.733 | 6.917 | 3.175 |
| fast, steal | 3.017 | 4.012 | 5.266 | 6.655 | 6.848 | 3.143 |
| **fast, new** | **3.039** (3.027-3.087) | **4.013** (3.873-4.267) | **5.271** (5.144-5.408) | **6.664** (6.478-6.818) | **6.840** (6.740-7.078) | **3.140** (3.127-3.191) |

Held-out suite, 78 cases (first run of the held-out suite with the audited protocol):

| build | 1 thread | 2 | 4 | 8 | best T per case | best T vs baseline-v0 best T |
|---|---|---|---|---|---|---|
| baseline-v0 | 1.000 | 0.697 | 0.901 | 0.907 | 2.189 | 1.000 |
| bit-identical, v1 | 1.204 | 1.542 | 2.222 | 2.896 | 3.145 | 1.437 |
| **bit-identical, new** | **1.201** (1.198-1.203) | **1.566** (1.446-1.646) | **2.292** (2.229-2.328) | **2.977** (2.870-3.016) | **3.136** (3.018-3.243) | **1.433** (1.394-1.464) |
| fast, v1 | 3.480 | 4.015 | 5.746 | 7.298 | 7.480 | 3.417 |
| **fast, new** | **3.482** (3.478-3.498) | **3.953** (3.702-4.122) | **5.721** (5.581-5.764) | **7.252** (6.933-7.365) | **7.422** (7.192-7.576) | **3.391** (3.311-3.449) |

Paired, same thread count, new over v1 (frozen / held-out):

| mode | 1 thread | 2 | 4 | 8 |
|---|---|---|---|---|
| bit-identical | 0.999 (0.998-1.001) / 0.998 (0.997-1.000) | 1.027 (1.020-1.027) / 1.016 (0.999-1.017) | 1.028 (1.016-1.036) / 1.031 (1.020-1.043) | 1.027 (1.019-1.027) / 1.028 (1.022-1.028) |
| fast | 1.008 (1.003-1.011) / 1.001 (0.997-1.012) | 1.000 (0.997-1.004) / 0.985 (0.985-1.010) | 1.003 (0.998-1.008) / 0.996 (0.988-0.996) | 0.990 (0.987-0.992) / 0.994 (0.984-0.999) |

The cap alone (new over steal, frozen) gives 0.997, 1.022, 1.027 and 1.034 in bit-identical mode at 1, 2, 4 and 8 threads, and 1.007, 1.000, 1.001 and 1.001 in fast mode, whose serial thresholds already covered most small problems. In fast mode at 8 threads, stealing costs about 1% without contention (0.988 steal over v1 on the frozen suite).

## Never much slower than 1 thread

These are cells where a build at T threads runs below 0.9x or 0.67x of its own 1-thread time (frozen suite, no contention). The worst cell is in parentheses.

| build | T=2: <0.9 / <0.67 (min) | T=4 | T=8 |
|---|---|---|---|
| baseline-v0 | 42 / 36 (0.01) | 40 / 38 (0.00) | 42 / 40 (0.00) |
| bit-identical, v1 | 11 / 2 (0.43 d_cf1009) | 9 / 4 (0.50 d_cf1009) | 9 / 4 (0.45 d_cf1009) |
| **bit-identical, new** | **5 / 0** (0.75 s_rb65536) | **2 / 0** (0.74 s_rb65536) | **3 / 0** (0.79 d_cfip4096) |
| fast, v1 | 5 / 3 (0.42 s_rb65536) | 4 / 1 (0.50) | 5 / 2 (0.52) |
| fast, new | 6 / 3 (0.42 s_rb65536) | 4 / 1 (0.53) | 5 / 1 (0.56) |

## Contended machine: a CPU hog on cpus 4-7 (frozen suite, 2 rounds)

`bench/suite/audit/hog.c` keeps one normal-priority spinning thread on each of cpus 4-7, standing in for other load or hypervisor steal. The 1-thread column is the uncontended run on cpu 2.

| build | 2 threads | 4 | 8 | best T | cells < 0.67 of own 1 thread (T=2 / 4 / 8) |
|---|---|---|---|---|---|
| baseline-v0 | 0.621 | 0.561 | 0.629 | 1.649 | 40 / 42 / 42 |
| bit-identical, v1 | 0.994 | 1.243 | 1.672 | 1.874 | 20 / 16 / 14 (worst 0.11 s_cf1009) |
| bit-identical, steal | 1.386 | 1.635 | 2.138 | 2.232 | 0 / 4 / 4 |
| **bit-identical, new** | **1.408** (1.376-1.450) | **1.690** (1.651-1.739) | **2.229** (2.182-2.284) | **2.253** | **0 / 0 / 0** (worst 0.70) |
| fast, v1 | 2.611 | 3.109 | 3.832 | 4.254 | 22 / 17 / 11 |
| **fast, new** | **3.536** (3.459-3.633) | **4.066** (3.965-4.193) | **5.014** (4.887-5.171) | **5.256** | **2 / 4 / 3** |

Paired new over v1 under contention: bit-identical 1.416, 1.360 and 1.334 at 2, 4 and 8 threads; fast 1.354, 1.308 and 1.308.

## Remaining losses

- **Fast mode, mid-size real transforms** (s_rb65536, d_rb65536, d_rf20014; held-out d_cf8192): 0.42-0.6x of their own 1-thread time at 2-8 threads, in v1 and new alike. Their rough work (0.7-2.6 Mflop) is far above the cap. The cost-model planner picks a threaded plan that runs slower than its serial plan, so this is plan quality, not thread overhead.
- **Bit-identical mode, s_rb65536** at 2-8 threads (0.65-0.76x) and d_rb6000 at 2 threads (held-out). These are baseline-v0's own threaded plans, and capping them by work would need W thresholds that hurt larger cases.
- **Two-thread run-to-run variation.** Some 2-thread cells are bimodal between rounds in every build (d_cf2048: 5.5 us or 15 us per round), so a 3-round median can land on either mode. It is likely, though not established, that the worker sometimes shares the caller's CPU. This is why the 2-thread CIs are wide.

## Defect 1: d_rb1048576 and the other cells "below 1" at 1 thread

- **20-round interleaved rerun** of the 8 flagged cells (`flagged-t1-20rounds.csv`; zero steal): d_rb1048576 **1.121 (1.100-1.131)**, d_cb1048576 1.145, d_rf65536 1.031, d_rb65536 1.006 (0.997-1.015), d_rf100h1000 1.038, d_cf4096c64 1.000, s_cf480x640 1.133, s_cf64h4096 0.972 (0.964-0.982). The A/A copy gets 0.979 (0.970-0.987) on that last cell, the same order effect. All 8 cells together: 1.054 (1.048-1.057); A/A 1.001.
- **The audit sweep's 3-round run on this branch** (`frozen-cmp-t1.csv`): d_rb1048576 1.110 (v1 1.123); d_rb1024, FFTE-6's 0.93x cell, is 1.26.
- **Plans** are identical between baseline-v0 and fftchode for all 8 cells at 1 and 8 threads (`fftsuite --mode plan`).
- **The audit's 0.58x is noise**, for two reasons:
  - Its raw rounds for fc-bit d_rb1048576 show wall time 7-25 ms above process CPU time (31 ms wall, 24 ms CPU) at 1 thread, which means the process was off its CPU (that round logged 91k ticks of steal), and only 3 rounds were taken.
  - The per-case 3-round bootstrap CI does not control false positives. In this run the A/A copy of baseline-v0 has 35 of 128 cells whose CI lies entirely below 1 (49 exclude 1 in either direction). In the audit's 1-thread data, 34 A/A cells exclude 1 (19 entirely below). This branch's bit-identical build has 8 such cells in this run, fewer than the A/A copy. "Entirely below 1 at 95%" per case is therefore not evidence of a regression at 3 rounds. Suite geomeans are not affected: they pool 128 cases.
- **System checks:** one NUMA node, THP `madvise` (fftw_malloc does not madvise, so both builds use 4 KiB pages alike), no cpufreq interface in the guest (frequency is the host's), same pinning (cpu 2) for every binary.
