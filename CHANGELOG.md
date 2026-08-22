# Changelog

## Initial release — 2026-08-21

First public release.

- Vulkan implicit-layer and GLX/EGL preload frame pacing.
- Compact GPU, CPU, thread, and FPS HUD.
- Per-executable live FPS rules.
- Opt-in per-thread cgroup-v2 CPU ceilings with transient cleanup.
- x86_64 and i386 build and test coverage.
- NVIDIA GPU telemetry for i386 games through a transient embedded x86_64 NVML
  helper, with coherent asynchronous snapshots, bounded retry and teardown,
  and no installed 32-bit driver packages, executables, or services.
- Real-loader Vulkan instance, device, swapchain, presentation, HUD submission,
  and teardown characterization for both ELF architectures.
- ABI, strict declaration/format warning, GCC analyzer, sanitizer,
  shell-syntax, and normalized coverage gates.
- Correct cleanup for Vulkan instances, repeated log initialization, external
  CPU controllers, controller interruption, and failed quota-state writes.
- Fail-closed handling for an absent Vulkan registry during instance teardown.
- Overflow-safe HUD counter parsing and utilization arithmetic, plus fail-closed
  configuration reloads when a config becomes insecure or is replaced by a
  symlink.
- Derived 352x196 maximum HUD geometry with full-size Vulkan, GLX, and EGL
  presentation probes and capacity sized from the worst valid text.
- Four-cell FPS columns aligned with every other metric, with a reviewed
  supported configuration range of 1 through 999 FPS.
- Atomic, identity-bound external CPU-controller protocol and installation of
  its helper for both source-checkout and installed layouts.
- Shared bounded logging that preserves the active PID log, never exceeds its
  64 MiB limit, and closes all acquired GL loader references on unload.
- A dedicated hidden GL dispatch module that owns downstream loader references
  and direct, `RTLD_NEXT`, GLX, and EGL function-resolution fallback.
- Transactional Vulkan instance/device/queue registration under allocation or
  loader-data failure, with bounded provider enumeration counts.
- A hidden Vulkan device-HUD module for dispatch completeness, coherent metrics
  snapshots, memory-property ownership, and teardown.
- Refactored the original Vulkan layer monolith into focused registry,
  lifecycle, HUD orchestration, resource, and presentation modules while
  preserving the exported ABI and fail-open behavior.
- Shared synchronized HUD metrics caching and renderer-owned maximum vertex
  workspaces, keeping the complete 352x196 panel off presentation stacks.
- Shared XDG state-directory and systemd-provider boundaries, including a
  native controller fallback that gives i386 clients live quota parity without
  requiring i386 libsystemd.
- A hidden GL HUD renderer that owns context-keyed objects, vertex upload, and
  exact state restoration independently of public symbol interception.
- A hidden external CPU-controller transport that owns secure helper discovery,
  atomic state/status handoff, cross-architecture launch, reaping, and protocol
  cleanup independently of direct cgroup reconciliation.
- A deterministic full-size HUD image generated from the production formatter,
  font, and geometry as part of the documentation acceptance gate.
- Measured HUD hot-path tuning: fixed-width text assembly and row-at-a-time
  glyph lookup reduce the repository benchmark's combined HUD preparation CPU
  cost by 25.10% without changing text, geometry, ABI, or backend behavior.
- Measured limiter-path tuning: steady-state configuration polling is 54.6%
  faster, and disabled per-present logging is 25–26% faster, using atomic
  publication while retaining serialized reloads and enabled log writes.
- Clean release builds and the complete automated acceptance suite pass for
  both x86_64 and i386, including ABI, deterministic HUD image, Vulkan and GLX
  presentation on both architectures, EGL presentation on x86_64,
  installation, configuration-security, and cleanup checks.
