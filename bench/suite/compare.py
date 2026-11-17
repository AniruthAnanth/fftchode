#!/usr/bin/env python3
"""Interleaved A/B(/C...) timing of the frozen suite on one machine (FFTE-6).

  compare.py --suite bench/suite/frozen.txt --threads 1 --rounds 10 --out DIR  base=BIN cand=BIN [...]

Each round runs every binary once over the whole suite (order rotates every round), each
binary pinned with taskset (--cpus, default: cpu 2 for 1 thread, all cpus otherwise).
Per case and binary, the statistic is the median over all batches of all rounds. The
speedup of X over the first binary is median_t(first)/median_t(X); the suite figure is the
geometric mean over cases. 95% CIs come from a paired bootstrap over rounds.
Writes DIR/raw-<label>-t<threads>.txt (all rounds) and DIR/cmp-t<threads>.csv.
"""
import argparse, math, os, random, statistics, subprocess, sys, time

def run(bin_, suite, threads, cpus, batches, tmin):
    cmd = ["taskset", "-c", cpus, bin_, "--suite", suite, "--threads", str(threads), "--batches", str(batches), "--tmin", str(tmin)]
    out = subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
    res = {}
    for line in out.splitlines():
        f = line.split()
        if len(f) < 5 or f[2] == "FAIL": continue
        res[f[0]] = (float(f[2]), [float(x) for x in f[4:]])
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
    cpus = a.cpus or ("2" if a.threads == 1 else "0-%d" % (os.cpu_count() - 1))
    os.makedirs(a.out, exist_ok=True)
    data = {lab: [] for lab, _ in libs}   # per lib: list over rounds of {case: (plan_s, [t...])}
    raws = {lab: os.path.join(a.out, "raw-%s-t%d.txt" % (lab, a.threads)) for lab, _ in libs}
    if a.analyze:
        for lab, _ in libs:
            cur = None
            for line in open(raws[lab]):
                if line.startswith("#round"): cur = {}; data[lab].append(cur); continue
                f = line.split(); cur[f[0]] = (float(f[1]), [float(x) for x in f[2:]])
    else:
        fh = {lab: open(raws[lab], "w") for lab, _ in libs}
        t0 = time.time()
        for r in range(a.rounds):
            k = r % len(libs); order = libs[k:] + libs[:k]
            for lab, b in order:
                res = run(b, a.suite, a.threads, cpus, a.batches, a.tmin)
                data[lab].append(res)
                fh[lab].write("#round %d\n" % r)
                for c, (p, ts) in res.items(): fh[lab].write("%s %.9g %s\n" % (c, p, " ".join("%.9g" % t for t in ts)))
                fh[lab].flush()
            print("[compare] t=%d round %d/%d done (%.0f s)" % (a.threads, r + 1, a.rounds, time.time() - t0), file=sys.stderr, flush=True)
    labs = [lab for lab, _ in libs]; base = labs[0]
    R = min(len(data[l]) for l in labs)
    cases = [c for c in data[base][0] if all(c in data[l][i] for l in labs for i in range(R))]
    def med(lab, c, rounds):
        return statistics.median([t for i in rounds for t in data[lab][i][c][1]])
    def planmed(lab, c): return statistics.median([data[lab][i][c][0] for i in range(R)])
    allr = list(range(R)); rng = random.Random(12345)
    boots = [[rng.randrange(R) for _ in range(R)] for _ in range(a.boot)]
    rows = []; summ = []
    for lab in labs[1:]:
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
            v = sorted(bcase[c]); rows.append((lab, c, med(base, c, allr), med(lab, c, allr), sp[c], v[int(0.025 * len(v))], v[int(0.975 * len(v)) - 1], planmed(base, c), planmed(lab, c)))
        for subset, pred in (("d", lambda c: c.startswith("d_")), ("s", lambda c: c.startswith("s_"))):
            sc = [c for c in cases if pred(c)]
            if sc: summ.append("%s vs %s [%s]: geomean speedup %.4f over %d cases" % (lab, base, subset, math.exp(sum(math.log(sp[c]) for c in sc) / len(sc)), len(sc)))
        summ.insert(0, "%s vs %s: geomean speedup %.4f (95%% CI %.4f-%.4f), %d cases, %d rounds, threads=%d" % (lab, base, gm, lo, hi, len(cases), R, a.threads))
    with open(os.path.join(a.out, "cmp-t%d.csv" % a.threads), "w") as f:
        f.write("lib,case,threads,t_base_s,t_lib_s,speedup,ci_lo,ci_hi,plan_base_s,plan_lib_s\n")
        for r_ in rows: f.write("%s,%s,%d,%.6g,%.6g,%.4f,%.4f,%.4f,%.6g,%.6g\n" % (r_[0], r_[1], a.threads, *r_[2:]))
    with open(os.path.join(a.out, "summary-t%d.txt" % a.threads), "w") as f: f.write("\n".join(summ) + "\n")
    print("\n".join(summ))

if __name__ == "__main__":
    main()
