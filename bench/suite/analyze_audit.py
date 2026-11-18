#!/usr/bin/env python3
"""Fairness-audit analysis (FFTE-8) of the thread sweep in bench/results/audit/sweep (raw-<lib>-t<T>.txt from compare.py).

  analyze_audit.py SWEEPDIR [--out FILE.md] [--csv DIR]

Per lib, T and case: wall = median over rounds of the round's fastest batch (compare.py's statistic); cpu = median over rounds of
the round's median CPU time; plan = median plan_s. Libraries that cannot do a case simply do not appear for that case.
"""
import argparse, glob, math, os, re, statistics, collections, random

POOL_T1 = {"fftw-pool-est": "fftw-est", "fftw-pool-measure": "fftw-measure", "aocl-pool-measure": "aocl-measure"}
SINGLE = ["kissfft", "ffte", "pffft-native"]
PLANLESS = ["ducc0-nofm", "pocketfft-native", "pffft-native", "kissfft", "ffte"]
LEVELS = {  # matched planning level: fftchode variant vs comparators planned at the same FFTW level (+ libraries without a planner)
    "ESTIMATE": ("fc-fast", ["fftw-est", "fftw-pool-est", "aocl-est"] + PLANLESS),
    "MEASURE": ("fc-fast-measure", ["fftw-measure", "fftw-pool-measure", "aocl-measure", "aocl-pool-measure"] + PLANLESS),
}
ALL_CMP = ["fftw-est", "fftw-pool-est", "aocl-est", "fftw-measure", "fftw-pool-measure", "aocl-measure", "aocl-pool-measure"] + PLANLESS

def load(d):
    raw = collections.defaultdict(dict)  # (lib, T) -> case -> list of rounds (plan, [t], [cpu])
    for f in glob.glob(os.path.join(d, "raw-*-t*.txt")):
        m = re.match(r"raw-(.*)-t(\d+)\.txt$", os.path.basename(f)); lib, T = m.group(1), int(m.group(2))
        rounds = []; cur = None
        for line in open(f):
            if line.startswith("#round"): cur = {}; rounds.append(cur); continue
            p = line.split(); i = p.index("cpu") if "cpu" in p else len(p)
            cur[p[0]] = (float(p[1]), [float(x) for x in p[2:i]], [float(x) for x in p[i + 1:]])
        if len(rounds) > 1 and len(rounds[-1]) < len(rounds[0]): rounds.pop()  # interrupted last round
        if not rounds: continue
        cases = set(rounds[0]).intersection(*rounds)
        for c in cases:
            raw[(lib, T)][c] = [r[c] for r in rounds]
    return raw

def stat(rs):
    w = statistics.median(min(r[1]) for r in rs)
    cp = [statistics.median(r[2]) for r in rs if r[2]]
    return w, (statistics.median(cp) if cp else float("nan")), statistics.median(r[0] for r in rs)

