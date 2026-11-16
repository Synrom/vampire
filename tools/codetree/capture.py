#!/usr/bin/env python3
"""Build normal Vampire and capture time-proportional queries across TPTP."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import threading
import time

REPO = Path(__file__).resolve().parents[2]


def checked(command, log):
    with log.open('w') as stream:
        result = subprocess.run(list(map(str, command)), stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f'Build failed: {log}\n{log.read_text()[-4000:]}')


def discover(root, pattern):
    directory = root / 'Problems' if (root / 'Problems').is_dir() else root
    return sorted({p.resolve() for p in directory.rglob(pattern) if p.is_file()})


def task_id(problem, root, algorithm):
    # TPTP can contain repeated basenames in different directories.
    relative = os.path.relpath(problem.resolve(), root.resolve())
    return algorithm + '-' + hashlib.sha256(relative.encode()).hexdigest()[:20]


def summary(folder):
    queries, draws, populations, cpu_ns = 0, 0, 0, 0
    for path in folder.rglob('sample.ctd'):
        with path.open() as stream:
            header = stream.readline().split()
            if len(header) != 7 or header[:2] != ['CTD', '2']:
                raise RuntimeError(f'Invalid time-sampling checkpoint: {path}')
            count = sum(line.startswith('Q ') for line in stream)
            queries += count
            populations += int(header[3])
            cpu_ns += int(header[4])
            if count:
                draws += int(header[5])
    return dict(retained_queries=queries, draws=draws, population=populations, baseline_cpu_ns=cpu_ns)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tptp', type=Path, help='TPTP root containing Problems/ and Axioms/, or a problem directory')
    parser.add_argument('--cpus', type=int, required=True, help='Maximum concurrent builds/prover processes')
    parser.add_argument('--output', type=Path, default=Path('codetree-capture'))
    parser.add_argument('--build-dir', type=Path, default=REPO / 'build-codetree-capture')
    parser.add_argument('--time-limit', type=float, default=60, help='Seconds per problem/algorithm run')
    parser.add_argument('--memory-limit', type=int, default=2048, help='MB per prover process')
    parser.add_argument('--samples', type=int, default=256, help='Independent time-weighted draws per tree/run')
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--algorithms', nargs='+', choices=['otter', 'discount'], default=['otter', 'discount'])
    parser.add_argument('--pattern', default='*.p', help='Recursive problem filename glob')
    parser.add_argument('--limit', type=int, help='First N sorted problems (smoke testing)')
    parser.add_argument('--activation-limit', type=int, default=0, help='Optional deterministic saturation limit')
    parser.add_argument('--resume', action='store_true', help='Continue an existing sweep with identical settings')
    args = parser.parse_args()
    if min(args.cpus, args.time_limit, args.memory_limit, args.samples, args.seed) <= 0:
        parser.error('CPU count, limits, samples and seed must be positive')
    if max(args.samples, args.seed) > 1000000 or args.activation_limit < 0 or (args.limit is not None and args.limit <= 0):
        parser.error('samples/seed must be <=1000000, activation-limit >=0, limit >0')
    args.tptp = args.tptp.resolve()
    if not args.tptp.is_dir():
        parser.error('TPTP folder does not exist')
    args.output = args.output.resolve()
    args.build_dir = args.build_dir.resolve()
    problems = discover(args.tptp, args.pattern)
    if args.limit is not None:
        problems = problems[:args.limit]
    if not problems:
        parser.error('No problem files found')
    algorithms = list(dict.fromkeys(args.algorithms))
    settings = dict(tptp=str(args.tptp), problems=list(map(str, problems)), algorithms=algorithms,
                    time_limit=args.time_limit, memory_limit=args.memory_limit, samples=args.samples,
                    seed=args.seed, activation_limit=args.activation_limit)
    manifest_path = args.output / 'capture.json'
    if args.resume:
        if not manifest_path.is_file() or json.loads(manifest_path.read_text())['settings'] != settings:
            parser.error('Resume requires an existing capture with identical sampling/run settings and problem list')
    elif args.output.exists() and any(args.output.iterdir()):
        parser.error('Output must be empty; use --resume to continue an existing sweep')
    for name in ['datasets', 'work', 'interrupted', 'logs']:
        (args.output / name).mkdir(parents=True, exist_ok=True)
    if not args.resume:
        manifest = dict(settings=settings, cpus=args.cpus, git_head=subprocess.check_output(
            ['git', '-C', REPO, 'rev-parse', 'HEAD'], text=True).strip(),
            git_diff=subprocess.check_output(['git', '-C', REPO, 'diff'], text=True))
        manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Building normal capture Vampire with {args.cpus} jobs', flush=True)
    checked(['cmake', '-S', REPO, '-B', args.build_dir, '-DCMAKE_BUILD_TYPE=Release',
             '-DCT_ANALYSIS=OFF', '-DCT_PROFILE=OFF', '-DTIME_PROFILING=OFF',
             '-DCMAKE_DISABLE_FIND_PACKAGE_Z3=ON'], args.output / 'logs/configure.log')
    checked(['cmake', '--build', args.build_dir, '--target', 'vampire', '-j', args.cpus],
            args.output / 'logs/build.log')
    vampire = args.build_dir / 'vampire'
    executable_hash = hashlib.sha256(vampire.read_bytes()).hexdigest()
    stop = threading.Event()
    processes = set()
    lock = threading.Lock()

    def terminate(process):
        try:
            os.killpg(process.pid, signal.SIGTERM)
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
        except ProcessLookupError:
            pass

    def worker(problem, algorithm):
        key = task_id(problem, args.tptp, algorithm)
        final = args.output / 'datasets' / key
        if final.exists():
            if not (final / 'run.json').is_file():
                raise RuntimeError(f'Incomplete published run: {final}')
            return json.loads((final / 'run.json').read_text())
        folder = args.output / 'work' / key
        if folder.exists():
            shutil.move(str(folder), args.output / 'interrupted' / f'{key}-{time.time_ns()}')
        folder.mkdir()
        command = [str(vampire), '-sa', algorithm, '-cts', 'on', '-t', f'{args.time_limit}s',
                   '--memory_limit', str(args.memory_limit), '--random_seed', str(args.seed),
                   '--activation_limit', str(args.activation_limit), str(problem)]
        environment = dict(os.environ, TPTP=str(args.tptp), VAMPIRE_CT_DATASET=str(folder / 'dataset'),
                           VAMPIRE_CT_SAMPLES=str(args.samples), VAMPIRE_CT_SEED=str(args.seed))
        environment.pop('VAMPIRE_CT_SAMPLES_PER_BUCKET', None)
        start = time.monotonic()
        timed_out = False
        with (folder / 'vampire.log').open('w') as out:
            with lock:
                if stop.is_set():
                    return dict(status='cancelled', problem=str(problem), algorithm=algorithm)
                process = subprocess.Popen(command, stdout=out, stderr=subprocess.STDOUT,
                                           env=environment, start_new_session=True)
                processes.add(process)
            try:
                process.wait(timeout=args.time_limit + max(30, args.time_limit * .2))
            except subprocess.TimeoutExpired:
                timed_out = True
                terminate(process)
            finally:
                with lock:
                    processes.discard(process)
        if stop.is_set():
            return dict(status='cancelled', problem=str(problem), algorithm=algorithm)
        log = (folder / 'vampire.log').read_text(errors='replace')
        legitimate = process.returncode == 0 or (process.returncode == 1 and 'Termination reason:' in log)
        status = 'timeout' if timed_out or 'Termination reason: Time limit' in log else 'ok' if legitimate else 'failed'
        record = dict(problem=str(problem), algorithm=algorithm, status=status, exit_code=process.returncode,
                      elapsed_seconds=time.monotonic() - start, command=command, binary_sha256=executable_hash,
                      **summary(folder / 'dataset'))
        if record['status'] == 'ok' and record['retained_queries'] == 0:
            record['status'] = 'empty'
        (folder / 'run.json').write_text(json.dumps(record, indent=2) + '\n')
        folder.rename(final)
        return record

    tasks = [(problem, algorithm) for problem in problems for algorithm in algorithms]
    records = []
    with ThreadPoolExecutor(max_workers=args.cpus) as pool:
        pending = [pool.submit(worker, *task) for task in tasks]
        try:
            for future in as_completed(pending):
                record = future.result()
                records.append(record)
                print(f'[{len(records)}/{len(tasks)}] {record["algorithm"]} {Path(record["problem"]).name}: '
                      f'{record["status"]}, {record.get("retained_queries", 0)} retained', flush=True)
        except BaseException:
            stop.set()
            for future in pending:
                future.cancel()
            with lock:
                running = list(processes)
            for process in running:
                terminate(process)
            raise
    records.sort(key=lambda r: (r['problem'], r['algorithm']))
    (args.output / 'runs.json').write_text(json.dumps(records, indent=2) + '\n')
    failures = sum(r['status'] == 'failed' for r in records)
    print(f'Capture saved to {args.output}; {failures} failed runs. Next:\n'
          f'  python3 tools/codetree/analyze_dataset.py {str(args.output)!r} --cpus {args.cpus}')
    return 1 if failures else 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
