# SPDX-License-Identifier: GPL-3.0-or-later
import tempfile
import unittest
from pathlib import Path
from summarize_gpu_timing import summarize

class TimingTests(unittest.TestCase):
    def analyze(self,body):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/"trace.csv"
            p.write_text("event,frame,scene,begin_ns,end_ns,ok\n"+body,encoding="utf-8")
            return summarize(p)
    def test_completion_bounds_and_failed_acquire(self):
        r=self.analyze("acquire,1,7,0,100000,0\nsubmit,1,7,200000,300000,1\ncompletion_bounds,1,7,400000,900000,1\ndropped,0,0,0,0,0\n")
        self.assertEqual(r["unavailable_swapchains"],1)
        self.assertEqual(r["completion_lower"]["p50_ms"],.2)
        self.assertEqual(r["completion_upper"]["p50_ms"],.7)
        self.assertEqual(r["completion_uncertainty"]["p50_ms"],.5)
    def test_missing_footer(self):
        with self.assertRaises(ValueError): self.analyze("submit,1,7,0,1,1\n")
    def test_invalid_bracket(self):
        with self.assertRaises(ValueError):
            self.analyze("submit,1,7,100,200,1\ncompletion_bounds,1,7,50,300,1\ndropped,0,0,0,0,0\n")
    def test_incomplete_is_not_zero_latency(self):
        r=self.analyze("submit,1,7,100,200,1\ndropped,0,0,0,0,0\n")
        self.assertEqual(r["incomplete_fences"],1)
        self.assertIsNone(r["completion_upper"])
