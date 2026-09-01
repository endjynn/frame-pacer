# frame-pacer

A lightweight FPS limiter and performance HUD for Steam games on Linux.
Works with Vulkan, Proton, GLX, and EGL games.

> **AI-assisted development:** Frame-pacer is created and maintained by a
> professional software developer who uses AI tools throughout development.
> The maintainer reviews the work and remains responsible for every technical
> decision and release.

![frame-pacer HUD](docs/images/frame-pacer-hud.png)

## Quick start

Frame-pacer supports the standard, non-Flatpak Steam client on x86_64,
systemd-based Linux systems. It does not require root access.

### Install

Install the required [build packages](docs/reference/environment.md), then run:

```sh
git clone https://github.com/endjynn/frame-pacer.git
cd frame-pacer
make check
make install
```

### Configure

```sh
mkdir -p ~/.config/frame-pacer
chmod 700 ~/.config/frame-pacer
nano ~/.config/frame-pacer/frame-pacer.conf
```

Add:

```ini
global_fps_limit = 60
hud = on
```

Save the file, then run:

```sh
chmod 600 ~/.config/frame-pacer/frame-pacer.conf
```

Both settings are optional:

- Omit `global_fps_limit`, or set it to `off`, for no default FPS limit.
- Omit `hud` to show it, or set it to `off` to hide it.

### Launch

Fully close Steam, then run:

```sh
ENABLE_FRAME_PACER=1 steam
```

For permanent setup, see [Steam integration](docs/steam-integration.md).
Start a game; the HUD should appear in the top-left corner. The `FPS` row shows
the current frame rate followed by the limit.

## Per-game settings

Add a section to give a game its own limit:

```ini
[Example game]
executable = "ExampleGame.exe"
fps_limit = 45
```

`fps_limit` accepts `1` through `999`, or `off`. A matching per-game setting
always overrides `global_fps_limit`; use `fps_limit = off` to leave one game
uncapped.

Changes apply while the game is running. See [Configuration](docs/configuration.md)
for executable matching and the optional CPU thread limit.

## Native OpenGL games

Native GLX/EGL games need this Steam launch option:

```text
LD_PRELOAD=/absolute/path/to/frame-pacer/build/${LIB}/libframe_pacer_gl_shim.so %command%
```

Replace `/absolute/path/to/frame-pacer` with this repository's location. Use
this only for the individual OpenGL game, never for Steam globally.

## More help

- [Installation and updates](docs/installation.md)
- [Steam integration](docs/steam-integration.md)
- [Configuration](docs/configuration.md)
- [Troubleshooting](docs/troubleshooting.md)
- [Technical details](docs/technical-details.md)

See [Contributing](CONTRIBUTING.md) if you want to help develop frame-pacer.

To disable frame-pacer, remove its Steam environment or per-game launch option
and fully restart Steam. To uninstall it, run `make uninstall` from the
repository.

## Acknowledgements

Frame-pacer is inspired by [MangoHud](https://github.com/flightlessmango/MangoHud)
and the work of its maintainers and contributors.
