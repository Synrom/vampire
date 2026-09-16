#!/usr/bin/env python3
"""
Sweeps nextThreshold from 1 to 7 across every ablation variant (and the
current implementation), rebuilding and running vampire on
condensed_detachment_4.p for each value, and records the avg/total time of
every "Clause Matcher next <approach>" TIME_TRACE entry into
build-vscode/ablation.csv.
"""
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO / "build-vscode"
PROBLEM = REPO.parent / "problems" / "condensed_detachment_4.p"

# Files containing the nextThreshold constant (master has none - it predates NEXT ops)
THRESHOLD_FILES = [
    REPO / "Indexing/ClauseCodeTree.cpp",
    REPO / "Indexing/ordering/ClauseCodeTree.cpp",
    REPO / "Indexing/simplify-canEnterLiteral-first/ClauseCodeTree.cpp",
    REPO / "Indexing/simplify-canEnterLiteral-second/ClauseCodeTree.cpp",
    REPO / "Indexing/remove-touchedSlots/ClauseCodeTree.cpp",
    REPO / "Indexing/save-bindings-directly/ClauseCodeTree.cpp",
    REPO / "Indexing/ilstruct-simplification/ClauseCodeTree.cpp",
]

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

THRESHOLD_PATTERN = re.compile(r"static const unsigned int nextThreshold = \d+;")

UNIT_TO_US = {"ns": 1e-3, "μs": 1.0, "ms": 1e3, "s": 1e6}


def set_threshold(value: int):
    for f in THRESHOLD_FILES:
        text = f.read_text()
        new_text, n = THRESHOLD_PATTERN.subn(
            f"static const unsigned int nextThreshold = {value};", text
        )
        if n == 0:
            raise RuntimeError(f"no nextThreshold occurrences found in {f}")
        f.write_text(new_text)


def build():
    r = subprocess.run(
        ["make", "-j4", "vampire"], cwd=BUILD_DIR, capture_output=True, text=True
    )
    if r.returncode != 0:
        print(r.stdout)
        print(r.stderr)
        raise RuntimeError("build failed")


def run_vampire() -> str:
    r = subprocess.run(
        [
            "./vampire",
            "--saturation_algorithm", "discount",
            "--time_statistics", "on",
            "--time_limit", "90",
            str(PROBLEM),
        ],
        cwd=BUILD_DIR,
        capture_output=True,
        text=True,
    )
    out = r.stdout + r.stderr
    if "Aborted" in out or "ASSERTION" in out.upper():
        print(out)
        raise RuntimeError("run aborted / assertion failure - stopping sweep")
    return out


LINE_RE = re.compile(
    r"Clause Matcher next (\S+)\s+\(total:\s*([\d.]+)\s*(\S+),\s*avg:\s*([\d.]+)\s*(\S+),"
)


def parse_times(output: str):
    times = {}
    for m in LINE_RE.finditer(output):
        name, total_val, total_unit, avg_val, avg_unit = m.groups()
        total_us = float(total_val) * UNIT_TO_US[total_unit]
        avg_us = float(avg_val) * UNIT_TO_US[avg_unit]
        times[name] = (avg_us, total_us)
    return times


def main():
    csv_path = BUILD_DIR / "ablation.csv"
    header = ["threshold"]
    for a in APPROACHES:
        header += [f"{a}_avg_us", f"{a}_total_us"]
    rows = []

    for threshold in range(1, 8):
        print(f"=== threshold={threshold} ===", file=sys.stderr)
        set_threshold(threshold)
        build()
        output = run_vampire()
        times = parse_times(output)
        missing = [a for a in APPROACHES if a not in times]
        if missing:
            print(output)
            raise RuntimeError(f"missing time trace entries for: {missing}")
        row = [str(threshold)]
        for a in APPROACHES:
            avg_us, total_us = times[a]
            row += [f"{avg_us:.3f}", f"{total_us:.3f}"]
        rows.append(row)
        csv_path.write_text(
            ",".join(header) + "\n" + "\n".join(",".join(r) for r in rows) + "\n"
        )
        print(f"threshold={threshold} done, wrote {csv_path}", file=sys.stderr)

    print(f"Wrote {csv_path}")


if __name__ == "__main__":
    main()
