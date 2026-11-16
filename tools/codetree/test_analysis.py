#!/usr/bin/env python3
"""Analysis regression tests. Run with the analysis Python environment and build dir."""
import csv
from pathlib import Path
import subprocess
import shutil
from threading import Barrier
from types import SimpleNamespace
import sys
import tempfile
import unittest
import resource
import numpy as np
import pandas as pd
from analyze import analyze_trees, classify_frames, concentration, parse_perf, query_weights, run_trees

BUILD = Path(sys.argv.pop(1)).resolve() if len(sys.argv)>1 else None
CAPTURE_BUILD = Path(sys.argv.pop(1)).resolve() if len(sys.argv)>1 else None


class Statistics(unittest.TestCase):
    @unittest.skipUnless(BUILD, 'Pass an analysis build directory')
    def test_capture_rejects_analysis_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            def no_core():
                resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
            p = subprocess.run([BUILD/'codetree-dataset-fixture', root/'data', root/'expected'],
                               capture_output=True, text=True, preexec_fn=no_core)
            self.assertNotEqual(p.returncode, 0)
            self.assertIn('capture requires CT_ANALYSIS=OFF', p.stderr)
            self.assertFalse((root/'data').exists())

    def test_parallel_scheduler(self):
        barrier = Barrier(2)
        def worker(value):
            barrier.wait(timeout=5)  # Fails if the scheduler serializes the work.
            return value * 2
        self.assertEqual(run_trees(worker, [3, 1], 2, 'test workers'), [6, 2])
        def fail(value):
            raise RuntimeError('worker failed')
        with self.assertRaisesRegex(RuntimeError, 'worker failed'):
            run_trees(fail, [1, 2], 1, 'test failure')

    def test_weighting(self):
        q = pd.DataFrame(dict(algorithm=['otter']*3+['discount'], problem=['a','a','b','a'],
                              weight=[90,10,1,2]))
        w = query_weights(q)
        np.testing.assert_allclose(w.query_weight, [90/101,10/101,1/101,1])
        np.testing.assert_allclose(w.problem_weight, [.45,.05,.5,1])

    def test_time_weighting(self):
        q = pd.DataFrame(dict(algorithm=['otter']*3, problem=['a', 'a', 'b'],
                             weight=[3, 1, 2], time_weight=[1, 3, 4]))
        w = query_weights(q, 'baseline-time')
        np.testing.assert_allclose(w.query_weight, [1/8, 3/8, 4/8])
        np.testing.assert_allclose(w.problem_weight, [1/8, 3/8, 1/2])
        with self.assertRaises(RuntimeError):
            query_weights(q.drop(columns='time_weight'), 'baseline-time')

    def test_hotness(self):
        _, _, stats = concentration(np.array([0,100]), np.array([99,1]))
        self.assertAlmostEqual(stats[0], .99)
        self.assertAlmostEqual(stats[1], 1)
        self.assertAlmostEqual(stats[4], .99)
        self.assertAlmostEqual(concentration(np.array([7]), np.array([100]))[2][0], 0)
        self.assertTrue(np.isnan(concentration(np.array([0]),np.array([10]))[2][0]))

    def test_perf_format(self):
        text = '''  1000000 cpu-clock:u:
            abcd Indexing::CodeTree::Matcher<false, false>::doCheckFun() (/binary)
            abce Indexing::ClauseCodeTree::ClauseMatcher::canEnterLiteral(Indexing::CodeTree::CodeOp*) (/binary)

  1200000 cpu-clock:u: abcd other (/binary)
            aaaa caller (/binary)
'''
        samples = list(parse_perf(text.splitlines()))
        self.assertEqual([p for p,_ in samples], [1000000,1200000])
        self.assertEqual(len(samples[0][1]), 2)
        self.assertIn('other',samples[1][1][0])
        self.assertEqual(classify_frames(samples[0][1]), ['CHECK_FUN','incremental_compatibility'])
        self.assertEqual(classify_frames(samples[1][1]), [])

    @unittest.skipUnless(BUILD and CAPTURE_BUILD, 'Pass separate analysis and normal capture build directories')
    def test_replay(self):
        subprocess.run([BUILD/'codetree-analysis-test'], check=True)
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            subprocess.run([CAPTURE_BUILD/'codetree-dataset-fixture', tmp/'dataset',tmp/'expected.csv'], check=True)
            with (tmp/'queries.csv').open('w') as out:
                subprocess.run([BUILD/'codetree-replay', tmp/'dataset/tree-0', '--all', '--verify',
                                '--analysis-dir',tmp/'stats'], stdout=out, check=True)
            q = pd.read_csv(tmp/'queries.csv')
            h = pd.read_csv(tmp/'stats/histograms.csv')
            self.assertTrue((q.expanded_ops >= q.dispatches).all())
            self.assertTrue((q.max_ils_depth >= -1).all())
            self.assertTrue((q.max_ils_depth < q.query_literals).all())
            with (tmp/'expected.csv').open() as source:
                expected = {int(r['query']):int(r['matches']) for r in csv.DictReader(source)}
            self.assertTrue(all(row.matches == expected[row.query] for row in q.itertuples()))
            for row in q.itertuples():
                hot = h[(h['query']==row.query)&(h.metric=='hotness')]
                self.assertEqual(hot['count'].sum(), row.logical_ops)
                self.assertEqual((hot.value*hot['count']).sum(),row.expanded_ops)
                branches = h[(h['query']==row.query)&(h.metric=='branching')]
                self.assertEqual(branches['count'].sum(),row.logical_ops)
            shutil.copytree(tmp/'dataset', tmp/'dataset2')
            trees = [tmp/'dataset/tree-0', tmp/'dataset2/tree-0']
            for cpus in [1, 2]:
                args = SimpleNamespace(output=tmp/f'workers-{cpus}', cpus=cpus, mode='all')
                analyze_trees(args, BUILD/'codetree-replay', trees)
            for filename in ['queries.csv', 'histograms.csv']:
                one = pd.read_csv(tmp/'workers-1'/filename)
                two = pd.read_csv(tmp/'workers-2'/filename)
                if filename == 'queries.csv':
                    one = one.drop(columns='total_ns')
                    two = two.drop(columns='total_ns')
                pd.testing.assert_frame_equal(one, two)



if __name__ == '__main__':
    unittest.main()
