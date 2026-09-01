# Configuration

Frame-pacer reads:

```text
~/.config/frame-pacer/frame-pacer.conf
```

If `XDG_CONFIG_HOME` is set, it reads
`$XDG_CONFIG_HOME/frame-pacer/frame-pacer.conf` instead.

## Complete example

```ini
global_fps_limit = 60
hud = on

[Example game]
executable = "ExampleGame.exe"
fps_limit = 45
thread_cpu_limit = 50%

[Uncapped game]
executable = "AnotherGame.exe"
fps_limit = off
```

Games without a matching section use the global 60 FPS limit. `ExampleGame.exe`
uses 45 FPS and the optional CPU thread limit. `AnotherGame.exe` remains
uncapped.

## Options at a glance

| Option | Where | Accepted values | If omitted |
| --- | --- | --- | --- |
| `global_fps_limit` | Before any game section | `1`–`999`, or `off` | No global FPS limit |
| `hud` | Before any game section | `on` or `off` | HUD on |
| `executable` | Inside a game section | Quoted executable filename, 1–511 bytes | Required |
| `fps_limit` | Inside a game section | `1`–`999`, or `off` | Required |
| `thread_cpu_limit` | Inside a game section | `1%`–`100%`, or `off` | Off |

Use lowercase `on` and `off`. An `executable` must be a filename such as
`"Game.exe"`, not a path. It cannot contain `/`, `\`, or another quote. If a
game section is missing either `executable` or `fps_limit`, the whole
configuration is invalid.

## Basic configuration

```ini
global_fps_limit = 60
hud = on
```

`global_fps_limit` applies to every game using frame-pacer unless a matching
per-game section overrides it. Use a value from `1` through `999`, or use
`off` to leave games uncapped by default:

```ini
global_fps_limit = off
hud = on
```

`global_fps_limit` is optional. Omitting it has the same uncapped behavior as
`off`. Per-game rules continue to work in either case.

`hud` is optional. Use `on` to show the overlay or `off` to hide it. Hiding the
HUD does not disable frame pacing.

## Per-game limits

```ini
global_fps_limit = 60
hud = on

[Tactical game]
executable = "TacticalGame.exe"
fps_limit = 45

[Native Linux game]
executable = "native-game"
fps_limit = 90

[Uncapped game]
executable = "uncapped-game"
fps_limit = off
```

Each section needs an `executable` and an `fps_limit`:

- The section name, such as `[Tactical game]`, is only a label.
- `executable` is the filename of the process that renders the game, without
  its directory.
- Native Linux executable names match exactly.
- Windows `.exe` names match without regard to letter case.
- `fps_limit` accepts a number from `1` through `999`, or `off`.
- A matching section always overrides `global_fps_limit`; `fps_limit = off`
  explicitly leaves that game uncapped even when the global limit is numeric.
- Games without a matching section use `global_fps_limit`, or remain uncapped
  when it is `off`.

For Proton launchers that hand rendering to another process, frame-pacer can
also match the configured Windows executable in the game's same-user parent
process chain.

## Optional CPU thread limit

Some games have one or more CPU-heavy threads that cause unnecessary boost,
power use, or heat. Add `thread_cpu_limit` to that game's section to limit each
game thread independently:

```ini
[CPU-heavy game]
executable = "CpuHeavyGame.exe"
fps_limit = 60
thread_cpu_limit = 50%
```

Accepted values are `1%` through `100%`, or `off`. Start with `off`; enable a
limit only after normal FPS pacing works.

The percentage is relative to one logical CPU core. A value of `50%` allows
each game thread to use roughly half of one core over time. It is not a limit
on the whole game, total CPU use, power, or temperature. Several threads can
each use their own allowance.

When the limit is active, the HUD adds a `THR` row. The second value shows the
confirmed configured limit. `N/A` means frame-pacer has not confirmed that the
limit is active.

## Live changes

Frame-pacer checks the file about once per second. Valid changes take effect
without restarting the game or Steam.

The file must:

- Be a regular file, not a symbolic link.
- Be owned by your user.
- Have no group or other permissions; `chmod 600` is recommended.
- Be no larger than 1 MiB.
- Contain only valid, complete settings.

If the file is missing, insecure, or invalid, frame-pacer applies no FPS limit
and disables the CPU thread limit. Both `global_fps_limit` and per-game
`fps_limit` accept `off`.

The 1 MiB limit supports thousands of per-game rules while keeping memory use
bounded if the file is accidentally replaced with something else.
