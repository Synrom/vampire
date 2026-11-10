# Directional pruning statistics branch

Branch: `optimization/code-tree-literal-occupancy-and-ordering-stats`.
Based on the packed eager implementation; `CodeOp` is still 16 bytes.
Instrumentation is always enabled on this diagnostic branch. It adds lookups,
counter updates, and a lock per pruning visit; do not use its runtimes to rank
pruning implementations.

## Run and analyze

```sh
cmake --build build-order-occupancy -j 8 --target vampire
VAMPIRE_KBO_PRUNING_CSV=discount-pruning.csv ./vampire -sa discount --time_limit 900 ~/Development/AutomatedDeduction/problems/condensed_detachment_4.p
python3 scripts/analyze_kbo_pruning.py discount-pruning.csv
```

Output defaults to `kbo-pruning.csv` in the process's working directory. The
environment variable chooses a different file (its parent directory must
exist). CSV output is written by the statistics-reporting hook, including
normal resource-limit reporting, and again at normal process exit. A repeated
report overwrites the file with cumulative counts. No file is written if no
clause tree has been annotated. Hard kills/crashes cannot guarantee output.
Use distinct paths for concurrent processes; counters and IDs are process-local.

The analysis script writes `<prefix>.summary.json` and
`<prefix>.correlations.csv`, plus `<prefix>.sites.csv` with every site ranked
by rejections, aggregated rates, and cumulative rejection shares. Use `--output-prefix` to change that prefix.

## Identity and structural changes

A **site** is a tree-local structural prefix plus its newly added ordering
constraints. Prefixes include instruction operands and clause-wide variable
identities at literal boundaries, but exclude pruning instructions, addresses
of code blocks, and search-structure layout. Thus annotation rebuilds, block
relocation, and insertion/removal of unrelated alternatives retain site IDs.
A changed constraint mask is a new site. Identical semantic sites reintroduced
later in the same tree retain their identity; a newly constructed tree gets a
new tree ID. IDs are not intended to match across runs.

Each row is one `(site_id, feature_epoch)`. If the structural features of a site
change, a new epoch starts, retaining earlier counters. Unchanged features
reuse the existing row. Removing instructions or destroying trees does **not**
discard their counters. Pointer mappings are invalidated before mutations to
avoid confusing allocator address reuse with instruction identity.

Aggregate rows by `site_id` for the distribution across pruning sites. Use the
epoch rows for feature correlations, since structural features can change as
clauses are inserted and removed. Zero-visit rows are retained.

## Counters

- `visits`: executions of this pruning instruction, including disabled checks.
- `enabled_visits`: executions where ordering pruning was enabled. This is the
  denominator for `rejection_rate`; unsupported large query masks are excluded.
- `rejections`: current query-literal candidates rejected **at this operation**.
  Candidates are LitInfos: equality orientations and polarities are distinct.
  A candidate rejected earlier cannot contribute to a later operation.
- `constraints_tested`: individual depth constraints examined before acceptance
  or the first failure; early exit is respected.
- `query_infos_sum`: sum of the matcher's available LitInfo counts over visits.
- `opposite_visits`, `opposite_rejections`: corresponding opposite-polarity counts.
- `share_of_all_rejections`: row rejections divided by total recorded rejections.
  Sum by site to measure that site's contribution. Undefined ratios are blank.

These are candidate rejection counts, not clauses subsumed or actual operations
saved. They are conditional on traversal order, earlier checks, occupancy, and
the query workload. A high rate does not establish a causal performance benefit.

## Features

Depths are zero-based/counts of preceding instructions. Ordinary instructions
count toward `code_depth`; pruning instructions and search dispatch do not.

| Column | Meaning |
|---|---|
| `greater_mask`, `smaller_mask` | Added constraints on earlier indexed depths. |
| `constraints_added`, `greater_constraints`, `smaller_constraints` | Popcounts of those masks. |
| `code_depth` | Ordinary instructions preceding this site on the chosen prefix. |
| `literal_depth` | Number of indexed literals already completed. |
| `ops_in_literal_before` | Ordinary instructions consumed within the current literal. |
| `ancestor_branch_points` | Decision nodes with more than one outgoing edge on the path. |
| `max_ancestor_fanout` | Largest outgoing-edge count on the path, including search dispatch. |
| `predecessor_indexed_continuations` | SUCCESS occurrences reachable from the preceding structural operation, including its alternatives: sharing before this check. |
| `predecessor_fanout` | Outgoing-edge count at that structural operation. |
| `indexed_continuations` | SUCCESS occurrences reachable after this pruning operation: indexed clause continuations sharing it. Duplicate clause insertions count separately. |
| `literal_continuations` | Reachable LIT_END occurrences before entering another literal. |
| `downstream_branch_points` | Decision nodes in the remaining subtree, including later literals. |
| `subtree_ops` | Remaining ordinary operations, including LIT_END/SUCCESS/FAIL, across the subtree. |
| `min_ops_to_lit_end` | Minimum ordinary operations before the next LIT_END; zero if it immediately follows, blank if none is reachable. |

The tree shares prefixes rather than merging control-flow paths after a split.
Continuation counts measure that sharing; branching counts measure where it
diverges. They are current structural features, not historical insertion counts.

## Distribution and correlation interpretation

The summary aggregates epochs by site and reports the top 1%, 5%, and 10%
shares of all rejections, the top 20 sites, the fraction of enabled sites with
zero rejections, and the Gini coefficient over enabled sites (including zeros).
Higher concentration/Gini means a smaller subset accounts for more pruning.
Unvisited sites are counted separately. With no rejections, shares/Gini are null.

The correlations CSV reports Pearson correlations with raw rejections and with
rejection rate, plus an exposure-weighted rate correlation using enabled visits
as weights. Empty/constant features give blank correlations. Epochs from one
site are dependent observations; these are descriptive statistics, not
independent samples, significance tests, or causal estimates. Compare both
counts and rates: exposure alone can make an instruction dominate raw pruning.
