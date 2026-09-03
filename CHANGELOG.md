# Changelog

## 0.1.0-beta.3 - 2026-09-03

- Improved AMD GPU activity readings by avoiding double counting concurrent
  engines.
- Moved HUD telemetry sampling into the background to keep sensor reads off
  rendering threads.

## 0.1.0-beta.2 — 2026-09-02

- Added AMD GPU activity and temperature telemetry.
- Made troubleshooting logs clearer and less noisy.
- Improved per-game detection for Proton collection launchers.

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
