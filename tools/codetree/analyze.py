#!/usr/bin/env python3
"""Replay a CTD collection, export exact distributions, plot, then sample with perf."""
import argparse
import csv
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[2]
CATEGORIES = {
    'doCheckFun': 'CHECK_FUN', 'doAssignVar': 'ASSIGN_VAR',
    'doCheckVar': 'CHECK_VAR', 'doSearchStruct': 'SEARCH_STRUCT',
    'canEnterLiteral': 'incremental_compatibility', 'checkCandidate': 'candidate_check',
}


def checked(command, log):
    with Path(log).open('w') as out:
        result = subprocess.run(list(map(str, command)), stdout=out, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"Command failed; see {log}\n{Path(log).read_text()[-4000:]}")


def binary(args, mode):
    supplied = getattr(args, mode + '_binary')
    if supplied:
        exe = supplied.resolve()
    else:
        if not shutil.which('cmake'):
            raise RuntimeError('cmake is required to build the replay binaries')
        build = args.build_root / mode
        print(f'Building {mode} replay (logs in {args.output})', flush=True)
        checked(['cmake', '-S', REPO, '-B', build, '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
                 '-DCMAKE_DISABLE_FIND_PACKAGE_Z3=ON', '-DCT_' + mode.upper() + '=ON',
                 '-DCMAKE_CXX_FLAGS=-fno-omit-frame-pointer -fno-optimize-sibling-calls'],
                args.output / f'configure-{mode}.log')
        checked(['cmake', '--build', build, '--target', 'codetree-replay', '-j', args.jobs],
                args.output / f'build-{mode}.log')
        exe = (build / 'codetree-replay').resolve()
    result = subprocess.run([exe, '--capabilities'], capture_output=True, text=True, check=True)
    if mode not in result.stdout.split():
        raise RuntimeError(f'{exe} needs a dedicated -DCT_{mode.upper()}=ON build')
    return exe


def preflight_perf():
    if platform.system() != 'Linux' or not shutil.which('perf'):
        raise RuntimeError('Linux and perf are required. Use --skip-perf for analysis only.')
    with tempfile.TemporaryDirectory(prefix='ct-perf-check-') as tmp:
        # Check the actual sampling event and DWARF unwinder, not just perf's existence.
        checked(['perf', 'record', '-q', '-e', 'cpu-clock:u', '-F', '99',
                 '--call-graph', 'dwarf,16384', '-o', Path(tmp) / 'perf.data', '--',
                 sys.executable, '-c', 'sum(i*i for i in range(1000000))'], Path(tmp) / 'check.log')
        checked(['perf', 'script', '-i', Path(tmp) / 'perf.data', '-F', 'period,event,ip,sym'],
                Path(tmp) / 'script.txt')
        if 'cpu-clock' not in (Path(tmp) / 'script.txt').read_text():
            raise RuntimeError('perf recorded no cpu-clock samples in the capability probe')


def stream_replay(command, destination, log, progress):
    with destination.open('w') as out, log.open('w') as err:
        process = subprocess.Popen(list(map(str, command)), stdout=subprocess.PIPE, stderr=err, text=True)
        try:
            for line in process.stdout:
                out.write(line)
                if line[:1] == '"':
                    progress.update(1)
            if process.wait():
                raise RuntimeError(f'Replay failed: {log}\n{log.read_text()[-3000:]}')
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait()


def parse_perf(lines):
    """perf script -F period,event,ip,sym; callchains are leaf first.

    Also accepts leaf IP/symbol on the header line. No percentages are parsed:
    use the actual sample period, in nanoseconds for cpu-clock.
    """
    period, frames = None, []
    header = re.compile(r'^\s*(\d+)\s+cpu-clock(?::u)?:\s*(.*)$')
    for line in lines:
        match = header.match(line)
        if match:
            if period is not None:
                yield period, frames
            period, frames = int(match[1]), [match[2]] if match[2] else []
        elif period is not None and line.strip():
            frames.append(line.strip())
    if period is not None:
        yield period, frames


def classify_frames(frames):
    """Unique named ancestors in leaf-first order; first owns exclusive time."""
    matched = []
    for frame in frames:
        for symbol, category in CATEGORIES.items():
            if '::' + symbol + '(' in frame and category not in matched:
                matched.append(category)
    return matched


