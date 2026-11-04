# SPDX-License-Identifier: GPL-3.0-or-later
import csv
from pathlib import Path
import tempfile
import unittest
from summarize_latency import summarize

class LatencySummaryTests(unittest.TestCase):
    def report(self, rows):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"trace.csv"
            fields="event,time_ns,packet,input,sampled_ns,queued_ns,scene,previous_packet,previous_input,previous_sampled_ns,previous_queued_ns,alpha,window_submitted".split(",")
            with path.open("w",newline="") as file:
                w=csv.writer(file);w.writerow(fields);w.writerows(rows);w.writerow(["dropped",0,0,0,0,0,0,0,0,0,0,1,0])
            return summarize(path)
    def test_history_and_queue_are_separate(self):
        report=self.report([
            ["dequeue",121000000,2,2,120000000,120500000,7,0,0,0,0,1,0],
            ["interpolated_submit",125000000,2,2,120000000,120500000,7,1,1,100000000,101000000,.25,1]])
        self.assertEqual(report["latest_sample_to_cpu_submission"]["p50_ms"],5)
        self.assertEqual(report["packet_queue_wait"]["p50_ms"],.5)
        self.assertEqual(report["matched_geometry_history_component"]["p50_ms"],15)
        self.assertEqual(report["matched_geometry_weighted_sample_age_proxy"]["p50_ms"],20)
    def test_source_progression_tracks_blend_and_clamping(self):
        report=self.report([
            ["interpolated_submit",125000000,2,2,120000000,120500000,7,1,1,100000000,101000000,.25,1],
            ["interpolated_submit",130000000,2,2,120000000,120500000,7,1,1,100000000,101000000,.5,1],
            ["interpolated_submit",135000000,2,2,120000000,120500000,7,1,1,100000000,101000000,1,1],
            ["interpolated_submit",140000000,2,2,120000000,120500000,7,1,1,100000000,101000000,1,1]])
        self.assertEqual(report["matched_geometry_source_interval_proxy"]["p50_ms"],4.875)
        self.assertEqual(report["repeated_source_times"],1)
        self.assertEqual(report["latest_endpoint_fraction"],.5)

    def test_failed_acquisition_not_presented(self):
        report=self.report([["original_submit",125000000,2,2,120000000,120500000,7,0,0,0,0,1,0]])
        self.assertEqual(report["submitted_frames"],0)
        self.assertEqual(report["unavailable_swapchains"],1)
        self.assertIsNone(report["latest_sample_to_cpu_submission"])
    def test_original_has_no_history_proxy(self):
        report=self.report([["original_submit",125000000,2,2,120000000,120500000,7,0,0,0,0,1,1]])
        self.assertIsNone(report["matched_geometry_history_component"])
        self.assertEqual(report["submitted_frames"],1)

if __name__=="__main__":unittest.main()
