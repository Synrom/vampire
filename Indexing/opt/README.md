> The active experiment uses these per-CodeOp files with occupancy pruning ported
> from `optimization/code-tree-literal-occupancy`. The alternative bitset
> implementation remains in [bitset/](bitset/README.md), but is not built here.

## Occupancy and attribution experiment

The default is `CT_ORDER_VARIANT=full`, `CT_ORDER_RUN=paired`. `perform` runs
both matchers on each query, alternates their execution order, and uses the
baseline result to drive saturation. `CT_ORDER_RUN=optimized` runs only the new
tree; `baseline` runs only the branch's baseline tree. A separate checkout of
local master is needed to measure actual master wall time.

Occupancy is derived from complete earlier match domains at literal entry.
Equality orientations and polarities share a query position. Positions exclusively
required by an earlier literal are skipped in `prepareLiteral`. Clauses exceeding
64 literals disable this bitset optimization, without disabling order pruning.
The first executed position is remembered so SUCCESS still gets returned once
when position zero was skipped, including when all positions are occupied.

The reproduction driver is `results/occupancy/run.py`. Run `--mode standalone`
for actual master and optimized end-to-end runs (use `--master` to select a
master binary), or `--mode prefix` for all seven paired variants on the first
100,000 queries. The latter sets `CT_ORDER_CHECKPOINT=100000` and deliberately
sends SIGTERM after the checkpoint; its exit code 3 is expected, not a mismatch.
Use `--prefix-queries` to change that horizon. Checkpoints also work in ordinary
90-second runs without stopping the process. See
[the measured results](results/occupancy/RESULTS.md).

For ablation, use `no_greater`, `no_less`, `no_depth`, `no_above`, `no_below`, or
`no_occupancy`. Each disables the named component of `full`. Disabling occupancy
only disables early skipping; the order check still rejects unsupported images
and excludes forced positions from depth counts. Compare completed paired runs
on identical baseline trajectories. Marginal savings need not add up: checks
interact, and eager completion can itself execute additional instructions.

`Order shares` additionally reports percentages of all rejection events, including
occupancy skips, with `total` as the denominator (zero for empty totals).
`Order entry causes` records the subset rejected at exactly `Matcher::entry`:
the tree root or the CodeOp immediately following a `LIT_END`. Alternatives and
search-structure targets do not qualify just because no query symbols have been
consumed. Per-cause counts and the root subset are reported. `checks_retained_pct`
uses non-occupancy rejections as its denominator; `retained_pct` adds the occupancy
skips to numerator and denominator, since that optimization already acts at entry.
These are observations on the unchanged full-pruning traversal, not a simulation
of the possibly different traversal/domain completion of an entry-only matcher.

`Order causes` reports rejection visits, not saved CodeOps. Earlier-domain
failures are charged to the first failing domain, separating direction failures
from absence of a distinct, polarity-compatible image (`support`). Depth
failures are disjoint: below-only, above-only, both, and union-only. Earlier
failures short-circuit depth checking. `occupancy_skipped` counts skipped LitInfo
records, so orientations/polarities can count separately. On timeout, diagnostic
counters can include the interrupted query, whereas CodeOps aggregate only
completed queries.

Build and test with:

```sh
cmake -S . -B build-order-occupancy -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_Z3=ON
cmake --build build-order-occupancy --target vampire -j 8
cmake -S . -B build-order-occupancy-debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_DISABLE_FIND_PACKAGE_Z3=ON
cmake --build build-order-occupancy-debug --target vtest -j 8
build-order-occupancy-debug/vtest run Inferences_CodeTreeSubsumptionAndResolution
ln -s build-order-occupancy/vampire vampire
CT_ORDER_RUN=paired CT_ORDER_VARIANT=full ./vampire -sa discount --time_limit 90 ~/Development/AutomatedDeduction/problems/condensed_detachment_4.p
```


# Order-preserving code-tree experiment

This directory contains independent copies of `ClauseCodeTree.{hpp,cpp}` and
`CodeTree.{hpp,cpp}`, named `OptClauseCodeTree` and `OptCodeTree` in C++.
The original tree's literal ordering and code sharing are retained. The original
implementation is changed only to count executed operations.

## Partial-order rules

