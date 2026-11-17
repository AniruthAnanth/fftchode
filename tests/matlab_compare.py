#!/usr/bin/env python3
"""Compare MATLAB R2026b fft ground truth against fftchode.cpp through tests/mlfft.

usage: matlab_compare.py <ground-truth dir> <mlfft binary> [--md rank|seq] [--scale mul|div] [--csv out.csv]

The ground-truth directory is the MATLAB handoff (nt1/ and nt10/ with manifest.json,
index.csv, pack_*.bin); it is read at run time and never stored in this repository.
"""
import argparse, csv, json, os, re, subprocess, sys, tempfile
import numpy as np

def minstd(seed, count):
    s = 1 + (seed * 7919) % 2147483646
    out = np.empty(count)
    for i in range(count):
        s = (48271 * s) % 2147483647
        out[i] = (s >> 7) / 16777216.0 - 0.5
    return out

def load(gt, sub):
    man = json.load(open(os.path.join(gt, sub, "manifest.json")))
    idx = {}
    with open(os.path.join(gt, sub, "index.csv"), newline="") as f:
        for row in csv.DictReader(f):
            idx[(int(row["id"]), row["role"])] = row
    return man, idx

def array(gt, sub, row):
    if not row["pack"]:
        return None
    with open(os.path.join(gt, sub, row["pack"]), "rb") as f:
        f.seek(int(row["offset"]))
        return f.read(int(row["bytes"]))

def first_nonsingleton(shape):
    for k, v in enumerate(shape):
        if v != 1:
            return k
    return 0

inv_default_dim = True

