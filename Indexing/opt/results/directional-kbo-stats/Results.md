# Pruning-site statistics validation

Collected on `condensed_detachment_4.p` with the statistics branch. These are
counter/structure measurements, not performance measurements.

## Discount (completed refutation)

- 3,213 stable pruning sites; 3,089 executed with pruning enabled.
- 38,847,291 enabled visits and 21,589,684 candidate rejections.
- Top 1% of executed sites: 50.7% of rejections.
- Top 5%: 80.3%; top 10%: 89.6%.
- Gini coefficient of rejections across executed sites: 0.9247.

Pruning is strongly concentrated on this workload. See `discount.sites.csv`
for every site, `discount.summary.json` for the top sites and concentration,
and `discount.correlations.csv` for feature correlations. These correlations
are descriptive and workload-dependent, not causal estimates.

The raw epoch ledger is retained as `discount.csv.gz`:

```sh
cd Indexing/opt/results/directional-kbo-stats
gzip -dc discount.csv.gz > discount.csv
python3 ../../../../scripts/analyze_kbo_pruning.py discount.csv
```

Validation: all 34 code-tree subsumption/resolution debug tests passed,
including stable identity under rebuild/insertion/removal. Three analysis
unit tests cover epoch aggregation, zero-exposure/no-pruning data, and known
correlations. A separate one-second timeout run produced a valid CSV; its
log is in `timeout.log`. Ledger validation checked unique (site,epoch) keys,
mask popcounts, exposure/rejection bounds, tested-constraint bounds, sharing
counts, and rejection shares summing to one.
