# Fast-mode ESTIMATE cost model

In fast numerics mode (`fftchode_set_numerics(1)`, `FFTCHODE_NUMERICS=fast`) `FFTW_ESTIMATE` no longer
stops at the first plan the pruning heuristic accepts. It plans every applicable solver and keeps the
cheapest by a cost that adds three terms to FFTW's operation count:

- the cache lines each loop fetches from L1, L2, L3 or DRAM (`X(fc_mem)`, including L1 set conflicts
  for power-of-two strides);
- the child-plan calls a vector loop makes (`X(fc_loop)`);
- codelet arithmetic weighted by register width (scalar, 128, 256, 512 bits) and codelet family
  (n1 n2 t1 t2 t3 q1 real), with a surcharge for radix > 16 (`X(fc_arith)`); SIMD codelets run on
  fewer than VL transforms are charged for full vectors;
- for a threaded plan, the parallel part's cost divided over the lanes the thread pool will really run
  it on (`X(fc_lanes)`: the CPUs in the process's affinity mask, fewer when the work is below the pool's
  minimum per thread, none when the blocks would share output lines), plus a fixed hand-off charge per
  parallel region (`X(fc_thr)`, `FFTCHODE_FC_SPAWN`). FFTW sums the children's counts and charges
  nothing for spawning, so without this term ESTIMATE threads tiny transforms;
- for a vrank loop that the column buffering will run through a shadow plan, the shadow on blocks of
  adjacent columns plus the copy of their lines back, instead of the strided loop.

The unpruned search is bounded: once a top-level plan has evaluated `FFTCHODE_PLAN_BUDGET` plans
(10000), the rest of its search prunes as stock ESTIMATE does. The bound is a count, not a time, so
plans are reproducible; highly composite sizes (720720 = 2^4 3^2 5 7 11 13) otherwise multiply the
search. In fast mode the column buffering also accepts a shadow plan whose structure differs from the
strided child's (baseline mode keeps the identical-structure rule that makes it bit-identical).

Baseline mode is untouched: plans, output bits and exported wisdom match baseline-v0 and FFTW 3.3.10
(`make test-upstream`).

The 36 weights (`FFTCHODE_FCW_INIT` in `src/fft.cpp`) were fitted on the timing machine (Zen 4, AVX-512)
against timed plans of the training cases in `train.txt`, which exclude every frozen-suite shape.

| file | purpose |
| --- | --- |
| `planner_patch.py` | re-applies the cost model to a regenerated `src/fft.cpp`: `planner_patch.py fft.cpp ["w0, ..., w35"]` |
| `calibrate.sh` | refits the weights on the current machine: `bench/planner/calibrate.sh OUTDIR [train.txt]` |
| `retime.cpp` | collects plans (wisdom) per case and times every distinct plan interleaved |
| `fit.py` | NNLS fit of the weights, with in-sample and 5-fold cross-validated plan regret |
| `gen_train.py` | draws random training cases outside the frozen suite |

`calibrate.sh` builds `src/fft.cpp` with `-DFFTCHODE_PLANNER_CALIBRATE`, which reads the weights from
`FFTCHODE_PLANW`, `FFTCHODE_PLANSPAWN` (hand-off cost), `FFTCHODE_PLANBUDGET` (plan budget) and
`FFTCHODE_PLANRAND=sigma,seed` (a random cost perturbation used to sample
alternative plans). Paste the printed `W=` line into `planner_patch.py` or `FFTCHODE_FCW_INIT`.
