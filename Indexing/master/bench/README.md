# Running the code tree comparison on TPTP

This directory contains everything needed to run the instrumented Vampire (current and master
code tree side by side, see [`../README.md`](../README.md)) on the whole TPTP library with
[BenchExec](https://github.com/sosy-lab/benchexec), and to turn the result into one CSV and one
PDF. The setup follows `~/vbenchmarking` (BenchExec tool `vampire`, `--no-container`,
one core per run, 60 runs in parallel).

Because both code trees are executed on the very same queries inside one Vampire run, **one
run per problem is enough**; the comparison is between the two `ClauseMatcher::next` nodes of the
same run. There is no second binary to run and no branch to check out.

## Files

| file | purpose |
|---|---|
| `run_benchmark.sh CONFIG` | does everything below for one configuration |
| `codetree-compare-CONFIG.xml` | the BenchExec definition of configuration `CONFIG` |
| `extract_csv.py` | BenchExec result + logs -> one CSV row per problem |
| `plot_codetree_compare.py` | CSV -> PDF (row of two box-plot panels) |
| `excluded_problems.txt` | problems that are ignored in the statistics (see below) |

## Configurations

| CONFIG | limit per problem | Vampire options |
|---|---|---|
| `t60` | 60 s time limit (`-t 60`) | `--input_syntax tptp -sa discount -p off --time_statistics on --statistics full` |

Further configurations (e.g. instruction-limit based ones) are added by copying
`codetree-compare-t60.xml` to `codetree-compare-<name>.xml` and changing the options.
`--time_statistics on --statistics full` must stay: they produce the numbers that are extracted.
BenchExec's own `timelimit` (90 s) is only a safety net for hanging runs; Vampire's `-t` fires
first, which makes it print its statistics.

## Prerequisites

* A **release build with the time profiler**, otherwise `TIME_TRACE` compiles to nothing:

  ```sh
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DTIME_PROFILING=ON -DBUILD_SHARED_LIBS=OFF
  ninja -C build vampire
  ```
* `benchexec` (the system one, 3.35, is used) and cgroups access (the user is in group `benchexec`).
* Python 3 with `matplotlib` and `numpy` (`pip install --user --break-system-packages matplotlib numpy`).
* TPTP at `/home/mleiwig/TPTP-v9.3.0` (`TPTP_DIR`; the script exports it as `$TPTP` for Vampire, which needs it to resolve `include(...)`; the inherited `$TPTP` is ignored because it may point to another TPTP version, on this machine v9.2.1).

## Running

```sh
cd Indexing/master/bench
./run_benchmark.sh t60
```

Defaults can be overridden through the environment: `TPTP_DIR`, `BUILD` (directory containing
`vampire`, default `<repo>/build`), `WORK` (default `/home/mleiwig/codetree-compare`),
`PARALLEL` (default 60), `TASKSET_FILE` (a list of problem files instead of all of TPTP).
Use a different `WORK` to keep the results of different runs apart, e.g. `WORK=~/codetree-compare-master-first ./run_benchmark.sh t60`. To keep the terminal free, run it detached:

```sh
setsid nohup ./run_benchmark.sh t60 > ~/codetree-compare/t60.launcher.log 2>&1 < /dev/null &
tail -f ~/codetree-compare/t60/benchexec.log   # one line per started / finished problem
```

The script refuses to overwrite an existing `$WORK/CONFIG/results`.

What it does:

1. Lists all `*.p` below `$TPTP_DIR/Problems` into `$WORK/tptp_all.set` (26 990 problems in v9.3.0,
   *all* of them: FOF, CNF, typed, HOL, ... Problems Vampire cannot parse or handle end up as
   `ERROR` and simply carry no measurements).
2. Snapshots the binary to `$WORK/CONFIG/bin/vampire` (so rebuilding during a run is harmless),
   instantiates the XML with the task set as `benchmark.xml`, and records `provenance.txt`
   (date, commit, uncommitted files).
3. Runs `benchexec --no-container -N 60 ...`. Results and logs: `$WORK/CONFIG/results/`
   (`*.xml.bz2` and `*.logfiles.zip`, one Vampire log per problem).
4. `extract_csv.py` writes `$WORK/CONFIG/codetree_compare.csv` and the list of ignored problems
   `codetree_compare_excluded.csv` (also printed at the end), and
   `plot_codetree_compare.py` writes `$WORK/CONFIG/codetree_compare.pdf` and prints a text summary.

Both scripts can be re-run by hand on existing results:

```sh
python3 extract_csv.py results/*.results.*.xml.bz2 results/*.logfiles.zip codetree_compare.csv
python3 plot_codetree_compare.py codetree_compare.csv codetree_compare.pdf
```

## The CSV

One row per problem (also those without measurements, where the fields are empty):

| column(s) | meaning |
|---|---|
| `problem`, `status`, `szs_status`, `cputime_s`, `instructions_million_total` | BenchExec / Vampire result for the whole run |
| `calls_current`, `calls_master` | number of `ClauseMatcher::next` calls |
| `time_ns_*`, `avg_time_ns_*` | total time / time per call in `next` (`*` = `current` or `master`) |
| `instr_*`, `avg_instr_*` | total instructions / instructions per call in `next` |
| `cur_<outcome>__master_<outcome>` | how often (current, master) returned that pair of outcomes (`nothing`, `subsumption`, `subsumption_resolution`), 9 columns |
| `inflight_tree`, `inflight_ms` | tree (`current`/`master`) that was still running a query when Vampire terminated, and for how long |
| `excluded` | empty, or why the problem is ignored in the statistics: `listed` or `final-query-hang` |

`current` is the optimized code tree of this branch. The data comes from Vampire's flat time
profile and its `CODE TREE COMPARISON` statistics group.

## Ignored problems

Some problems must not enter the statistics; they stay in the CSV, with a reason in `excluded`,
and are ignored by the plot:

* `listed`: problems in `excluded_problems.txt` (currently `SYO599+1` and `GRA045^1`, see
  [`../investigation-large-queries.md`](../investigation-large-queries.md)).
* `final-query-hang`: automatically, if a query was still running when Vampire terminated (time
  limit), had been running for >= 1 s and for >= 25 % of that tree's total time in `next`
  (`HANG_MIN_MS`, `HANG_MIN_SHARE` in `extract_csv.py`). Because the two trees run one after the
  other, the tree that is first eats the whole limit on such a query and the other one never sees it,
  so the totals would only say which tree happened to go first.
  The running query is measured by the driver (`CodeTreeForwardSubsumptionAndResolution`), outside of
  both trees and outside of the timed `next` calls, and printed by `--statistics full` (group
  `CODE TREE COMPARISON`, "in-flight query at termination").

`extract_csv.py` prints the list of all ignored problems at the end of its output and writes it to
`codetree_compare_excluded.csv`; the plot's footnote gives their number.

## The PDF

One row, two panels, each with two box plots (optimized = current, master). Each box summarizes
one value per problem, taken over the problems in which both trees were queried:

* left: average time per `ClauseMatcher::next` call,
* right: average instructions per `ClauseMatcher::next` call.

Log scale, box = quartiles, line = median, diamond = mean, whiskers = 1.5 x IQR, outliers not
drawn. Medians and means are printed next to the boxes.

## Things to keep in mind when reading the numbers

* **Call counts.** Both counts should agree up to 1: the trace is snapshotted when the time limit
  hits, possibly between the current call and the master call that follows it. The counts of the
  outcome matrix can exceed the call counts by a few, because Vampire keeps running until it
  notices the limit.
* **Time resolution.** Vampire prints durations truncated to a unit (>= 10 s: seconds,
  >= 10 ms: ms, >= 10 us: us, else ns), i.e. totals are accurate to a few percent for short runs,
  and better for long ones. Instruction counts are exact.
* **Parallel runs.** 60 runs share the machine (caches, memory bandwidth, frequency scaling), so
  wall-clock times are noisier than instruction counts. Both trees run inside the same process
  and are subject to the same noise.
* **Order.** In every query the tree that runs first pays for cold caches, the second one can
  profit from data the first one pulled in (mostly affects time, not instructions). Sweep 1
  (`~/codetree-compare/t60`) ran the current tree first, sweep 2
  (`~/codetree-compare-master-first/t60`) runs master first.
* **Time limit and search.** The saturation is driven by the current tree's results, so runs are
  cut off after 60 s of time, which includes the time of the master tree; problems whose search
  is time-dependent are not repeatable.
* **Aborts.** If the current tree ever finds nothing while master finds something, Vampire aborts
  with a message on stderr (`CodeTree comparison: current found nothing ...`); such a problem shows up
  as a crash status in the CSV and its log holds the details. Search the logs for it with
  `unzip -p results/*.logfiles.zip | grep "CodeTree comparison"`.
