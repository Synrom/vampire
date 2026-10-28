# Literal-boundary bitset experiments

The default is `CT_ORDER_VARIANT=cheap`, `CT_ORDER_SORT=sharing`, and
`CT_ORDER_RUN=paired`. See [RESULTS.md](RESULTS.md) for the recommendation.

This is the active replacement for the original per-CodeOp prototype in the parent
directory. `CodeOp` is unchanged in size (16 bytes), with no pruning callback or
ordering metadata. `prepareLiteral` performs one mask-membership test when starting
a query literal. All constraint work happens between indexed literals.

## Representation and correctness

`ILStruct` has a 24-byte continuation summary:

- a 64-bit set of earlier indexed positions greater than the next literal;
- a 64-bit set of earlier indexed positions less than the next literal;
- minimum numbers of remaining literals below/above the next literal;
- a continuation-present flag and a common next predicate/polarity header.

The header fits in the padding, so adding it does not enlarge the summary. The
summary is computed after insertion's chosen literal ordering. At a shared
`LIT_END`, intersect the relation sets, take minimum counts, and retain the header
only if it agrees. Terminal clauses do not contribute a continuation. Removal
may leave weaker summaries but cannot make a summary unsound. Queries with 64 or
more literals, and relations for indexed clauses with more than 64 literals, use
conservative fallbacks; no truncation or oversized bit shifts are used.

For a complete earlier match domain `M_i`, union the query positions strictly
below each image in `M_i`. This is a bitset of possible images of a next literal
known to be below indexed position `i`. Intersect these support sets for the
earlier indexed positions required by the summary. The converse uses above sets.
Incomplete domains provide no negative evidence, unless a variant explicitly
finishes them with eager matching first. Eagerness is invoked only at a boundary;
it cannot recurse through an opcode callback.

Keep separate normal/complemented 64-bit masks. A complemented earlier match
supports only normal later images. Exclude its own original query position in
both polarities. Equality orientations share an original-position bit, but both
orientations are still executed if that position is allowed. Lazy per-query KBO
rows avoid ordering work until an actual relation or capacity check needs it.

Two further filters need no KBO:

- **Used positions:** if a complete earlier domain occupies one original query
  position, exclude it from both later polarity masks. This is an injectivity
  constraint, not an ordering assumption.
- **Common header:** if every continuation begins with the same predicate and
  polarity, restrict masks to query literals with that signed header. Equality
  orientation is unaffected.

A terminal-only summary can select the existing immediate-success scan directly.
Successes must remain reachable even when *all* literal positions are filtered.
The first allowed position, rather than position zero, controls normal success
emission; if no position survives, the matcher scans immediate successes.

The cached variants construct a domain mask once after its literal is entered,
and cache directional support unions on demand. Complete earlier domains are
stable: compatibility filtering removes matches only from the new current
literal, while final matching may permute earlier matches but does not remove
them. Invalidate that depth's cache whenever a new literal is entered. All query
arrays are fixed-size, per matcher; there is no per-CodeOp allocation.

## Variants

Set `CT_ORDER_VARIANT` before starting Vampire:

| Variant | Checks |
|---|---|
| `off` | No pruning; control for copied implementation |
| `used` | Complete singleton-domain exclusions |
| `greater` | Earlier `>` relations; no extra eager matching |
| `both` | Both relation directions; no extra eager matching |
| `eager` | `>` relations, completing needed domains eagerly |
| `capacity` | `>` relations plus remaining-literal capacity |
| `hybrid` | `>` relations plus used positions |
| `full` | Both directions, eager completion, capacity, used positions |
| `cached_used` | Used positions with cached domain masks |
| `cached_greater` | Cached used positions and `>` supports |
| `cached_both` | Cached used positions and both directional supports |
| `cached_full` | Full checks with domain/support caching |
| `header` | Common-header filter only |
| `terminal` | Terminal-only immediate-success shortcut |
| `cheap` | Cached used positions, header filter, terminal shortcut; no KBO |
| `balanced` | Cheap checks plus cached relations in both directions |

`CT_ORDER_SORT=sharing` retains the original insertion order: initial weight sort
followed by the sharing optimization. `CT_ORDER_SORT=weight` stops after the
initial weight sort in the experimental tree. The baseline always retains its
original sharing order, so the comparison includes any cost of lost sharing.

## Measurements

`CT_ORDER_RUN=paired` (default) calls each approach exactly once and always uses
the baseline's result for saturation. An optimized miss aborts in release and
debug builds. Different valid first results are permitted and counted. Query
timings include initialization, `next`, and reset. Measurement order alternates
by query to reduce cache bias. Index-maintenance times are recorded separately.
Only completed queries contribute to paired aggregate counts and timings.

`CT_ORDER_RUN=baseline` and `optimized` maintain/run only the chosen index and
matcher, permitting standalone checks. Different first results can change the
optimized standalone trajectory, so compare workload counts before interpreting
whole-run times.

```sh
# Detailed exploration (depth, match-domain, eager, comparison counters).
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCODETREE_ORDER_PROFILE=ON
cmake --build build -j 24

# Timing build: detailed profiling code is compiled out, CodeOp counts retained.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCODETREE_ORDER_PROFILE=OFF
cmake --build build -j 24
ln -sf build/vampire vampire

CT_ORDER_VARIANT=cheap CT_ORDER_SORT=sharing ./vampire -sa discount --time_limit 90 ~/condensed_detachment_4.p
CT_ORDER_VARIANT=cheap CT_ORDER_SORT=sharing ./vampire -sa otter --time_limit 90 ~/condensed_detachment_4.p

python3 Indexing/opt/bitset/run_experiments.py \
  --output /tmp/bitset-results --variants off cheap balanced \
  --sorts sharing weight --jobs 1
```

The runner records binary/input hashes and return codes. `results/stage1` and
`stage2` contain detailed-profile sweeps. `results/runtime` uses profiling OFF.
The final report distinguishes these measurements; profiling-on timings should
not be interpreted as deployment performance.

Debug tests use `build-debug/vtest run Inferences_CodeTreeSubsumptionAndResolution`.
They include differential insert/query/remove sequences, opposite polarities,
equality orientations, and 63/64/65-literal query boundaries.
