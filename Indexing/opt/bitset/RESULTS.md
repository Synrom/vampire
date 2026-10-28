# Bitset/boundary exploration: results

## Recommendation

**Keep sharing-based insertion and use the `cheap` boundary variant.** It uses
cached singleton-domain exclusions, a common predicate/polarity header filter,
and a terminal-only shortcut. It performs **no KBO comparisons**. This is now the
default experimental variant; paired checking remains enabled by default.

Compared with the original matcher on exactly the same query stream:

| Saturation | Baseline CodeOps | Cheap CodeOps | CodeOps saved | Matcher time reduction |
|---|---:|---:|---:|---:|
| discount | 666,285,944 | 473,091,791 | 29.00% | 11.7% |
| otter | 536,343,845 | 332,630,931 | 37.98% | 24.5% |

Matcher time reductions are medians of three complete runs per algorithm with
**detailed profiling compiled out**. Timing includes initialization, `next`, and
reset, with baseline/optimized measurement order alternating by query. These are
matcher timings, not whole-prover speedups. The baseline's result always drives
saturation. CodeOp counts are identical across the repetitions.

- discount: measured time reductions ranged from **11.2% to 12.2%**.
- otter: measured time reductions ranged from **24.0% to 26.0%**.

The previous per-CodeOp prototype saved 37.86%/38.30% of CodeOps. The much simpler
cheap variant retains about **77% of those savings on discount and 99% on otter**.
The CodeOp itself is back to **16 bytes**, with **zero pruning callbacks in the
opcode dispatch loop**. The shared experimental layout adds **24 bytes per
`ILStruct`** (88 → 112 bytes), not per operation. The header uses existing padding.
A production version containing only the winning checks would need just the
common header and continuation flag, rather than either ordering bitset.

## What the experiments showed

Fourteen variants received full benchmark runs, followed by weight-vs-sharing
comparisons and repetitions of the winner. The configuration matrix and exact
commands are in [README.md](README.md).

### Ordering checks: more pruning does not necessarily pay

The following **timing-build** measurements all retain the sharing sort. Negative
time changes mean faster matching. Cheap is the three-run median; the other
entries are single exploratory runs.

| Variant | Discount CodeOps saved | Discount matcher time change | Otter CodeOps saved | Otter matcher time change |
|---|---:|---:|---:|---:|
| Copied matcher, pruning off | 0.00% | +1.4% | 0.00% | +0.8% |
| Cheap | 29.00% | -11.7% | 37.98% | -24.5% |
| Cheap + cached both-direction KBO | 36.47% | +15.9% | 42.64% | -15.9% |
| Cached both-direction KBO + eager + capacity + used | 30.28% | +20.3% | 36.84% | -14.0% |

The pruning-off control reproduces the baseline result and CodeOp count exactly;
its timing differs by about 1%, providing a useful check on the copied matcher
and measurement harness. KBO can preserve much of the initial prototype's pruning,
but its extra query comparisons still make discount slower. It is not the best
common default for these two algorithms.

The first **profiling-enabled** ablation sweep separated the proposed mechanisms.
These ratios include detailed diagnostic counters and are used to identify
promising variants, not as deployment timing claims.

| Variant | Discount CodeOps saved | Discount time/baseline | Otter CodeOps saved | Otter time/baseline |
|---|---:|---:|---:|---:|
| `used` | 19.75% | 0.926 | 30.09% | 0.781 |
| `greater` | 9.32% | 1.147 | 5.58% | 1.039 |
| `both` | 13.52% | 1.329 | 9.21% | 1.102 |
| `eager` | 9.34% | 1.144 | 5.59% | 1.037 |
| `capacity` | 10.46% | 1.308 | 6.63% | 1.105 |
| `hybrid` | 25.86% | 1.048 | 33.18% | 0.803 |
| `full` | 30.28% | 1.238 | 36.84% | 0.873 |

- **Greater-only:** cheaper than both directions, but still slower than baseline
  in this ablation. It saves only 9.32%/5.58% of CodeOps by itself.
- **Forced eagerness:** changes the saving by only about 0.02/0.01 percentage
  points. Existing eager matching has already completed almost all useful domains.
- **Capacity:** adds approximately 1.14/1.05 percentage points over greater-only,
  with an unfavorable timing cost. Common-prefix summaries weaken many capacity
  bounds to zero, especially at the root.
- **Caching:** domain/support caching helps, but does not remove the initial KBO
  comparison cost. Cached full ordering remains slower on discount.
