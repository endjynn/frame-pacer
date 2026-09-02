# Changelog

## 0.1.0-beta.2 — unreleased

- Versioned Vulkan and OpenGL startup logs identify the exact loaded build.
- Effective-configuration reports explain selected values, sources, executable
  matches, live reloads, and precise safe-default reasons.
- Disabled logging bypasses report collection and formatting on presentation
  paths; HUD and thread CPU configuration reads remain lock-free.
- Diagnostic logs omit routine per-frame messages while preserving startup,
  configuration changes, important transitions, failures, and shutdown totals.
- Logs activate only for rendering/pacing processes or actionable failures, so
  Wine and Proton helpers that merely load frame-pacer create no routine files.
- Configuration diagnostics now distinguish missing, insecure, unreadable, and
  malformed files, including exact parser reasons and line numbers.
- Proton collection launchers now identify each mapped child game executable;
  exact child rules override collection-wide launcher fallbacks.

## 0.1.0-beta.1 — 2026-09-01

- FPS limiting for Vulkan, DXVK, vkd3d-proton, GLX, and EGL games.
- Compact, resolution-aware GPU, CPU, thread, and FPS HUD.
- Optional global and per-executable FPS limits, explicit per-game uncapping,
  large-library configurations, and live configuration reloads.
- Optional per-thread CPU limits with transient cleanup.
- x86_64 and i386 game support.
- NVIDIA and DRM GPU telemetry with safe `N/A` fallback.
- Automated build, installation, graphics, ABI, safety, and cleanup tests.
- Ready-to-install, reproducible multilib release archives.
- Read-only pull-request CI and isolated tag-driven GitHub releases.
