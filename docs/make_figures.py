#!/usr/bin/env python3
"""Figures for docs/fftchode.tex, from the audited per-case CSV in bench/results/audit.

    python3 docs/make_figures.py        # writes docs/fig-scaling.pdf
"""
import collections, csv, math, os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
SWEEP = os.path.join(HERE, "..", "bench", "results", "audit", "sweep-percase.csv")
plt.rcParams.update({"font.size": 8, "font.family": "serif", "pdf.fonttype": 42})

wall = collections.defaultdict(dict)
for r in csv.DictReader(open(SWEEP)):
    wall[(r["lib"], int(r["threads"]))][r["case"]] = float(r["wall_s"])
base1 = wall[("base", 1)]


def gm(xs):
    xs = list(xs)
    return math.exp(sum(math.log(x) for x in xs) / len(xs))


def fig_scaling():
    libs = [
        ("fc-fast-measure", "fftchode fast, MEASURE", "k", "-", "s"),
        ("fc-fast", "fftchode fast, ESTIMATE", "k", "-", "o"),
        ("fc-bit", "fftchode bit-identical", "k", "-", "^"),
        ("fftw-pool-measure", "FFTW MEASURE, persistent pool", "tab:blue", "--", "s"),
        ("fftw-measure", "FFTW MEASURE, stock threads", "tab:blue", ":", "s"),
        ("aocl-pool-measure", "AOCL MEASURE, persistent pool", "tab:red", "--", "s"),
        ("mkl", "oneMKL (SSE2 on this CPU)", "tab:gray", "-.", "x"),
        ("ducc0-nofm", "ducc0 (IEEE build)", "tab:green", "--", "d"),
        ("base", "baseline (FFTW 3.3.10 scalar, ESTIMATE)", "tab:orange", "-", "v"),
    ]
    ts = (1, 2, 4, 8)
    fig, ax = plt.subplots(figsize=(6.5, 2.8))
    for lib, label, color, ls, mk in libs:
        ys = []
        for t in ts:
            d = wall[(lib, t)]
            ys.append(gm(base1[c] / d[c] for c in d if c in base1))
        ax.plot(ts, ys, ls, color=color, marker=mk, ms=3.5, lw=1, label=label)
    ax.set_xscale("log", base=2)
    ax.set_xticks(ts)
    ax.set_xticklabels([str(t) for t in ts])
    ax.set_xlabel("threads (1-4: one per core; 8: two per core)")
    ax.set_ylabel("speedup over baseline at 1 thread")
    ax.set_ylim(bottom=0)
    ax.grid(alpha=0.3, lw=0.5)
    ax.legend(frameon=False, fontsize=6.5, loc="center left", bbox_to_anchor=(1.01, 0.5))
    fig.tight_layout()
    fig.savefig(os.path.join(HERE, "fig-scaling.pdf"))


if __name__ == "__main__":
    fig_scaling()
