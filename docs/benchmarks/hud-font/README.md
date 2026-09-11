# HUD font performance evidence

Measured 2026-09-11. The user requested a reduced benchmark set; performance
coverage is x86_64, while i386 rendering correctness is tested separately.
Implementation and automated validation are complete. The reduced benchmark
scope and its limitations are recorded below.

These measurements precede the final visual tuning: the reference font size
changed from 24 to 23 pixels, and the panel now fits visible ink with symmetrical
padding. Rendering and layout checks were rerun after those adjustments; the
historical timing and allocation numbers below have not been relabeled as
measurements of the final sizing policy.

## Workloads and method

Both GLX and Vulkan: 1440×900 and 3840×2160, HUD off and four-row HUD, uncapped
and 60 FPS. Each workload has three repetitions of 120 frames, with the first
60 excluded from steady-state statistics. Separate uncapped four-row workloads
alternate 900p/4K every 30 frames and report tagged resize frames separately.

GLX used Intel RPL-S/Mesa 26.0.8; Vulkan used the NVIDIA RTX 4060 Laptop GPU,
driver value 2496987136. Both implementations used the same compiled probes,
deterministic telemetry fixture, machine, compiler, and settings. Raw metadata
records device identity, library/harness hashes, and the session environment.
The baseline is the pre-feature renderer from revision
`8b99e859cdacb5cf5372a5965bcbc7f963ac3721`, with the benchmark harness changes.

Timings cover completed rendering/presentation, not merely CPU submission.
Isolated GPU timestamps were not collected in this reduced suite; GPU cost is
included in end-to-end frame measurements, not inferred from process CPU time.
The separate GPU readback tests validate pixels, not performance.

## Results and investigation

The initial reduced comparison passed 131/136 steady-state/lifecycle/memory
checks and 21/22 resize checks. All raw failures are retained. Targeted reruns
reversed candidate/baseline order; the HUD-enabled CPU and frame-tail flags
did not reproduce. The remaining HUD-off Vulkan control was tested with six
alternating AB/BA pairs: all eight comparisons passed, with a 1.21% CPU-time
difference inside measured variability. No thresholds were widened.

There was no reproducible regression beyond the predefined tolerances in this
reduced set. This is not a claim of zero overhead or universal performance
equivalence across games, drivers, or architectures.

| Measurement | Baseline | Candidate |
| --- | ---: | ---: |
| GLX 900p four-row uncapped CPU/frame, initial run | 81.8 µs | 71.1 µs |
| GLX 4K four-row uncapped CPU/frame, initial run | 117.2 µs | 73.2 µs |
| Vulkan 900p four-row uncapped CPU/frame, targeted run | 192.8 µs | 146.6 µs |
| Vulkan 4K four-row uncapped CPU/frame, initial run | 278.0 µs | 258.5 µs |
| GLX median resize frame, initial run | 12.48 ms | 12.69 ms |
| Vulkan median resize frame, initial run | 69.43 ms | 78.19 ms |

These are medians across repetitions and include driver/presentation work.
They should not be interpreted as isolated font-rendering costs. Peak process
RSS changes in the initial steady-state set ranged from −468 to +1656 KiB.
Resize measurements include drawable/swapchain changes and first-frame upload.

Embedded coverage occupies 1,280,256 bytes plus generated metrics. The x86_64
benchmark GL library's `size` text segment grew from 89,830 to 1,472,211 bytes,
while BSS fell from 201,672 to 69,480 bytes. Vulkan text grew from 108,477 to
1,494,674 bytes. Read-only atlas data accounts for most of this increase;
there is no runtime font rasterizer or new dynamic library dependency.

## Build and validation costs

The optional pinned generator rebuild and two-pass asset reproducibility check
completed in 9.67 seconds wall time (17.55 seconds user CPU, 3.94 seconds system
CPU), with 110,904 KiB maximum resident set size. This used the already downloaded,
checksum-verified FreeType archive; network transfer is excluded. These are
single-run development-tool measurements, not runtime costs. Normal builds do
not invoke this workflow.

A forced ordinary multilib rebuild (`make -B -j4 all`) took 3.02 seconds for
the baseline and 3.13 seconds for the candidate. Peak build-process RSS was
61,700 and 62,048 KiB respectively; user CPU was 5.30 and 5.51 seconds. These
single-run warm-cache measurements describe build impact, not a statistical
performance guarantee. Neither ordinary build invoked the font generator.

