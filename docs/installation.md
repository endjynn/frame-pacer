# Installation

Frame-pacer is currently distributed as source. It does not install a system
service, alter a game directory, or require root access.

## Requirements

- A systemd-based Linux desktop using cgroup v2.
- Steam and a working Vulkan loader.
- Build tools listed in [Build requirements](reference/environment.md).
- Enough access to create a transient user scope when `thread_cpu_limit` is
  enabled.

## Build

Clone the repository and run:

```sh
make check
make install
```

`make install` installs the x86_64 and i386 Vulkan layer libraries under
`~/.local/lib/frame-pacer` and their manifests under
`~/.local/share/vulkan/implicit_layer.d`. No root access is required. The
Vulkan loader discovers those manifests automatically, so Steam only needs
`ENABLE_FRAME_PACER=1` to activate all frame-pacer Vulkan functionality.

The same private library directory contains
`frame-pacer-thread-cpu-controller`. It is an executable helper used only when
an enabled `thread_cpu_limit` must cross a Steam Runtime read-only cgroup mount;
installation does not start it or create a permanent service.

No NVIDIA telemetry executable is installed. The i386 HUD-capable libraries
contain a small x86-64 image used only when a 32-bit NVIDIA process cannot load
NVML directly. It executes anonymously from sealed memory, uses the existing
host driver library, and disappears with the game. Installing a separate
32-bit NVIDIA compute package is not required.

Set `PREFIX`, `INSTALL_LIBDIR`, or `INSTALL_LAYERDIR` when using a non-default
installation. `DESTDIR` is supported for staged package builds. The GLX/EGL
preload shim is not installed: it remains an explicit per-game launch option.

## Configure a game

Create `${XDG_CONFIG_HOME}/frame-pacer/frame-pacer.conf` when
`XDG_CONFIG_HOME` is set, or `~/.config/frame-pacer/frame-pacer.conf`
otherwise. The file must exclude group and other users (for example,
`chmod 600`):

```ini
global_fps_limit = 70

[Example game]
executable = "ExampleGame.exe"
fps_limit = 60
thread_cpu_limit = off
```

Set `thread_cpu_limit` to `1%` through `100%` only after confirming the game
works normally. It is a per-thread ceiling, not a total-process CPU cap.

Follow [Steam integration](steam-integration.md) to activate the relevant
backend. Start with one game, confirm the HUD and pacing, then expand slowly.

## Uninstall and recovery

Remove frame-pacer's Steam launcher environment and any per-game GL preload
option, then fully restart Steam. Run `make uninstall` from the same checkout
to remove the installed Vulkan manifests and libraries. You may then remove
the configuration file and checkout. The CPU controller and anonymous NVIDIA
telemetry helper clean up when the game, frame-pacer, or their owning runtime
connections exit.
