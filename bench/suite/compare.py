#!/usr/bin/env python3
"""Interleaved A/B(/C...) timing of the frozen suite on one machine (FFTE-6).

  compare.py --suite bench/suite/frozen.txt --threads 1 --rounds 10 --out DIR  base=BIN cand=BIN [...]

Each round runs every case once per binary, interleaved case by case (one short process per
case and binary, order rotating), so host-side contention hits A and B alike. Binaries are
pinned with taskset (--cpus; default: the first N cpus of the order that takes one SMT sibling of
every physical core before any second sibling, starting at cpu 2, so N <= cores never shares a core). Per round the
value is the minimum over that round's batches; the statistic is the median over rounds.
Hypervisor steal time per round is logged. The
speedup of X over the first binary is median_t(first)/median_t(X); the suite figure is the
geometric mean over cases. 95% CIs come from a paired bootstrap over rounds.
Process CPU time (all threads, CLOCK_PROCESS_CPUTIME_ID) per transform is recorded beside wall time
when the binary prints it ("cpu" marker) and reported as the median over rounds of each round's
median. Writes DIR/raw-<label>-t<threads>.txt (all rounds, one line per case as soon as it is measured,
so an interrupted run keeps its data; --analyze drops an incomplete last round) and DIR/cmp-t<threads>.csv.
"""
import argparse, math, os, random, statistics, subprocess, sys, time

def steal():
    f = open("/proc/stat").readline().split(); return int(f[8])

def cases_of(suite):
    out = []
    for line in open(suite):
        f = line.split()
        if len(f) == 7 and not line.startswith("#"): out.append(f[0])
    return out

def pin_order():
    cores = {}
    for c in range(os.cpu_count()):
        try: key = open("/sys/devices/system/cpu/cpu%d/topology/core_id" % c).read().strip() + "/" + open("/sys/devices/system/cpu/cpu%d/topology/physical_package_id" % c).read().strip()
        except OSError: key = str(c)
        cores.setdefault(key, []).append(c)
    groups = sorted(cores.values())
    order = [g[k] for k in range(max(len(g) for g in groups)) for g in groups if k < len(g)]
    k = order.index(2) if 2 in order[:len(groups)] else 0
    first = order[:len(groups)]; first = first[k:] + first[:k]
    rest = order[len(groups):]; rest = rest[k:] + rest[:k]
    return first + rest

def parse_times(f):
    if "cpu" in f:
        i = f.index("cpu"); return [float(x) for x in f[:i]], [float(x) for x in f[i + 1:]]
    return [float(x) for x in f], []

