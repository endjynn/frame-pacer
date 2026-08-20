# Development

## Workflow

Inspect the working tree, then run the normal acceptance suite:

```sh
git status --short
make check
```

`make check` builds the x86_64 and i386 Vulkan and GL artifacts, runs unit and
GL integration tests, validates shaders and manifests, and checks the expected
ELF architectures.

Close any game using frame-pacer before replacing its runtime libraries. Do
not change Steam launcher settings while Steam is running.

## Design constraints

Each API family owns at most one final presentation boundary. Do not add a
second limiter, a game-name backend selector, or a heuristic that races
backends for ownership.

For GLX/EGL interception, dynamic loading, resolver handling, or Steam Runtime
behavior, compare the design with current upstream projects such as MangoHud
and document any deliberate divergence. Do not copy code without preserving
its license and attribution.

The CPU controller must remain strictly opt-in, private to its transient user
scope, and fail closed. It must never modify a parent cgroup, Steam, or an
unrelated process.

## Validation

Validate FPS and `thread_cpu_limit` changes live. Test `off` as well as an
enabled value. For Vulkan quiet-present changes, test focused, unfocused, and
refocused behavior using physical GPU measurements; HUD values and logs alone
do not prove hidden GPU work is capped.

## Logs

`FRAME_PACER_LOG=1` enables diagnostic logs only for the target process. Logs
are stored under `${XDG_STATE_HOME}/frame-pacer`, or
`~/.local/state/frame-pacer` when `XDG_STATE_HOME` is unset. Files are
PID-qualified; each backend keeps its 10 newest logs and stops writing at
64 MiB per file without affecting pacing.
