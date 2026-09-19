#!/usr/bin/env python3
"""
Turns the result of one benchexec run of codetree-compare.xml into a single CSV with one row
per problem: how the current and the master code tree performed on exactly the same queries.

    extract_csv.py RESULTS.xml.bz2 LOGFILES.zip OUT.csv [EXCLUDED.txt]

Problems that should be ignored in the statistics get a reason in the column `excluded`
(they are still listed): "listed" if they are in EXCLUDED.txt (default: excluded_problems.txt
next to this script), "final-query-hang" if a query was still running when Vampire terminated,
had been running for at least HANG_MIN_MS and accounts for at least HANG_MIN_SHARE of the
time that tree spent in ClauseMatcher::next. Such a hanging last query is an artifact of running
the two trees one after the other: the tree that is first in the order eats the whole time limit
and the other one never sees that query.

The numbers are taken from the logs of Vampire (--time_statistics on --statistics full):
the flat time profile gives total time, call count and instructions of
`ClauseCodeTree::ClauseMatcher::next (current|master)`, and the statistics group
`CODE TREE COMPARISON` gives how often each outcome pair occurred.

Note: Vampire prints durations truncated to a coarse unit (>=10 s: whole seconds,
>=10 ms: ms, >=10 us: us, else ns), so times of very short runs are only accurate to
a few percent. Instruction counts are exact.
"""
import bz2
import csv
import os
import re
import sys
import zipfile
import xml.etree.ElementTree as ET

IMPLS = ("current", "master")
TREE_OF_INFLIGHT = {1: "current", 2: "master"}
HANG_MIN_MS = 1000
HANG_MIN_SHARE = 0.25
OUTCOMES = ("nothing", "subsumption", "subsumption resolution")
UNIT_NS = {"s": 1_000_000_000, "ms": 1_000_000, "μs": 1_000, "ns": 1}

# only the flat profile at the end of the output has its lines start with exactly this indent
# (in the tree, children of `codetree forward subsumption` are indented with "│")
NEXT_RE = re.compile(
    r"^  [├└]──\[\s*\d+%\]\s+ClauseCodeTree::ClauseMatcher::next \((current|master)\)\s+"
    r"\(total:\s*(\d+) (s|ms|μs|ns), avg:\s*\d+ \S+, cnt:\s*(\d+), instr:\s*(-?\d+)\)"
)
MATRIX_RE = re.compile(
    r"^% current: (nothing|subsumption|subsumption resolution) / master: "
    r"(nothing|subsumption|subsumption resolution)\s+\|\s+(\d+)\s*$"
)


INFLIGHT_TREE_RE = re.compile(r"^% in-flight query at termination: tree .*\|\s+(\d+)\s*$")
INFLIGHT_MS_RE = re.compile(r"^% in-flight query at termination: running for \[ms\]\s+\|\s+(\d+)\s*$")


def matrix_col(cur, mas):
    return f"cur_{cur.replace(' ', '_')}__master_{mas.replace(' ', '_')}"


def parse_log(text):
    """Returns a dict of the measurements found in one Vampire log."""
    out = {}
    for line in text.splitlines():
        m = NEXT_RE.match(line)
        if m:
            impl, total, unit, cnt, instr = m.groups()
            out[f"calls_{impl}"] = int(cnt)
            out[f"time_ns_{impl}"] = int(total) * UNIT_NS[unit]
            out[f"instr_{impl}"] = int(instr)
            continue
        m = INFLIGHT_TREE_RE.match(line)
        if m:
            out["inflight_tree"] = TREE_OF_INFLIGHT.get(int(m.group(1)), "")
            continue
        m = INFLIGHT_MS_RE.match(line)
        if m:
            out["inflight_ms"] = int(m.group(1))
            continue
        m = MATRIX_RE.match(line)
        if m:
            out[matrix_col(m.group(1), m.group(2))] = int(m.group(3))
    return out


def read_excluded(path):
    names = set()
    with open(path) as f:
        for line in f:
            line = line.split("#")[0].strip()
            if line:
                names.add(line.split()[0])
    return names


