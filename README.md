# frame-pacer

Frame-pacer is a Linux frame limiter and compact performance HUD for Steam
games. It supports Vulkan through an implicit layer and GLX/EGL through an
opt-in per-game preload shim.

> **Early release:** frame-pacer changes graphics presentation and can create a
> private cgroup-v2 hierarchy for an explicitly configured game. Start with one
> game, keep a rollback path, and report reproducible problems before enabling
> it broadly.

## Quick start

Frame-pacer is currently used directly from a source checkout. It needs a
systemd-based Linux desktop, cgroup v2 for the optional CPU limiter, Steam, a
working Vulkan loader, and the build tools listed in
[Build requirements](docs/reference/environment.md).

Build and test both 64-bit and 32-bit artifacts:

```sh
git clone https://github.com/endjynn/frame-pacer.git
cd frame-pacer
make check
```

Create the configuration directory and a first rule:

```sh
mkdir -p ~/.config/frame-pacer
chmod 700 ~/.config/frame-pacer
```

Create `~/.config/frame-pacer/frame-pacer.conf` with permissions `0600`:

```ini
global_fps_limit = 70

[Example game]
executable = "ExampleGame.exe"
fps_limit = 60
thread_cpu_limit = off
```

Replace `ExampleGame.exe` with the game's actual renderer executable basename.
`thread_cpu_limit` is optional; enable it only after normal pacing is working.
After saving the file, run `chmod 600 ~/.config/frame-pacer/frame-pacer.conf`.

### Enable Vulkan games

For a safe first test, start Steam from a terminal in the checkout:

```sh
VK_ADD_IMPLICIT_LAYER_PATH="$PWD/build/x86_64/implicit_layer:$PWD/build/i386/implicit_layer" \
ENABLE_FRAME_PACER_HUD=1 \
steam
```

This enables frame-pacer's Vulkan implicit layer for that Steam session only.
Confirm the HUD and pacing in one Vulkan/DXVK/vkd3d-proton game. For a
permanent desktop-launcher setup, see [Steam integration](docs/steam-integration.md).

### Enable GLX/EGL games

Do **not** enable the GLX/EGL shim globally. In Steam's launch options for one
known GLX/EGL game, add:

```text
LD_PRELOAD=/absolute/path/to/frame-pacer/build/${LIB}/libframe_pacer_gl_shim.so %command%
```

Replace `/absolute/path/to/frame-pacer` with this checkout's absolute path.
Fully exit Steam before changing per-game launch options, then restart it.

## Inspired by MangoHud

Frame-pacer is directly inspired by [MangoHud](https://github.com/flightlessmango/MangoHud).
Thank you to the MangoHud maintainers and contributors for their excellent,
long-running work on Linux gaming tooling.

The two projects have deliberately different priorities:

| Project | Primary focus | HUD role |
| --- | --- | --- |
| MangoHud | A highly configurable performance and monitoring HUD; FPS limiting is one of its capabilities. | Rich and configurable. |
| frame-pacer | Predictable FPS pacing and opt-in per-thread CPU limiting. | Minimal, fixed, and secondary to pacing. |

Frame-pacer is complementary to MangoHud, not a replacement for it.

## Features

- Vulkan, DXVK, and vkd3d-proton frame pacing and HUD through an implicit
  layer.
- GLX/EGL pacing and HUD through an opt-in per-game preload shim.
- Live, per-executable FPS rules.
- Optional live per-thread CPU ceilings using a private transient cgroup-v2
  controller.
- GPU, CPU, FPS, and thread-CPU HUD telemetry that degrades safely to `N/A`.

## Documentation

- [Installation](docs/installation.md) — requirements, safe activation, and
  rollback.
- [FPS limiter](docs/fps-limiter.md) — configuration, matching, and reload
  behavior.
- [CPU thread limiter](docs/cpu-thread-limiter.md) — controller behavior,
  cleanup, and verification.
- [HUD](docs/hud.md) — metric meanings and `N/A` behavior.
- [Steam integration](docs/steam-integration.md) — Vulkan and GLX/EGL
  activation boundaries.
- [Contributing](CONTRIBUTING.md) — development and test expectations.
- [Security policy](SECURITY.md) — reporting guidance.
- [Backend reference](docs/reference/backends.md) and
  [quiet-present policy](docs/reference/vulkan-quiet-present.md) — coverage,
  limitations, and evidence.

## Support scope

The supported deployment is a source build on a systemd-based Linux desktop
with cgroup v2, Steam, and a Vulkan loader. x86_64 and i386 artifacts are
built together. Flatpak/Flathub packaging and non-Steam launchers are not
currently supported.
