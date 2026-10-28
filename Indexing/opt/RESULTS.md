> Historical per-CodeOp experiment. The active, much smaller implementation and new comparisons are in [bitset/](bitset/README.md).

# Results: order-preserving code-tree pruning

Both requested runs completed with refutations before the 90-second limit.
Each query ran the original and optimized `ClauseMatcher::next` once, and the
original result drove saturation. No randomized candidate skipping was used.

| Saturation | Paired queries | Baseline CodeOps | Optimized CodeOps | Saved | Reduction |
|---|---:|---:|---:|---:|---:|
| discount | 648,436 | 666,285,944 | 414,055,090 | 252,230,854 | 37.86% |
| otter | 226,159 | 536,343,845 | 330,924,444 | 205,419,401 | 38.30% |

**Conclusion:** roughly 38% fewer executed CodeOps in both saturation algorithms
is substantial enough to justify further development. This is evidence of
operation-count potential, not evidence of a wall-clock speedup. The prototype
executes both matchers, maintains two indexes, and adds ordering comparisons,
metadata, and domain checks. These extra costs are not CodeOps. Extra eager
matching *is* included in the optimized CodeOp total.

## Correctness and search results

- **discount:** 252,978 baseline successes and 252,978 optimized successes; **0 missed successes**. The first result differed on 6,036 queries. Refutation in 80.636 s in the paired harness.
- **otter:** 135,638 baseline successes and 135,638 optimized successes; **0 missed successes**. The first result differed on 1,538 queries. Refutation in 69.623 s in the paired harness.
- The debug subsumption/subsumption-resolution suite passed **33/33** tests,
  including a new multi-literal regression query. Existing cases cover reversed
  equality orientations, positive/negative results, and subsumption resolution.
- An initial otter run exposed nested eager matching corrupting static scratch
  stacks. Those stacks are now local in the optimized copy. Both full runs above
  were rerun after the fix. This is empirical validation, not an exhaustive proof
  of implementation correctness.

## Pruning diagnostics

These count rejected operation visits, not saved CodeOps; they cannot be used to
attribute the percentage saving independently to each mechanism.

| Saturation | Earlier-domain rejections | Remaining-capacity rejections |
|---|---:|---:|
| discount | 77,297,425 | 16,661,669 |
| otter | 64,535,983 | 11,005,179 |

## Scope and next step

The prototype preserves the original literal ordering and sharing, weakens
constraints at shared prefixes, and uses conservative necessary conditions for
partial orders and earlier match domains. It does not implement a full Hall
matching test across all remaining literal domains. Deletion can leave weaker
constraints on shared prefixes. Thus the measured savings are for this approach,
not a theoretical upper bound.

The next useful experiment is a standalone optimized matcher with cached domain
checks and compact metadata, compared against a standalone baseline on identical
search trajectories. That would establish whether these CodeOp savings overcome
the cost of the ordering machinery. The current timings include both matchers
and should not be used to estimate that speedup.

Built with CMake Release, GCC 11, without Z3. Both benchmark processes ran
concurrently on this machine; timings are contextual only. Exact requested
commands and the algorithm are documented in [README.md](README.md).

Input SHA-256: `d908978970aa8425be5c44af2e2fda14b49ddd522898c6777c142b0cbf97e50f`.

Raw logs: [discount](results/discount.log), [otter](results/otter.log),
[debug tests](results/unit-tests.log). Machine-readable counts:
[summary.json](results/summary.json).
