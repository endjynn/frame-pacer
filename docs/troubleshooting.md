# Troubleshooting

## The HUD does not appear

1. Confirm that `make check` and `make install` completed successfully.
2. Fully close Steam. Check that no Steam icon remains in the notification
   area, then start it with `ENABLE_FRAME_PACER=1 steam`.
3. Confirm that you are using the standard Steam client, not Flatpak Steam.
4. For a native OpenGL game, use the per-game GLX/EGL launch option from
   [Steam integration](steam-integration.md).
5. Do not add the GLX/EGL `LD_PRELOAD` option to a Vulkan or Proton game unless
   you know that it renders through OpenGL.

## The wrong FPS limit is shown

The `FPS` row shows the measured frame rate first and the configured limit
second.

Check that:

- `global_fps_limit`, when present, is `off` or a number from `1` to `999`.
- The configuration file is named exactly `frame-pacer.conf`.
- The file has mode `600`.
- A per-game `executable` matches the rendering process filename.
- Every per-game section contains an `fps_limit` set to `off` or a number from
  `1` to `999`.

Run this to check the normal configuration location:

```sh
ls -l ~/.config/frame-pacer/frame-pacer.conf
```

If the file is invalid or insecure, frame-pacer applies no FPS limit.

## A game does not start

Remove frame-pacer from the launch path before investigating further:

1. Remove the game's frame-pacer `LD_PRELOAD` launch option.
2. Remove `ENABLE_FRAME_PACER=1` from the Steam launcher.
3. Fully restart Steam and try the game again.

If the game works again, re-enable only the integration required by that game.

## Collect a diagnostic log

Enable logging only while reproducing a problem.

For Vulkan and Proton games, fully close Steam and run:

```sh
FRAME_PACER_LOG=1 ENABLE_FRAME_PACER=1 steam
```

For a GLX/EGL game, add `FRAME_PACER_LOG=1` before its existing per-game launch
option:

```text
FRAME_PACER_LOG=1 LD_PRELOAD=/absolute/path/to/frame-pacer/build/${LIB}/libframe_pacer_gl_shim.so %command%
```

Logs are written to `~/.local/state/frame-pacer`, or below
`$XDG_STATE_HOME/frame-pacer` when `XDG_STATE_HOME` is set. Remove
`FRAME_PACER_LOG=1` after collecting the log.

Before sharing a log, review it for private paths or account information.
Include your distribution, desktop session, GPU and driver, Steam
Runtime/Proton version, game executable, configuration, and reproduction steps
in the issue report.
