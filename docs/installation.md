# Installation and updates

The ready-to-install release is the easiest option. It includes x86_64 and
i386 support and does not need a compiler or `sudo`.

Frame-pacer currently supports the standard, non-Flatpak Steam client on an
x86_64, systemd-based Linux system.

## Install a release

Download the `.tar.xz` archive and matching `.sha256` file from
[GitHub Releases](https://github.com/endjynn/frame-pacer/releases). In the
download directory, run:

```sh
sha256sum -c frame-pacer-*.tar.xz.sha256
tar -xf frame-pacer-*.tar.xz
cd frame-pacer-*-linux-x86_64-multilib
./install.sh
```

The installer copies frame-pacer into `~/.local`. It does not modify Steam,
game files, system configuration, or your frame-pacer configuration.

Continue with [Steam integration](steam-integration.md).

## Update

Fully close Steam and all games. Download and verify the new release, extract
it, and run its `./install.sh`. It safely replaces frame-pacer-owned runtime
files and keeps your configuration.

## Uninstall

1. Remove `ENABLE_FRAME_PACER=1` from the Steam launcher.
2. Remove frame-pacer `LD_PRELOAD` options from individual games.
3. Fully restart Steam.
4. Run `./uninstall.sh` from an extracted release directory.

The uninstaller removes only frame-pacer runtime files. It leaves
`~/.config/frame-pacer/frame-pacer.conf` and unrelated files untouched.

## Custom installation prefix

The default prefix is `~/.local`. To choose another absolute path, use the
same `PREFIX` for installation and removal:

```sh
PREFIX=/absolute/path ./install.sh
PREFIX=/absolute/path ./uninstall.sh
```

For a custom prefix, replace `$HOME/.local` in the native OpenGL Steam launch
option with that absolute path.

Package maintainers can combine an absolute `PREFIX` with `DESTDIR` to stage
an installation without writing to the final filesystem.

## Build from source

Developers and distribution packagers can install from a checkout:

```sh
git clone https://github.com/endjynn/frame-pacer.git
cd frame-pacer
make check
make install
```

Install the [source-build requirements](reference/environment.md) first.
`make install` uses the same file layout and installer as the prebuilt release.
Use `make uninstall` to remove it.
