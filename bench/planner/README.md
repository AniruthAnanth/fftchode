# Fast-mode ESTIMATE cost model

In fast numerics mode (`fftchode_set_numerics(1)`, `FFTCHODE_NUMERICS=fast`) `FFTW_ESTIMATE` no longer
stops at the first plan the pruning heuristic accepts. It plans every applicable solver and keeps the
cheapest by a cost that adds three terms to FFTW's operation count:

- the cache lines each loop fetches from L1, L2, L3 or DRAM (`X(fc_mem)`, including L1 set conflicts
  for power-of-two strides);
- the child-plan calls a vector loop makes (`X(fc_loop)`);
- codelet arithmetic weighted by register width (scalar, 128, 256, 512 bits) and codelet family
  (n1 n2 t1 t2 t3 q1 real), with a surcharge for radix > 16 (`X(fc_arith)`); SIMD codelets run on
  fewer than VL transforms are charged for full vectors.

Baseline mode is untouched: plans, output bits and exported wisdom match baseline-v0 and FFTW 3.3.10
(`make test-upstream`).

The 36 weights (`FFTCHODE_FCW_INIT` in `src/fft.cpp`) were fitted on one CPU class (Zen 4, AVX-512)
against timed plans of the training cases in `train.txt`, which exclude every frozen-suite shape.

| file | purpose |
| --- | --- |
| `planner_patch.py` | re-applies the cost model to a regenerated `src/fft.cpp`: `planner_patch.py fft.cpp ["w0, ..., w35"]` |
| `calibrate.sh` | refits the weights on the current machine: `bench/planner/calibrate.sh OUTDIR [train.txt]` |
| `retime.cpp` | collects plans (wisdom) per case and times every distinct plan interleaved |
| `fit.py` | NNLS fit of the weights, with in-sample and 5-fold cross-validated plan regret |
| `gen_train.py` | draws random training cases outside the frozen suite |

`calibrate.sh` builds `src/fft.cpp` with `-DFFTCHODE_PLANNER_CALIBRATE`, which reads the weights from
`FFTCHODE_PLANW` and `FFTCHODE_PLANRAND=sigma,seed` (a random cost perturbation used to sample
alternative plans). Paste the printed `W=` line into `planner_patch.py` or `FFTCHODE_FCW_INIT`.
