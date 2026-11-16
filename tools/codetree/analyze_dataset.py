#!/usr/bin/env python3
"""Analyze a completed capture using separate instrumented replay builds."""
import argparse
from pathlib import Path
import subprocess
import sys

REPO = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path, help='Output of capture.py, or a CTD collection')
    parser.add_argument('--cpus', type=int, required=True, help='Build jobs and concurrent analysis/perf processes')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--build-root', type=Path, default=REPO / 'build-codetree-analysis')
    parser.add_argument('--mode', choices=['first', 'all'], default='first')
    parser.add_argument('--perf', action='store_true', help='Also run Linux perf; default: counters/plots only')
    parser.add_argument('--weighting', choices=['query', 'baseline-time'], default='baseline-time')
    parser.add_argument('--analysis-binary', type=Path)
    parser.add_argument('--profile-binary', type=Path)
    args = parser.parse_args()
    if args.cpus <= 0:
        parser.error('cpus must be positive')
    capture = args.capture.resolve()
    dataset = capture / 'datasets' if (capture / 'capture.json').is_file() else capture
    output = args.output or capture / 'analysis'
    command = [sys.executable, str(REPO / 'tools/codetree/analyze.py'), str(dataset),
               '--output', str(output), '--build-root', str(args.build_root),
               '--jobs', str(args.cpus), '--cpus', str(args.cpus), '--profile-cpus', str(args.cpus),
               '--mode', args.mode, '--weighting', args.weighting]
    if not args.perf:
        command.append('--skip-perf')
    for flag, value in [('--analysis-binary', args.analysis_binary), ('--profile-binary', args.profile_binary)]:
        if value:
            command.extend([flag, str(value)])
    return subprocess.call(command)


if __name__ == '__main__':
    sys.exit(main())
