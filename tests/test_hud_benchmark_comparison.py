#!/usr/bin/env python3
"""Exercise the performance gate with known regressions and mismatched runs."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location("compare_hud", ROOT / "tools/compare_hud_benchmarks.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ComparisonTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.before = Path(self.temporary.name) / "before"
        self.after = Path(self.temporary.name) / "after"
        self.write(self.before)
        self.write(self.after)

    def write(self, directory, cpu=100_000, late=0, device="test GPU", repetitions=3):
        directory.mkdir(exist_ok=True)
        metadata = dict(harness_sha256={"probe": "abc"}, frames=180, warmup_frames=60,
                        repetitions=repetitions, system="test", compiler="test", session={})
        (directory / "environment.json").write_text(json.dumps(metadata))
        records = []
        for index in range(repetitions):
            name = f"glx-x86_64-1440x900-rows4-60-{index}"
            records.append(dict(name=name, median_cpu_ns=cpu, median_wall_ns=16_666_667,
                                p95_wall_ns=16_700_000, p99_wall_ns=16_710_000,
                                first_frame_ns=20_000_000, late_60fps_frames=late))
            (directory / f"{name}.stderr").write_text(f"gl_renderer={device}\npeak_rss_kib=10000\n")
        (directory / "summary.json").write_text(json.dumps(records))

    def test_identical_runs_pass(self):
        self.assertTrue(all(row["passed"] for row in MODULE.compare(self.before, self.after)))

    def test_cpu_regression_fails(self):
        self.write(self.after, cpu=120_000)
        result = MODULE.compare(self.before, self.after)
        self.assertFalse(next(row for row in result if row["metric"] == "median_cpu_ns")["passed"])

    def test_missed_deadlines_fail(self):
        self.write(self.after, late=10)
        result = MODULE.compare(self.before, self.after)
        self.assertFalse(next(row for row in result if row["metric"] == "late_60fps_frames")["passed"])

    def test_device_change_is_rejected(self):
        self.write(self.after, device="different GPU")
        with self.assertRaisesRegex(ValueError, "device/driver"):
            MODULE.compare(self.before, self.after)

    def test_harness_change_is_rejected(self):
        path = self.after / "environment.json"
        metadata = json.loads(path.read_text())
        metadata["harness_sha256"]["probe"] = "modified"
        path.write_text(json.dumps(metadata))
        with self.assertRaisesRegex(ValueError, "harness_sha256"):
            MODULE.compare(self.before, self.after)

    def test_insufficient_repetitions_are_rejected(self):
        self.write(self.before, repetitions=1)
        self.write(self.after, repetitions=1)
        with self.assertRaisesRegex(ValueError, "three"):
            MODULE.compare(self.before, self.after)

    def test_resize_regression_fails(self):
        for directory in (self.before, self.after):
            path = directory / "environment.json"
            metadata = json.loads(path.read_text())
            metadata["resize"] = True
            path.write_text(json.dumps(metadata))
            path = directory / "summary.json"
            records = json.loads(path.read_text())
            for record in records:
                record.update(resize_median_wall_ns=20_000_000,
                              resize_max_wall_ns=30_000_000,
                              resize_median_cpu_ns=10_000_000)
                if directory == self.after:
                    record["resize_median_cpu_ns"] = 30_000_000
            path.write_text(json.dumps(records))
        report = MODULE.compare(self.before, self.after)
        self.assertFalse(next(row for row in report if row["metric"] == "resize_median_cpu_ns")["passed"])

    def test_throughput_regression_fails(self):
        for directory, throughput in ((self.before, 1000), (self.after, 900)):
            path = directory / "summary.json"
            records = json.loads(path.read_text())
            for record in records:
                record["throughput_fps"] = throughput
            path.write_text(json.dumps(records))
        report = MODULE.compare(self.before, self.after)
        self.assertFalse(next(row for row in report if row["metric"] == "throughput_fps")["passed"])

    def test_memory_regression_fails(self):
        for path in self.after.glob("*.stderr"):
            path.write_text(path.read_text().replace("peak_rss_kib=10000", "peak_rss_kib=20000"))
        report = MODULE.compare(self.before, self.after)
        self.assertFalse(next(row for row in report if row["metric"] == "peak_rss_kib")["passed"])

    def test_candidate_variability_does_not_widen_allowance(self):
        path = self.after / "summary.json"
        records = json.loads(path.read_text())
        for record, cpu in zip(records, (1000, 200_000, 400_000)):
            record["median_cpu_ns"] = cpu
        path.write_text(json.dumps(records))
        report = MODULE.compare(self.before, self.after)
        cpu = next(row for row in report if row["metric"] == "median_cpu_ns")
        self.assertEqual(cpu["allowance"], 5000)
        self.assertFalse(cpu["passed"])


if __name__ == "__main__":
    unittest.main()
