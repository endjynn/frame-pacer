# Installation and updates

Frame-pacer is installed from source into your home directory. It does not
need `sudo`, install a system service, or modify game files.

## Before you install

You need:

- An x86_64 Linux desktop using systemd.
- The standard Steam client supplied by your distribution. Flatpak Steam is
  not currently supported.
- A working Vulkan driver.
- A C compiler, 32-bit compiler support, Vulkan and OpenGL development files,
  and the small build tools listed in [Build requirements](reference/environment.md).

## 1. Download and install

```sh
git clone https://github.com/endjynn/frame-pacer.git
cd frame-pacer
make check
make install
```

`make check` builds and tests both 64-bit and 32-bit support. `make install`
copies frame-pacer into `~/.local` for your user account.

## 2. Create the configuration

```sh
mkdir -p ~/.config/frame-pacer
chmod 700 ~/.config/frame-pacer
nano ~/.config/frame-pacer/frame-pacer.conf
```

Paste this starting configuration:

```ini
global_fps_limit = 60
hud = on
```

Save the file and run:

```sh
chmod 600 ~/.config/frame-pacer/frame-pacer.conf
```

Frame-pacer deliberately ignores configuration files that other users can
modify. See [Configuration](configuration.md) when you are ready to add
per-game limits.

## 3. Enable it in Steam

For a first test, fully close Steam and start it from a terminal:

```sh
ENABLE_FRAME_PACER=1 steam
```

Start a Vulkan or Proton game. The HUD should appear in the top-left corner
and show `60` as the FPS limit. See [Steam integration](steam-integration.md)
for a permanent launcher setup and for native OpenGL games.

## Updating

Fully close Steam and any running games before replacing frame-pacer. From the
repository:

```sh
git pull --ff-only
make check
make install
```

Your configuration is stored separately and is not replaced.

## Uninstalling

1. Remove `ENABLE_FRAME_PACER=1` from your Steam launcher.
2. Remove any frame-pacer `LD_PRELOAD` options from individual games.
3. Fully restart Steam.
4. From the repository, run:

   ```sh
   make uninstall
   ```

You may then delete the repository and
`~/.config/frame-pacer/frame-pacer.conf`. No permanent service or game-file
change is left behind.