def exclusion_reason(row, listed):
    if row["problem"] in listed:
        return "listed"
    tree, ms = row.get("inflight_tree"), row.get("inflight_ms")
    if tree and ms is not None and ms >= HANG_MIN_MS:
        total_ms = row.get(f"time_ns_{tree}", 0) / 1e6
        if total_ms > 0 and ms >= HANG_MIN_SHARE * total_ms:
            return "final-query-hang"
    return ""


def main(results_file, logs_zip, out_csv, excluded_file=None):
    listed = read_excluded(excluded_file or os.path.join(os.path.dirname(os.path.abspath(__file__)), "excluded_problems.txt"))
    root = ET.parse(bz2.open(results_file)).getroot()
    zf = zipfile.ZipFile(logs_zip)
    log_of = {}  # problem file name -> path inside the zip
    for n in zf.namelist():
        if n.endswith(".log"):
            # <run>.logfiles/<rundefinition>.<problem file>.log
            log_of[n.split("/")[-1].split(".", 1)[1][: -len(".log")]] = n

    matrix_cols = [matrix_col(c, m) for c in OUTCOMES for m in OUTCOMES]
    cols = (
        ["problem", "status", "szs_status", "cputime_s", "instructions_million_total"]
        + [f"calls_{i}" for i in IMPLS]
        + [f"time_ns_{i}" for i in IMPLS]
        + [f"avg_time_ns_{i}" for i in IMPLS]
        + [f"instr_{i}" for i in IMPLS]
        + [f"avg_instr_{i}" for i in IMPLS]
        + ["inflight_tree", "inflight_ms", "excluded"]
        + matrix_cols
    )

    n_rows = n_missing_log = 0
    n_excluded = {}
    excluded_rows = []
    with open(out_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        for run in root.iter("run"):
            col = {c.get("title"): c.get("value") for c in run.findall("column")}
            fname = run.get("name").split("/")[-1]
            row = {
                "problem": fname[: -len(".p")] if fname.endswith(".p") else fname,
                "status": col.get("status"),
                "szs_status": col.get("szs-status"),
                "cputime_s": (col.get("cputime") or "").rstrip("s"),
                "instructions_million_total": col.get("instruction-count"),
            }
            if fname in log_of:
                row.update(parse_log(zf.read(log_of[fname]).decode(errors="replace")))
            else:
                n_missing_log += 1
            for i in IMPLS:
                cnt = row.get(f"calls_{i}")
                if cnt:
                    row[f"avg_time_ns_{i}"] = row[f"time_ns_{i}"] / cnt
                    row[f"avg_instr_{i}"] = row[f"instr_{i}"] / cnt
            row["excluded"] = exclusion_reason(row, listed)
            w.writerow(row)
            n_rows += 1
            n_excluded[row["excluded"]] = n_excluded.get(row["excluded"], 0) + 1
            if row["excluded"]:
                excluded_rows.append(row)
    print(f"{n_rows} problems written to {out_csv} ({n_missing_log} without log); excluded: "
          f"{n_excluded.get('listed', 0)} listed, {n_excluded.get('final-query-hang', 0)} final-query-hang")
    write_excluded_list(excluded_rows, os.path.splitext(out_csv)[0] + "_excluded.csv")


def write_excluded_list(rows, path):
    """Writes (and prints) the list of all ignored problems, hangs first."""
    cols = ["problem", "excluded", "status", "inflight_tree", "inflight_ms",
            "time_ns_current", "time_ns_master", "calls_current", "calls_master"]
    rows = sorted(rows, key=lambda r: (r["excluded"] != "final-query-hang", r["problem"]))
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)
    print(f"\nignored problems ({len(rows)}), also written to {path}:")
    for r in rows:
        hang = (f"; query on {r['inflight_tree']} tree still running after {r['inflight_ms']} ms"
                if r.get("inflight_tree") else "")
        print(f"  {r['problem']:<16} {r['excluded']:<17} status {r['status']}{hang}")


if __name__ == "__main__":
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    main(*sys.argv[1:])
