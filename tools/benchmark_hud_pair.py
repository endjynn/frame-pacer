#!/usr/bin/env python3
"""Interleave short baseline/candidate runs for one flagged HUD workload."""

import argparse
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("baseline_libraries", type=Path)
    parser.add_argument("case")
    parser.add_argument("--pairs", type=int, default=6)
    args = parser.parse_args()
    if args.pairs < 3:
        parser.error("at least three pairs required")
    root = Path(__file__).resolve().parent.parent
    args.output.mkdir(parents=True, exist_ok=False)
    collected = {"baseline": [], "candidate": []}
    metadata = {}
    for repetition in range(args.pairs):
        order = ("baseline", "candidate") if repetition % 2 == 0 else ("candidate", "baseline")
        for variant in order:
            output = args.output / f"{variant}-{repetition}"
            command = [sys.executable, str(root / "tools/benchmark_hud.py"), str(output),
                       "--quick", "--frames", "120", "--repetitions", "1", "--case", args.case]
            if variant == "baseline":
                command.extend(("--libraries", str(args.baseline_libraries)))
            subprocess.run(command, check=True)
            current = json.loads((output / "environment.json").read_text())
            if variant in metadata:
                for key in ("harness_sha256", "libraries_sha256", "system", "compiler", "session"):
                    if current[key] != metadata[variant][key]:
                        raise RuntimeError(f"conditions changed: {variant} {key}")
            metadata[variant] = current
            destination = args.output / variant
            destination.mkdir(exist_ok=True)
            records = json.loads((output / "summary.json").read_text())
            for record in records:
                old_name = record["name"]
                record["name"] = f"{args.case}-{repetition}"
                for extension in ("csv", "stderr"):
                    (destination / f"{record['name']}.{extension}").write_bytes(
                        (output / f"{old_name}.{extension}").read_bytes())
                collected[variant].append(record)
    for variant in collected:
        destination = args.output / variant
        metadata[variant]["repetitions"] = args.pairs
        metadata[variant]["paired_order"] = "alternating AB/BA"
        (destination / "environment.json").write_text(json.dumps(metadata[variant], indent=2) + "\n")
        (destination / "summary.json").write_text(json.dumps(collected[variant], indent=2) + "\n")


if __name__ == "__main__":
    main()
