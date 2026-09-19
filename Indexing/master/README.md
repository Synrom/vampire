# Comparing the `opposite-optimization` code tree against master

Branch: `opposite-optimization-compare` (branched off `opposite-optimization`).

The goal is to compare the code tree of `opposite-optimization` ("current") with the one on
`master`, on runtime and on what they return, by running both on exactly the same queries.

## What was done

* **`Indexing/master/`** holds verbatim copies of `CodeTree.{hpp,cpp}` and
  `ClauseCodeTree.{hpp,cpp}` from `master`. The only changes are:
  * they live in the namespace `Indexing::Master` (so `Indexing::Master::ClauseCodeTree` is the
    master implementation, `Indexing::ClauseCodeTree` the current one),
  * the include guards are renamed (`__MasterCodeTree__`, `__MasterClauseCodeTree__`),
  * `TIME_TRACE("ClauseCodeTree::ClauseMatcher::next (master)")` at the top of
    `ClauseMatcher::next`.
* **`Indexing/ClauseCodeTree.cpp`**: `TIME_TRACE("ClauseCodeTree::ClauseMatcher::next (current)")`
  at the top of `ClauseMatcher<sres>::next`.
* **`Indexing/CodeTreeInterfaces.hpp`**: `CodeTreeSubsumptionIndex` now owns a second tree
  (`Master::ClauseCodeTree`, `getMasterClauseCodeTree()`); every clause added to / removed from
  the index goes into both trees, so they always contain the same clauses.
* **`Inferences/CodeTreeForwardSubsumptionAndResolution.cpp`** (`performWith`): the whole
  `RSI_SKIP_PROB` / `rsi` logic is gone. Each `perform` now calls `next` exactly once on the
  current tree and exactly once on the master tree (first version: current first, then master;
  since 2026-09-19: **master first**, then current). Only the
  result of the **current** tree is used for the simplification, the master result is only
  compared. Because there is no skipping any more, `next` is called once per `perform` on a
  non-empty tree, for both implementations.
* **Comparison statistics**: `Statistics::codeTreeComparison[current][master]` (outcome =
  nothing / subsumption / subsumption resolution) is incremented on every query and printed
  as the group `CODE TREE COMPARISON (current vs. master)` with `--statistics full`.
* **Query in flight**: the driver records which tree is running a query since when (outside the
  timed `next` calls; the trees themselves contain only the `TIME_TRACE` line), and
  `--statistics full` prints it, so that a last query that is still running when the time limit hits
  can be recognized (see `bench/README.md`, "Ignored problems").
* **Assertion**: if the current tree finds nothing while master finds something, the run prints
  the query and master's premise to stderr and calls `std::abort()`. This is deliberately not
  an `ASS`, so it is also checked in release builds.
* **Build plumbing**: the new files are listed in `cmake/sources.cmake` and `Makefile`.
  `Lib/Vector.hpp` needed two extra `friend` declarations (`Indexing::Master::CodeTree`,
  `Indexing::Master::ClauseCodeTree`), since `CodeTree::firstOpToCodeBlock` reads the
  protected `Vector::_array`.
* **Unrelated build fix**: `Saturation/LRS.hpp` now includes `<fstream>`. It holds a
  `unique_ptr<std::ofstream>` but only included `<iosfwd>`, which GCC 14 rejects. This is
  independent of this work but was needed to build here.

