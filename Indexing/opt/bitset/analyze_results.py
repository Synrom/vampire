#!/usr/bin/env python3
"""Extract paired counts/timings and detailed profiles from experiment logs."""
import json
from pathlib import Path
import re

root = Path(__file__).parent
records = []
for path in sorted((root / 'results').rglob('*.log')):
    text = path.read_text()
    lines = re.findall(r'^% Order pruning: (.*)$', text, re.M)
    if not lines:
        continue
    record = {}
    for key, value in re.findall(r'(\w+)=(\S+)', lines[-1]):
        record[key] = int(value) if value.isdigit() else value
    record.setdefault('sort', 'sharing')
    record['file'] = str(path.relative_to(root))
    record['profiled'] = path.parent.name in ('stage1', 'stage2')
    record['algorithm'] = 'discount' if 'discount' in path.name else 'otter'
    reason = re.search(r'^% Termination reason: (.*)$', text, re.M)
    record['termination'] = reason[1] if reason else 'unknown'
    elapsed = re.search(r'^% Time elapsed: ([\d.]+)', text, re.M)
    record['elapsed_seconds'] = float(elapsed[1]) if elapsed else None
    if record['master_ops']:
        record['codeop_reduction_percent'] = 100 * (1 - record['optimized_ops'] / record['master_ops'])
    if record['master_ns']:
        record['matcher_time_ratio'] = record['optimized_ns'] / record['master_ns']
    for label in ['depth', 'execution', 'layout']:
        record[label] = [dict((k, int(v)) for k, v in re.findall(r'(\w+)=(\d+)', line))
                         for line in re.findall(r'^% Order ' + label + r': (.*)$', text, re.M)]
    records.append(record)
(root / 'results' / 'summary.json').write_text(json.dumps(records, indent=2) + '\n')
for r in records:
    if not r['profiled']:
        print(f"{r['algorithm']:8} {r['variant']:12} {r['sort']:7} "
              f"saved={r.get('codeop_reduction_percent', 0):6.2f}% "
              f"time={r.get('matcher_time_ratio', 0):.3f}x mismatches={r['mismatches']}")