def profile(args, exe, trees):
    from tqdm import tqdm
    rows = []
    for tree in tqdm(trees, desc='perf trees'):
        key = hashlib.sha256(str(tree).encode()).hexdigest()[:16]
        folder = args.output / 'raw' / key
        data = folder / 'perf.data'
        mode = ['--all'] if args.mode == 'all' else []
        command = ['perf', 'record', '-q', '-e', 'cpu-clock:u', '-F', args.frequency,
                   '--call-graph', 'dwarf,16384', '-D', '-1', '-o', data, '--', exe,
                   tree, '--profile', '--repeat', args.profile_repeat, *mode]
        # Events start disabled. Replay enables them only around matcher execution.
        with tqdm(desc=f'profile {key}', unit='query', leave=False) as progress:
            stream_replay(command, folder / 'profile_queries.csv', folder / 'perf.log', progress)
        checked(['perf', 'script', '-i', data, '-F', 'period,event,ip,sym'], folder / 'perf.script')
        counts = {}
        total = 0
        with (folder / 'perf.script').open() as source:
            for ns, frames in parse_perf(source):
                total += 1
                matched = classify_frames(frames)
                for view, cats in [('exclusive', matched[:1] or ['other']),
                                   ('inclusive', matched or ['other'])]:
                    for cat in cats:
                        old = counts.get((view, cat), (0, 0))
                        counts[view, cat] = old[0] + 1, old[1] + ns
        if not total:
            raise RuntimeError(f'No samples for {tree}; increase --profile-repeat. Data: {folder}')
        if not any(cat != 'other' for view, cat in counts):
            raise RuntimeError(f'No named regions resolved for {tree}; inspect {folder}/perf.script')
        for view in ['exclusive', 'inclusive']:
            for cat in [*CATEGORIES.values(), 'other']:
                samples, ns = counts.get((view, cat), (0, 0))
                rows.append(dict(dataset=str(tree), view=view, category=cat, samples=samples,
                                 estimated_cpu_ns=ns, repeats=args.profile_repeat))
    import pandas as pd
    pd.DataFrame(rows).to_csv(args.output / 'profile.csv', index=False)


def query_weights(q):
    """Query-population and equal-problem weights, normalized within algorithm."""
    q = q.copy()
    q['query_weight'] = q.weight / q.groupby('algorithm').weight.transform('sum')
    totals = q.groupby(['algorithm', 'problem']).weight.transform('sum')
    nproblems = q.groupby('algorithm').problem.transform('nunique')
    q['problem_weight'] = q.weight / totals / nproblems
    return q


def concentration(values, counts):
    import numpy as np
    order = np.argsort(values)
    v, c = np.asarray(values)[order], np.asarray(counts)[order]
    x = np.r_[0., np.cumsum(c) / sum(c)]
    work = float(sum(v * c))
    y = np.r_[0., np.cumsum(v * c) / work] if work else x.copy()
    # No-execution queries have undefined concentration, retained as NaN.
    gini = 1 - sum((y[1:] + y[:-1]) * np.diff(x)) if work else float('nan')
    top = [1 - np.interp(1 - f, x, y) if work else float('nan') for f in [.01, .05, .1]]
    return x, y, [gini, *top, sum(c[v == 0]) / sum(c)]


def savefig(fig, path):
    import matplotlib.pyplot as plt
    fig.savefig(path.with_suffix('.png'), dpi=170, bbox_inches='tight')
    fig.savefig(path.with_suffix('.pdf'), bbox_inches='tight')
    plt.close(fig)


