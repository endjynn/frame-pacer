# Steam integration

Use the Vulkan setup for most games. Use the GLX/EGL setup only for a native
OpenGL game that does not use Vulkan.

## Vulkan and Proton games

Frame-pacer's Vulkan support covers native Vulkan games, DXVK, and
vkd3d-proton.

### Test it once

Fully close Steam, then run:

```sh
ENABLE_FRAME_PACER=1 steam
```

Start a game. If the frame-pacer HUD appears, the integration is working.

### Make it permanent

Add `ENABLE_FRAME_PACER=1` to the environment of the Steam desktop launcher.
The exact editor depends on your desktop, but the change is always the same:
prefix Steam's existing command with `env ENABLE_FRAME_PACER=1`.

For example, change:

```text
Exec=/usr/games/steam %U
```

to:

```text
Exec=env ENABLE_FRAME_PACER=1 /usr/games/steam %U
```

Keep the Steam path and arguments already used by your distribution. If you
edit desktop files manually:

1. Fully close Steam.
2. If `~/.local/share/applications/steam.desktop` already exists, edit that
   file. Otherwise copy your distribution's `steam.desktop` from
   `/usr/share/applications` into `~/.local/share/applications` and edit the
   copy. Do not edit the system file directly.
3. Add `env ENABLE_FRAME_PACER=1` to every Steam `Exec=` entry you use,
   including menu shortcuts.
4. If Steam starts automatically, make the same change in
   `~/.config/autostart/steam.desktop`.
5. Log out and back in if your desktop continues to use the old command.

Use `hud = off` in the configuration when you only want to hide the overlay.

## Native OpenGL games

For a GLX or EGL game, open the game's Steam properties and put this in
**Launch Options**:

```text
LD_PRELOAD=/absolute/path/to/frame-pacer/build/${LIB}/libframe_pacer_gl_shim.so %command%
```

Replace `/absolute/path/to/frame-pacer` with the full path to the repository.
Steam expands `${LIB}` for 64-bit and 32-bit games.

This option belongs on one game, not on Steam itself. A Vulkan game does not
need it.

## Disable or recover

If a game does not start:

1. Remove its frame-pacer `LD_PRELOAD` launch option, if present.
2. Remove `ENABLE_FRAME_PACER=1` from the Steam launcher.
3. Fully restart Steam.

This stops frame-pacer from loading. See [Troubleshooting](troubleshooting.md)
before enabling it again.
