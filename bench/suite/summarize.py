#!/usr/bin/env python3
"""Summarize the fftchode benchmark results in bench/results/ into bench/results/SUMMARY.md.

Inputs (all written by bench/suite/compare.py or fftsuite --mode mem/err, then copied here):
  frozen-t{1,8}.csv heldout-t{1,8}.csv ablation-t{1,8}.csv
  comparators-estimate-t{1,8}.csv comparators-measure-t{1,8}.csv comparators-patient-t1.csv
  memory.csv error.csv size.csv
Every speedup is relative to baseline-v0 built with clang 18 (-O2 -ffp-contract=off) in the same run.
"""
import csv, math, os, sys
from collections import defaultdict

R = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "results")

def gm(v):
    v = [x for x in v if x > 0]
    return math.exp(sum(math.log(x) for x in v) / len(v)) if v else float("nan")

def load(name):
    p = os.path.join(R, name)
    return list(csv.DictReader(open(p))) if os.path.exists(p) else []

def speedups(rows):
    d = defaultdict(dict)
    for r in rows: d[r["lib"]][r["case"]] = float(r["speedup"])
    return d

def size_class(case):
    c = case[2:]
    kind = c[:2]
    body = c[2:]
    if body.startswith("ip"): body = body[2:]
    if "c" in body: return "strided"
    if "h" in body: return "batched"
    if "x" in body: return "3-D" if body.count("x") == 2 else "2-D"
    n = int(body)
    if n <= 4096 and n & (n - 1) == 0: return "pow2 small"
    if n & (n - 1) == 0: return "pow2 large"
    def isprime(m):
        if m < 2: return False
        i = 2
        while i * i <= m:
            if m % i == 0: return False
            i += 1
        return True
    if isprime(n): return "prime"
    m = n
    for p in (2, 3, 5, 7): 
        while m % p == 0: m //= p
    return "mixed radix" if m == 1 else "large prime factor"

def kindname(case):
    return {"cf": "c2c", "cb": "c2c", "rf": "r2c", "rb": "c2r"}[case[2:4]]

out = []
w = out.append
w("# fftchode benchmark summary\n")
w("Machine: AMD EPYC (Zen 4) cloud VM, 4 cores / 8 threads, AVX-512, 32 GB; Ubuntu 24.04, clang 18.1.3. "
  "Every figure is a geometric-mean speedup over baseline-v0 (FFTW 3.3.10 scalar in one file, clang -O2 -ffp-contract=off) "
  "measured in the same interleaved run; 95% confidence intervals come from a paired bootstrap over rounds. "
  "Suites: bench/suite/frozen.txt (128 cases) and bench/suite/heldout.txt (78 cases, never used for tuning). "
  "Planning is FFTW_ESTIMATE unless stated.\n")

w("## Headline\n")
w("| build | suite | 1 thread | 8 threads |\n|---|---|---|---|")
for lab, nm in (("final2", "fftchode baseline mode (bit-identical)"), ("final2-fast", "fftchode fast mode")):
    for suite in ("frozen", "heldout"):
        cells = []
        for t in (1, 8):
            rows = [r for r in load("%s-t%d.csv" % (suite, t)) if r["lib"] == lab]
            if not rows: cells.append("n/a"); continue
            sp = [float(r["speedup"]) for r in rows]
            cells.append("%.3f" % gm(sp))
        w("| %s | %s | %s | %s |" % (nm, suite, cells[0], cells[1]))
w("\nConfidence intervals for these rows are in the run summaries (docs/fftchode.pdf, Table 2).\n")

w("## Ablation (frozen suite, 5 rounds): remove one change from the final build\n")
w("| build | 1 thread | 8 threads |\n|---|---|---|")
names = {"final2": "baseline mode, all changes", "basenomv": "baseline mode without per-ISA scalar codelets",
         "final2-fast": "fast mode, all changes", "noplan": "fast mode without the cost-model planner (stock ESTIMATE)",
         "nosimd": "fast mode without SIMD codelets", "nomv": "fast mode without per-ISA scalar codelets",
         "nosolv": "fast mode without fftchode's fast solvers (pair/embed/colpack)"}
ab = {t: speedups(load("ablation-t%d.csv" % t)) for t in (1, 8)}
for k, v in names.items():
    w("| %s | %.3f | %.3f |" % (v, gm(ab[1][k].values()), gm(ab[8][k].values())))