def distribution_plots(q, h, out):
    import numpy as np
    import pandas as pd
    import matplotlib.pyplot as plt
    from tqdm import tqdm
    keys = ['dataset', 'query']
    meta = q[keys + ['problem', 'algorithm', 'weight', 'query_weight', 'problem_weight']]
    exports, summaries = [], []
    metrics = ['expanded_ops', 'dispatches', 'max_ils_depth', 'branching',
               'branching_nonterminal', 'hotness']
    for metric in tqdm(metrics, desc='Distribution plots'):
        if metric in q:
            d = q[keys + [metric]].rename(columns={metric: 'value'})
            d['count'] = 1
        else:
            d = h.loc[h.metric == metric, keys + ['value', 'count']].copy()
        if d.empty:
            continue
        d['mass'] = d['count'] / d.groupby(keys)['count'].transform('sum')
        d = d.merge(meta, on=keys, validate='many_to_one')
        fig, axes = plt.subplots(2, 3, figsize=(16, 9), constrained_layout=True)
        fig.suptitle(metric + ' — exact weighted distributions; both algorithms shown separately')
        perproblem = []
        for (algo, problem), group in d.groupby(['algorithm', 'problem']):
            # Normalize within this problem, summing all its runs/tree populations.
            w = group['mass'] * group.weight
            dist = w.groupby(group.value).sum()
            dist /= dist.sum()
            perproblem.append((algo, problem, dist))
            mean = sum(dist.index * dist)
            cdf = dist.cumsum()
            quantiles = [float(dist.index[min(np.searchsorted(cdf, p), len(dist)-1)]) for p in [.5,.9,.99]]
            summaries.append(dict(metric=metric, algorithm=algo, problem=problem,
                                  mean=mean, median=quantiles[0], p90=quantiles[1], p99=quantiles[2]))
        for algo, group in d.groupby('algorithm'):
            for view in ['query', 'problem']:
                mass = group.mass * group[view + '_weight']
                dist = mass.groupby(group.value).sum().sort_index()
                dist /= dist.sum()
                x, p = dist.index.to_numpy(), dist.to_numpy()
                label = f'{algo}: {view} average'
                style = '-' if view == 'query' else '--'
                axes[0,0].plot(x, p, '.', ms=3, label=label)
                axes[0,1].step(x, np.cumsum(p), where='post', ls=style, label=label)
                tail = np.maximum(0, 1 - np.cumsum(p))
                axes[0,2].step(x, np.where(tail > 1e-15, tail, np.nan), where='post', ls=style, label=label)
                # Coarse bins in log1p-space, no KDE fiction for discrete counts.
                if x.min() >= 0 and x.max() > 20:
                    edges = np.unique(np.r_[0, np.expm1(np.linspace(0, np.log1p(x.max()+1), 45))])
                else:
                    edges = np.arange(x.min()-.5, x.max()+1.5)
                binned, edges = np.histogram(x, bins=edges, weights=p)
                axes[1,0].stairs(binned, edges, label=label, ls=style)
                for value, probability, cumulative in zip(x, p, np.cumsum(p)):
                    exports.append(dict(metric=metric, algorithm=algo, view=view, value=value,
                                        probability=probability, cdf=cumulative))
            ps = [(problem, dist) for a, problem, dist in perproblem if a == algo]
            for i, (problem, dist) in enumerate(ps):
                axes[1,1].step(dist.index, dist.cumsum(), where='post',
                               color='C0' if algo == 'discount' else 'C2',
                               label=algo if i == 0 else None,
                               alpha=max(.04, min(.7, 3/len(ps))))
        for ax, title in zip(axes.flat, ['Raw probability mass', 'ECDF', 'P(value > x)',
                                        'Coarsened probability mass', 'Every problem: ECDF',
                                        'Paired problem means']):
            ax.set_title(title)
            ax.set_xlabel(metric)
            ax.grid(alpha=.2)
        for ax in axes.flat:
            if d.value.max() > 20:
                ax.set_xscale('symlog', linthresh=1)
        axes[0,0].set_yscale('log')
        axes[0,2].set_yscale('log')
        axes[0,0].set_ylabel('Probability (exact values)')
        axes[1,0].set_ylabel('Probability per bin (unequal bin widths)')
        for ax in [axes[0,1], axes[1,1]]:
            ax.set_ylim(0, 1)
        axes[0,1].legend(fontsize=8)
        axes[1,1].legend(fontsize=8)
        sm = pd.DataFrame([s for s in summaries if s['metric'] == metric])
        pivot = sm.pivot(index='problem', columns='algorithm', values='mean')
        ax = axes[1,2]
        if {'otter','discount'} <= set(pivot.columns):
            paired = pivot[['otter','discount']].dropna()
            ax.scatter(paired.otter, paired.discount, s=15, alpha=.65)
            low = min(d.value.min(), 0)
            high = max(float(paired.max().max()) if len(paired) else 1, 1)
            ax.plot([low, high], [low, high], ':', color='gray')
            if d.value.max() > 20:
                ax.set_yscale('symlog', linthresh=1)
            ax.set_xlabel('Otter mean'); ax.set_ylabel('Discount mean')
            ax.set_title(f'Paired problem means (n={len(paired)})')
        else:
            ax.text(.5,.5,'No paired algorithms', ha='center', transform=ax.transAxes)
        savefig(fig, out / metric)
        # A row per problem makes distinct clusters visible, even for large corpora.
        grid = np.unique(np.r_[d.value.min(), np.expm1(np.linspace(0, np.log1p(max(1,d.value.max())), 100))])
        fig, axes = plt.subplots(1, 2, figsize=(14, max(4, min(14, len(perproblem)*.2))), constrained_layout=True)
        heatrows = []
        for ax, algo in zip(axes, ['otter','discount']):
            ps = sorted([(p, dist) for a,p,dist in perproblem if a == algo], key=lambda t: sum(t[1].index*t[1]))
            if not ps:
                ax.set_axis_off(); continue
            matrix = []
            for problem, dist in ps:
                # Right-continuous empirical CDF; no interpolation across gaps.
                ix = np.searchsorted(dist.index, grid, side='right')
                cdf = np.r_[0, dist.cumsum().to_numpy()][ix]
                matrix.append(cdf)
                heatrows.extend(dict(metric=metric, algorithm=algo, problem=problem, value=x, cdf=y)
                                for x,y in zip(grid,cdf))
            im = ax.imshow(matrix, aspect='auto', origin='lower', vmin=0, vmax=1)
            ticks = np.linspace(0,len(grid)-1,6).astype(int)
            ax.set_xticks(ticks, [f'{grid[i]:.2g}' for i in ticks])
            ax.set_title(algo + ': problem CDFs sorted by mean')
            ax.set_xlabel(metric); ax.set_ylabel('Problem (see CSV for identities)')
            fig.colorbar(im, ax=ax, label='CDF')
        savefig(fig, out / (metric + '_problems'))
        pd.DataFrame(heatrows).to_csv(out / (metric + '_problem_cdfs.csv'), index=False)
    pd.DataFrame(exports).to_csv(out / 'distributions.csv', index=False)
    pd.DataFrame(summaries).to_csv(out / 'problem_summary.csv', index=False)