- **Headers:** alone save 11.19%/10.33% of CodeOps, mostly eliminating one-opcode
  predicate mismatches. Their standalone timing gain is modest.
- **Terminal-only:** alone saves just 0.48%/0.23% of CodeOps. It is mainly useful
  for avoiding unnecessary filtering at leaves in the combined cheap variant.

### Where the useful work is

Depth zero is the first indexed literal. The detailed `cheap` profiles show:

- **discount:** 90.6% of saved opcode dispatches occur at depths **1–3**. Root work is 15.7% of baseline dispatches. 60.6% of complete domain visits are singletons. The deepest recorded execution level was 9. In the full ordering profile, 26.4% of common directional relation occurrences are `<`.
- **otter:** 85.7% of saved opcode dispatches occur at depths **1–3**. Root work is 9.0% of baseline dispatches. 64.6% of complete domain visits are singletons. The deepest recorded execution level was 10. In the full ordering profile, 36.5% of common directional relation occurrences are `<`.

These domain and relation percentages count visits at queried boundaries, not
unique indexed clauses. They explain why singleton masks are attractive and why
assuming that sharing preserves a mostly descending ordering is unreliable.
The header filter removes many cheap one-instruction failures; singleton
exclusions remove fewer attempts but often avoid traversing a whole literal.
This is why skipped-literal counts alone also overstate the value of headers.

## Weight-only insertion

Only the experimental tree changes insertion order. The baseline keeps its
original sharing sort and drives the same saturation trajectory. Thus these
measurements include both lost sharing and any improvement in pruning.

All changes below are relative to the original sharing-sorted baseline; positive
CodeOp changes mean more work.

| Variant | Experimental sort | Discount CodeOp change | Discount matcher time change | Otter CodeOp change | Otter matcher time change |
|---|---|---:|---:|---:|---:|
| `off` | sharing | -0.00% | +1.4% | -0.00% | +0.8% |
| `off` | weight | +14.80% | +14.0% | +15.45% | +21.1% |
| `cheap` | sharing | -29.00% | -11.7% | -37.98% | -24.5% |
| `cheap` | weight | -16.47% | -6.1% | -28.34% | -15.0% |
| `balanced` | sharing | -36.47% | +15.9% | -42.64% | -15.9% |
| `balanced` | weight | -22.40% | +8.9% | -31.76% | -11.6% |

**Retain the sharing sort for this workload.** Weight-only insertion increases
unpruned executed CodeOps by about 15%, and is slower even with the best cheap
filters. It reduces experimental index-maintenance time, particularly for otter,
but that saving is much smaller than the added matching cost. This is an index
comparison on a fixed query stream; it does not claim that a different inference
order could never produce a shorter proof search on another problem.

## Validation and scope

- **48 complete benchmark runs**, all refutations within 90 seconds; **20,990,280 paired checks**, zero baseline-success/optimized-failure cases. Each configuration reuses the same 648,436 discount and 226,159 otter query positions; these are repeated checks, not that many distinct queries.
- All **35 debug tests** passed for each of **16 variants × 2 insertion sorts**
  (1,120 test passes). The new differential test exercises insertions, queries,
  removals, opposite polarity and equality orientations; a separate test covers
  63/64/65-literal query sizes and fallback behavior.
- Debug `perform` also validates optimized successes with the independent SAT
  subsumption checker. Release builds retain the explicit missed-result abort.
- The cheap variant returns a different valid first result on 288 discount and
  160 otter queries. Pairing prevents these differences from changing the measured
  search path.
- The runs used CMake Release/GCC 11 without Z3. Exploratory jobs used 2–4 concurrent
  processes; the winner's two repetitions used two. Alternating per-query timing
  and the pruning-off control reduce, but do not eliminate, timing noise. No
  new instruction-set requirement or architecture-specific popcount flag was used.
- This is one benchmark. The evidence supports proceeding with cheap boundary
  masks, not a universal speedup claim for all Vampire workloads.

## Suggested next implementation step

Port the cheap checks to the original matcher with only a small per-literal
header/continuation summary and cached query-position masks. Keep the shared
literal order. The opcode interpreter needs only the preparation-time mask check,
not a per-operation callback. If KBO pruning is pursued further, the next useful
experiment is to compute only comparisons surviving header/occupied-position
filters, rather than building a whole signed query-order row on first use.

Raw logs and hashes are in [results/](results/); the machine-readable aggregation
is [summary.json](results/summary.json). Reproduce the tables with
`python3 Indexing/opt/bitset/analyze_results.py`.
