# Forced-occupancy pruning

This branch keeps the original weight-then-sharing insertion order. At each indexed-literal boundary, it unions original query positions across each earlier **complete** match domain. A singleton domain forces that position to be occupied; subsequent indexed literals skip it. Lazy domains cannot justify exclusions.

One 64-bit mask controls both polarities; equality orientations share a position. Complete domains are cached per active depth and invalidated when that depth changes. Existing matching enforces substitution compatibility and the single complementary match restriction. Immediate successes remain reachable even when every candidate is excluded. Queries of length 64 or more use the original unfiltered path.

There are no header checks, ordering comparisons, terminal-summary shortcuts, or additional code-tree metadata. CodeOps remain unchanged. Benchmark instrumentation is kept separately on `optimization/occupancy-evaluation`.
