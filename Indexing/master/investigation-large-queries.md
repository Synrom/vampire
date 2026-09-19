# Why `SYO599+1` and `GRA045^1` are slow: large queries

In the first TPTP sweep (`t60`, current tree runs first) the current tree looked 35x / 24x worse
than master in instructions on `SYO599+1` and `GRA045^1`, the two biggest regressions. Both
problems have queries with many literals, but the two cases turned out to be different in nature.
Both are excluded from the statistics for now (`bench/excluded_problems.txt`).

All numbers below were measured with the instrumented binary of this branch on a single core
(`-sa discount`, `-t 20`/`-t 60`/`-t 300`), with temporary counters and cycle timers that have been
removed again.

## `GRA045^1`: an artifact of running the trees one after the other

* Ramsey-style THF conjecture, 5 input clauses, only 25 active clauses at the end.
* For queries 1..655, both trees do the same work (182 literal levels entered, ~35 M cycles per
  query in each).
* **Query 656** (72 literals) is exponential in `matchGlobalVars` (the join over the shared
  variables of the matched literals). With a limit of 300 s it finishes after ~50 s **in both
  trees**: master 132.9 G cycles, current 138.4 G cycles, of which 99.99 % are in
  `checkCandidate`. `canEnterLiteral`, `enterLiteral`, `next` are negligible.
* In a time-limited run, whichever tree is called first on query 656 consumes the rest of the
  limit, and the other tree never sees this query. Hence the call counts differ by one (656 vs.
  655) and the tree that went first "loses" everything:

  | order | first tree, total time | second tree, total time |
  |---|---|---|
  | current first (sweep 1, 60 s) | current 57 s | master 2.5 s |
  | master first (20 s) | master 17 s | current 2.5 s |

  When the order is swapped, the roles flip. There is **no performance difference between the
  trees** here; the measurement is just dominated by one final, never-finishing query.

## `SYO599+1`: the current tree does much more work on long queries

This one is real and independent of the order (current is 22x slower with either order).

* 310 KB problem. The last ~130 queries are clauses with **1450-1580 literals**: one such clause is
  simplified over and over, each round removing one literal by subsumption resolution (query
  length 1581, 1580, 1579, ...). Every candidate on the way is a single-literal clause, so
  `matchGlobalVars` is never called; the time is in the tree traversal
  (`canEnterLiteral` -> `existsCompatibleMatch`).
* Per query, for the same query, both trees return the same result (subsumption resolution):

  | query | master: Mcycles / literal levels entered | current: Mcycles / literal levels entered | current: `canEnterLiteral` calls (master ~16 000) |
  |---|---|---|---|
  | 4835 | 28 / 9 | 44 / 27 | 58 000 |
  | 4863 | 26 / 9 | 189 / 148 | 347 000 |
  | 4893 | 25 / 9 | 298 / 251 | 566 000 |
  | 4933 | 25 / 9 | 495 / 435 | 967 000 |
  | 4962 | 24 / 9 | 585 / 535 | 1 154 000 |

  Master finds its subsumption resolution after entering 9 literal levels; the current tree needs
  27, then 148, ... 535 levels, growing with every query, i.e. with the number of long clauses
  that have accumulated in the tree.
* **Cause.** The current tree *prefers subsumption to subsumption resolution* by deferring
  opposite (resolution) matches: in `LiteralMatcher::next` and in `LiteralMatcher::doEagerMatching`
  the opposite matches of a literal are returned only after all non-opposite ones. In the depth-first
  traversal of the code tree this means that at every level the complete non-opposite search space
  is explored first, down to the full depth of ~1500 literals, before the first opposite branch is
  tried. Master follows the tree order, so it reaches the resolution branch immediately. If
  the query has a resolution partner but many long clauses that share a long prefix with it and never
  subsume it, all of them are walked to the end first.
* **Test of the cause.** Disabling both deferrals (temporarily, behind an environment variable, now
  reverted) makes the current tree do exactly what master does: 379 vs. 379 literal levels for
  the same query, 9.24 s vs. 9.16 s total time in `next`, 60.6 G vs. 58.3 G instructions
  (the query sequence differs from the normal run because the saturation follows the current tree's
  results, which is why master is slower here than in the normal run).

Ideas (not tried): apply the preference only where a candidate is actually chosen (like
`checkCandidate` already does), or bound the non-opposite exploration.

## What the statistics do about it

* `bench/excluded_problems.txt` lists both problems; they stay in the CSV but are ignored by the
  plot (column `excluded`, reason `listed`).
* Automatically, any problem in which a query was **still running when Vampire terminated** and had
  been running for >= 1 s and >= 25 % of that tree's total time in `next` is ignored, too (reason
  `final-query-hang`). This is measured directly: the driver
  (`CodeTreeForwardSubsumptionAndResolution`, outside of both trees and outside of the timed
  `next` calls) records which tree is running a query since when, and `--statistics full` prints
  it in the `CODE TREE COMPARISON` group. The tree implementations themselves contain nothing but
  the `TIME_TRACE` line.
* `extract_csv.py` writes the list of all ignored problems to `codetree_compare_excluded.csv` and
  prints it at the end.
* The same pattern is probably behind the other `GRA*^1` outliers of sweep 1 (`GRA047^1`,
  `GRA051^1`, `GRA053^1`, ...: timeouts with ~650-780 calls, first tree in the order takes everything);
  this has not been checked individually. The new sweep, where master goes first, will list them
  under `final-query-hang` if so.