Actual NVIDIA Vulkan image-plus-staging allocations were 8,192 bytes at font
size 14 (900p), 28,160 bytes at size 24 (1600p), and 30,976 bytes at size 32
(4K), per swapchain. Corresponding raw R8 coverage sizes are 4,096, 11,776,
and 14,592 bytes. Descriptor/driver bookkeeping is additional and is reflected
only indirectly in process RSS; other drivers may align allocations differently.

The complete ASan/UBSan unit suite and complete i386 unit suite passed in an
isolated source copy. Static analysis passed for 94 C files across 376 build
configurations. GPU pixel tests and package/install checks are tracked separately
from performance measurements.

## Fixed comparison limits

- CPU median: max(2 µs, 5% baseline, 3× baseline repetition MAD).
- Wall median/p95/p99: max(50 µs, 5%, 3× MAD).
- First-frame and resize timing: max(10 ms, 5%, 3× MAD).
- Process lifecycle outside measured frames: max(50 ms, 5%, 3× MAD).
- Peak process RSS increase: 8 MiB, including font and driver allocations.
- Missed 60 FPS deadlines: max(1 frame, 3× baseline MAD).
- Throughput loss: max(5%, 3× baseline MAD).

MAD is the median absolute deviation of the baseline repetitions. Candidate
variability cannot increase the allowance. Regression-gate unit tests cover
timing, resize, memory, throughput, conditions, and insufficient repetitions.

## Reproduction and retained evidence

`validation.tar.gz` preserves completion logs and the retired implementation
plan. The completion audit covered:

| Requirement | Evidence |
| --- | --- |
| Pinned font, license, deterministic offline assets | Two-pass `--check`, asset integrity tests, packaged OFL checks |
| Native advance/line metrics; panel fits ink | Atlas/vertex unit tests, including hinted overhang and tiny-window cases |
| Shared GL/Vulkan layout and native coverage rendering | x86_64/i386 GPU readback matrix, 3/4 rows, 720p through 4K and ultrawide/tiny extents |
| Resize, cached upload, format handling | Repeated GL resize and Vulkan initial/cached readback, UNORM/BGRA/sRGB cases |
| Failure handling, state preservation, cleanup | Vulkan allocation-failure tests, GL context/state tests, live presentation probes, ASan/UBSan |
| Supported builds and unchanged non-HUD behavior | Full `make check`, full i386 unit suite, ABI and package/install tests |
| Maintainability and removal of bitmap path | Full quality checks, obsolete font source/API removal, updated documentation/reference image |
| Runtime performance and development cost | Reduced paired comparison evidence below, build/generation costs above |

OpenGL resources follow the owning context/share-group lifetime; Vulkan font
resources follow swapchain lifetime. No Vulkan validation layer was installed
on this test machine, so validation-layer coverage is not claimed. Actual GPU
readback, command/resource unit tests and sanitizer checks provide the automated
correctness evidence instead.

`results.tar.gz` contains raw CSV/stderr, environment metadata, summary JSON,
all initial and targeted comparison reports, and the paired control results.
`baseline-source.tar.gz` preserves the old source and benchmark prerequisites
without build products or Python caches. It is a historical benchmark artifact,
not a retained runtime fallback and is not included in release packages.

Extract the baseline into an isolated directory. Copy the current
`tools/benchmark_hud.py`, `tests/present_benchmark.h`, `tests/present_extent.h`,
`tests/glx_present_probe.c`, `tests/vulkan_present_probe.c`, and
`tests/hud_benchmark_fixture.c` into their corresponding baseline paths, then
run `make -j4 hud-benchmark-build` in both trees. Do not build concurrently with
measurements. From the candidate checkout:

```sh
python3 tools/benchmark_hud.py build/before --libraries /absolute/baseline/build/font-benchmark --quick --frames 120 --repetitions 3
python3 tools/benchmark_hud.py build/after --quick --frames 120 --repetitions 3
python3 tools/compare_hud_benchmarks.py build/before build/after build/comparison.json
```

Repeat with `--resize` and separate output directories. Use `--case` to rerun
only flagged workloads, and `tools/benchmark_hud_pair.py` for alternating
paired controls. Keep every result, including failures; investigate rather
than repeatedly rerunning until one happens to pass.
