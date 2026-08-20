# HUD

The HUD appears in the top-left corner. Its compact, fixed-cell layout keeps
values aligned even when a provider is unavailable.

Set `hud = off` at the top level of `frame-pacer.conf` to disable it without
disabling frame pacing. `hud = on` is the default; valid changes are applied
to a running game within one second.

| Row | Meaning |
| --- | --- |
| `GPU <GPU usage> <GPU temperature>` | Game-associated GPU utilisation and temperature. |
| `CPU <CPU usage> <CPU temperature>` | System CPU utilisation and CPU package temperature. |
| `THR <peak thread CPU usage> <configured limit>` | Per-thread CPU activity and configured thread ceiling. Visible only for an active `thread_cpu_limit`. |
| `FPS <current frame rate>` | Measured frame rate. |

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
by the game. NVIDIA utilisation is obtained by dynamically loading
`libnvidia-ml.so.1` in the game process's own architecture and loader
namespace. Other DRM drivers can expose render-engine time through
`/proc/<pid>/fdinfo`; duplicate DRM client IDs are counted once.

Unavailable providers display `N/A` and never affect pacing or HUD rendering.
Frame-pacer does not bundle NVML, search hard-coded host paths, run
`nvidia-smi`, or start a telemetry helper.