## How to build and run

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DTIME_PROFILING=ON -DBUILD_SHARED_LIBS=OFF
ninja -C build vampire
./build/vampire -sa discount -t 90 --time_statistics on --statistics full ~/condensed_detachment_4.p
```

`TIME_PROFILING=ON` is required, otherwise `TIME_TRACE` compiles to nothing.
The `next (current)` / `next (master)` nodes in the time trace report total, avg, cnt and
instruction count.

## Running on the whole TPTP library

See [`bench/README.md`](bench/README.md) (BenchExec setup, one CSV row per problem, PDF with box
plots; first configuration: 60 s time limit).

## TPTP results, configuration `t60` (60 s per problem, `-sa discount`)

All 26 990 problems of TPTP v9.3.0, run 2026-09-18/19, release build. Raw data:
`~/codetree-compare/t60/` (`codetree_compare.csv`: one row per problem, `codetree_compare.pdf`:
box plots, `results/`: BenchExec results and all Vampire logs). 25 155 problems had queries to
the code trees (the rest: parse/input errors, or solved before any query); of all problems,
12 472 were proved, 1 091 disproved, 12 327 timed out, 793 input errors (`ERROR (4)`),
304 incomplete, 3 out of memory. No run crashed and the "current NULL, master not NULL"
abort never fired.

This sweep ran the current tree first (the sweep with master first is in
`~/codetree-compare-master-first/t60`, see below). Its two biggest regressions, `SYO599+1` and
`GRA045^1`, are analysed in [`investigation-large-queries.md`](investigation-large-queries.md):
`GRA045^1` is an artifact of the execution order (one query that takes ~50 s in *both* trees),
`SYO599+1` is real and caused by the current tree's "opposite matches last" ordering on
1500-literal queries. Both are excluded from the statistics from now on.

**Sanity check.** 16 945 187 388 calls of `next (current)` vs. 16 945 184 549 of `next (master)`;
per problem they never differ by more than 1.

**Outcomes** (16.9 G queries in total, current vs. master):

| current \ master | nothing | subsumption | subsumption resolution |
|---|---|---|---|
| **nothing**                | 12 604 531 564 | 0 | 0 |
| **subsumption**            | 0 | 3 147 440 721 | 521 239 |
| **subsumption resolution** | 0 | 1 019 | 1 192 928 017 |

* master subsumption, current subsumption resolution: **1 019**
* master subsumption resolution, current subsumption: **521 239** (0.003 %)
* current NULL while master != NULL: **0**, master NULL while current != NULL: **0**

**Cost** (`ClauseMatcher::next`, sums over all problems):

| | current | master | current / master |
|---|---|---|---|
| total time | 148 118 s | 144 652 s | 1.024 |
| total instructions | 6.515e14 | 6.473e14 | 1.006 |
| time per call (pooled) | 8.74 µs | 8.54 µs | |

Per problem (each problem counts once), current / master:

| | 1 % | 5 % | 25 % | median | 75 % | 95 % | 99 % | max |
|---|---|---|---|---|---|---|---|---|
| time | 0.80 | 0.88 | 0.96 | 1.00 | 1.07 | 1.20 | 1.64 | 22.8 |
| instructions | 0.77 | 0.83 | 0.92 | 0.96 | 1.00 | 1.08 | 1.28 | 34.9 |

* On the typical problem the current tree needs ~4 % fewer instructions (fewer in 72.7 % of the
  problems) but is not faster in wall time (median ratio 1.00, faster in 41 % of the problems).
* The totals are dominated by a **tail of blow-ups**: 72 problems are more than 2x worse in
  instructions, 7 more than 5x, and none is more than 2x better. The 10 worst problems account
  for 80 % of the net extra instructions. They are timeouts with few but extremely expensive
  calls, e.g. `SYO599+1` (5 124 calls, 35x instructions), `GRA045^1` / `GRA047^1` (~660 calls,
  ~24x), `GRA051^1` / `GRA053^1` (~11x), `SYO583+1` (3.5x). Without the 100 worst problems the
  instruction ratio is 0.986 and the time ratio 1.014.
* By domain, current costs most extra in `GRA` (1.12x) and `SYO` (1.10x); it saves most in `NUM`,
  `SEU`, `SET`, `NLP` (0.94-0.97x).
* Time is worse than instructions suggest (1.024 vs. 1.006); part of this may be the fixed call
  order (current runs first, see Caveats).

## First run: `condensed_detachment_4.p`, `-sa discount`, `-t 90`

Release build, `TIME_PROFILING=ON`, no Z3. Vampire found a refutation after ~51 s
(so the 90 s limit was not hit). The abort assertion never fired.
Raw output: `results/condensed_detachment_4.discount.90s.out`.

### Runtime

| | total | avg | cnt | instructions |
|---|---|---|---|---|
| `next (current)` | 22 s | 34 µs | 648 505 | 97.4 G |
| `next (master)`  | 21 s | 32 µs | 648 505 | 89.4 G |

* Call counts are **identical** (648 505 each), as expected. (`perform` was entered 648 509 times;
  4 hit an empty tree and skip both.)
* On this problem the current tree is **not faster**: ~9 % more instructions and ~5-6 % more
  time than master.

### Results

Rows: current, columns: master. Total 648 505 queries.

| current \ master | nothing | subsumption | subsumption resolution |
|---|---|---|---|
| **nothing**                | 395 568 | 0       | 0      |
| **subsumption**            | 0       | 183 995 | **580** |
| **subsumption resolution** | 0       | **2**   | 68 360 |

* master subsumption, current subsumption resolution: **2**
* master subsumption resolution, current subsumption: **580**
* current NULL while master != NULL: **0** (asserted, never triggered)
* master NULL while current != NULL: **0**

Outcomes agree in 647 923 / 648 505 queries (99.91 %). All disagreements are between
subsumption and subsumption resolution; the trees never disagree on whether *something* exists.

## Caveats

* One problem, one run, so it is a first data point rather than a benchmark.
* Execution order is fixed (current first, master second). The second call may profit from
  data the first one already pulled into the cache; this favours master slightly. Alternating
  the order per call would remove that bias.
* Both `next` calls carry the same `TIME_TRACE` overhead, so the comparison is fair, but the
  absolute times are somewhat inflated.
* When the trees return different outcomes (582 queries), they have done different amounts of
  work, so their per-call times are not directly comparable for those queries.
* Only the current result drives the saturation; master's result is discarded.
* Debug (`VDEBUG`) build and the unit tests were not built or run.
