#!/usr/bin/env python3
"""
Plots the CSV produced by extract_csv.py: one row, two panels, each with two box plots
(optimized = current code tree, master). Every data point behind a box is one problem.

    plot_codetree_compare.py IN.csv OUT.pdf

  left:  average time per ClauseMatcher::next call (per problem)
  right: average instructions per ClauseMatcher::next call (per problem)

Only problems in which both trees were actually called are used, and problems with a reason in
the CSV column `excluded` (see extract_csv.py) are ignored.
Also prints a small text summary (medians, means, paired ratio) to stdout.
"""
import csv
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Patch
from matplotlib.ticker import FuncFormatter, NullFormatter

# categorical slots 1 and 2 of the reference palette (validated: validate_palette.js, light)
SERIES = [("optimized (current)", "#2a78d6"), ("master", "#eb6834")]
SURFACE, INK, INK_2, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"


def load(path):
    cur_t, mas_t, cur_i, mas_i = [], [], [], []
    n_all = 0
    n_excl = {}
    with open(path) as f:
        for r in csv.DictReader(f):
            n_all += 1
            if r.get("excluded"):
                n_excl[r["excluded"]] = n_excl.get(r["excluded"], 0) + 1
                continue
            if not (r["calls_current"] and r["calls_master"]):
                continue
            if int(r["calls_current"]) == 0 or int(r["calls_master"]) == 0:
                continue
            cur_t.append(float(r["avg_time_ns_current"]) / 1000)  # -> µs
            mas_t.append(float(r["avg_time_ns_master"]) / 1000)
            cur_i.append(float(r["avg_instr_current"]))
            mas_i.append(float(r["avg_instr_master"]))
    return n_all, n_excl, [np.array(a) for a in (cur_t, mas_t, cur_i, mas_i)]


def fmt(v, unit):
    if unit == "time":  # µs
        return f"{v:.2f} µs"
    return f"{v:,.0f}"


def panel(ax, cur, mas, title, ylabel, unit):
    data = [cur, mas]
    bp = ax.boxplot(
        data,
        positions=[1, 2],
        widths=0.5,
        whis=1.5,
        showfliers=False,  # outliers would only smear the log axis
        showmeans=True,
        patch_artist=True,
        manage_ticks=False,
        medianprops=dict(color=SURFACE, linewidth=2),
        meanprops=dict(marker="D", markerfacecolor=SURFACE, markeredgecolor=INK, markersize=5),
        whiskerprops=dict(linewidth=1.2),
        capprops=dict(linewidth=1.2),
        boxprops=dict(linewidth=1.2),
    )
    for i, (name, color) in enumerate(SERIES):
        bp["boxes"][i].set(facecolor=color, edgecolor=color)
        bp["whiskers"][2 * i].set(color=color)
        bp["whiskers"][2 * i + 1].set(color=color)
        bp["caps"][2 * i].set(color=color)
        bp["caps"][2 * i + 1].set(color=color)

    ax.set_yscale("log")
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:,.0f}" if v >= 1 else f"{v:g}"))
    ax.yaxis.set_minor_formatter(NullFormatter())
    ax.set_xlim(0.4, 2.6)
    ax.set_xticks([1, 2])
    ax.set_xticklabels([s[0] for s in SERIES], color=INK)
    ax.set_ylabel(ylabel, color=INK_2)
    ax.set_title(title, loc="left", color=INK, fontsize=11, fontweight="bold")
    ax.set_facecolor(SURFACE)
    ax.grid(axis="y", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    for s in ("left", "bottom"):
        ax.spines[s].set_color(GRID)
    ax.tick_params(colors=INK_2, length=3)

    # direct labels: median (and n once, in the caption), placed beside each box
    for x, d in ((1, cur), (2, mas)):
        ax.annotate(
            f"median {fmt(np.median(d), unit)}\nmean {fmt(d.mean(), unit)}",
            (x + 0.28, np.median(d)),
            va="center",
            ha="left",
            fontsize=8,
            color=INK_2,
        )


def summary(name, cur, mas, unit):
    ratio = cur / mas
    print(f"{name}: n={len(cur)}")
    for label, d in (("current", cur), ("master", mas)):
        q1, med, q3 = np.percentile(d, [25, 50, 75])
        print(f"  {label:8s} median {fmt(med, unit):>12s}  mean {fmt(d.mean(), unit):>12s}"
              f"  IQR [{fmt(q1, unit)}, {fmt(q3, unit)}]")
    print(f"  per-problem current/master: median {np.median(ratio):.3f}, "
          f"geometric mean {np.exp(np.log(ratio).mean()):.3f}, "
          f"current faster in {(ratio < 1).mean() * 100:.1f}% of problems")


def main(csv_path, pdf_path):
    n_all, n_excl, (cur_t, mas_t, cur_i, mas_i) = load(csv_path)
    n = len(cur_t)
    if n == 0:
        sys.exit("no problem with calls to both code trees in " + csv_path)

    fig, axes = plt.subplots(1, 2, figsize=(11, 4.6), facecolor=SURFACE)
    panel(axes[0], cur_t, mas_t, "Average time per call of ClauseMatcher::next",
          "time per call in µs (log scale)", "time")
    panel(axes[1], cur_i, mas_i, "Average instructions per call of ClauseMatcher::next",
          "instructions per call (log scale)", "instr")

    fig.legend(
        handles=[Patch(facecolor=c, label=n_) for n_, c in SERIES],
        loc="upper right", ncol=2, frameon=False, fontsize=9, labelcolor=INK,
    )
    fig.text(
        0.01, 0.01,
        f"One data point per problem: {n} of {n_all} TPTP problems where both code trees were queried"
        + (f" ({sum(n_excl.values())} ignored: " + ", ".join(f"{v} {k}" for k, v in sorted(n_excl.items())) + ")" if n_excl else "")
        + ". Box: quartiles, line: median, diamond: mean, whiskers: 1.5×IQR (outliers not drawn).",
        fontsize=7.5, color=INK_2, ha="left", va="bottom",
    )
    fig.tight_layout(rect=(0, 0.04, 1, 0.94))
    fig.savefig(pdf_path, facecolor=SURFACE)
    print(f"wrote {pdf_path}; ignored problems: {n_excl or 'none'}")
    summary("avg time per call", cur_t, mas_t, "time")
    summary("avg instructions per call", cur_i, mas_i, "instr")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(*sys.argv[1:])
