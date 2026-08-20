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
```

This creates `build/` inside the checkout. Keep that checkout and its build
directory in place while Steam is configured to reference it; moving or
deleting it disables the integration.

## Configure a game

Create `~/.config/frame-pacer/frame-pacer.conf` with permissions that exclude
group and other users (for example, `chmod 600`):

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
option, then fully restart Steam. You may remove the configuration file and
the checkout after no Steam setting references its build directory. The CPU
controller cleans up when the game, frame-pacer, or its transient scope exits.
