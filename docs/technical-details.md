# Technical details

This document describes frame-pacer's runtime design and safety boundaries.
It is not required for normal installation or use.

## Graphics integration

The Vulkan layer limits the final `vkQueuePresentKHR` presentation stream. Its
user-installed manifests support both x86_64 and i386 processes and are
activated by `ENABLE_FRAME_PACER=1`.

The GLX/EGL shim is loaded into one selected game with `LD_PRELOAD`. It forwards
graphics calls to the real libraries and limits the final GLX or EGL buffer
swap. The shim is deliberately not installed or enabled globally.

Each backend fails open: if frame-pacer cannot safely manage a presentation,
the original graphics call continues rather than intentionally stopping the
game.

## Installation layout

`make install` places the Vulkan libraries and CPU controller below
`~/.local/lib/frame-pacer` and the Vulkan manifests below
`~/.local/share/vulkan/implicit_layer.d`. The GLX/EGL shim remains in the
repository's `build` directory for explicit per-game use.

Alternative package builds can set `PREFIX`, `INSTALL_LIBDIR`,
`INSTALL_LAYERDIR`, and `DESTDIR`.

## Configuration loading

The rendering process reads the configuration directly and checks for changes
at most once per second. Executable matching is performed inside that process;
there is no game database, window-title matching, or desktop-environment
integration.

Configuration loading rejects symbolic links, oversized files, files with
group or other permissions, ownership changes, malformed values, and partial
replacement. Missing or invalid input applies no FPS limit and disables thread
CPU control.

## CPU thread control

`thread_cpu_limit` uses a private cgroup-v2 subtree inside a delegated,
transient systemd user scope. Every discovered game thread receives its own
`cpu.max` value. Frame-pacer verifies ownership and cgroup paths before moving
a thread and does not modify Steam, a parent cgroup, or unrelated processes.

Some Steam Runtime containers expose cgroups read-only. In that case a
short-lived native controller performs the same operations outside the
container. It is tied to the game process and is not a permanent service.
Owned cgroups are removed when the option is disabled, the backend unloads, or
the game exits.

The `THR` HUD limit remains `N/A` until every current game thread has been
verified with the requested ceiling. Usage is calculated from cgroup CPU-time
deltas; 100% represents one fully used logical core.

## HUD telemetry

CPU use comes from `/proc/stat`, and CPU temperature uses a suitable hwmon
sensor when available. GPU selection starts with the DRM render device opened
by the game.

NVIDIA telemetry normally uses the host's NVML library. If an i386 game cannot
load NVML directly, frame-pacer can run an embedded x86_64 helper from sealed,
anonymous memory. The helper communicates through a private socket, creates no
installed executable or named temporary file, and exits with the game. Other
DRM drivers may provide game-associated engine activity through process
`fdinfo`.

Missing telemetry displays `N/A` and does not affect pacing.

## Runtime state

Diagnostic logs and temporary CPU-controller communication use
`$XDG_STATE_HOME/frame-pacer`, or `~/.local/state/frame-pacer` when
`XDG_STATE_HOME` is unset. Protocol files and transient helpers are removed
during teardown. Each backend retains at most ten PID-qualified logs, with a
64 MiB maximum per file.