def run(bin_, suite, threads, cpus, batches, tmin, only=None):
    cmd = ["taskset", "-c", cpus, bin_, "--suite", suite, "--threads", str(threads), "--batches", str(batches), "--tmin", str(tmin)]
    if only: cmd += ["--only", only]
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    res = {}
    for line in out.splitlines():
        f = line.split()
        if len(f) < 5 or f[2] == "FAIL": continue
        res[f[0]] = (float(f[2]),) + parse_times(f[4:])
    return res

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--suite", required=True); ap.add_argument("--threads", type=int, default=1)
    ap.add_argument("--rounds", type=int, default=10); ap.add_argument("--batches", type=int, default=3)
    ap.add_argument("--tmin", type=float, default=0.02); ap.add_argument("--cpus")
    ap.add_argument("--out", required=True); ap.add_argument("--boot", type=int, default=2000)
    ap.add_argument("--analyze", action="store_true", help="only re-analyze existing raw files")
    ap.add_argument("libs", nargs="+", help="label=binary")
    a = ap.parse_args()
    libs = [x.split("=", 1) for x in a.libs]
    cpus = a.cpus or ",".join(str(c) for c in sorted(pin_order()[:a.threads]))
    os.makedirs(a.out, exist_ok=True)
    data = {lab: [] for lab, _ in libs}   # per lib: list over rounds of {case: (plan_s, [t...])}
    raws = {lab: os.path.join(a.out, "raw-%s-t%d.txt" % (lab, a.threads)) for lab, _ in libs}
    if a.analyze:
        for lab, _ in libs:
            cur = None
            for line in open(raws[lab]):
                if line.startswith("#round"): cur = {}; data[lab].append(cur); continue
                f = line.split(); cur[f[0]] = (float(f[1]),) + parse_times(f[2:])
            if len(data[lab]) > 1 and len(data[lab][-1]) < len(data[lab][0]): data[lab].pop()  # interrupted last round
    else:
        fh = {lab: open(raws[lab], "w") for lab, _ in libs}
        t0 = time.time()
        names = cases_of(a.suite)
        for r in range(a.rounds):
            st0 = steal()
            for lab, _ in libs: data[lab].append({}); fh[lab].write("#round %d\n" % r)
            for j, c in enumerate(names):
                k = (r + j) % len(libs); order = libs[k:] + libs[:k]
                for lab, b in order:
                    res = run(b, a.suite, a.threads, cpus, a.batches, a.tmin, only=c); data[lab][-1].update(res)
                    for cc, (p, ts, cs) in res.items(): fh[lab].write("%s %.9g %s%s\n" % (cc, p, " ".join("%.9g" % t for t in ts), (" cpu " + " ".join("%.9g" % t for t in cs)) if cs else ""))
                    fh[lab].flush()
            print("[compare] t=%d round %d/%d done (%.0f s, steal %d ticks)" % (a.threads, r + 1, a.rounds, time.time() - t0, steal() - st0), file=sys.stderr, flush=True)
    labs = [lab for lab, _ in libs]; base = labs[0]
    R = min(len(data[l]) for l in labs)
    def cases_for(lab): return [c for c in data[base][0] if all(c in data[l][i] for l in (base, lab) for i in range(R))]
    def med(lab, c, rounds):
        return statistics.median([min(data[lab][i][c][1]) for i in rounds])
    def cpumed(lab, c):
        v = [statistics.median(data[lab][i][c][2]) for i in range(R) if data[lab][i][c][2]]
        return statistics.median(v) if v else float("nan")
    def planmed(lab, c): return statistics.median([data[lab][i][c][0] for i in range(R)])
    cases = []
    allr = list(range(R)); rng = random.Random(12345)
    boots = [[rng.randrange(R) for _ in range(R)] for _ in range(a.boot)]
    rows = []; summ = []
    for lab in labs[1:]:
        cases = cases_for(lab)
        if not cases: summ.append("%s: no cases" % lab); continue
        sp = {c: med(base, c, allr) / med(lab, c, allr) for c in cases}
        gm = math.exp(sum(math.log(v) for v in sp.values()) / len(sp))
        bgm = []; bcase = {c: [] for c in cases}
        for br in boots:
            s = 0.0
            for c in cases:
                v = med(base, c, br) / med(lab, c, br); bcase[c].append(v); s += math.log(v)
            bgm.append(math.exp(s / len(cases)))
        bgm.sort(); lo, hi = bgm[int(0.025 * len(bgm))], bgm[int(0.975 * len(bgm)) - 1]
        for c in cases:
            v = sorted(bcase[c]); rows.append((lab, c, med(base, c, allr), med(lab, c, allr), sp[c], v[int(0.025 * len(v))], v[int(0.975 * len(v)) - 1], planmed(base, c), planmed(lab, c), cpumed(base, c), cpumed(lab, c)))
        for subset, pred in (("d", lambda c: c.startswith("d_")), ("s", lambda c: c.startswith("s_"))):
            sc = [c for c in cases if pred(c)]
            if sc: summ.append("%s vs %s [%s]: geomean speedup %.4f over %d cases" % (lab, base, subset, math.exp(sum(math.log(sp[c]) for c in sc) / len(sc)), len(sc)))
        summ.insert(0, "%s vs %s: geomean speedup %.4f (95%% CI %.4f-%.4f), %d cases, %d rounds, threads=%d" % (lab, base, gm, lo, hi, len(cases), R, a.threads))
    with open(os.path.join(a.out, "cmp-t%d.csv" % a.threads), "w") as f:
        f.write("lib,case,threads,t_base_s,t_lib_s,speedup,ci_lo,ci_hi,plan_base_s,plan_lib_s,cpu_base_s,cpu_lib_s\n")
        for r_ in rows: f.write("%s,%s,%d,%.6g,%.6g,%.4f,%.4f,%.4f,%.6g,%.6g,%.6g,%.6g\n" % (r_[0], r_[1], a.threads, *r_[2:]))
    with open(os.path.join(a.out, "summary-t%d.txt" % a.threads), "w") as f: f.write("\n".join(summ) + "\n")
    with open(os.path.join(a.out, "pin-t%d.txt" % a.threads), "w") as f: f.write("taskset -c %s\n" % cpus)
    print("\n".join(summ))

if __name__ == "__main__":
    main()
