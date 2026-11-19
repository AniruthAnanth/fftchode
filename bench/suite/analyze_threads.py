#!/usr/bin/env python3
"""Thread-count analysis of compare.py runs (FFTE-9): one fixed reference (baseline-v0 at 1 thread), every library at
1, 2, 4 and 8 threads and at its best thread count per case, and each library at T threads against its own 1-thread time.

  analyze_threads.py RUNDIR [--ref base] [--pairs new=old ...] [--csv FILE]

RUNDIR holds raw-<lib>-t<T>.txt from compare.py. Statistic per (lib, T, case): median over rounds of the round's fastest
batch. 95% CIs: bootstrap over rounds, resampled independently per run (thread count), 2000 draws. A run without a
1-thread file (a contended run) uses the 1-thread files of --t1 DIR.
"""
import argparse, glob, math, os, random, re, statistics, collections

def load(d):
    raw = collections.defaultdict(dict)
    for f in glob.glob(os.path.join(d, "raw-*-t*.txt")):
        m = re.match(r"raw-(.*)-t(\d+)\.txt$", os.path.basename(f)); lib, T = m.group(1), int(m.group(2))
        rounds = []; cur = None
        for line in open(f):
            if line.startswith("#round"): cur = {}; rounds.append(cur); continue
            p = line.split(); i = p.index("cpu") if "cpu" in p else len(p)
            cur[p[0]] = min(float(x) for x in p[2:i])
        if len(rounds) > 1 and len(rounds[-1]) < len(rounds[0]): rounds.pop()
        cases = set(rounds[0]).intersection(*rounds)
        for c in cases: raw[(lib, T)][c] = [r[c] for r in rounds]
    return raw

def gm(v): v = list(v); return math.exp(sum(math.log(x) for x in v) / len(v))

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("dir"); ap.add_argument("--ref", default="base")
    ap.add_argument("--t1"); ap.add_argument("--pairs", nargs="*", default=[]); ap.add_argument("--csv"); ap.add_argument("--boot", type=int, default=2000)
    a = ap.parse_args()
    raw = load(a.dir)
    if a.t1:
        for k, v in load(a.t1).items():
            if k[1] == 1: raw[k] = v
    libs = sorted({l for l, _ in raw}); Ts = sorted({T for _, T in raw})
    cases = sorted(set.intersection(*(set(v) for v in raw.values())))
    nr = {T: min(len(next(iter(raw[(l, T)].values()))) for l in libs if (l, T) in raw) for T in Ts}
    rng = random.Random(12345)
    draws = [None] + [{T: [rng.randrange(nr[T]) for _ in range(nr[T])] for T in Ts} for _ in range(a.boot)]
    def t(l, T, c, d): rs = raw[(l, T)][c]; return statistics.median(rs[i] for i in d[T]) if d else statistics.median(rs)
    def ci(f):
        v = f(None); b = sorted(f(d) for d in draws[1:]); return v, b[int(0.025 * len(b))], b[int(0.975 * len(b)) - 1]
    fmt = lambda x: "%.3f (%.3f-%.3f)" % x
    R = a.ref
    print("cases: %d; rounds per thread count: %s; reference: %s at 1 thread\n" % (len(cases), ", ".join("T=%d: %d" % (T, nr[T]) for T in Ts), R))
    print("| library | " + " | ".join("T=%d vs %s T=1" % (T, R) for T in Ts) + " | best T vs %s T=1 | best T vs %s best T |" % (R, R))
    print("|---" * (len(Ts) + 3) + "|")
    best = lambda l, c, d: min(t(l, T, c, d) for T in Ts)
    for l in libs:
        row = [fmt(ci(lambda d, T=T: gm(t(R, 1, c, d) / t(l, T, c, d) for c in cases))) for T in Ts]
        row.append(fmt(ci(lambda d: gm(t(R, 1, c, d) / best(l, c, d) for c in cases))))
        row.append(fmt(ci(lambda d: gm(best(R, c, d) / best(l, c, d) for c in cases))))
        print("| %s | %s |" % (l, " | ".join(row)))
    print("\nSlowdown against the library's own 1-thread time (cells where T threads run slower than 1 thread):\n")
    print("| library | " + " | ".join("T=%d: gm / min / cells <0.9 / <0.67" % T for T in Ts if T > 1) + " |"); print("|---" * len(Ts) + "|")
    for l in libs:
        cells = []
        for T in Ts:
            if T == 1: continue
            r = {c: t(l, 1, c, None) / t(l, T, c, None) for c in cases}
            worst = min(r, key=r.get)
            cells.append("%.3f / %.2f (%s) / %d / %d" % (gm(r.values()), r[worst], worst, sum(v < 0.9 for v in r.values()), sum(v < 0.67 for v in r.values())))
        print("| %s | %s |" % (l, " | ".join(cells)))
    for p in a.pairs:
        n, o = p.split("=")
        print("\n%s over %s, same thread count (paired by round within each run):" % (n, o))
        for T in Ts:
            def f(d, T=T):
                return gm(t(o, T, c, d) / t(n, T, c, d) for c in cases)
            v = ci(f); r = {c: t(o, T, c, None) / t(n, T, c, None) for c in cases}; w = sorted(r, key=r.get)
            print("  T=%d: %s; worst cells %s; best %s" % (T, fmt(v), ", ".join("%s %.2f" % (c, r[c]) for c in w[:3]), ", ".join("%s %.2f" % (c, r[c]) for c in w[-3:])))
    if a.csv:
        with open(a.csv, "w") as f:
            f.write("lib,case,threads,wall_s\n")
            for (l, T), d in sorted(raw.items()):
                for c in sorted(d): f.write("%s,%s,%d,%.6g\n" % (l, c, T, statistics.median(d[c])))

if __name__ == "__main__":
    main()
