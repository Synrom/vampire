#!/usr/bin/env python3
"""
Sweeps nextThreshold from 1 to 7 across every ablation variant (and the
current implementation), rebuilding and running vampire on
condensed_detachment_4.p for each value, and records the avg number of
executed CodeOps (excluding NEXT) and avg number of executed NEXT ops per
ClauseMatcher::next() call into build-vscode/ablation-record-ops.csv.
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

# approach name -> RSTAT stat-name suffix used in the instrumentation
APPROACHES = {
    "current": "current",
    "master": "master",
    "ordering": "ordering",
    "simplifyCanEnterFirst": "simplifyCanEnterFirst",
    "simplifyCanEnterSecond": "simplifyCanEnterSecond",
    "removeTouchedSlots": "removeTouchedSlots",
    "saveBindingsDirectly": "saveBindingsDirectly",
    "ilstructSimplification": "ilstructSimplification",
}

THRESHOLD_PATTERN = re.compile(r"static const unsigned int nextThreshold = \d+;")

CALLS_RE = re.compile(r"% clause matcher calls (\S+): (\d+)")
CODEOPS_RE = re.compile(r"% executed code ops (\S+): (\d+)")
NEXTOPS_RE = re.compile(r"% executed next ops (\S+): (\d+)")


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


def parse_counts(output: str):
    calls = {m.group(1): int(m.group(2)) for m in CALLS_RE.finditer(output)}
    codeops = {m.group(1): int(m.group(2)) for m in CODEOPS_RE.finditer(output)}
    nextops = {m.group(1): int(m.group(2)) for m in NEXTOPS_RE.finditer(output)}
    return calls, codeops, nextops


def main():
    csv_path = BUILD_DIR / "ablation-record-ops.csv"
    header = ["threshold"]
    for a in APPROACHES:
        header += [f"{a}_avg_codeops", f"{a}_avg_nextops"]
    rows = []

    for threshold in range(1, 8):
        print(f"=== threshold={threshold} ===", file=sys.stderr)
        set_threshold(threshold)
        build()
        output = run_vampire()
        calls, codeops, nextops = parse_counts(output)
        missing = [a for a in APPROACHES if a not in calls or a not in codeops or a not in nextops]
        if missing:
            print(output)
            raise RuntimeError(f"missing op-count stats for: {missing}")
        row = [str(threshold)]
        for a in APPROACHES:
            n = calls[a]
            avg_codeops = codeops[a] / n if n else 0.0
            avg_nextops = nextops[a] / n if n else 0.0
            row += [f"{avg_codeops:.3f}", f"{avg_nextops:.3f}"]
        rows.append(row)
        csv_path.write_text(
            ",".join(header) + "\n" + "\n".join(",".join(r) for r in rows) + "\n"
        )
        print(f"threshold={threshold} done, wrote {csv_path}", file=sys.stderr)

    print(f"Wrote {csv_path}")


if __name__ == "__main__":
    main()
