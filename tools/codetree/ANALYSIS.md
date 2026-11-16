# Exploring recorded clause code trees

From the repository root, on Linux:

```sh
python3 -m venv .venv-ct
. .venv-ct/bin/activate
python -m pip install -r tools/codetree/requirements-analysis.txt
python tools/codetree/analyze.py /path/to/dataset-root --output /path/to/results
```

The runner checks Python dependencies, CMake (unless binaries are supplied),
Linux, `perf`, sampling permission, and DWARF call-stack support. Install the
Linux distribution's perf package if missing. A failed permission probe reports
perf's actual error; the runner does not change system settings.
It builds two separate optimized replay binaries, recursively discovers CTD
checkpoints, replays every retained query, exports CSVs and PNG/PDF plots, then
runs a separate counter-free profiling pass. Trees execute in fresh processes
because symbol IDs belong to individual datasets. Empty checkpoints are skipped.
Progress bars show trees and queries. Build and replay logs are retained.
Use a new/empty output directory for each run.

Use `--cpus N` to replay up to N independent trees concurrently during analysis.
Each worker is a separate single-threaded C++ process; queries within one tree
remain sequential. CSV ordering is deterministic regardless of completion order.
Progress bars aggregate completed trees and queries across workers. Memory use
increases with the number of concurrent reconstructed trees. Plotting remains
sequential. This is a worker limit, not CPU affinity or a CPU limit for the build.

Profiling defaults to one worker to limit cache/memory contention in measurements.
Use `--profile-cpus N` to parallelize that pass too; resulting CPU shares describe
that concurrent workload. `--jobs N` independently controls compiler parallelism.
Both replay worker options default to 1, preserving the previous behavior.

Useful options:

```sh
# Parallel analysis with 16 workers (profiling stays sequential):
python tools/codetree/analyze.py DATASET --output RESULTS --cpus 16

# Parallelize both passes:
python tools/codetree/analyze.py DATASET --output RESULTS --cpus 16 --profile-cpus 16

# Analysis on macOS, or analysis without CPU profiling:
python tools/codetree/analyze.py DATASET --output RESULTS --skip-perf

# Redraw all figures without replaying or needing perf:
python tools/codetree/analyze.py DATASET --output RESULTS --plots-only

# Longer profiling for small workloads; first-result matching instead of exhaustive:
python tools/codetree/analyze.py DATASET --output RESULTS --profile-repeat 10000 --mode first

# Reuse a dedicated instrumented binary:
python tools/codetree/analyze.py DATASET --output RESULTS --skip-perf \
  --analysis-binary build-dataset/codetree-replay
```

`--build-root`, `--jobs`, `--profile-binary`, and `--frequency` control builds and
sampling. Defaults: exhaustive matching (`--mode all`), 1,000 warmed repetitions
per retained query in the profiling pass, and 999 samples/second. The analysis
pass always executes each query once. `--mode first` stops at the first result;
neither mode reconstructs the historical consumer's stopping decisions.
“All queries” means all **retained samples**, not the queries discarded by the
recorder. Statistical weights estimate the recorded population.

## Definitions

* **dispatches**: visits to the dispatch switch in `Matcher::execute`, including
  failed checks, repeated visits, eager matching, `LIT_END`, and success/fail.
  Reconstruction, verification, and cached result retrieval are excluded.
* **expanded_ops**: same visits, except a `SEARCH_STRUCT` dispatch contributes
  the number of **non-null** targets. This is an explicit accounting convention,
  not a claim that binary search actually executed all those comparisons. The
  selected target's subsequent `CHECK_FUN`/ground check is a separate real
  dispatch and contributes again. Both counts are exported for comparison.
* **success_scan_checks**: instruction inspections in the success-only shortcut,
  exported separately rather than counted as interpreter dispatches.
* **max_ils_depth**: maximum `ILStruct::depth` on an executed `LIT_END`, including
  ones subsequently rejected by compatibility matching. Depth is zero-based:
  the first `LIT_END` has depth 0. `-1` means no `LIT_END` was reached. This is
  not stack depth or number of term instructions.
