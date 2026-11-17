#!/usr/bin/env python3
"""fit.py TIMES [FEATS]: fit the fast-mode ESTIMATE cost weights by NNLS.

TIMES is retime --retime output (median time per plan); FEATS defaults to TIMES and supplies the
FEAT lines (pass another retime output when the feature definition changed after timing).
Env: NEAR=k weights samples by exp(-k*(t/tmin-1)) toward near-optimal plans (calibration used 1);
LAM=x ridge-pulls the width x family weights toward the arithmetic weight; PREC=d|s fits one precision.
Prints in-sample and 5-fold CV regret (geomean of t(chosen)/t(best) over the sampled plans) and
W=..., the 36 weights for FFTCHODE_PLANW or for kFastCostWeights in src/fft.cpp."""
import sys, os, math, random
import numpy as np
from scipy.optimize import nnls
def load(f, med=True):
    out = {}; cur = None; pend = None
    for line in open(f):
        if line.startswith('PLAN '):
            a = line.split(' ', 2); cur = [a[2].rstrip()]
        elif line.startswith('FEAT '):
            a = line.split()
            if pend and cur is not None: out[(a[1], '\n'.join(cur))] = (pend, [float(x) for x in a[2:]])
            cur = None; pend = None
        elif line.startswith(' ') and cur is not None: cur.append(line.rstrip())
        else:
            a = line.split()
            if len(a) >= 5 and a[2] != 'FAIL':
                try: pend = float(a[4]) if med else min(float(x) for x in a[4:])
                except ValueError: pend = None
    return out
T0 = load(sys.argv[1]); F0 = load(sys.argv[2] if len(sys.argv) > 2 else sys.argv[1])
fam = ['n1', 'n2', 't1', 't2', 't3', 'q1', 'r']
names = ['arith', 'other', 'L1', 'L2', 'L3', 'DRAM', 'calls'] + ['%s.%s' % (w, f) for w in ('s', 'x', 'y', 'z') for f in fam] + ['big']
def fx(f):
    ar = f[0] + f[1] + 2 * f[2]; b = f[9:37]
    return [ar - sum(b), f[3], f[4], f[5], f[6], f[7], f[8]] + list(b) + [f[37]]
samples = {}
for (c, ps), (t, _) in T0.items():
    if (c, ps) in F0: samples.setdefault(c, []).append((t, fx(F0[(c, ps)][1])))
prec = os.environ.get('PREC')
cases = sorted(c for c in samples if not prec or c[0] == prec)
X = {c: np.array([x for t, x in samples[c]]) for c in cases}; T = {c: np.array([t for t, x in samples[c]]) for c in cases}
print('cases %d samples %d' % (len(cases), sum(len(T[c]) for c in cases)))
lam = float(os.environ.get('LAM', '0'))
def fitnnls(cs):
    A = np.vstack([X[c] / T[c].min() for c in cs]); b = np.concatenate([T[c] / T[c].min() for c in cs])
    # optional weighting toward near-optimal plans
    wt = np.concatenate([np.exp(-float(os.environ.get('NEAR', '0')) * (T[c] / T[c].min() - 1)) for c in cs])
    A = A * wt[:, None]; b = b * wt
    if lam > 0:
        n = A.shape[1]; idx = list(range(7, n - 1)); R = np.zeros((len(idx), n))
        cn = np.linalg.norm(A, axis=0); sc = lam * np.median(cn[cn > 0])
        for i, j in enumerate(idx): R[i, j] = sc; R[i, 0] = -sc
        A = np.vstack([A, R]); b = np.concatenate([b, np.zeros(len(idx))])
    w = nnls(A, b, maxiter=5000)[0]
    return w / w[0] if w[0] > 0 else w
def regret(cs, w):
    return math.exp(sum(math.log(T[c][int(np.argmin(X[c] @ w))] / T[c].min()) for c in cs) / len(cs))
w = fitnnls(cases)
print('NNLS ', ' '.join('%s=%.3g' % x for x in zip(names, w)))
print('NNLS in-sample regret %.4f' % regret(cases, w))
print('arith-only regret %.4f' % regret(cases, np.array([1, 0, 0, 0, 0, 0, 0] + [1] * 28 + [0], float)))
random.seed(1); cs = cases[:]; random.shuffle(cs); K = 5; ln = 0
for k in range(K):
    test = cs[k::K]; tr = [c for c in cs if c not in test]
    if not test or not tr: continue
    ln += math.log(regret(test, fitnnls(tr))) * len(test)
print('5-fold CV regret NNLS %.4f' % math.exp(ln / len(cs)))
print('W=' + ','.join('%.4g' % x for x in w))
bad = sorted(cases, key=lambda c: -T[c][int(np.argmin(X[c] @ w))] / T[c].min())[:10]
print('worst:', ' '.join('%s:%.2f' % (c, T[c][int(np.argmin(X[c] @ w))] / T[c].min()) for c in bad))
