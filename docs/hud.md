# HUD

The HUD appears in the top-left corner. Its compact, fixed-cell layout keeps
values aligned even when a provider is unavailable. Its geometry scales
automatically from the current Vulkan swapchain or GLX/EGL drawable extent;
it does not query the window system, desktop environment, monitor, or physical
DPI.

The 2560 by 1600 reference resolution uses three screen pixels for each
bitmap-font pixel. At that size, the complete panel is 264 by 147 pixels with
the optional fourth row and the usual three-row panel is 264 by 117 pixels. A
1920 by 1200 drawable selects two screen pixels per font pixel, making those
panels 176 by 98 and 176 by 78 pixels respectively. Every value occupies four
glyph cells, including the supported maximum of 999 FPS.

Scaling uses whichever drawable axis is more constrained and rounds to the
nearest whole font-pixel size. This keeps the pixel font sharp, prevents an
ultrawide or tall aspect ratio from enlarging the HUD based on only one axis,
and updates naturally when a drawable or swapchain is recreated at a new
resolution. Extremely small drawables are additionally constrained so the
complete optional panel cannot extend outside their bounds. Because rendering
APIs do not expose physical DPI portably, the policy preserves proportional
screen footprint rather than an exact physical measurement across unrelated
monitors.

Set `hud = off` at the top level of `frame-pacer.conf` to disable it without
disabling frame pacing. `hud = on` is the default; valid changes are applied
to a running game within one second.

The FPS value is recalculated every 500 ms from accepted presentation calls.
GPU, CPU, temperature, and thread metrics are sampled once per second. The HUD
is still drawn on presentation between samples using the latest coherent
snapshot; these cadences are independent of the configured FPS limit.

| Row | Meaning |
| --- | --- |
| `GPU <GPU usage> <GPU temperature>` | Game-associated GPU utilisation and temperature. |
| `CPU <CPU usage> <CPU temperature>` | System CPU utilisation and CPU package temperature. |
| `THR <peak thread CPU usage> <configured limit>` | Per-thread CPU activity and configured thread ceiling. Visible only for an active `thread_cpu_limit`. |
| `FPS <current frame rate> <configured limit>` | Measured frame rate and the active presentation cap. |

Available percentages use `%`; temperatures use the small degree glyph; FPS
uses a matching half-height frame glyph. `N/A` occupies the same value column
as a number, so it does not shift adjacent metrics.

## THR

`THR 42% 50%` means the busiest owned game thread used 42% of one logical CPU
core in the preceding sample window while a 50% per-thread limit is confirmed.
The first value is real usage, not a synthetic estimate or a clamped display.
A value near the configured limit means that a thread is actively reaching the
ceiling.

Usage comes from the owned thread cgroups' `cpu.stat` `usage_usec` deltas over
about one second; 100% is one fully used logical core. Because cgroup CPU
accounting uses 100-ms quota periods and the sample window is not aligned to
them, a short sample can modestly exceed the displayed limit at a period
boundary. That does not by itself mean the ceiling failed. A sustained or large
excess warrants investigation.

The limit field is `N/A` until every currently discovered thread has been
verified in an owned child with the exact requested `cpu.max`. Thread usage can
also briefly be `N/A` while its first sample is collected or after a controller
hierarchy reset. See [CPU thread limiter](cpu-thread-limiter.md) for controller
behavior.

## Telemetry sources

CPU use is read from `/proc/stat`; CPU temperature comes from the suitable
`coretemp` hwmon entry. GPU association starts with the DRM render node opened
by the game and its PCI identity. NVIDIA utilisation and temperature normally
come from `libnvidia-ml.so.1` loaded directly in the game process.

When a 32-bit NVIDIA game cannot load 32-bit NVML, frame-pacer starts its
embedded x86-64 telemetry image from a sealed anonymous `memfd`. The helper
loads the host's existing 64-bit NVML, selects the exact PCI device, and sends
one versioned snapshot per second over a private socket. A dedicated client
thread owns process management and socket I/O; presentation only copies the
latest validated snapshot. The helper has no separately installed executable,
named temporary file, service, or persistent state and exits when its socket or
target process disappears. Startup is bounded to three attempts for the owning
backend and target process; failure leaves the affected values at `N/A`.

Other DRM drivers can expose render-engine time through `/proc/<pid>/fdinfo`;
duplicate DRM client IDs are counted once.

Unavailable providers display `N/A` and never affect pacing or HUD rendering.
Frame-pacer does not bundle NVML, search hard-coded host library paths, invoke
`nvidia-smi`, or require an i386 NVIDIA compute package.