def ops_for(call, shape, md, inverse=None, fwd_shape=None, real_input=False):
    """MATLAB call -> list of (kind, dims, lens). inverse: None, 'I' or 'S' (the inverse of call)."""
    m = re.fullmatch(r"(i?)(fft|fft2|fftn)\(x(.*)\)", call)
    inv, fn, args = m.group(1), m.group(2), m.group(3)
    sym = "'symmetric'" in args
    args = [a.strip() for a in args.replace(",'symmetric'", "").split(",") if a.strip()]
    if fn == "fft":
        n = -1 if not args or args[0] == "[]" else int(args[0])
        d = int(args[1]) - 1 if len(args) > 1 else first_nonsingleton(shape)
        dims, lens = [d], [n]
    elif fn == "fft2":
        dims = [0, 1]
        lens = [int(args[0]), int(args[1])] if args else [-1, -1]
    else:
        if args:
            lens = [int(v) for v in args[0].strip("[]").split()]
            dims = list(range(len(lens)))
        else:
            dims = list(range(len(shape)))
            lens = [-1] * len(dims)
    if inverse:  # inverse of the forward call, applied to MATLAB's forward output
        kind, lens = inverse, [-1] * len(dims)
        if fn == "fft" and inv_default_dim:
            dims = [first_nonsingleton(fwd_shape)]
    else:
        kind = ("S" if sym else "I") if inv else "F"
    if len(dims) == 1:
        return [(kind, dims, lens)]
    if md == "matlab":
        # observed: one 1-D pass per dim in ascending order; fft2 of an N-D (N>2) array and the
        # 'symmetric' 2-D inverse go in descending order (c2r on the last transformed dim)
        rev = (fn == "fft2" and len(shape) > 2) or kind == "S"
        md = "seqrev" if rev else "seq"
    if md.endswith("rev"):
        dims, lens, md = dims[::-1], lens[::-1], md[:-3]
    if md == "half":
        # MATLAB-style multi-dim: work on the half spectrum of the first transformed dim
        full = [l if l >= 0 else (fwd_shape or shape)[d] for d, l in zip(dims, lens)]
        if kind == "F" and real_input:
            return [("H", dims[:1], lens[:1])] + [("F", [d], [l]) for d, l in zip(dims[1:], lens[1:])] + \
                   [("E", dims, [full[0]])]
        if kind == "S":
            n0 = full[0]
            return [("T", dims[:1], [n0 // 2 + 1])] + [("I", [d], [-1]) for d in dims[1:]] + \
                   [("C", dims[:1], [n0])]
        return [(kind, dims, lens)]
    if md == "rank":
        return [("R" + kind, dims, lens)]
    if kind == "S":   # complex inverse along the trailing dims, then c2r along the first
        return [("I", dims[1:], lens[1:]), ("S", dims[:1], lens[:1])][::-1][::-1]
    return [(kind, dims, lens)]

def opstr(ops):
    return " ".join("%s:%s:%s" % (k, ",".join(map(str, d)), ",".join(map(str, l))) for k, d, l in ops)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gt"); ap.add_argument("mlfft")
    ap.add_argument("--md", default="matlab"); ap.add_argument("--scale", default="div")
    ap.add_argument("--csv")
    a = ap.parse_args()
    man1, idx1 = load(a.gt, "nt1")
    man10, idx10 = load(a.gt, "nt10")
    tmp = tempfile.mkdtemp()
    proc = subprocess.Popen([a.mlfft], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    results = []
    for case in man1["cases"]:
        if case["error"]:
            results.append((case, "error", "skip", "MATLAB raised " + case["error"].split(":")[1] if ":" in case["error"] else "error", 1))
            continue
        cid, cls = case["id"], case["class"]
        dt = np.float64 if cls == "double" else np.float32
        shape = [int(v) for v in case["in_shape"]]
        cplx = not case["in_isreal"]
        N = int(np.prod(shape))
        if case["gen"] == "gen":
            v = minstd(cid, N * (2 if cplx else 1)).astype(dt)
            xin = v.tobytes()
        else:
            xin = array(a.gt, "nt1", idx1[(cid, "in")])
        roles = {o["role"]: o for o in case["outputs"]}
        fwd_bytes = array(a.gt, "nt1", idx1[(cid, "fwd")]) if "fwd" in roles else None
        jobs = []
        if "fwd" in roles:
            jobs.append(("fwd", shape, cplx, xin, ops_for(case["call"], shape, a.md, real_input=not cplx)))
        for role, kind in (("inv", "I"), ("inv_symmetric", "S")):
            if role in roles:
                fo = roles["fwd"]
                jobs.append((role, [int(v) for v in fo["shape"]], not fo["isreal"], fwd_bytes,
                             ops_for(case["call"], shape, a.md, inverse=kind, fwd_shape=[int(v) for v in fo["shape"]])))
        for role, sh, cx, data, ops in jobs:
            for nt, idx, sub in ((1, idx1, "nt1"), (10, idx10, "nt10")):
                row = idx[(cid, role)]
                if nt == 10 and not row["pack"]:
                    continue  # identical to nt1
                exp = array(a.gt, sub, row)
                inp = os.path.join(tmp, "in.bin"); outp = os.path.join(tmp, "out.bin")
                if nt == 10 and role != "fwd" and idx10[(cid, "fwd")]["pack"]:
                    data = array(a.gt, "nt10", idx10[(cid, "fwd")])  # inverse of the 10-thread forward
                open(inp, "wb").write(data)
                line = "%s %d %d %s %s %s %d %s %d %s\n" % ("d" if cls == "double" else "s", int(cx), len(sh),
                        " ".join(map(str, sh)), inp, outp, nt, a.scale, len(ops), opstr(ops))
                proc.stdin.write(line); proc.stdin.flush()
                resp = proc.stdout.readline().split()
                if not resp:
                    raise SystemExit("mlfft failed on case %d %s: %s" % (cid, role, line))
                got = open(outp, "rb").read()
                exp_real = int(row["isreal"])
                status, detail = compare(got, int(resp[0]), exp, exp_real, dt)
                results.append((case, role, status, detail, nt))
    proc.stdin.close(); proc.wait()
    summarize(results, a.csv)

def compare(got, got_real, exp, exp_real, dt):
    if got == exp and got_real == exp_real:
        return "bit-exact", ""
    g = np.frombuffer(got, dt).astype(np.float64)
    e = np.frombuffer(exp, dt).astype(np.float64)
    if got_real != exp_real or g.size != e.size:
        return "layout", "isreal %d vs %d, %d vs %d values" % (got_real, exp_real, g.size, e.size)
    gi = np.frombuffer(got, np.int64 if dt == np.float64 else np.int32).astype(np.int64)
    ei = np.frombuffer(exp, np.int64 if dt == np.float64 else np.int32).astype(np.int64)
    nan = np.isnan(g) & np.isnan(e)
    diff = np.where(nan, 0, np.abs(gi - ei))
    if diff.max() == 0:
        return "bit-exact", "NaN payload differs"
    scale = np.max(np.abs(e[np.isfinite(e)])) if np.isfinite(e).any() else 1
    rel = np.max(np.abs(g - e)[~nan]) / scale
    return "ulp", "%d/%d values differ, max %d ulp, max rel err %.2g" % ((diff > 0).sum(), diff.size, diff.max(), rel)

def summarize(results, csvpath):
    from collections import Counter
    c = Counter((r[0]["group"], r[2]) for r in results)
    groups = sorted({r[0]["group"] for r in results})
    for g in groups:
        print("%-11s" % g, " ".join("%s=%d" % (s, c[(g, s)]) for s in ("bit-exact", "ulp", "layout", "skip") if c[(g, s)]))
    tot = Counter(r[2] for r in results)
    print("TOTAL", dict(tot))
    for case, role, status, detail, nt in results:
        if status not in ("bit-exact", "skip"):
            print("  %5d %-10s %-6s %-5s %-14s nt%d %-22s %s" % (case["id"], case["group"], case["class"], case["input"], role, nt,
                                                      case["call"] + " " + "x".join(map(str, case["in_shape"])), detail))
    if csvpath:
        with open(csvpath, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["id", "group", "call", "class", "input", "shape", "role", "nthreads", "status", "detail"])
            for case, role, status, detail, nt in results:
                w.writerow([case["id"], case["group"], case["call"], case["class"], case["input"],
                            "x".join(map(str, case["in_shape"])), role, nt, status, detail])

if __name__ == "__main__":
    main()
