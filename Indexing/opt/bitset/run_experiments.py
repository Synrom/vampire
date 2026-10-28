#!/usr/bin/env python3
"""Run paired comparisons (or a standalone matcher) on the requested benchmark."""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--binary', default='./vampire')
parser.add_argument('--input', default='~/condensed_detachment_4.p')
parser.add_argument('--output', required=True)
parser.add_argument('--variants', nargs='+', default=['off', 'cheap', 'balanced'])
parser.add_argument('--sorts', nargs='+', default=['sharing', 'weight'])
parser.add_argument('--algorithms', nargs='+', default=['discount', 'otter'])
parser.add_argument('--mode', choices=['paired', 'baseline', 'optimized'], default='paired')
parser.add_argument('--jobs', type=int, default=1)
parser.add_argument('--repeats', type=int, default=1)
args = parser.parse_args()
binary = Path(args.binary).resolve()
problem = Path(args.input).expanduser().resolve()
outdir = Path(args.output)
outdir.mkdir(parents=True, exist_ok=True)
manifest = dict(vars(args), binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                input_sha256=hashlib.sha256(problem.read_bytes()).hexdigest())
(outdir / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')

def run(job):
    variant, sort, algorithm, repetition = job
    env = dict(os.environ, CT_ORDER_VARIANT=variant, CT_ORDER_SORT=sort, CT_ORDER_RUN=args.mode)
    cmd = [str(binary), '-sa', algorithm, '--time_limit', '90', str(problem)]
    dest = outdir / f'{variant}-{sort}-{algorithm}-{repetition}.log'
    with dest.open('w') as output:
        output.write(f'% Experiment: variant={variant} sort={sort} run={args.mode} repetition={repetition}\n')
        output.flush()
        result = subprocess.run(cmd, env=env, stdout=output, stderr=subprocess.STDOUT)
    return dict(variant=variant, sort=sort, algorithm=algorithm,
                repetition=repetition, returncode=result.returncode)

jobs = [(v, s, a, r) for r in range(args.repeats) for v in args.variants
        for s in args.sorts for a in args.algorithms]
results = []
with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
    for result in pool.map(run, jobs):
        results.append(result)
        print(json.dumps(result), flush=True)
(outdir / 'returncodes.json').write_text(json.dumps(results, indent=2) + '\n')
