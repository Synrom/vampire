# Clause code tree datasets

Capture **inputs**, then change the replay code as your analysis evolves. The
recorder does not measure instruction execution, timing, matches, or query
outcomes. It covers `ClauseCodeTree` (forward subsumption and subsumption
resolution), not the term/literal code tree indexes.

## Build

```sh
cmake -S . -B build-dataset -DCMAKE_BUILD_TYPE=Release
cmake --build build-dataset --target vampire codetree-replay -j 8
```

Z3 is optional; `-DCMAKE_DISABLE_FIND_PACKAGE_Z3=ON` builds without it. Use a
separate build directory to avoid reusing objects from other code tree branches.

## Capture

Use a **new output directory for each single-strategy run**. Its parent must
already exist. Recording is disabled unless `VAMPIRE_CT_DATASET` is set.

```sh
VAMPIRE_CT_DATASET=/tmp/ct-otter \
  build-dataset/vampire -sa otter -cts on -t 3s \
  /Users/synrom/Development/AutomatedDeduction/problems/condensed_detachment_4.p

VAMPIRE_CT_DATASET=/tmp/ct-discount \
  build-dataset/vampire -sa discount -cts on -t 3s \
  /Users/synrom/Development/AutomatedDeduction/problems/condensed_detachment_4.p
```

A timeout exit status is expected for unfinished problems. The latest complete
checkpoint is still usable. Do not use portfolio modes or concurrent writers in
one destination. Each tree has its own `tree-N` directory and population.

Configuration (environment variables):

| Variable | Default | Meaning |
|---|---:|---|
| `VAMPIRE_CT_DATASET` | unset | Destination; must not exist |
| `VAMPIRE_CT_SAMPLES_PER_BUCKET` | 4 | Reservoir capacity in each of 64 buckets |
| `VAMPIRE_CT_SEED` | 1 | Independent sampling RNG seed |

Both numeric settings accept integers from 1 to 1,000,000. The default retains
at most **256 queries per tree/run**, often fewer because some buckets are empty.

The strata are the Cartesian product of:

| Feature | Four ranges |
|---|---|
| Indexed clause count | <32; 32–255; 256–2047; ≥2048 |
| Query literal count | 0–1; 2; 3–4; ≥5 |
| Query ordinal | 1–256; 257–4096; 4097–65536; ≥65537 |

Bucket ID is `(phase * 4 + size) * 4 + length`. Each bucket uses standard uniform
reservoir sampling, so it does not just retain the first queries. Counts are
structural sampling metadata; no query is executed again during capture.
The RNG is separate from Vampire's RNG. A seed reproduces selection for the
same input operation sequence, not necessarily for two wall-clock-limited runs.

## Replay and extend

```sh
build-dataset/codetree-replay /tmp/ct-otter/tree-0 --repeat 100 > first.csv
build-dataset/codetree-replay /tmp/ct-otter/tree-0 --all --repeat 100 > all.csv
build-dataset/codetree-replay /tmp/ct-otter/tree-0 --all --verify > checked.csv
build-dataset/codetree-replay /tmp/ct-otter/tree-0 --query 123 --repeat 10000
build-dataset/codetree-replay /tmp/ct-otter/tree-0 --bucket 16
build-dataset/codetree-replay /path/to/collection --problem ALG --algorithm otter
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

For the three views in `CodeTreeAnalysis.md`:

- To estimate a mean over queries, weight each retained query by its bucket's
  `population / retained`, then divide by the total represented population.
- For a mean over problems, estimate each problem's mean first, then give each
  problem equal weight. Keep repeated runs/tree instances distinct.
- Compare Otter and Discount separately, preferably on matched problems and
  capture budgets. Equal wall time may cover different execution prefixes.

Unweighted samples are useful for diversity exploration, but overrepresent
rare strata. This sample is structurally diverse, not a guarantee of diverse
execution behavior. Capturing several problems is still necessary; replay
eliminates repeated saturation during later experiments.

## Format and fidelity

Each `tree-N` directory contains:

- `metadata.txt`: versioned run provenance: problem path, input-file FNV-1a
  fingerprint, algorithm, Vampire version, encoded strategy, sampling settings.
- `history.ctd`: append-only symbol/term DAG definitions and ordered indexed
  clause insertions/removals. Insertions retain original clause literal order
  and the compiled literal permutation.
- `sample.ctd`: atomically replaced checkpoint with the committed history byte
  length, represented query population, bucket populations, and retained query
  inputs. A query references a mutation position and shared literal IDs.

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
cost are not**. No result, execution trace, or instruction metric is recorded.

Checkpoints are published after reservoir changes and every 1,024 queries, and
on normal tree destruction. History is flushed before the checkpoint is
renamed. Vampire's timer uses `_Exit`, so the final uncheckpointed prefix can be
lost (fewer than 1,024 query arrivals). Counts and weights refer exactly to the
published prefix. An interrupted trailing history record is ignored; a missing
byte inside the committed prefix is an error. This protects against process
termination, not power loss (`fsync` is not used).

## Tests

```sh
cmake --build build-dataset --target codetree-dataset-fixture codetree-replay -j 8
python3 tools/codetree/test_dataset.py build-dataset
```

The synthetic fixture tests live-versus-replayed result counts, SAT oracle
agreement, insertion/removal history, typed terms, equality orientation,
polymorphic predicates, both subsumption-resolution settings, deterministic
sampling, sample bounds, query filtering/repetition, and interrupted/truncated
history handling. Test-only expected results are outside the dataset.