def hotness_plots(q, h, out):
    import numpy as np
    import pandas as pd
    import matplotlib.pyplot as plt
    grid = np.linspace(0, 1, 201)
    curves, rows = [], []
    for (dataset, query), group in h[h.metric == 'hotness'].groupby(['dataset','query']):
        x, y, stats = concentration(group.value.to_numpy(), group['count'].to_numpy())
        rows.append(dict(dataset=dataset, query=query, gini=stats[0], top_1pct=stats[1],
                         top_5pct=stats[2], top_10pct=stats[3], unexecuted_fraction=stats[4]))
        curves.append(np.interp(grid, x, y) if np.isfinite(stats[0]) else np.full(len(grid), np.nan))
    stats = pd.DataFrame(rows).merge(q, on=['dataset','query'], validate='one_to_one')
    stats.to_csv(out / 'query_concentration.csv', index=False)
    curves = np.asarray(curves)
    fig, axes = plt.subplots(1, 3, figsize=(16, 4.5), constrained_layout=True)
    records = []
    for algo, group in stats.groupby('algorithm'):
        valid = group[group.gini.notna()]
        for view in ['query','problem']:
            if valid.empty: continue
            weights = query_weights(valid)[view + '_weight'].to_numpy()
            mean = np.average(curves[valid.index], axis=0, weights=weights)
            axes[0].plot(grid, mean, label=f'{algo}: {view} average')
            records.extend(dict(algorithm=algo, view=view, op_fraction=x, execution_fraction=y)
                           for x,y in zip(grid,mean))
        axes[1].scatter(group.logical_ops, group.top_1pct, s=9, alpha=.3, label=algo)
        axes[2].scatter(group.unexecuted_fraction, group.gini, s=9, alpha=.3, label=algo)
    axes[0].plot([0,1],[0,1],':',color='gray')
    axes[0].set(xlabel='Fraction of ops, coldest first', ylabel='Fraction of expanded executions', title='Mean within-query Lorenz curves')
    axes[0].legend(fontsize=8)
    axes[1].set(xlabel='Logical ops in tree', ylabel='Execution share of hottest 1% of ops', title='Every sampled query (unweighted points)')
    axes[1].set_xscale('log'); axes[1].legend()
    axes[2].set(xlabel='Fraction of ops never executed', ylabel='Gini of execution counts', title='Every sampled query (unweighted points)')
    axes[2].legend()
    savefig(fig, out / 'concentration')
    pd.DataFrame(records).to_csv(out / 'lorenz.csv', index=False)
    # Each problem's concentration summaries, not only a single corpus average.
    p = stats.groupby(['algorithm','problem'])[['weight','gini','top_1pct','top_5pct','top_10pct','unexecuted_fraction']].apply(
        lambda g: pd.Series({col: np.average(g.loc[g[col].notna(),col], weights=g.loc[g[col].notna(),'weight'])
                             if g[col].notna().any() else np.nan
                             for col in ['gini','top_1pct','top_5pct','top_10pct','unexecuted_fraction']}),
        )
    p.to_csv(out / 'problem_concentration.csv')
    fig, axes = plt.subplots(1, 2, figsize=(12,4), constrained_layout=True)
    for algo, group in p.reset_index().groupby('algorithm'):
        axes[0].plot(np.sort(group.top_1pct.dropna()), np.arange(1,group.top_1pct.notna().sum()+1)/group.top_1pct.notna().sum(), '.', label=algo)
        axes[1].scatter(group.unexecuted_fraction, group.gini, label=algo, alpha=.6)
    axes[0].set(xlabel='Problem mean: hottest 1% execution share', ylabel='Fraction of problems', title='Equal-problem distribution'); axes[0].legend()
    axes[1].set(xlabel='Problem mean: unexecuted fraction', ylabel='Problem mean: Gini', title='Problem populations'); axes[1].legend()
    savefig(fig, out / 'concentration_problems')


