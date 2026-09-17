#!/usr/bin/env python3
"""Sweep both ablation branches and generate their CSVs and line-only PDFs.

Run with build-profile/.venv/bin/python scripts/next_op_ablation_sweep.py
Add --dry-run to validate both branches, all threshold replacements, statistics
parsing and plotting with synthetic data, without compiling or running Vampire
or replacing previous results. Requires matplotlib, git, cmake and make.

Real runs use committed local branch tips in temporary detached worktrees, Debug
builds (invariants enabled), and TIME_PROFILING. Your checkout is never switched
or edited. All four results replace the previous results only after both sweeps
succeed. Logs are retained under build-vscode/ablation-sweep-logs/.
"""
import argparse
import csv
from contextlib import contextmanager
from datetime import datetime
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile

REPO = Path(__file__).resolve().parent.parent
APPROACHES = (
    'current', 'master', 'ordering', 'simplifyCanEnterFirst',
    'simplifyCanEnterSecond', 'removeTouchedSlots', 'saveBindingsDirectly',
    'ilstructSimplification',
)
COLORS = ('#2a78d6', '#eb6834', '#1baf7a', '#eda100', '#e87ba4', '#008300', '#4a3aa7', '#e34948')
RUNS = (
    ('optimization/next-op-ablation', 'ablation', ('avg_us', 'total_us')),
    ('optimization/next-op-ablation-record-ops', 'ablation-record-ops', ('avg_codeops', 'avg_nextops')),
)
THRESHOLD_FILES = {
    'Indexing/ClauseCodeTree.cpp': 1,
    'Indexing/ordering/ClauseCodeTree.cpp': 2,
    'Indexing/simplify-canEnterLiteral-first/ClauseCodeTree.cpp': 1,
    'Indexing/simplify-canEnterLiteral-second/ClauseCodeTree.cpp': 1,
    'Indexing/remove-touchedSlots/ClauseCodeTree.cpp': 1,
    'Indexing/save-bindings-directly/ClauseCodeTree.cpp': 1,
    'Indexing/ilstruct-simplification/ClauseCodeTree.cpp': 1,
}
THRESHOLD_RE = re.compile(r'(static const unsigned int nextThreshold\s*=\s*)(\d+)(\s*;)')
TIME_RE = re.compile(
    r'Clause Matcher next (\S+)\s+\(total:\s*([\d.]+)\s*(\S+),\s*'
    r'avg:\s*([\d.]+)\s*(\S+),\s*cnt:\s*(\d+),'
)
UNIT_TO_US = {'ns': .001, 'μs': 1., 'µs': 1., 'us': 1., 'ms': 1000., 's': 1e6}


def git(*args):
    return subprocess.check_output(['git', '-C', str(REPO), *args], text=True).strip()


def set_threshold(source, threshold):
    """Require exactly eight replacements, including both ordering constants."""
    for name, expected in THRESHOLD_FILES.items():
        path = source / name
        updated, count = THRESHOLD_RE.subn(
            lambda m: f'{m[1]}{threshold}{m[3]}', path.read_text())
        if count != expected:
            raise RuntimeError(f'{name}: expected {expected} thresholds, found {count}')
        path.write_text(updated)
        values = [int(m[2]) for m in THRESHOLD_RE.finditer(path.read_text())]
        if values != [threshold] * expected:
            raise RuntimeError(f'{name}: threshold replacement failed')


def check_call_counts(calls):
    missing = set(APPROACHES) - calls.keys()
    if missing:
        raise RuntimeError(f'Missing matcher call counts for {sorted(missing)}')
    counts = {name: calls[name] for name in APPROACHES}
    if min(counts.values()) <= 0:
        raise RuntimeError(f'No matcher calls: {counts}')
    # The time limit can interrupt between approaches within a single query.
    if max(counts.values()) - min(counts.values()) > 1:
        raise RuntimeError(f'Matcher call counts differ by more than 1: {counts}')


