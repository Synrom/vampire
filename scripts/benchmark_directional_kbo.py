#!/usr/bin/env python3
"""Serial runs of the requested workload; retain logs and focused matcher timings."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('label')
p.add_argument('--binary', default='./vampire')
p.add_argument('--repetitions', type=int, default=3)
p.add_argument('--time-limit', type=int, default=900)
p.add_argument('--problem', default=str(Path.home() / 'Development/AutomatedDeduction/problems/condensed_detachment_4.p'))
p.add_argument('--output', type=Path, default=Path('Indexing/opt/results/directional-kbo'))
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
rows = []
for repeat in range(1, a.repetitions + 1):
    for strategy in ('discount', 'otter'):
        command = [a.binary, '-sa', strategy, '--time_limit', str(a.time_limit), a.problem]
        start = time.monotonic()
        run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        wall = time.monotonic() - start
        name = f'{a.label}-{strategy}-{repeat}.log'
        (a.output / name).write_text(run.stdout)
        if run.returncode or '% Termination reason: Refutation' not in run.stdout:
            raise RuntimeError(f'{name}: unexpected exit {run.returncode}; see log')
        row = dict(label=a.label, strategy=strategy, repeat=repeat, wall=wall, log=name)
        row['elapsed'] = float(re.search(r'% Time elapsed: ([\d.]+) s', run.stdout)[1])
        profile = re.search(r'codetree-profile next=([\d.]+) setup=([\d.]+) compile=([\d.]+) calls=(\d+)', run.stdout)
        if profile:
            row.update(zip(('next', 'setup', 'compile'), map(float, profile.groups()[:3])))
            row['calls'] = int(profile[4])
        rows.append(row)
        print(json.dumps(row), flush=True)
        (a.output / f'{a.label}.json').write_text(json.dumps(rows, indent=2) + '\n')