def workload_plot(q, out):
    import numpy as np
    import pandas as pd
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(1, 2, figsize=(12,4.5), constrained_layout=True)
    records = []
    for algo, group in q.groupby('algorithm'):
        for view in ['query','problem']:
            d = group.sort_values('expanded_ops')
            w = d[view+'_weight'].to_numpy()
            work = d.expanded_ops.to_numpy() * w
            if not work.sum(): continue
            x = np.r_[0, np.cumsum(w)/sum(w)]
            y = np.r_[0, np.cumsum(work)/sum(work)]
            axes[0].plot(x,y,label=f'{algo}: {view} average')
            records.extend(dict(algorithm=algo, view=view, query_fraction=a, execution_fraction=b)
                           for a,b in zip(x,y))
        axes[1].scatter(group.max_ils_depth, group.expanded_ops, s=8, alpha=.25, label=algo)
    axes[0].plot([0,1],[0,1],':',color='gray')
    axes[0].set(xlabel='Weighted fraction of queries, cheapest first', ylabel='Fraction of expanded execution work', title='Do a few queries dominate the work?')
    axes[0].legend(fontsize=8)
    axes[1].set(xlabel='Maximum ILStruct depth (-1 = none)', ylabel='Expanded executions', title='Raw retained queries: depth versus work')
    axes[1].set_yscale('symlog', linthresh=1); axes[1].legend()
    savefig(fig, out/'query_work_concentration')
    pd.DataFrame(records).to_csv(out/'query_work_concentration.csv', index=False)


def plots(out):
    import pandas as pd
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    q = query_weights(pd.read_csv(out / 'queries.csv'))
    h = pd.read_csv(out / 'histograms.csv')
    distribution_plots(q, h, out)
    hotness_plots(q, h, out)
    workload_plot(q, out)
    profile_plot(out, q)