def gm(xs): xs = list(xs); return math.exp(sum(math.log(x) for x in xs) / len(xs)) if xs else float("nan")

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("sweep"); ap.add_argument("--out"); ap.add_argument("--csv"); a = ap.parse_args()
    raw = load(a.sweep)
    S = {k: {c: stat(v) for c, v in d.items()} for k, d in raw.items()}
    Ts = sorted({T for _, T in S}); libs = sorted({l for l, _ in S})
    # a pool variant at T=1 is the plain library (the callback is never installed for one thread); single-threaded libraries
    # (KissFFT, FFTE, PFFFT) run the same code at any thread count, so their T=1 times stand for every T.
    for pool, plain in POOL_T1.items():
        if (plain, 1) in S and (pool, 1) not in S and any(l == pool for l, _ in S): S[(pool, 1)] = S[(plain, 1)]
    for l in SINGLE:
        for T in Ts:
            if (l, 1) in S and (l, T) not in S: S[(l, T)] = S[(l, 1)]
    cases = sorted(S[("base", 1)])
    out = []
    P = out.append
    # best over thread counts: per lib and case, the T with the smallest wall time (a library at T=1 only counts as its T=1 time)
    best = {}
    for l in libs:
        for c in cases:
            v = [(S[(l, T)][c][0], T) for T in Ts if (l, T) in S and c in S[(l, T)]]
            if v: best[(l, c)] = min(v)
    if a.csv:
        os.makedirs(a.csv, exist_ok=True)
        with open(os.path.join(a.csv, "sweep-percase.csv"), "w") as f:
            f.write("lib,case,threads,wall_s,cpu_s,plan_s\n")
            for (l, T), d in sorted(S.items()):
                for c, (w, cp, pl) in sorted(d.items()): f.write("%s,%s,%d,%.6g,%.6g,%.6g\n" % (l, c, T, w, cp, pl))
        with open(os.path.join(a.csv, "sweep-best-threads.csv"), "w") as f:
            f.write("lib,case,best_threads,wall_s\n")
            for (l, c), (w, T) in sorted(best.items()): f.write("%s,%s,%d,%.6g\n" % (l, c, T, w))
    P("## Suite geomean speedup over baseline-v0, by thread count\n")
    P("Same T: t(base at T)/t(lib at T), the FFTE-6 convention. vs base T=1: t(base at 1 thread)/t(lib at T). best T: per case each library at its own fastest thread count, over baseline-v0 at its own fastest thread count (and over baseline-v0 at 1 thread). CPU: suite geomean of process CPU time per transform (all threads, spinning included) relative to baseline-v0 at 1 thread and to the same library at 1 thread (>1 = more CPU).\n")
    hdr = "| library | cases | " + " | ".join("same T=%d" % T for T in Ts if T > 1) + " | " + " | ".join("vs base T=1, T=%d" % T for T in Ts) + " | best T | best T vs base T=1 | best-T mode | CPU at T=%d vs base T=1 | CPU at T=%d vs own T=1 |" % (Ts[-1], Ts[-1])
    P(hdr); P("|" + "---|" * (hdr.count("|") - 1))
    for l in libs:
        cs = [c for c in cases if (l, c) in best]
        row = [l, str(len(cs))]
        for T in Ts:
            if T == 1: continue
            k = (l, T); cc = [c for c in cs if k in S and c in S[k] and c in S[("base", T)]]
            row.append("%.2f" % gm(S[("base", T)][c][0] / S[k][c][0] for c in cc) if cc else "-")
        for T in Ts:
            k = (l, T); cc = [c for c in cs if k in S and c in S[k]]
            row.append("%.2f" % gm(S[("base", 1)][c][0] / S[k][c][0] for c in cc) if cc else "-")
        row.append("%.2f" % gm(best[("base", c)][0] / best[(l, c)][0] for c in cs))
        row.append("%.2f" % gm(S[("base", 1)][c][0] / best[(l, c)][0] for c in cs))
        row.append(str(statistics.mode(best[(l, c)][1] for c in cs)))
        k = (l, Ts[-1]); cc = [c for c in cs if k in S and c in S[k] and not math.isnan(S[k][c][1])]
        row.append("%.2f" % gm(S[k][c][1] / S[("base", 1)][c][1] for c in cc) if cc else "-")
        cc = [c for c in cc if (l, 1) in S and c in S[(l, 1)] and not math.isnan(S[(l, 1)][c][1])]
        row.append("%.2f" % gm(S[k][c][1] / S[(l, 1)][c][1] for c in cc) if cc else "-")
        P("| " + " | ".join(row) + " |")
    # head-to-head
    P("\n## fftchode fast mode over each FFTW/AOCL configuration (suite geomean of time ratios, common cases)\n")
    P("| pair | " + " | ".join("T=%d" % T for T in Ts) + " | best T each |"); P("|---|" + "---|" * (len(Ts) + 1))
    for fc, cm in [("fc-fast", "fftw-est"), ("fc-fast", "fftw-pool-est"), ("fc-fast", "fftw-measure"), ("fc-fast", "fftw-pool-measure"), ("fc-fast-measure", "fftw-measure"), ("fc-fast-measure", "fftw-pool-measure"),
                   ("fc-fast", "aocl-est"), ("fc-fast-measure", "aocl-measure"), ("fc-fast-measure", "aocl-pool-measure"), ("fc-bit", "fftw-est"), ("fc-bit", "base")]:
        row = ["%s / %s" % (fc, cm)]
        for T in Ts:
            if (fc, T) in S and (cm, T) in S:
                cc = [c for c in cases if c in S[(fc, T)] and c in S[(cm, T)]]; row.append("%.2f" % gm(S[(cm, T)][c][0] / S[(fc, T)][c][0] for c in cc))
            else: row.append("-")
        cc = [c for c in cases if (fc, c) in best and (cm, c) in best]
        row.append("%.2f" % gm(best[(cm, c)][0] / best[(fc, c)][0] for c in cc) if cc else "-")
        P("| " + " | ".join(row) + " |")
    # per-cell best
    P("\n## Ratio to the fastest comparator per cell (fftchode speed / fastest comparator's speed; >1 = fftchode faster)\n")
    P("Comparators that cannot do a cell are absent from that cell. oneMKL is excluded (generic SSE2 kernels on this AMD CPU) and shown separately. ducc0 is the IEEE (no -ffast-math) build.\n")
    P("| fftchode variant | comparators | condition | geomean ratio | cells won | cells |"); P("|---|---|---|---|---|---|")
    def cellratio(fcl, cmps, T=None, use_best=False, extra=()):
        rs = []; won = 0
        for c in cases:
            if use_best:
                if (fcl, c) not in best: continue
                tf = best[(fcl, c)][0]; tc = [best[(x, c)][0] for x in list(cmps) + list(extra) if (x, c) in best]
            else:
                if (fcl, T) not in S or c not in S[(fcl, T)]: continue
                tf = S[(fcl, T)][c][0]; tc = [S[(x, T)][c][0] for x in list(cmps) + list(extra) if (x, T) in S and c in S[(x, T)]]
            if not tc: continue
            r = min(tc) / tf; rs.append(r); won += r >= 1
        return gm(rs), won, len(rs)
    CONDS = [("T=%d" % T, dict(T=T)) for T in Ts] + [("best T each", dict(use_best=True))]
    for lev, (fcl, cmps) in LEVELS.items():
        for cond, kw in CONDS:
            g, w, n = cellratio(fcl, cmps, **kw); P("| %s | %s level | %s | %.2f | %d | %d |" % (fcl, lev, cond, g, w, n))
    for fcl in ("fc-fast", "fc-fast-measure"):
        for cond, kw in CONDS:
            g, w, n = cellratio(fcl, ALL_CMP, **kw); P("| %s | every planning level (FFTE-6 style, minus MKL) | %s | %.2f | %d | %d |" % (fcl, cond, g, w, n))
            g, w, n = cellratio(fcl, ALL_CMP, extra=["mkl"], **kw); P("| %s | every planning level, MKL included | %s | %.2f | %d | %d |" % (fcl, cond, g, w, n))
    # planning
    P("\n## Planning time (seconds, per case: median over rounds; suite median / 90th percentile / max), %d thread(s) and %d threads\n" % (Ts[0], Ts[-1]))
    P("| library | T | median | p90 | max | suite total |"); P("|---|---|---|---|---|---|")
    for l in libs:
        for T in sorted({Ts[0], Ts[-1]}):
            if (l, T) not in S: continue
            v = sorted(x[2] for x in S[(l, T)].values())
            P("| %s | %d | %.3g | %.3g | %.3g | %.1f |" % (l, T, statistics.median(v), v[int(0.9 * (len(v) - 1))], v[-1], sum(v)))
    P("\n## Planning budget: break-even execution counts\n")
    P("Per case, A = the fftchode variant, B = the comparator. When one side plans longer but runs faster, it pays off after n* = (extra plan time) / (time saved per execution) executions. Columns: cases where A is faster to plan and to run; cases where B is; cases where A plans longer and runs faster (median and 90th-percentile n*); cases where B plans longer and runs faster (same).\n")
    P("| A vs B | T | A dominates | B dominates | A plans longer, runs faster: cases / median n* / p90 n* | B plans longer, runs faster: cases / median n* / p90 n* | cases |"); P("|---|---|---|---|---|---|---|")
    for A, Bc in [("fc-fast", "fftw-est"), ("fc-fast", "aocl-est"), ("fc-fast", "fftw-measure"), ("fc-fast", "aocl-measure"), ("fc-fast-measure", "fftw-measure"), ("fc-fast-measure", "aocl-measure")]:
        for T in sorted({Ts[0], Ts[-1]}):
            if (A, T) not in S or (Bc, T) not in S: continue
            da = db = n = 0; na = []; nb_ = []
            for c in cases:
                if c not in S[(A, T)] or c not in S[(Bc, T)]: continue
                n += 1; ta, _, pa = S[(A, T)][c]; tb, _, pb = S[(Bc, T)][c]
                if ta <= tb and pa <= pb: da += 1
                elif tb <= ta and pb <= pa: db += 1
                elif ta < tb: na.append((pa - pb) / (tb - ta))
                else: nb_.append((pb - pa) / (ta - tb))
            def q(v): v = sorted(v); return "%d / %s / %s" % (len(v), ("%.3g" % statistics.median(v)) if v else "-", ("%.3g" % v[int(0.9 * (len(v) - 1))]) if v else "-")
            P("| %s vs %s | %d | %d | %d | %s | %s | %d |" % (A, Bc, T, da, db, q(na), q(nb_), n))
    txt = "\n".join(out) + "\n"
    if a.out: open(a.out, "w").write(txt)
    print(txt)

if __name__ == "__main__":
    main()
