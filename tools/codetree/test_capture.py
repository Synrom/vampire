#!/usr/bin/env python3
"""Sweep discovery, checkpoint accounting, and analysis CLI forwarding tests."""
import tempfile
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
import capture
import analyze_dataset


class SweepTests(unittest.TestCase):
    def test_discovery_and_names(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ['Problems/A/same.p', 'Problems/B/same.p', 'Axioms/not_a_problem.p']:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            problems = capture.discover(root, '*.p')
            self.assertEqual(len(problems), 2)
            self.assertNotEqual(capture.task_id(problems[0], root, 'otter'), capture.task_id(problems[1], root, 'otter'))
            self.assertNotEqual(capture.task_id(problems[0], root, 'otter'), capture.task_id(problems[0], root, 'discount'))

    def test_summary(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root/'sample.ctd').write_text('CTD 2 100 4 6000 2 0\nQ 1 1 0 0 1 1000 0\nQ 4 1 0 0 1 3000 0\n')
            self.assertEqual(capture.summary(root), dict(retained_queries=2, draws=2, population=4, baseline_cpu_ns=6000))

    def test_analysis_forwarding(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root/'capture.json').write_text('{}')
            with patch.object(sys, 'argv', ['analyze_dataset.py', str(root), '--cpus', '3']), \
                 patch.object(analyze_dataset.subprocess, 'call', return_value=0) as call:
                self.assertEqual(analyze_dataset.main(), 0)
            cmd = call.call_args.args[0]
            self.assertIn(str(root.resolve()/'datasets'), cmd)
            for flag in ['--jobs', '--cpus', '--profile-cpus']:
                self.assertEqual(cmd[cmd.index(flag)+1], '3')
            self.assertIn('--skip-perf', cmd)
            self.assertEqual(cmd[cmd.index('--weighting')+1], 'baseline-time')


if __name__ == '__main__':
    unittest.main()
