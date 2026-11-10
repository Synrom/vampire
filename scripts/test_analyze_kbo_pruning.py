#!/usr/bin/env python3
import csv
import tempfile
import unittest
from pathlib import Path
from analyze_kbo_pruning import FEATURES, analyze, correlation

class AnalysisTest(unittest.TestCase):
    def test_epochs_are_aggregated_for_concentration(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'input.csv'
            rows = []
            # Two feature epochs for site 1 must count as one site, including
            # its earlier (now retired) epoch. Keep zero-pruning exposures.
            for site, visits, rejects in [(1, 10, 4), (1, 10, 6), (2, 10, 0), (3, 0, 0)]:
                row = dict.fromkeys(FEATURES, 1)
                row.update(site_id=site, visits=visits, enabled_visits=visits, rejections=rejects)
                rows.append(row)
            with path.open('w', newline='') as out:
                writer = csv.DictWriter(out, fieldnames=list(rows[0]))
                writer.writeheader(); writer.writerows(rows)
            summary, correlations, distribution = analyze(path)
            self.assertEqual(distribution[0]['rejections'], 10)
            self.assertEqual(distribution[-1]['cumulative_share_of_rejections'], 1)
            self.assertEqual(summary['sites'], 3)
            self.assertEqual(summary['enabled_sites'], 2)
            self.assertEqual(summary['never_visited_sites'], 1)
            self.assertEqual(summary['rejections'], 10)
            self.assertEqual(summary['gini_rejections'], .5)
            self.assertEqual(summary['top_1pct_share'], 1)
            self.assertTrue(all(c['pearson_rejection_rate'] is None for c in correlations))

    def test_empty_ledger(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'empty.csv'
            path.write_text('site_id,visits,enabled_visits,rejections\n')
            summary, correlations, distribution = analyze(path)
            self.assertEqual(summary['rejections'], 0)
            self.assertIsNone(summary['gini_rejections'])
            self.assertEqual(distribution, [])
            self.assertTrue(all(c['observed_epochs'] == 0 for c in correlations))

    def test_correlation_and_undefined_cases(self):
        self.assertAlmostEqual(correlation([1,2,3], [2,4,6], [1,5,1]), 1)
        self.assertAlmostEqual(correlation([1,2,3], [6,4,2], [1,5,1]), -1)
        self.assertIsNone(correlation([], [], []))
        self.assertIsNone(correlation([1,1], [2,3], [1,1]))

if __name__ == '__main__':
    unittest.main()
