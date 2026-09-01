# HUD guide

The frame-pacer HUD appears in the top-left corner and scales automatically
with the game's rendering resolution.

![frame-pacer HUD](images/frame-pacer-hud.png)

## Reading the HUD

| Row | First value | Second value |
| --- | --- | --- |
| `GPU` | GPU use associated with the game | GPU temperature |
| `CPU` | Total system CPU use | CPU package temperature |
| `THR` | Busiest game thread | Configured per-thread CPU limit |
| `FPS` | Current frame rate | Configured FPS limit, or `OFF` |

The `THR` row appears only when `thread_cpu_limit` is enabled for the game.
Its percentages are measured against one logical CPU core.

`N/A` means that the value is unavailable or has not yet been confirmed. This
does not stop frame pacing. GPU and temperature availability depends on the
driver and hardware.

## Hide the HUD

Set this at the top of `frame-pacer.conf`:

```ini
hud = off
```

The HUD disappears within about one second while the FPS limit remains active.
Set it back to `on` to show the HUD again.

For telemetry sources and CPU-limit verification details, see
[Technical details](technical-details.md).