def parse_metrics(output, mode):
    if 'ASSERTION' in output.upper() or 'ABORTED' in output.upper():
        raise RuntimeError('Vampire assertion/abort; stopping sweep')
    if mode == 'ablation':
        metrics = {}
        calls = {}
        for match in TIME_RE.finditer(output):
            name, _, _, avg, unit, count = match.groups()
            calls[name] = int(count)
            avg_us = float(avg) * UNIT_TO_US[unit]
            # Printed totals are coarsely rounded; retain the existing avg*cnt convention.
            metrics[name] = (avg_us, avg_us * calls[name])
    else:
        counts = [dict((name, int(value)) for name, value in re.findall(
            rf'% {label} (\S+): (\d+)', output))
            for label in ('clause matcher calls', 'executed code ops', 'executed next ops')]
        calls, ops, next_ops = counts
        metrics = {name: (ops[name] / calls[name], next_ops[name] / calls[name])
                   for name in APPROACHES
                   if all(name in values for values in counts) and calls[name] > 0}
    check_call_counts(calls)
    missing = set(APPROACHES) - metrics.keys()
    if missing:
        raise RuntimeError(f'Missing statistics for {sorted(missing)}')
    return metrics


def write_csv(path, rows, fields):
    with path.open('w', newline='') as stream:
        writer = csv.writer(stream)
        writer.writerow(['threshold'] + [f'{a}_{f}' for a in APPROACHES for f in fields])
        writer.writerows(rows)


def plot(csv_path, pdf_path, mode, fields):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt

    with csv_path.open() as stream:
        rows = list(csv.DictReader(stream))
    thresholds = [int(row['threshold']) for row in rows]
    if mode == 'ablation':
        titles = ('Avg time per ClauseMatcher::next call', 'Total ClauseMatcher::next time')
        labels = ('avg time (μs)', 'total time (ms)')
        scales = (1., .001)
    else:
        titles = ('Avg executed CodeOps (excluding NEXT)', 'Avg executed NEXT operations')
        labels = ('avg CodeOps per next() call', 'avg NEXT ops per next() call')
        scales = (1., 1.)
    fig, axes = plt.subplots(1, 2, figsize=(12, 6.5))
    for ax, field, title, label, scale in zip(axes, fields, titles, labels, scales):
        for approach, color in zip(APPROACHES, COLORS):
            values = [float(row[f'{approach}_{field}']) * scale for row in rows]
            ax.plot(thresholds, values, color=color, label=approach, linewidth=2,
                    linestyle='-', marker=None)
        ax.set(title=title, xlabel='nextThreshold', ylabel=label, xticks=thresholds)
        ax.grid(True, alpha=.3)
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc='lower center', ncol=2, fontsize=9)
    fig.suptitle('NEXT-op threshold ablation' + (' — executed operations' if mode != 'ablation' else ''))
    fig.tight_layout(rect=[0, .22, 1, .95])
    fig.savefig(pdf_path)
    plt.close(fig)


@contextmanager
def worktree(revision):
    with tempfile.TemporaryDirectory(prefix='vampire-ablation-') as temporary:
        root = Path(temporary)
        source = root / 'source'
        git('worktree', 'add', '--detach', str(source), revision)
        try:
            # Reuse dependencies only if their checkout matches this branch's gitlink.
            # Otherwise CMake initializes the dependencies in the temporary worktree.
            for name in ('cadical', 'viras'):
                dependency = REPO / name
                expected = git('rev-parse', f'{revision}:{name}')
                if (dependency / '.git').exists():
                    actual = subprocess.check_output(
                        ['git', '-C', str(dependency), 'rev-parse', 'HEAD'], text=True).strip()
                    dirty = subprocess.check_output(
                        ['git', '-C', str(dependency), 'status', '--porcelain', '--untracked-files=no'], text=True)
                    if actual == expected and not dirty:
                        (source / name).rmdir()
                        (source / name).symlink_to(dependency, target_is_directory=True)
            yield source, root / 'build'
        finally:
            # Only our disposable worktree contains threshold edits/build artifacts.
            git('worktree', 'remove', '--force', str(source))


def execute(command, log, cwd=None, vampire=False, time_limit=90):
    print(shlex.join(map(str, command)), flush=True)
    with log.open('w') as stream:
        result = subprocess.run(command, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT,
                                timeout=max(120, time_limit * 4) if vampire else None)
    if vampire:
        output = log.read_text()
        # Exit 1 is expected only for Vampire's requested time limit.
        if result.returncode != 0 and not (result.returncode == 1 and '% Termination reason: Time limit' in output):
            raise RuntimeError(f'Vampire failed ({result.returncode}); see {log}')
        return output
    if result.returncode:
        raise RuntimeError(f'Command failed ({result.returncode}); see {log}')


