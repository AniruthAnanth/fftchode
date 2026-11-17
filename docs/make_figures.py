#!/usr/bin/env python3
"""Figures for docs/fftchode.tex, from the committed CSVs in bench/results.

    python3 docs/make_figures.py        # writes docs/fig-*.pdf
"""
import csv, math, os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "..", "bench", "results")
plt.rcParams.update({"font.size": 8, "font.family": "serif", "pdf.fonttype": 42})


def speedups(name, lib):
    out = {}
    for r in csv.DictReader(open(os.path.join(RES, name))):
        if r["lib"] == lib:
            out[r["case"]] = float(r["speedup"])
    return out


def gm(xs):
    return math.exp(sum(math.log(x) for x in xs) / len(xs))


def fig_distribution():
    fig, axes = plt.subplots(1, 2, figsize=(6.5, 2.3), sharey=True)
    for ax, t in zip(axes, (1, 8)):
        for lib, label, style in (("final2", "baseline mode", "-"), ("final2-fast", "fast mode", "--")):
            s = sorted(speedups("frozen-t%d.csv" % t, lib).values())
            ax.plot(range(1, len(s) + 1), s, style, color="black", lw=1,
                    label="%s (geomean %.2f)" % (label, gm(s)))
        ax.axhline(1, color="gray", lw=0.5)
        ax.set_yscale("log")
        ax.set_xlabel("case rank (frozen suite, %d)" % len(s))
        ax.set_title("%d thread%s" % (t, "" if t == 1 else "s"))
        ax.legend(frameon=False, loc="upper left")
    axes[0].set_ylabel("speedup over baseline")
    fig.tight_layout()
    fig.savefig(os.path.join(HERE, "fig-distribution.pdf"))


if __name__ == "__main__":
    fig_distribution()
