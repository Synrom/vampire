#!/usr/bin/env python3
"""
Renders build-vscode/ablation.csv as a two-panel PDF: avg time (left) and
total time (right) of each ClauseMatcher::next approach, across nextThreshold
values 1..7.

Usage: build-profile/.venv/bin/python scripts/plot_ablation_csv.py \
           [build-vscode/ablation.csv] [build-vscode/ablation.pdf]
"""
import sys
import csv
from pathlib import Path

import matplotlib.pyplot as plt

REPO = Path(__file__).resolve().parent.parent

APPROACHES = [
    "current",
    "master",
    "ordering",
    "simplifyCanEnterFirst",
    "simplifyCanEnterSecond",
    "removeTouchedSlots",
    "saveBindingsDirectly",
    "ilstructSimplification",
]

# dataviz skill reference categorical palette (light mode), fixed assignment order
COLORS = [
    "#2a78d6",  # blue
    "#eb6834",  # orange
    "#1baf7a",  # aqua
    "#eda100",  # yellow
    "#e87ba4",  # magenta
    "#008300",  # green
    "#4a3aa7",  # violet
    "#e34948",  # red
]


def main():
    csv_path = Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "build-vscode/ablation.csv"
    pdf_path = Path(sys.argv[2]) if len(sys.argv) > 2 else REPO / "build-vscode/ablation.pdf"

    thresholds = []
    avg_us = {a: [] for a in APPROACHES}
    total_us = {a: [] for a in APPROACHES}

    with open(csv_path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            thresholds.append(int(row["threshold"]))
            for a in APPROACHES:
                avg_us[a].append(float(row[f"{a}_avg_us"]))
                total_us[a].append(float(row[f"{a}_total_us"]) / 1000.0)  # ms

    fig, (ax_avg, ax_total) = plt.subplots(1, 2, figsize=(12, 6.5))

    for a, color in zip(APPROACHES, COLORS):
        ax_avg.plot(thresholds, avg_us[a], marker="o", color=color, label=a, linewidth=2)
        ax_total.plot(thresholds, total_us[a], marker="o", color=color, label=a, linewidth=2)

    ax_avg.set_title("Avg time per ClauseMatcher::next call")
    ax_avg.set_xlabel("nextThreshold")
    ax_avg.set_ylabel("avg time (μs)")
    ax_avg.grid(True, alpha=0.3)

    ax_total.set_title("Total ClauseMatcher::next time")
    ax_total.set_xlabel("nextThreshold")
    ax_total.set_ylabel("total time (ms)")
    ax_total.grid(True, alpha=0.3)

    handles, labels = ax_avg.get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", ncol=2, bbox_to_anchor=(0.5, -0.02), fontsize=9)
    fig.suptitle("NEXT-op threshold ablation")
    fig.tight_layout(rect=[0, 0.22, 1, 0.95])

    fig.savefig(pdf_path)
    print(f"Wrote {pdf_path}")


if __name__ == "__main__":
    main()