def profile_plot(out, q):
    import pandas as pd
    import matplotlib.pyplot as plt
    if (out / 'profile.csv').exists():
        p = pd.read_csv(out / 'profile.csv').merge(q[['dataset','algorithm','problem']].drop_duplicates(), on='dataset')
        fig, axes = plt.subplots(1, 2, figsize=(14,5), constrained_layout=True)
        for ax, view in zip(axes, ['exclusive','inclusive']):
            table = p[p.view == view].groupby(['category','algorithm']).estimated_cpu_ns.sum().unstack(fill_value=0)
            # Denominator is always exclusive total; inclusive regions overlap.
            total = p[p.view == 'exclusive'].groupby('algorithm').estimated_cpu_ns.sum()
            table = table.divide(total, axis=1) * 100
            table.plot.barh(ax=ax)
            ax.set_title(view + ' sampled CPU time'); ax.set_xlabel('% of all sampled query CPU time')
        savefig(fig, out / 'profile')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dataset', type=Path)
    parser.add_argument('--output', type=Path, default=Path('codetree-analysis'))
    parser.add_argument('--build-root', type=Path, default=REPO / 'build-codetree-analysis')
    parser.add_argument('--analysis-binary', type=Path)
    parser.add_argument('--profile-binary', type=Path)
    parser.add_argument('--jobs', type=int, default=min(os.cpu_count() or 1, 8))
    parser.add_argument('--mode', choices=['all','first'], default='all')
    parser.add_argument('--skip-perf', action='store_true')
    parser.add_argument('--profile-repeat', type=int, default=1000)
    parser.add_argument('--frequency', type=int, default=999)
    parser.add_argument('--plots-only', action='store_true')
    args = parser.parse_args()
    missing = [m for m in ['numpy','pandas','matplotlib','tqdm'] if importlib.util.find_spec(m) is None]
    if missing:
        parser.error('Missing ' + ', '.join(missing) + '; install with: python3 -m pip install -r tools/codetree/requirements-analysis.txt')
    if min(args.jobs, args.profile_repeat, args.frequency) <= 0:
        parser.error('jobs, profile-repeat and frequency must be positive')
    args.output = args.output.resolve()
    if args.plots_only:
        plots(args.output); return
    if args.output.exists() and any(args.output.iterdir()):
        parser.error('Output directory must be empty; use --plots-only to redraw existing CSVs')
    if not args.skip_perf:
        preflight_perf()
    trees = sorted(p.parent.resolve() for p in args.dataset.resolve().rglob('sample.ctd')
                   if (p.parent / 'metadata.txt').is_file())
    nonempty = []
    for tree in trees:
        with (tree/'sample.ctd').open() as sample:
            if any(line.startswith('Q ') for line in sample): nonempty.append(tree)
    trees = nonempty
    if not trees:
        parser.error('No CTD tree directories found')
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = dict(arguments={k:str(v) for k,v in vars(args).items()}, datasets=list(map(str,trees)),
                    platform=platform.platform(), python=sys.version)
    manifest['git_diff'] = subprocess.run(['git','-C',REPO,'diff'], text=True,capture_output=True).stdout
    manifest['git_head'] = subprocess.run(['git','-C',REPO,'rev-parse','HEAD'], text=True,capture_output=True).stdout.strip()
    (args.output/'manifest.json').write_text(json.dumps(manifest, indent=2))
    analysis = binary(args, 'analysis')
    from tqdm import tqdm
    import pandas as pd
    qframes, hframes = [], []
    for tree in tqdm(trees, desc='Analysis trees'):
        key = hashlib.sha256(str(tree).encode()).hexdigest()[:16]
        folder = args.output / 'raw' / key
        folder.mkdir(parents=True)
        with (tree/'sample.ctd').open() as sample:
            nqueries = sum(line.startswith('Q ') for line in sample)
        command = [analysis, tree, '--analysis-dir', folder]
        if args.mode == 'all': command.append('--all')
        with tqdm(total=nqueries, desc=key, unit='query', leave=False) as progress:
            stream_replay(command, folder/'queries.csv', folder/'replay.log', progress)
        qframes.append(pd.read_csv(folder/'queries.csv'))
        hframes.append(pd.read_csv(folder/'histograms.csv').assign(dataset=str(tree)))
    pd.concat(qframes, ignore_index=True).to_csv(args.output/'queries.csv', index=False)
    pd.concat(hframes, ignore_index=True).to_csv(args.output/'histograms.csv', index=False)
    plots(args.output)
    if not args.skip_perf:
        profile(args, binary(args, 'profile'), trees)
        profile_plot(args.output, pd.read_csv(args.output/'queries.csv'))
    print(f'Analysis saved to {args.output}')


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
