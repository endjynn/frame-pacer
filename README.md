# frame-pacer

A lightweight FPS limiter and performance HUD for Steam games on Linux.
Works with Vulkan, Proton, GLX, and EGL games.

[![CI](https://github.com/endjynn/frame-pacer/actions/workflows/ci.yml/badge.svg)](https://github.com/endjynn/frame-pacer/actions/workflows/ci.yml)

> **AI-assisted development:** Frame-pacer is created and maintained by a
> professional software developer who uses AI tools throughout development.
> The maintainer reviews the work and remains responsible for every technical
> decision and release.

![frame-pacer HUD](https://raw.githubusercontent.com/endjynn/frame-pacer/main/docs/images/frame-pacer-hud.png)

## Install

Frame-pacer supports the standard, non-Flatpak Steam client on x86_64,
systemd-based Linux systems. Installation is per-user and needs no `sudo`.

1. Download the `.tar.xz` archive and matching `.sha256` file from
   [GitHub Releases](https://github.com/endjynn/frame-pacer/releases).
2. Open a terminal in the download directory and run:

   ```sh
   sha256sum -c frame-pacer-*.tar.xz.sha256
   tar -xf frame-pacer-*.tar.xz
   cd frame-pacer-*-linux-x86_64-multilib
   ./install.sh
   ```

See [Installation and updates](https://github.com/endjynn/frame-pacer/blob/main/docs/installation.md)
for source builds, custom locations, updates, and removal.

## Configure

Create `~/.config/frame-pacer/frame-pacer.conf`:

```sh
mkdir -p ~/.config/frame-pacer
chmod 700 ~/.config/frame-pacer
nano ~/.config/frame-pacer/frame-pacer.conf
```

Add any settings you want:

```ini
global_fps_limit = 60
hud = on
```

Then protect the file:

```sh
chmod 600 ~/.config/frame-pacer/frame-pacer.conf
```

Both settings are optional. Use `global_fps_limit = off` for no default limit
and `hud = off` to hide the HUD.

## Start Steam

Fully close Steam, then run:

```sh
ENABLE_FRAME_PACER=1 steam
```

Start a game. The HUD should appear in the top-left corner. See
[Steam integration](https://github.com/endjynn/frame-pacer/blob/main/docs/steam-integration.md)
to make this permanent.

## Per-game limits

Add a section to the configuration:

```ini
[Example game]
executable = "ExampleGame.exe"
fps_limit = 45
```

`fps_limit` accepts `1` through `999`, or `off`. A matching game setting always
overrides `global_fps_limit`. Changes apply while the game is running. See
[Configuration](https://github.com/endjynn/frame-pacer/blob/main/docs/configuration.md)
for all options.

## Native OpenGL games

Add this only to the game's Steam **Launch Options**:

```text
LD_PRELOAD="$HOME/.local/lib/frame-pacer/${LIB}/libframe_pacer_gl_shim.so" %command%
```

Never add this option globally to Steam. Vulkan and Proton games do not need
it.

## Help and development

- [Installation and updates](https://github.com/endjynn/frame-pacer/blob/main/docs/installation.md)
- [Steam integration](https://github.com/endjynn/frame-pacer/blob/main/docs/steam-integration.md)
- [Configuration](https://github.com/endjynn/frame-pacer/blob/main/docs/configuration.md)
- [Troubleshooting](https://github.com/endjynn/frame-pacer/blob/main/docs/troubleshooting.md)
- [Technical details](https://github.com/endjynn/frame-pacer/blob/main/docs/technical-details.md)
- [Contributing](https://github.com/endjynn/frame-pacer/blob/main/CONTRIBUTING.md)

## Acknowledgements

Frame-pacer is inspired by [MangoHud](https://github.com/flightlessmango/MangoHud)
and the work of its maintainers and contributors.
