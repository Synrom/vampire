#!/usr/bin/env python3
"""Analysis regression tests. Run with the analysis Python environment and build dir."""
import csv
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import numpy as np
import pandas as pd
from analyze import classify_frames, concentration, parse_perf, query_weights

BUILD = Path(sys.argv.pop(1)).resolve() if len(sys.argv)>1 else None


class Statistics(unittest.TestCase):
    def test_weighting(self):
        q = pd.DataFrame(dict(algorithm=['otter']*3+['discount'], problem=['a','a','b','a'],
                              weight=[90,10,1,2]))
        w = query_weights(q)
        np.testing.assert_allclose(w.query_weight, [90/101,10/101,1/101,1])
        np.testing.assert_allclose(w.problem_weight, [.45,.05,.5,1])

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

    @unittest.skipUnless(BUILD, 'Pass the analysis build directory for integration tests')
    def test_replay(self):
        subprocess.run([BUILD/'codetree-analysis-test'], check=True)
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            subprocess.run([BUILD/'codetree-dataset-fixture', tmp/'dataset',tmp/'expected.csv'], check=True)
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


if __name__ == '__main__':
    unittest.main()
