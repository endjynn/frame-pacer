# Troubleshooting

## The HUD does not appear

1. Confirm that the release installer completed successfully.
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

When logging is disabled, frame-pacer bypasses effective-report collection and
diagnostic formatting. The report machinery does not take locks or format
messages on the presentation path.

For Vulkan and Proton games, fully close Steam and run:

```sh
FRAME_PACER_LOG=1 ENABLE_FRAME_PACER=1 steam
```

For a GLX/EGL game, add `FRAME_PACER_LOG=1` before its existing per-game launch
option:

```text
FRAME_PACER_LOG=1 LD_PRELOAD="$HOME/.local/lib/frame-pacer/${LIB}/libframe_pacer_gl_shim.so" %command%
```

Logs are written to `~/.local/state/frame-pacer`, or below
`$XDG_STATE_HOME/frame-pacer` when `XDG_STATE_HOME` is set. Remove
`FRAME_PACER_LOG=1` after collecting the log.

A PID log is created only when that process creates Vulkan presentation
resources, attempts frame presentation or pacing, or encounters an actionable
frame-pacer initialization failure. Wine/Proton helpers that merely load the
Vulkan layer do not create routine logs. A launcher and its child game can
still have separate logs when both genuinely render frames.

The first `startup` line identifies the loaded frame-pacer version and graphics
backend. An `effective-config` line then shows the effective FPS, HUD, and
thread CPU settings. Its `*_source` fields say whether each value came from a
global setting, a per-game rule, or the safe default. `rule` and `match` show
which per-game rule was selected.

The `config` and `reason` fields explain rejected or deliberately disabled
settings. For example, `config=insecure reason=insecure-permissions` means the
configuration is not mode `600`, while `config=malformed`,
`reason=invalid-value`, and `line=17` identify the exact bad line.
`trigger=reload` means a live edit
changed the effective configuration.

Logs record important state changes and failures, not every successful frame.
Vulkan submit fallback is reported when it starts and ends, while repeated
identical presentation failures are reported once until the result changes.
The final shutdown line reports the number of presentations or swaps.

Effective-configuration reports deliberately omit configuration paths, home
directories, command lines, and process ancestry. Other diagnostic messages
may still contain environment-specific details. Before sharing a log, review
it for private paths or account information. Include your distribution,
desktop session, GPU and driver, Steam Runtime/Proton version, game executable,
configuration, and reproduction steps in the issue report.
