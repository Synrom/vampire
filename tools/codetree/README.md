# Clause code tree datasets

Capture **inputs**, then change the replay code as your analysis evolves. The
recorder measures only query-boundary thread CPU time, for probability-proportional-to-time
sampling. It does not count instructions or save matcher results. It covers
`ClauseCodeTree` (forward subsumption and subsumption
resolution), not the term/literal code tree indexes.

## Build

```sh
cmake -S . -B build-codetree-capture -DCMAKE_BUILD_TYPE=Release \
  -DCT_ANALYSIS=OFF -DCT_PROFILE=OFF -DTIME_PROFILING=OFF
cmake --build build-codetree-capture --target vampire codetree-replay -j 8
```

Z3 is optional; `-DCMAKE_DISABLE_FIND_PACKAGE_Z3=ON` builds without it. Use a
separate build directory to avoid reusing objects from other code tree branches.

## Capture

Use a **new output directory for each single-strategy run**. Its parent must
already exist. Recording is disabled unless `VAMPIRE_CT_DATASET` is set.

```sh
VAMPIRE_CT_DATASET=/tmp/ct-otter \
  build-codetree-capture/vampire -sa otter -cts on -t 3s \
  /Users/synrom/Development/AutomatedDeduction/problems/condensed_detachment_4.p

VAMPIRE_CT_DATASET=/tmp/ct-discount \
  build-codetree-capture/vampire -sa discount -cts on -t 3s \
  /Users/synrom/Development/AutomatedDeduction/problems/condensed_detachment_4.p
```

A timeout exit status is expected for unfinished problems. The latest complete
checkpoint is still usable. Do not use portfolio modes or concurrent writers in
one destination. Each tree has its own `tree-N` directory and population.

## Parallel TPTP sweep, then analysis

```sh
python3 tools/codetree/capture.py /path/to/TPTP --cpus 8 \
  --output /path/to/capture --time-limit 60 --samples 256

# Use a Python environment with requirements-analysis.txt installed.
python3 tools/codetree/analyze_dataset.py /path/to/capture --cpus 8
```

Capture discovers `Problems/**/*.p` if `Problems/` exists, otherwise `**/*.p`.
It sets `TPTP` for include resolution, runs both Otter and Discount, and uses
collision-resistant names derived from relative paths. Each process gets its
own output directory and sampling RNG. `--pattern`, `--limit`, `--algorithms`,
`--activation-limit`, `--time-limit`, and `--memory-limit` control the sweep.
The default memory limit is 2,048 MB **per worker**; choose CPU/memory budgets
accordingly. Higher-order inputs are unsupported and are reported as failures.

`--cpus` limits compiler jobs and concurrent prover processes. The second script
passes the same limit to analysis workers, compiler jobs and optional perf
workers. `--perf` enables the Linux perf pass; it is off by default. Counter
analysis is independent of CPU contention, but parallel timing/profiling is
not equivalent to isolated microbenchmarking.

Capture writes `capture.json`, per-run logs and `run.json`, and a `runs.json`
summary. Completed runs are atomically published under `datasets/`; interrupted
work stays under `work/` and is moved to `interrupted/` on resume. `--resume`
requires the same problem list/settings and skips all published runs, including
reported failures. It does not silently overwrite an earlier dataset. Each run
records the executable SHA-256. A nonzero sweep exit indicates failed runs;
completed datasets can still be analyzed. Preprocessing-only runs may be empty.
The analysis wrapper reads only `datasets/`, not interrupted work.

Capture uses a dedicated Release build (`build-codetree-capture`) with
`CT_ANALYSIS=OFF`, `CT_PROFILE=OFF`, and `TIME_PROFILING=OFF`. Recording from an
instrumented build is rejected. Analysis uses separate builds under
`build-codetree-analysis`; no source rewriting, branch switching, or build-cache
sharing is necessary. The CodeOp interpreter and ClauseMatcher bodies retain
normal behavior in capture. Clocks wrap calls from the inference engine, **not
individual CodeOps or internal matcher steps**. Index serialization, RNG draws,
consumer work and disk writes are outside these timed regions.

## Time-proportional sampling (CTD2)

| Variable | Default | Meaning |
|---|---:|---|
| `VAMPIRE_CT_DATASET` | unset | New destination; its parent must exist |
| `VAMPIRE_CT_SAMPLES` | 256 | Independent sample draws per tree/run |
| `VAMPIRE_CT_SEED` | 1 | Independent sampling RNG seed |