Write `L` for the indexed literal currently being matched, `q` for its proposed
query image, and `M_i` for the complete set of recorded query images of an earlier
indexed literal `L_i`. Comparisons use Vampire's global simplification ordering
(KBO for the requested runs). No linear extension of the partial order is used.

1. If `L > L_i`, a necessary condition is that some image `r` in `M_i` satisfies
   `q > r`. If `L < L_i`, require some `r` with `q < r`. The image must also use a
   different query literal, and both images cannot be complemented. Reject `q`
   if any earlier literal has no such support. Incomparable indexed literals
   impose no ordering condition. This considers all earlier literals, rather
   than choosing one minimum, which need not exist in a partial order. Supports
   for different earlier literals are allowed to differ; this is a conservative
   relaxation of simultaneous compatible matching.

   A missing support is meaningful only for a complete domain. Complete a lazy
   earlier domain on demand when its ordering relation is used. Its additional
   CodeOps count against the optimization. Eager-result scratch stacks are local
   in this copy because these requests can nest. Static scratch stacks from the
   original implementation are not safe in this new call pattern.

2. Let `b` and `a` be the numbers of *remaining* indexed literals strictly below
   and strictly above `L`. At least `b` distinct available query literals must
   admit an image below `q`, and at least `a` must admit an image above `q`.
   Their union must contain at least `a+b` distinct literals. Exclude `q` itself
   and query literals forced by complete singleton earlier domains. Thus the
   capacity test also accounts for already matched literals. Remaining indexed
   literals incomparable with `L` impose no directional capacity requirement.
   Non-singleton earlier domains are not reserved, making the bound conservative.

Both rules follow from stability of strict ordering under substitution and the
matcher's injective use of query literals. A strict indexed comparison requires
the same strict comparison between the actual images; an incomparable query pair
cannot satisfy it. Different query variables remain distinct when comparing query
literals, as the query is not instantiated by this subsumption test.

For subsumption resolution, compare the complemented query literal when the
match uses the opposite polarity. For remaining-literal capacity, allow either
polarity when resolution is still possible; this relaxation can overestimate
capacity but cannot underestimate it. Count each query literal once even if both
polarities or both equality orientations are available. The union bound handles
overlap between the above/below sets.

## Shared prefixes

Each compiled literal's operations receive its relations to earlier positions
and its remaining above/below counts. On insertion, every shared operation keeps
only relations agreed upon by both paths, and the minimum of each count. These
are necessary conditions for every clause using that operation. Later operations
can carry stronger constraints after paths diverge. Alternatives are placed on
the backtracking stack before the current operation is pruned, so rejecting one
branch does not reject its siblings. Search-structure landing operations and
successes have no ordering constraints.

Removal retains weakened constraints on surviving prefixes. This is safe, but
can reduce pruning until those prefixes are removed. This prototype deliberately
uses per-operation shared metadata and per-query comparison matrices; its memory
and checking overhead need optimization before runtime claims are appropriate.

## Paired experiment

`CodeTreeSubsumptionIndex` maintains both trees from the same insert/remove events.
`CodeTreeForwardSubsumptionAndResolution::perform` calls each `ClauseMatcher::next`
exactly once per nonempty-index query. Randomized candidate skipping is disabled.
The baseline result drives the actual inference and search path. The experiment
aborts even in release builds if baseline succeeds and optimized fails. Different
valid first results are counted separately and are permitted.

Both `next` methods record executed CodeOps, including eager execution and scans
for immediate successes. Rejected operations are not dispatched and are not
counted; the cost of metadata checks, comparisons, tree maintenance, and the
paired harness is not a CodeOp. Aggregate operation totals include only completed
paired queries, so a query interrupted by timeout is not partially compared.
Prune counters count rejected operation visits, not whole literals or saved ops.

## Build and reproduce

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 24
ln -sf build/vampire vampire
./vampire -sa discount --time_limit 90 ~/condensed_detachment_4.p
./vampire -sa otter --time_limit 90 ~/condensed_detachment_4.p
```

The build additionally required a missing `<fstream>` include in `Saturation/LRS.hpp`.

Debug validation:

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --target vtest -j 24
build-debug/vtest run Inferences_CodeTreeSubsumptionAndResolution
```

See `results/` for the final run logs and `RESULTS.md` for the measurements.
