#!/usr/bin/env python3
"""End-to-end tests; takes a build directory containing both codetree targets."""
import csv
import io
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    build = Path(sys.argv[1]).resolve()
    os.environ.pop("VAMPIRE_CT_SAMPLES_PER_BUCKET", None)
    os.environ["VAMPIRE_CT_SAMPLES"] = "256"
    os.environ["VAMPIRE_CT_SEED"] = "1"
    subprocess.run([build / "codetree-time-sampler-test"], check=True)
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
        assert sum(int(r["multiplicity"]) for r in rows) == 256
        assert all(r["sampling"] == "time_pps" and int(r["baseline_cpu_ns"]) > 0 for r in rows)
        for r in rows:
            expected_weight = int(r["multiplicity"]) * int(r["total_baseline_cpu_ns"]) / (256 * int(r["baseline_cpu_ns"]))
            assert math.isclose(float(r["weight"]), expected_weight, rel_tol=1e-9)
        assert math.isclose(sum(float(r["time_weight"]) for r in rows), int(rows[0]["total_baseline_cpu_ns"]), rel_tol=1e-9)
        assert all(int(r["matches"]) == 3 * expected_counts[r["query"]] for r in rows)
        assert any(int(r["query"]) > 4096 for r in rows)
        assert {r["sres"] for r in rows} == {"0", "1"}
        assert any(int(r["matches"]) == 0 for r in rows)
        assert any(int(r["matches"]) > 0 for r in rows)
        assert all(r["algorithm"] == "otter" and r["problem"] == "synthetic-fixture" for r in rows)
        population = int((tree / "sample.ctd").read_text().splitlines()[0].split()[3])
        assert population == 5000
        # Clock measurements vary across runs: only the sampler with fixed weights is deterministic.
        run2 = root / "run2"
        subprocess.run([build / "codetree-dataset-fixture", run2, root / "expected2.csv"], check=True)
        second_count = sum(line.startswith("Q ") for line in (run2 / "tree-0/sample.ctd").read_text().splitlines())
        collection = subprocess.run([build / "codetree-replay", root, "--problem", "synthetic", "--algorithm", "otter"],
                                    check=True, capture_output=True, text=True)
        combined = list(csv.DictReader(io.StringIO(collection.stdout)))
        assert len(combined) == len(rows) + second_count
        assert len({r["dataset"] for r in combined}) == 2
        # CTD1 histories share the same DAG/mutation encoding. Convert a sample
        # to a valid single-stratum legacy fixture to exercise the old reader.
        legacy = root / "legacy"
        shutil.copytree(tree, legacy)
        metadata = (legacy / "metadata.txt").read_text().replace("CTD 2\n", "CTD 1\n", 1)
        (legacy / "metadata.txt").write_text(metadata)
        lines = (tree / "sample.ctd").read_text().splitlines()
        header = lines[0].split()
        converted = [f"CTD 1 {header[2]} {header[3]}", f"B 0 {header[3]} {len(rows)}"]
        for line in lines[1:]:
            fields = line.split()
            converted.append(' '.join(['Q', fields[1], fields[3], '0', fields[4], fields[5], *fields[7:]]))
        converted += [f"B {i} 0 0" for i in range(1, 64)]
        (legacy / "sample.ctd").write_text('\n'.join(converted) + '\n')
        old = subprocess.run([build / "codetree-replay", legacy, "--all", "--verify"],
                             check=True, capture_output=True, text=True)
        assert len(list(csv.DictReader(io.StringIO(old.stdout)))) == len(rows)
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
              "removals, time-proportional bounded sampling, collection/repeat/filter, checkpoint recovery and truncation.")


if __name__ == "__main__":
    main()