* **branching**: the number of logical alternatives at the following instruction
  in its block. A search landing node is flattened into its non-null targets;
  its alternatives are included too. Target heads are real instructions and are
  not counted a second time as extra structural nodes. Thus `op1` followed by
  `op2 -> op3 -> op4 -> op5` has branching factor 4. A block-ending instruction
  has factor 1 by convention. `branching_nonterminal` excludes those instructions.
* **hotness**: expanded execution count for each logical instruction in the
  current tree. Each compressed-search dispatch adds one hit to each non-null
  target head; subsequent actual target checks add their own hits. Unvisited
  instructions are retained with count zero. The sum of all node hits equals
  the query's `expanded_ops`. Search landing nodes are not additional logical
  instructions. Success/fail instructions remain in the population even when
  the interpreter never visits them.

Node identities are **query-state local**. Insertion/removal can relocate, merge,
or reuse instructions; aggregating by pointer across mutations would silently
mix different nodes. Instead we save the full empirical distribution of node
hits and branching at each query state. Histograms are lossless for the requested
univariate distributions but do not retain instruction addresses or code-order
traces. This is a distribution of **within-query hotness**, not an estimate of
lifetime hotness for a supposedly persistent node.

## The three views in CodeTreeAnalysis.md

For each algorithm separately:

1. **Query average**: give query `q` weight `population / retained` from its
   reservoir bucket. Scalar metrics use a point mass at the measured value.
   Tree metrics first normalize that query's node histogram to mass 1; a large
   tree does not automatically outweigh a small tree. Average these distributions
   with the query weights.
2. **Problem average**: form the weighted distribution within each problem,
   then average those distributions with equal problem weights. Runs/tree
   instances retain distinct dataset/query keys in the raw CSVs; their represented
   query populations are pooled within the recorded problem path. No automatic
   basename or fingerprint deduplication is attempted.
3. **Otter versus Discount**: separate curves throughout, separate per-problem
   heatmaps, and paired problem-mean scatterplots on the intersection of recorded
   problem paths. The full-corpus algorithm curves may have different problem
   sets; only the paired panel controls for that. Equal capture budgets and
   matched strategies still require checking dataset provenance.

The raw exact-value probability plot, step ECDF, and strict upper-tail plot
preserve the empirical distribution. A separate coarsened plot groups wide
ranges in log1p-space; it shows **probability per unequal-width bin**, not density.
For small discrete ranges the bins remain exact. No Gaussian KDE is imposed on
zero-heavy discrete counts. Every-problem curves and CDF heatmaps sorted by
problem mean reveal potential clusters hidden by the average. Quantile/mean
CSVs permit different orderings and subgroup analysis later.

Hotness has within-query Lorenz curves, Gini coefficients, hottest 1%, 5%, and
10% execution shares, and the fraction of unexecuted ops. Fractional nodes at a
percentile boundary use linear interpolation. Queries with no dispatches have
undefined concentration (`NaN`) and are excluded from Lorenz averages; their
zero-hit nodes remain in hotness distributions. Lorenz weights are renormalized
among valid queries. Equal-problem Lorenz averaging conditions within each
problem before giving represented problems equal weight.
Raw scatterplots are explicitly unweighted retained samples; their point density
must not be interpreted as population density. Problem summaries/curves and
weighted distribution plots provide the population views. A separate query-cost
Lorenz plot shows whether a small fraction of queries accounts for most work.
No confidence intervals or sampling-variance estimates are claimed.

## Output files

* `queries.csv`: dataset/problem/algorithm, sampling and query metadata, physical
  and expanded execution counts, depth, logical node count, shortcut scan count.
  Its `total_ns` includes instrumentation overhead and is **not a benchmark**.
* `histograms.csv`: exact `(dataset, query, metric, value, count)` histograms.
  Join to `queries.csv` on `(dataset, query)` for all weighting/provenance.
