#!/usr/bin/env python3
"""End-to-end tests; takes a build directory containing both codetree targets."""
import csv
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    build = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="codetree-dataset-") as tmp:
        root = Path(tmp)
        expected = root / "expected.csv"
        run = root / "run"
        subprocess.run([build / "codetree-dataset-fixture", run, expected], check=True)
        tree = run / "tree-0"
        result = subprocess.run([build / "codetree-replay", tree, "--all", "--verify", "--repeat", "3"],
                                check=True, text=True, capture_output=True)
        expected_counts = {r["query"]: int(r["matches"]) for r in csv.DictReader(expected.open())}
        rows = list(csv.DictReader(io.StringIO(result.stdout)))
        assert 0 < len(rows) <= 256 < 5000
        assert len({r["query"] for r in rows}) == len(rows)
        assert all(int(r["retained"]) <= 4 for r in rows)
        assert all(int(r["matches"]) == 3 * expected_counts[r["query"]] for r in rows)
        assert any(int(r["query"]) > 4096 for r in rows)
        assert {r["sres"] for r in rows} == {"0", "1"}
        assert any(int(r["matches"]) == 0 for r in rows)
        assert any(int(r["matches"]) > 0 for r in rows)
        assert all(r["algorithm"] == "otter" and r["problem"] == "synthetic-fixture" for r in rows)
        population = sum(int(line.split()[2]) for line in (tree / "sample.ctd").read_text().splitlines() if line.startswith("B "))
        assert population == 5000
        # Same input sequence/seed must yield the same input dataset.
        run2 = root / "run2"
        subprocess.run([build / "codetree-dataset-fixture", run2, root / "expected2.csv"], check=True)
        for file in ("sample.ctd", "history.ctd"):
            assert (tree / file).read_bytes() == (run2 / "tree-0" / file).read_bytes()
        collection = subprocess.run([build / "codetree-replay", root, "--problem", "synthetic", "--algorithm", "otter"],
                                    check=True, capture_output=True, text=True)
        combined = list(csv.DictReader(io.StringIO(collection.stdout)))
        assert len(combined) == 2 * len(rows)
        assert len({r["dataset"] for r in combined}) == 2
        # A timer can leave an incomplete tail; only the committed prefix matters.
        with (tree / "history.ctd").open("a") as out:
            out.write("I deliberately incomplete")
        subprocess.run([build / "codetree-replay", tree, "--query", rows[0]["query"]], check=True, capture_output=True)
        # Truncation inside the committed prefix must fail clearly.
        damaged = root / "damaged"
        shutil.copytree(tree, damaged)
        (damaged / "history.ctd").write_text("T truncated\n")
        failed = subprocess.run([build / "codetree-replay", damaged], capture_output=True, text=True)
        assert failed.returncode != 0 and "Truncated history" in failed.stderr
        print(f"Passed: {len(rows)} sampled queries; round-trip counts, SAT oracle, typed/equality terms, "
              "removals, deterministic bounded sampling, collection/repeat/filter, checkpoint recovery and truncation.")


if __name__ == "__main__":
    main()
