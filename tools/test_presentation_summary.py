# SPDX-License-Identifier: GPL-3.0-or-later
import tempfile
import unittest
from pathlib import Path
from summarize_presentation import summarize

class PresentationTests(unittest.TestCase):
    def analyze(self,rows,frequency=10000000):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/"present.csv"; t=Path(d)/"trace.csv"
            p.write_text("QPCTime,Dropped,PresentMode,msUntilDisplayed\n"+rows,encoding="utf-8")
            t.write_text("event,time_ns,scene\ninterpolated_submit,100000000,7\ninterpolated_submit,200000000,7\ndropped,0,0\n",encoding="utf-8")
            return summarize(p,t,frequency)
    def test_qpc_join_and_dropped_separation(self):
        r=self.analyze("1000000,0,flip,2\n1500000,1,flip,0\n2000000,0,flip,4\n3000000,0,flip,99\n")
        self.assertEqual(r["frames"],3);self.assertEqual(r["dropped_frames"],1)
        self.assertEqual(r["metrics"]["msUntilDisplayed"]["samples"],2)
        self.assertEqual(r["span_coverage"],1)
    def test_partial_coverage_disclosed(self):
        r=self.analyze("1250000,0,flip,2\n1750000,0,flip,3\n")
        self.assertEqual(r["span_coverage"],.5)
    def test_no_matching_records(self):
        with self.assertRaises(ValueError): self.analyze("3000000,0,flip,2\n")
    def test_invalid_frequency(self):
        with self.assertRaises(ValueError): self.analyze("1000000,0,flip,2\n",0)