Numeric settings accept 1–1,000,000. The old `VAMPIRE_CT_SAMPLES_PER_BUCKET`
setting is rejected rather than silently ignored. There are **no structural
buckets** in new captures. Old CTD1 bucket datasets remain readable.

For query i, let t_i be the summed thread CPU time in `init`, every `next`
actually called by Vampire, and `reset`. Let T be total measured query CPU time
in this tree/run. Each of K independent reservoir slots selects query i with
probability **t_i / T**. A new query of duration t replaces each slot with
probability t/(T+t). A binomial replacement count followed by a uniform subset
implements this without K RNG draws per query.

This is sampling **with replacement**. Identical selections are stored once
with a multiplicity m_i, so there are at most K unique queries. For durations
1,1,1,3, each draw selects the last query with probability 1/2. Its probability
of appearing at least once among K draws is 1-(1/2)^K; inclusion probability is
not the same as probability per draw.

Thread CPU time excludes time when the worker is descheduled. It does not
eliminate cache/frequency/memory-contention effects. Boundary clock reads have
cost and finite resolution; this particularly affects tiny queries. Zero
measurements get a 1 ns floor and are counted in the checkpoint. There is no
claim of zero-overhead timing or an unperturbed hardware execution trace.
Only completed queries are eligible; a query cut off by timeout is censored.
Seeds reproduce selection for the same duration sequence, not for repeated
real-time captures with different measured durations.

This represents **baseline time spent**, not uniform query frequency and not
all of TPTP automatically. Expensive baseline queries get more probability;
queries expensive only under a future implementation may be rare. Fixed run
budgets still observe execution prefixes. Validate final optimization claims on
held-out problems and real prover runs. No choice of sampling removes all bias.

### Analysis weights

CSV includes `multiplicity`, `baseline_cpu_ns`, `total_baseline_cpu_ns`, `draws`,
`sampling`, and two distinct weights:

- `time_weight = m_i * T / K`: contribution to the baseline CPU-time measure.
  Its sum is exactly T. Normalize it across runs to get the captured corpus's
  baseline-time distribution; normalize within problems for equal-problem views.
- `weight = m_i * T / (K * t_i)`: Hansen–Hurwitz contribution to query counts.
  For a measurement x, `sum(weight*x)` estimates the population total. Dividing
  by the **known** represented query count estimates its mean without ratio
  bias. Plotting distributions normalizes by `sum(weight)` instead; these are
  self-normalized estimates, not exactly unbiased at finite sample size.

The analysis wrapper defaults to `--weighting baseline-time`. Use
`--weighting query` for query-frequency estimates. Both retain per-problem and
Otter/Discount comparisons. Low-probability fast queries can have large inverse
weights, so query-frequency estimates may have high variance.

For optimization timing, estimate total baseline/candidate work using
`sum(weight * replay_time)` on the **same inputs and stopping rule**. Do not
simply average elapsed times over time-sampled queries: that weights expensive
queries twice. Replay times differ from capture due to cache state and timing
boundaries. `--all` answers an exhaustive-search question; default first-match
replay is closer to the sweep's normal non-randomized simplification consumer.

The optional perf plots aggregate raw execution of unique retained queries.
They are explicitly labelled as **unweighted replay-sample CPU time**, not an
inverse-probability-corrected estimate of population CPU time.

## Replay and extend

```sh
build-codetree-capture/codetree-replay /tmp/ct-otter/tree-0 --repeat 100 > first.csv
build-codetree-capture/codetree-replay /tmp/ct-otter/tree-0 --all --repeat 100 > all.csv
build-codetree-capture/codetree-replay /tmp/ct-otter/tree-0 --all --verify > checked.csv
build-codetree-capture/codetree-replay /tmp/ct-otter/tree-0 --query 123 --repeat 10000
build-codetree-capture/codetree-replay /path/to/collection --problem ALG --algorithm otter
```

You can pass either `tree-N`, its parent run directory, or a collection of run
directories. Collections are scanned recursively and replayed sequentially in
fresh processes, preserving independent symbol-ID spaces. `--problem` matches
a substring of the recorded path, and `--algorithm` selects Otter or Discount.
CSV includes the dataset path so query IDs remain unambiguous across runs.

