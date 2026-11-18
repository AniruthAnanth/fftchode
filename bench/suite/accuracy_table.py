#!/usr/bin/env python3
"""Accuracy table for the fairness audit: relative rms error vs long-double FFTW 3.3.10 on identical inputs (err mode, 1 thread).
  accuracy_table.py ERRDIR   (one <label>.txt per library: "case rel_rms rel_max" or "case FAIL ...")"""
import glob, math, os, sys
d = sys.argv[1]; R = {}
for f in sorted(glob.glob(os.path.join(d, "*.txt"))):
    lab = os.path.basename(f)[:-4]; R[lab] = {}
    for l in open(f):
        p = l.split()
        if len(p) == 3 and p[1] != "FAIL": R[lab][p[0]] = (float(p[1]), float(p[2]))
gm = lambda v: math.exp(sum(math.log(max(x, 1e-300)) for x in v) / len(v)) if v else float("nan")
print("| library | cases | double geomean rms | single geomean rms | worst double | worst single | geomean ratio to baseline-v0 (common cases) |")
print("|---|---|---|---|---|---|---|")
b = R["base"]
for lab in ["base"] + sorted(k for k in R if k != "base"):
    r = R[lab]; dd = [v[0] for c, v in r.items() if c[0] == "d"]; ss = [v[0] for c, v in r.items() if c[0] == "s"]
    cc = [c for c in r if c in b and b[c][0] > 0 and r[c][0] > 0]
    print("| %s | %d | %.3g | %.3g | %.3g | %.3g | %.3f |" % (lab, len(r), gm(dd), gm(ss), max(dd) if dd else float("nan"), max(ss) if ss else float("nan"), gm([r[c][0] / b[c][0] for c in cc])))