def dry_run_output():
    """Synthetic statistics exercise both parsers without running a benchmark."""
    return '\n'.join(
        f'Clause Matcher next {name} (total: 1 ms, avg: 10 μs, cnt: 100, instr: -)\n'
        f'% clause matcher calls {name}: 100\n'
        f'% executed code ops {name}: 1000\n'
        f'% executed next ops {name}: 200'
        for name in APPROACHES)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--problem', type=Path, default=REPO.parent / 'problems/condensed_detachment_4.p')
    parser.add_argument('--output-dir', type=Path, default=REPO / 'build-vscode')
    parser.add_argument('--time-limit', type=int, default=90)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--cmake-arg', action='append', default=[], help='Extra CMake flag, e.g. --cmake-arg=-DZ3_DIR=/path')
    args = parser.parse_args()
    if args.time_limit <= 0 or args.jobs <= 0:
        parser.error('--time-limit and --jobs must be positive')
    if not args.problem.is_file():
        parser.error(f'Problem not found: {args.problem}')
    for tool in ('git', 'cmake', 'make'):
        if not shutil.which(tool):
            parser.error(f'Required tool not found: {tool}')
    import matplotlib  # Fail before starting a long sweep if plotting is unavailable.

    revisions = {branch: git('rev-parse', '--verify', f'refs/heads/{branch}^{{commit}}')
                 for branch, _, _ in RUNS}
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    logs = output_dir / 'ablation-sweep-logs' / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    if not args.dry_run:
        logs.mkdir(parents=True)
    # Stage results on the same filesystem; failed/incomplete sweeps leave old outputs alone.
    with tempfile.TemporaryDirectory(prefix='.ablation-results-', dir=output_dir) as temporary:
        staged = Path(temporary)
        for branch, mode, fields in RUNS:
            print(f'{branch}: {revisions[branch]}', flush=True)
            with worktree(revisions[branch]) as (source, build):
                configure = ['cmake', '-S', str(source), '-B', str(build), '-G', 'Unix Makefiles',
                             '-DCMAKE_BUILD_TYPE=Debug', '-DTIME_PROFILING=ON',
                             '-DCMAKE_CXX_FLAGS=-DCOMPARE_PROFILING', '-DBUILD_TESTING=OFF']
                if (REPO / 'z3/build').is_dir():
                    configure.append(f'-DZ3_DIR={REPO / "z3/build"}')
                configure += args.cmake_arg
                if args.dry_run:
                    print('Would configure: ' + shlex.join(configure))
                else:
                    execute(configure, logs / f'{mode}-configure.log')
                rows = []
                for threshold in range(1, 8):
                    set_threshold(source, threshold)
                    print(f'{mode}: threshold {threshold}; verified 8 replacements (ordering: 2)', flush=True)
                    build_command = ['make', '-C', str(build), f'-j{args.jobs}', 'vampire']
                    run_command = [str(build / 'vampire'), '--saturation_algorithm', 'discount',
                                   '--time_statistics', 'on', '--time_limit', str(args.time_limit), str(args.problem.resolve())]
                    if args.dry_run:
                        print('Would build: ' + shlex.join(build_command))
                        print('Would run: ' + shlex.join(run_command))
                        output = dry_run_output()
                    else:
                        execute(build_command, logs / f'{mode}-{threshold}-build.log')
                        output = execute(run_command, logs / f'{mode}-{threshold}-run.log',
                                         cwd=build, vampire=True, time_limit=args.time_limit)
                    metrics = parse_metrics(output, mode)
                    rows.append([threshold] + [f'{value:.3f}' for a in APPROACHES for value in metrics[a]])
                write_csv(staged / f'{mode}.csv', rows, fields)
                plot(staged / f'{mode}.csv', staged / f'{mode}.pdf', mode, fields)
        if args.dry_run:
            print('Dry run passed: both branches, thresholds 1–7, parsers and line-only PDFs. Synthetic results discarded; existing outputs unchanged.')
        else:
            for _, mode, _ in RUNS:
                for extension in ('csv', 'pdf'):
                    name = f'{mode}.{extension}'
                    os.replace(staged / name, output_dir / name)
                    print(f'Wrote {output_dir / name}')
            print(f'Logs: {logs}')


if __name__ == '__main__':
    main()
