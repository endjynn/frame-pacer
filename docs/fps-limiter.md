# FPS limiter

Frame-pacer caps the final presentation stream: Vulkan at `vkQueuePresentKHR`
and GL at GLX/EGL swap calls. It does not select a backend from a game name,
Steam AppID, install path, or window title.

## Configuration

`~/.config/frame-pacer/frame-pacer.conf` has one global fallback and optional
per-executable rules:

```ini
global_fps_limit = 70
hud = on

[Neverwinter Nights 2]
executable = "nwn2.exe"
fps_limit = 60
```

`global_fps_limit` is required and must be from 1 through 1000. Each rule
requires exactly one quoted executable basename and one `fps_limit` in the
same range. Section names are descriptive only.

Native basenames match exactly. Windows `.exe` basenames match
case-insensitively. The directly rendering executable wins; for a
helper-rendered Proton launcher, frame-pacer may then examine a bounded chain
of same-user ancestors for an exact configured `.exe` target.

The legacy top-level `fps_limit` key is not accepted. Use
`global_fps_limit`; `fps_limit` belongs inside a rule.

`hud` is an optional top-level switch with values `on` (the default) and
`off`. It controls only HUD drawing; pacing remains active.

## Safe reloads

The process checks configuration at most once per second. A valid change starts
a fresh pacing interval and updates the HUD without restarting Steam or the
game. Write updates atomically so a reader never sees a partial file.

For safety the file must be a current-user regular file, have no group or
other permissions, be no larger than 4 KiB, and contain only complete valid
configuration. An absent or invalid file falls back to 70 FPS and disables any
thread CPU limit for the process.
