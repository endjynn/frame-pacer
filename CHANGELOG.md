# Changelog

## Initial release — 2026-08-21

- FPS limiting for Vulkan, DXVK, vkd3d-proton, GLX, and EGL games.
- Compact, resolution-aware GPU, CPU, thread, and FPS HUD.
- Optional global and per-executable FPS limits, explicit per-game uncapping,
  large-library configurations, and live configuration reloads.
- Optional per-thread CPU limits with transient cleanup.
- x86_64 and i386 game support.
- NVIDIA and DRM GPU telemetry with safe `N/A` fallback.
- Automated build, installation, graphics, ABI, safety, and cleanup tests.
