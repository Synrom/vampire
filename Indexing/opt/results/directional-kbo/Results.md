# Directional KBO experiment — 2026-11-09

The ranking below measures **only cumulative `ClauseMatcher::next` time**.
Compilation, annotation rebuilding, query setup, and process wall time are
excluded from the ranking. Each cell is the median of three complete runs.

| Variant | discount next (s) | Change vs occupancy | otter next (s) | Change vs occupancy |
|---|---:|---:|---:|---:|
| baseline-profile | 5.585 | +0.0% | 4.755 | +0.0% |
| packed-lazy | 5.256 | -5.9% | 4.772 | +0.3% |
| packed-eager | 5.229 | -6.4% | 4.777 | +0.4% |
| split-eager | 5.246 | -6.1% | 4.648 | -2.3% |
| cached-eager | 5.243 | -6.1% | 4.758 | +0.1% |

Negative percentages mean less matcher time. The four pruning implementations
are close on this workload; the differences among them are not a reliable
basis for discarding any before broader TPTP testing. Packed eager is the
default because it uses the compact inline representation and complete
domains, without the cached variant’s extra allocations.

## Preserved branches

All names below begin with
`optimization/code-tree-literal-occupancy-and-ordering`.

| Suffix | Implementation commit | Behavior |
|---|---|---|
| (none) | based on `e59594e3e` | Packed eager; includes this report and benchmark helper updates. |
| `-packed-lazy` | `f8853fa5a` | Two packed depth masks; skip unfinished earlier domains. |
| `-packed-eager` | `e59594e3e` | Same encoding; complete earlier domains first. |
| `-split-eager` | `c80bcd1e6` | Emit separate single-direction instructions; complete domains. |
| `-cached-eager` | `143f30b1c` | Pointer payload to constraints and a per-query allowed-mask cache. |

The base is `optimization/code-tree-literal-occupancy` at `a04fcdf77`.
That branch was not modified. All variants assert `sizeof(CodeOp) == 16`.

## Measurement method

- macOS arm64, Apple Clang, Release `-O3`, thin LTO, Z3 disabled.
- Original baseline commands were run first with `--time_limit 90`; see
  `baseline-discount.log` and `baseline-otter.log`.
- Focused comparisons used `--time_limit 900` so compilation time would not
  prevent completion. Every recorded run ended in Refutation.
- The baseline was rebuilt with only the identical timer hooks, captured in
  [baseline-instrumentation.patch](baseline-instrumentation.patch).
- `CODETREE_ORDER_PROFILE=ON` adds one scoped steady-clock timer per next call,
  and separate setup/compilation timers. No per-instruction timers were added.
- Runs were serial, with no builds or test suites running concurrently.
  Each variant had two initial runs, then one additional run in reverse variant
  order (`*-extra.json`). Baseline has three runs in `baseline-profile.json`.
- Raw logs and JSON preserve all measurements, including non-ranking timings.
  Later JSON files also record commands and binary SHA-256 hashes.
- The calls per completed proof differ slightly, so this is a whole-proof
  comparison, not a replay of an identical query stream:

| Variant | discount next calls | otter next calls |
|---|---:|---:|
| baseline-profile | 649105 | 226159 |
| packed-lazy | 649175 | 226195 |
| packed-eager | 649163 | 226192 |
| split-eager | 649163 | 226192 |
| cached-eager | 649163 | 226192 |

The packed eager discount samples were 5.164, 5.229, 5.241 seconds;
its otter samples were 4.655, 4.777, 4.793 seconds.
These small samples support further evaluation, not a general speedup claim.

## Validation

Each variant passed all 33 debug tests in
`Inferences_CodeTreeSubsumptionAndResolution`. The added differential test
compares ordering enabled/disabled on the same tree after insertions and
removals, including divergent shared paths, multiple supports, both directions,
equality orientations, opposite polarity, search structures, 32 indexed
literals, and 65 query literals. See `tests-*.log`. The final requested branch was rebuilt with profiling
disabled; both final smoke runs proved the problem (`final-*.log`), and its
33-test debug suite passed again (`tests-final.log`).

The >64-LitInfo case conservatively skips ordering pruning. Only the first
30 earlier indexed depths are encoded; later depths remain fully matchable.
Ordering configuration must stay fixed throughout a tree’s lifetime.

## Reproduce

```sh
cmake -S . -B build-order-occupancy -DCODETREE_ORDER_PROFILE=ON
cmake --build build-order-occupancy -j 8 --target vampire
python3 scripts/benchmark_directional_kbo.py LABEL --repetitions 3 --time-limit 900
```

The `vampire` symlink points at that build. Switch to the desired variant
before building. For baseline measurements, apply the supplied instrumentation
patch to an isolated checkout of `a04fcdf77`, then use the same build settings
and benchmark script. Profiling defaults off for normal builds.
