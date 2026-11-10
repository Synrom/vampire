#!/usr/bin/env python3
"""Summarize per-site pruning concentration and exposure-weighted correlations."""
import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path

FEATURES = ['constraints_added', 'greater_constraints', 'smaller_constraints',
            'code_depth', 'literal_depth', 'ops_in_literal_before',
            'ancestor_branch_points', 'max_ancestor_fanout', 'subtree_ops',
            'downstream_branch_points', 'indexed_continuations',
            'literal_continuations', 'min_ops_to_lit_end',
            'predecessor_indexed_continuations', 'predecessor_fanout']

def correlation(xs, ys, weights):
    total = sum(weights)
    if not total:
        return None
    mx = sum(x*w for x,w in zip(xs,weights))/total
    my = sum(y*w for y,w in zip(ys,weights))/total
    cov = sum(w*(x-mx)*(y-my) for x,y,w in zip(xs,ys,weights))
    vx = sum(w*(x-mx)**2 for x,w in zip(xs,weights))
    vy = sum(w*(y-my)**2 for y,w in zip(ys,weights))
    return cov/math.sqrt(vx*vy) if vx and vy else None

def analyze(path):
    with open(path, newline='') as stream:
        rows = list(csv.DictReader(stream))
    sites = defaultdict(lambda: {'visits': 0, 'enabled_visits': 0, 'rejections': 0})
    for row in rows:
        assert 0 <= int(row['rejections']) <= int(row['enabled_visits']) <= int(row['visits'])
        for key in sites[row['site_id']]:
            sites[row['site_id']][key] += int(row[key])
    visited = {site: counts for site,counts in sites.items() if counts['enabled_visits']}
    ordered = sorted(visited.items(), key=lambda item: item[1]['rejections'], reverse=True)
    values = sorted(counts['rejections'] for counts in visited.values())
    total = sum(values)
    n = len(values)
    summary = {'feature_epochs': len(rows), 'sites': len(sites), 'enabled_sites': n,
               'never_visited_sites': sum(c['visits']==0 for c in sites.values()),
               'enabled_visits': sum(c['enabled_visits'] for c in sites.values()),
               'rejections': total,
               'zero_rejection_fraction': sum(v==0 for v in values)/n if n else None,
               'gini_rejections': sum((2*i-n-1)*v for i,v in enumerate(values,1))/(n*total) if total else None,
               'top_sites': [{'site_id': site, **counts} for site,counts in ordered[:20]]}
    for fraction in (.01, .05, .1):
        k = max(1, math.ceil(n*fraction)) if n else 0
        summary[f'top_{int(fraction*100)}pct_share'] = sum(c['rejections'] for _,c in ordered[:k])/total if total else None
    correlations = []
    for feature in FEATURES:
        observations = [r for r in rows if r[feature] and int(r['enabled_visits'])]
        x = [float(r[feature]) for r in observations]
        exposures = [int(r['enabled_visits']) for r in observations]
        rejected = [int(r['rejections']) for r in observations]
        rates = [r/e for r,e in zip(rejected, exposures)]
        correlations.append({'feature': feature, 'observed_epochs': len(x),
            'pearson_rejections': correlation(x, rejected, [1]*len(x)),
            'pearson_rejection_rate': correlation(x, rates, [1]*len(x)),
            'exposure_weighted_pearson_rate': correlation(x, rates, exposures)})
    return summary, correlations

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--output-prefix', type=Path)
    args = parser.parse_args()
    summary, correlations = analyze(args.csv)
    prefix = args.output_prefix or args.csv.with_suffix('')
    Path(str(prefix)+'.summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    with open(str(prefix)+'.correlations.csv', 'w', newline='') as out:
        writer = csv.DictWriter(out, fieldnames=list(correlations[0]))
        writer.writeheader()
        writer.writerows(correlations)
    print(json.dumps(summary, indent=2))