* `distributions.csv`: plotted exact probabilities and CDFs for both weightings.
* `problem_summary.csv`: per-problem means, medians, p90 and p99 by metric.
* `*_problem_cdfs.csv`: problem identities and CDF values on the heatmap grid.
* `query_concentration.csv`, `problem_concentration.csv`, `lorenz.csv`, and
  `query_work_concentration.csv`: concentration statistics and plotted curves.
* `profile.csv`: per-tree sample counts and estimated sampled CPU nanoseconds,
  by exclusive/inclusive region; contains explicit zero-sample categories.
* `raw/<tree-key>/`: per-tree query CSVs, histograms, logs, profile query timings,
  `perf.data`, and symbolized `perf.script` for reclassification.
* `manifest.json`: arguments, dataset paths, platform, Python version, Git HEAD
  and tracked diff. Preserve source and binaries alongside results for archival
  reproducibility; Git diff does not capture untracked files.
* Figures are saved as both `.png` and `.pdf`.

The histogram export is much smaller than one row per node/query, but collection
plotting loads the histogram tables into memory. Full tree traversal per query
can be expensive on large indexes. No artificial node sampling or tail clipping
is used to hide this cost.

## CPU profiling interpretation

The profile build uses `CT_PROFILE`, never `CT_ANALYSIS`, and has debug symbols,
frame pointers, and disabled sibling-call optimization. The four requested
instruction bodies, `canEnterLiteral`, and `checkCandidate` are kept out of line
so perf can attribute them by symbol. This introduces call overhead and changes
optimization: the result estimates the **profiling build's** CPU shares, not
exact production-build time for inlined instructions. Dispatch, backtracking,
setup/teardown, and other work are included in `other` unless inside a named
matching region. A CPU-clock sampling period is measured in nanoseconds;
summing sample periods estimates CPU time, not wall time.

Perf events start disabled (`perf record -D -1`). Replay enables them with
`PR_TASK_PERF_EVENTS_ENABLE` only around matcher initialization/execution/reset
for each query, then disables them before reconstruction and output. Dataset
loading and index mutations therefore do not pollute the profile.
See the [perf record manual](https://man7.org/linux/man-pages/man1/perf-record.1.html)
and [Linux perf-event control documentation](https://man7.org/linux/man-pages/man2/PR_TASK_PERF_EVENTS_DISABLE.2const.html).

Each sample is classified by its leaf-first call stack:

* **Exclusive allocation** assigns it to the deepest named region, or `other`.
  These categories partition the sampled CPU time.
* **Inclusive allocation** credits every named region present in the stack.
  For example, `canEnterLiteral` can invoke eager matching that executes
  instruction bodies. Inclusive shares therefore overlap and need not sum to
  100%. `candidate_check` includes the final candidate validation, including its
  eager matching and global-variable matching.

Profiling executes each retained query equally often. Profile shares are the
**unweighted retained workload**, separated by algorithm in the plot; they are
not population-weighted query estimates or equal-problem estimates. Per-tree
raw results remain available. Increasing repetitions improves sampling precision
but emphasizes warmed caches. Zero samples for a category do not prove zero
cost. A tree with no samples fails with a request to increase repetitions.
Very small categories remain noisy, and stack truncation/unresolved frames can
leave time in `other`; retain and inspect the perf logs and raw stacks.

## Tests

```sh
cmake -S . -B build-dataset -DCMAKE_BUILD_TYPE=Release -DCT_ANALYSIS=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_Z3=ON
cmake --build build-dataset --target codetree-replay codetree-dataset-fixture codetree-analysis-test -j 8
python tools/codetree/test_dataset.py build-dataset
python tools/codetree/test_analysis.py build-dataset
```

Tests cover alternative-chain branching, compressed targets with null holes,
block ends, zero-hit nodes, zero-based depth, expansion/hit conservation,
unequal query/problem weights, known concentration extremes, perf-text parsing,
replay result preservation against fixture expectations and the SAT oracle,
concurrent scheduling/error propagation, and identical serial/parallel CSVs
(excluding elapsed timings).