Default execution stops at the first match. `--all` exhausts the matcher.
Neither mode claims to reproduce historical consumer behavior, particularly
randomized simplification. The subsumption-resolution enable flag is part of
each query's input.

CSV goes to stdout, including problem, saturation algorithm, query ID, bucket,
sampling weight, index size, and the chosen execution mode. `matches` is the
total across repetitions; `total_ns` includes matcher initialization, execution,
and reset across repetitions. Loading and index reconstruction are reported
separately to stderr. Repetitions at the same state emphasize warmed caches;
these timings are not an estimate of end-to-end prover speed.

`--verify` independently checks every returned match and checks result
completeness against all active clauses using SAT-based subsumption. It runs
outside the timed section and can be much slower than replay. It also warms
caches, so use separate invocations for correctness and measurement.
The completeness check follows the code tree's **multiset** resolution
semantics by flipping each query literal and checking multiset subsumption;
the SAT solver's general resolution mode also permits set matches and is
therefore not an equivalent completeness oracle.

The query loop in `replay.cpp` is the intended place to add new analysis. Each
query is accompanied by a reconstructed tree and its active clauses. Changes to
matcher instrumentation require only rebuilding and replaying the saved data.
Insertion receives the recorded literal permutation; experiments on insertion
heuristics can instead call `tree.insert(m.clause)` to recompute it.

## Format and fidelity

Each `tree-N` directory contains:

- `metadata.txt`: versioned run provenance: problem path, input-file FNV-1a
  fingerprint, algorithm, Vampire version, encoded strategy, sampling settings.
- `history.ctd`: append-only symbol/term DAG definitions and ordered indexed
  clause insertions/removals. Insertions retain original clause literal order
  and the compiled literal permutation.
- `sample.ctd`: atomically replaced checkpoint with the committed history byte
  length, represented query count, total measured CPU time, draw count, zero-time
  count, and retained query inputs with durations and multiplicities. A query references a mutation position and shared literal IDs.

The fingerprint covers the named input file, not transitive TPTP includes, and
is not cryptographic. The dataset itself is self-contained and does not need
the original problem files to replay.

Shared term IDs preserve equality orientation; symbol numbers preserve code
tree dispatch keys. Recorded literal permutations preserve compilation choices
without relying on parser-created IDs. Replay rebuilds clauses directly,
without parsing, preprocessing, or saturation. Source symbol names are retained
in the history, but replay uses synthetic names to recreate numeric IDs without
special parser semantics. Typed first-order terms, parametric sorts, polymorphic
symbol types, and equality are encoded. Higher-order and special/non-shared
terms are rejected. This is a matching workload representation, not a general
Vampire proof/state serialization format.

Every index mutation is retained: the final clause set alone does not determine
tree structure. Sample replacements can leave unused DAG definitions in the
history. Consequently the query count is bounded, **history bytes and setup
cost are not**. Only sampling durations are recorded; no result, execution trace, or instruction metric is recorded.

Checkpoints are published after reservoir changes and every 1,024 queries, and
on normal tree destruction. History is flushed before the checkpoint is
renamed. Vampire's timer uses `_Exit`, so the final uncheckpointed prefix can be
lost (fewer than 1,024 completed queries, plus an interrupted query). Counts and weights refer exactly to the
published prefix. An interrupted trailing history record is ignored; a missing
byte inside the committed prefix is an error. This protects against process
termination, not power loss (`fsync` is not used).

## Tests

```sh
cmake --build build-codetree-capture --target codetree-dataset-fixture codetree-replay codetree-time-sampler-test -j 8
python3 tools/codetree/test_dataset.py build-codetree-capture
```

The synthetic fixture tests live-versus-replayed result counts, SAT oracle
agreement, insertion/removal history, typed terms, equality orientation,
polymorphic predicates, both subsumption-resolution settings, statistical PPS
frequencies (including 1,1,1,3), scale invariance, inverse weights, sample bounds, query filtering/repetition, and interrupted/truncated
history handling. Test-only expected results are outside the dataset.

## Exploratory analysis

[`analyze.py`](analyze.py) turns a dataset root into weighted distribution plots,
exact per-sample histogram CSVs, per-problem and Otter/Discount comparisons, and a separate
Linux perf pass. See [ANALYSIS.md](ANALYSIS.md) for the one-command workflow,
metric definitions (including SEARCH_STRUCT expansion), outputs, and profiling
limitations.
