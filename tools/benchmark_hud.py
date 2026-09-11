#!/usr/bin/env python3
"""Record raw, completed-frame timings through the real presentation backends."""

import argparse
import hashlib
import json
import os
import platform
from pathlib import Path
import statistics
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--frames", type=int, default=360)
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--backend", choices=("all", "glx", "vulkan"), default="all")
    parser.add_argument("--architecture", choices=("all", "x86_64", "i386"), default="all")
    parser.add_argument("--resize", action="store_true", help="alternate 900p/4K every 30 frames")
    parser.add_argument("--libraries", type=Path, help="use an isolated baseline's benchmark libraries")
    parser.add_argument("--quick", action="store_true", help="x86_64 controls/four-row HUD, or a focused resize workload")
    parser.add_argument("--case", action="append", default=[], help="run only this exact workload name (without repetition suffix)")
    args = parser.parse_args()
    if not 61 <= args.frames <= 4096 or args.repetitions < 1:
        parser.error("need 61–4096 frames and at least one repetition")
    if args.resize and args.frames < 92:
        parser.error("resize workloads need at least 92 frames")
    root = Path(__file__).resolve().parent.parent
    args.output.mkdir(parents=True, exist_ok=False)
    libraries = (args.libraries or root / "build/font-benchmark").resolve()
    metadata = {
        "revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "system": platform.platform(),
        "compiler": subprocess.check_output(["gcc", "--version"], text=True),
        "frames": args.frames, "warmup_frames": 60, "repetitions": args.repetitions,
        "resize": args.resize,
        "quick": args.quick,
        "harness_sha256": {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest() for path in (
            Path(__file__), root / "tests/present_benchmark.h", root / "tests/glx_present_probe.c",
            root / "tests/present_extent.h",
            root / "tests/vulkan_present_probe.c", root / "tests/hud_benchmark_fixture.c",
        )},
        "libraries_sha256": {str(path.relative_to(libraries)): hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(libraries.glob("*/*.so"))},
        "libraries_directory": str(libraries),
        "session": {key: os.environ.get(key) for key in ("XDG_SESSION_TYPE", "DISPLAY", "WAYLAND_DISPLAY")},
        "timing_scope": "completed frame including presentation; not isolated GPU HUD time",
    }
    (args.output / "environment.json").write_text(json.dumps(metadata, indent=2) + "\n")
    records = []
    with tempfile.TemporaryDirectory(prefix="frame-pacer-hud-benchmark-") as state:
        config = Path(state) / "frame-pacer"
        config.mkdir(mode=0o700)
        layer = Path(state) / "layers"
        layer.mkdir()
        for backend in (("glx", "vulkan") if args.backend == "all" else (args.backend,)):
            for architecture in (("x86_64",) if args.quick else (("x86_64", "i386") if args.architecture == "all" else (args.architecture,))):
                suffix = "-i386" if architecture == "i386" else ""
                executable = root / "build" / f"{backend}-present-probe{suffix}"
                for width, height in (((1440, 900),) if args.quick and args.resize else ((1440, 900), (3840, 2160))):
                    for row_count in ((4,) if args.quick and args.resize else (0, 4) if args.quick else (0, 3, 4)):
                        hud = "on" if row_count else "off"
                        for fps in (("off",) if args.quick and args.resize else ("off", "60")):
                            mode = "-resize" if args.resize else ""
                            case = f"{backend}-{architecture}-{width}x{height}-rows{row_count}{mode}-{fps}"
                            if args.case and case not in args.case:
                                continue
                            config_file = config / "frame-pacer.conf"
                            config_file.write_text(f"global_fps_limit = {fps}\nhud = {hud}\n")
                            config_file.chmod(0o600)
                            for repetition in range(args.repetitions):
                                name = f"{case}-{repetition}"
                                environment = {key: value for key, value in os.environ.items()
                                               if not key.startswith("FRAME_PACER_") and key not in (
                                                   "LD_PRELOAD", "VK_LAYER_PATH", "VK_INSTANCE_LAYERS",
                                                   "ENABLE_FRAME_PACER", "DISABLE_FRAME_PACER",
                                               )}
                                environment.update(
                                    XDG_CONFIG_HOME=state,
                                    XDG_STATE_HOME=state,
                                    FRAME_PACER_BENCH_FRAMES=str(args.frames),
                                    FRAME_PACER_BENCH_WIDTH=str(width),
                                    FRAME_PACER_BENCH_HEIGHT=str(height),
                                    FRAME_PACER_LOG="0",
                                    FRAME_PACER_BENCH_ROWS=str(row_count),
                                )
                                if args.resize:
                                    environment["FRAME_PACER_BENCH_RESIZE"] = "1"
                                if backend == "glx":
                                    environment["LD_PRELOAD"] = str(libraries / architecture / "libframe_pacer_gl_shim.so")
                                else:
                                    manifest = json.loads((root / "build" / architecture / "layer/VkLayer_frame_pacer.json").read_text())
                                    manifest["layer"]["library_path"] = str(libraries / architecture / "libVkLayer_frame_pacer.so")
                                    (layer / "VkLayer_frame_pacer.json").write_text(json.dumps(manifest))
                                    environment["VK_LAYER_PATH"] = str(layer)
                                    environment["VK_INSTANCE_LAYERS"] = "VK_LAYER_ENDJYNN_frame_pacer"
                                process_start = time.monotonic_ns()
                                result = subprocess.run([str(executable)], env=environment, text=True, capture_output=True, timeout=180, check=False)
                                process_elapsed = time.monotonic_ns() - process_start
                                (args.output / f"{name}.csv").write_text(result.stdout)
                                (args.output / f"{name}.stderr").write_text(result.stderr)
                                result.check_returncode()
                                if row_count and f"benchmark_hud_rows={row_count}\n" not in result.stderr:
                                    raise RuntimeError(f"deterministic HUD fixture was not exercised: {name}")
                                rows = [list(map(int, line.split(","))) for line in result.stdout.splitlines()[1:]]
                                if len(rows) != args.frames:
                                    raise RuntimeError(f"incomplete samples: {name}")
                                resize_frames = [int(line.split("=", 1)[1]) for line in result.stderr.splitlines()
                                                 if line.startswith("resize_frame=")]
                                expected_resizes = list(range(30, args.frames, 30)) if args.resize else []
                                if resize_frames != expected_resizes:
                                    raise RuntimeError(f"resize workload was not exercised: {name}")
                                measured = [row for row in rows[60:] if row[0] not in resize_frames]
                                wall = sorted(row[1] for row in measured)
                                records.append(dict(
                                    name=name,
                                    median_wall_ns=statistics.median(wall),
                                    p95_wall_ns=wall[int((len(wall)-1)*0.95)],
                                    p99_wall_ns=wall[int((len(wall)-1)*0.99)],
                                    median_cpu_ns=statistics.median(row[2] for row in measured),
                                    throughput_fps=1e9 * len(wall) / sum(wall),
                                    late_60fps_frames=sum(value > 17_166_667 for value in wall),
                                    first_frame_ns=rows[0][1],
                                    lifecycle_ns=max(0, process_elapsed - sum(row[1] for row in rows)),
                                ))
                                if resize_frames:
                                    records[-1].update(
                                        resize_median_wall_ns=statistics.median(rows[index][1] for index in resize_frames),
                                        resize_max_wall_ns=max(rows[index][1] for index in resize_frames),
                                        resize_median_cpu_ns=statistics.median(rows[index][2] for index in resize_frames),
                                    )
                                print(name, records[-1]["median_wall_ns"], flush=True)
                                (args.output / "summary.json").write_text(json.dumps(records, indent=2) + "\n")
    if not records:
        parser.error("no matching workloads")


if __name__ == "__main__":
    main()
