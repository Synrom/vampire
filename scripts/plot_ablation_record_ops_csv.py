#!/usr/bin/env python3
"""
Renders build-vscode/ablation-record-ops.csv as a two-panel PDF: avg number
of executed CodeOps excluding NEXT (left) and avg number of executed NEXT
ops (right) per ClauseMatcher::next() call, across nextThreshold values 1..7.

Usage: build-profile/.venv/bin/python scripts/plot_ablation_record_ops_csv.py \
           [build-vscode/ablation-record-ops.csv] [build-vscode/ablation-record-ops.pdf]
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
    csv_path = Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "build-vscode/ablation-record-ops.csv"
    pdf_path = Path(sys.argv[2]) if len(sys.argv) > 2 else REPO / "build-vscode/ablation-record-ops.pdf"

    thresholds = []
    avg_codeops = {a: [] for a in APPROACHES}
    avg_nextops = {a: [] for a in APPROACHES}

    with open(csv_path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            thresholds.append(int(row["threshold"]))
            for a in APPROACHES:
                avg_codeops[a].append(float(row[f"{a}_avg_codeops"]))
                avg_nextops[a].append(float(row[f"{a}_avg_nextops"]))

    fig, (ax_ops, ax_next) = plt.subplots(1, 2, figsize=(12, 6.5))

    for a, color in zip(APPROACHES, COLORS):
        ax_ops.plot(thresholds, avg_codeops[a], marker="o", color=color, label=a, linewidth=2)
        ax_next.plot(thresholds, avg_nextops[a], marker="o", color=color, label=a, linewidth=2)

    ax_ops.set_title("Avg executed CodeOps (excl. NEXT) per next() call")
    ax_ops.set_xlabel("nextThreshold")
    ax_ops.set_ylabel("avg executed ops")
    ax_ops.grid(True, alpha=0.3)

    ax_next.set_title("Avg executed NEXT ops per next() call")
    ax_next.set_xlabel("nextThreshold")
    ax_next.set_ylabel("avg executed NEXT ops")
    ax_next.grid(True, alpha=0.3)

    handles, labels = ax_ops.get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", ncol=2, bbox_to_anchor=(0.5, -0.02), fontsize=9)
    fig.suptitle("NEXT-op threshold ablation: executed op counts")
    fig.tight_layout(rect=[0, 0.22, 1, 0.95])

    fig.savefig(pdf_path)
    print(f"Wrote {pdf_path}")


if __name__ == "__main__":
    main()
