#!/usr/bin/env python3
"""Compare identical HUD benchmark workloads using baseline-only noise allowances."""

import argparse
import json
from pathlib import Path
import statistics
import sys


def groups(directory):
    result = {}
    for record in json.loads((directory / "summary.json").read_text()):
        case, _ = record["name"].rsplit("-", 1)
        result.setdefault(case, []).append(record)
    return result


def identity(directory, record):
    lines = (directory / (record["name"] + ".stderr")).read_text().splitlines()
    keys = ("gl_vendor=", "gl_renderer=", "gl_version=", "vk_device=", "vk_driver_version=", "vk_device_type=")
    return sorted(line for line in lines if line.startswith(keys))


def peak_rss(directory, records):
    values = []
    for record in records:
        for line in (directory / (record["name"] + ".stderr")).read_text().splitlines():
            if line.startswith("peak_rss_kib="):
                values.append(int(line.split("=", 1)[1]))
    if len(values) != len(records):
        raise ValueError("missing memory samples")
    return statistics.median(values)


def compare(before, after):
    baseline, candidate = groups(before), groups(after)
    if baseline.keys() != candidate.keys():
        raise ValueError("workloads do not match")
    a = json.loads((before / "environment.json").read_text())
    b = json.loads((after / "environment.json").read_text())
    if a.get("resize", False) != b.get("resize", False):
        raise ValueError("benchmark resize conditions differ")
    for key in ("harness_sha256", "frames", "warmup_frames", "repetitions", "system", "compiler", "session"):
        if a[key] != b[key]:
            raise ValueError(f"benchmark conditions differ: {key}")
    report = []
    for case in sorted(baseline):
        old, new = baseline[case], candidate[case]
        if len(old) < 3 or len(new) != len(old):
            raise ValueError("need at least three matching repetitions")
        device = identity(before, old[0])
        if not device or any(identity(directory, record) != device
                             for directory, records in ((before, old), (after, new)) for record in records):
            raise ValueError(f"graphics device/driver changed: {case}")
        metrics = ["median_cpu_ns", "median_wall_ns", "p95_wall_ns", "p99_wall_ns", "first_frame_ns"]
        if "lifecycle_ns" in old[0]:
            metrics.append("lifecycle_ns")
        if a.get("resize", False):
            metrics.extend(("resize_median_wall_ns", "resize_max_wall_ns", "resize_median_cpu_ns"))
        for metric in metrics:
            values = [entry[metric] for entry in old]
            base = statistics.median(values)
            actual = statistics.median(entry[metric] for entry in new)
            mad = statistics.median(abs(value - base) for value in values)
            floor = 2_000 if metric == "median_cpu_ns" else 50_000
            if metric == "first_frame_ns":
                floor = 10_000_000
            if metric.startswith("resize_"):
                floor = 10_000_000
            if metric == "lifecycle_ns":
                floor = 50_000_000
            allowance = max(floor, base * 0.05, mad * 3)
            report.append(dict(case=case, metric=metric, baseline=base, candidate=actual,
                               delta=actual-base, percent=(actual-base)*100/base if base else None,
                               baseline_mad=mad, allowance=allowance, passed=actual <= base + allowance))
        if "throughput_fps" in old[0]:
            values = [entry["throughput_fps"] for entry in old]
            base = statistics.median(values)
            actual = statistics.median(entry["throughput_fps"] for entry in new)
            mad = statistics.median(abs(value - base) for value in values)
            allowance = max(base * 0.05, mad * 3)
            report.append(dict(case=case, metric="throughput_fps", baseline=base, candidate=actual,
                               delta=actual-base, percent=(actual-base)*100/base,
                               baseline_mad=mad, allowance=allowance, passed=actual >= base - allowance))
        if case.endswith("-60"):
            values = [entry["late_60fps_frames"] for entry in old]
            base = statistics.median(values)
            actual = statistics.median(entry["late_60fps_frames"] for entry in new)
            mad = statistics.median(abs(value - base) for value in values)
            allowance = max(1, mad * 3)
            report.append(dict(case=case, metric="late_60fps_frames", baseline=base, candidate=actual,
                               delta=actual-base, allowance=allowance, passed=actual <= base + allowance))
        base, actual = peak_rss(before, old), peak_rss(after, new)
        report.append(dict(case=case, metric="peak_rss_kib", baseline=base, candidate=actual,
                           delta=actual-base, percent=(actual-base)*100/base, allowance=8192,
                           passed=actual <= base + 8192))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    report = compare(args.baseline, args.candidate)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    failures = [entry for entry in report if not entry["passed"]]
    for entry in failures:
        print(entry["case"], entry["metric"], "REGRESSION", entry["delta"])
    print(f"{len(report) - len(failures)}/{len(report)} comparisons passed")
    return bool(failures)


if __name__ == "__main__":
    sys.exit(main())
