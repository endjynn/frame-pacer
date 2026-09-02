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

The release and source installers place the Vulkan libraries, GLX/EGL runtime,
CPU controller, and installed version below `~/.local/lib/frame-pacer`. Vulkan
manifests are generated below `~/.local/share/vulkan/implicit_layer.d` with
absolute paths to the selected prefix.

The GL runtime is installed in both Debian/Ubuntu multiarch and Arch-family
`lib`/`lib32` layouts so Steam's `${LIB}` expansion selects the correct binary.
The preload shim loads its same-directory backend. Alternative package builds
can set `PREFIX` and `DESTDIR` without changing the runtime contract.

## Configuration loading

The rendering process reads the configuration directly and checks for changes
at most once per second. Executable matching is performed inside that process;
there is no game database, window-title matching, or desktop-environment
integration. For Wine processes, the mapped PE executable is authoritative and
takes priority over command-line and same-user ancestor candidates. This keeps
individual games distinguishable when a collection launcher starts a child and
Wine clears or preserves the launcher's command line.

Configuration loading rejects symbolic links, oversized files, files with
group or other permissions, ownership changes, malformed values, and partial
replacement. Missing or invalid input applies no FPS limit and disables thread
CPU control.

Each completed poll publishes one immutable effective snapshot under the
configuration mutex, then release-publishes its semantic revision. The revision
changes only when a value, source, match, status, or diagnostic changes—not for
comments, whitespace, or timestamp-only edits. Vulkan, GLX, and EGL use a
shared bounded formatter and separate atomic report slots, so an opted-in log
contains at most one complete effective report per revision and active backend.
The report is a single write of at most 511 bytes.

Opting into logging makes a process eligible for a log but does not create one
during routine layer or interposer initialization. The logger activates when a
Vulkan swapchain establishes rendering intent, on the first presentation or
pacing attempt, or on a directly diagnosable initialization failure. Its
startup header is written before the descriptor is published, so concurrent
activation cannot place another event before that header.

The presentation hot paths check the activated log descriptor. When logging is
disabled, they bypass report revision checks, snapshot locking, formatting,
varargs setup, and writes. First-presentation detection reuses existing Vulkan
presentation and OpenGL swap counters, adding no steady-state logging work.
HUD and thread CPU settings remain lock-free atomic reads regardless of
logging state.

Enabled logs are event-oriented. They record startup, effective configuration,
important state changes, failures, and shutdown totals, but not routine
successful presentations or swaps. Consecutive identical Vulkan presentation
failures produce one record until the result changes.

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

Telemetry providers and filesystem sampling run on a request-driven worker.
The worker starts only after the HUD first requests telemetry, samples at most
once per second, and sleeps when the HUD is not rendering. Presentation threads
only copy the latest completed snapshot.

NVIDIA telemetry normally uses the host's NVML library. If an i386 game cannot
load NVML directly, frame-pacer can run an embedded x86_64 helper from sealed,
anonymous memory. The helper communicates through a private socket, creates no
installed executable or named temporary file, and exits with the game.

DRM process `fdinfo` supplies game-associated render, graphics, and compute
engine activity when the driver exposes those counters. Descriptors are
deduplicated by DRM client ID, and temporary counter regressions retain their
last valid high-water mark. Each engine class is calculated independently and
the busiest class is displayed, avoiding double counting concurrent engines.
For an AMD render device, frame-pacer reads the `edge` temperature from the
hwmon provider linked to that device, with `temp1_input` as a compatibility
fallback. It does not scan unrelated GPUs.

Missing telemetry displays `N/A` and does not affect pacing.

## Runtime state

Diagnostic logs and temporary CPU-controller communication use
`$XDG_STATE_HOME/frame-pacer`, or `~/.local/state/frame-pacer` when
`XDG_STATE_HOME` is unset. Protocol files and transient helpers are removed
during teardown. Each backend retains at most ten PID-qualified logs, with a
64 MiB maximum per file. Launchers and child games retain separate PID logs
when each renders, while non-rendering helper processes create none.