for t in (1, 8):
    est = speedups(load("comparators-estimate-t%d.csv" % t))
    mea = speedups(load("comparators-measure-t%d.csv" % t))
    pat = speedups(load("comparators-patient-t%d.csv" % t))
    comp = {}
    for d, labs in ((est, ["mkl", "aocl-est", "fftw-est", "ducc0-native", "ducc0-matched", "pocketfft-native", "pocketfft-matched", "pffft-native", "pffft-matched", "kissfft", "ffte"]),
                    (mea, ["fftw-measure", "aocl-measure", "aocl-app-measure"]), (pat, ["fftw-patient"])):
        for l in labs:
            if l in d: comp[l] = d[l]
    me = mea.get("final2-fast", {}); mm = mea.get("final2-measure", {})
    w("\n## Against the fastest comparator, %d thread%s\n" % (t, "" if t == 1 else "s"))
    w("Comparators: " + ", ".join("%s (%d cases, geomean %.2f)" % (l, len(v), gm(v.values())) for l, v in sorted(comp.items())) + ".\n")
    w("Ratio = fftchode speedup / best comparator speedup in that cell (>1: fftchode faster). "
      "'ESTIMATE' is fftchode fast mode with ESTIMATE planning (MATLAB's mode); 'MEASURE' is fftchode fast mode with FFTW_MEASURE.\n")
    w("| precision | type | class | cells | ESTIMATE | MEASURE | cells won (MEASURE) | fastest comparator most often |\n|---|---|---|---|---|---|---|---|")
    groups = defaultdict(list)
    for c in me:
        best = max(((v[c], l) for l, v in comp.items() if c in v), default=None)
        if not best: continue
        groups[(c[0], kindname(c), size_class(c))].append((c, me[c] / best[0], mm.get(c, float("nan")) / best[0], best[1]))
    allE, allM, wonE, wonM = [], [], 0, 0
    for k in sorted(groups):
        g = groups[k]
        e = [x[1] for x in g]; m = [x[2] for x in g]
        allE += e; allM += m; wonE += sum(1 for x in e if x >= 1); wonM += sum(1 for x in m if x >= 1)
        top = max(set(x[3] for x in g), key=lambda l: sum(1 for x in g if x[3] == l))
        w("| %s | %s | %s | %d | %.2f | %.2f | %d | %s |" % (k[0], k[1], k[2], len(g), gm(e), gm(m), sum(1 for x in m if x >= 1), top))
    w("| all | | | %d | %.2f | %.2f | %d | |" % (len(allE), gm(allE), gm(allM), wonM))
    w("\nCells where fftchode (MEASURE) is at least as fast as every comparator: %d of %d; with ESTIMATE: %d.\n" % (wonM, len(allM), wonE))

mem = load("memory.csv")
if mem:
    w("## Peak heap (frozen suite, bytes above the caller's buffers, plan + one execution)\n")
    w("| library | threads | geomean peak (KiB) | suite total peak (MiB) |\n|---|---|---|---|")
    by = defaultdict(list)
    for r in mem: by[(r["lib"], r["threads"])].append(max(1, int(r["peak_bytes"])))
    for (l, t), v in sorted(by.items()):
        w("| %s | %s | %.1f | %.1f |" % (l, t, gm(v) / 1024, sum(v) / 2**20))
err = load("error.csv")
if err:
    w("\n## Accuracy (relative rms error vs long-double FFTW, frozen suite)\n")
    w("| library | double geomean | single geomean | worst double | worst single |\n|---|---|---|---|---|")
    by = defaultdict(lambda: {"d": [], "s": []})
    for r in err:
        if r["rel_rms"] != "FAIL": by[r["lib"]][r["case"][0]].append(float(r["rel_rms"]))
    for l, v in by.items():
        w("| %s | %.3g | %.3g | %.3g | %.3g |" % (l, gm(v["d"]), gm(v["s"]), max(v["d"] or [float('nan')]), max(v["s"] or [float('nan')])))
sz = load("size.csv")
if sz:
    w("\n## Compiled size\n")
    w("| object | text bytes | data bytes |\n|---|---|---|")
    for r in sz: w("| %s | %s | %s |" % (r["object"], r["text"], r["data"]))
open(os.path.join(R, "SUMMARY.md"), "w").write("\n".join(out) + "\n")
print("\n".join(out))
