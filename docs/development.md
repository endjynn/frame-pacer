# Development

## Workflow

Inspect the working tree, then run the normal acceptance suite:

```sh
git status --short
make check
```

`make check` builds the x86_64 and i386 Vulkan and GL artifacts, runs unit, GL,
Vulkan loader, controller CLI, shell-syntax, install, and ABI checks, validates
shaders, manifests, and the reproducibly generated HUD image, and checks the
expected ELF architectures. It does not modify cgroups or require a graphical
session.

Additional gates are intentionally separate:

| Target | Purpose and side effects |
| --- | --- |
| `make check-unit-i386` | Cleanly rebuild and execute every isolated unit fixture as i386, then remove its temporary artifacts. |
| `make check-analyzer` | Clean GCC `-fanalyzer` build of the acceptance graph and presentation probes. |
| `make check-sanitize` | Clean ASan+UBSan run of isolated unit tests. |
| `make check-tsan` | Clean ThreadSanitizer run of isolated unit tests. |
| `make check-coverage` | Clean normalized GCC coverage run; exercises available Vulkan, GLX, and EGL presentation paths and delegated cgroup controller integration. |
| `make run-vulkan-present-probe` | Real x86_64/i386 X11 swapchain, two-frame HUD presentation, and teardown; requires a graphical Vulkan device. |
| `make run-glx-present-probe` | Real x86_64/i386 GLX contexts, two HUD swaps, and GL-state restoration; requires a graphical GLX device. |
| `make run-egl-present-probe` | Real x86_64 X11/EGL context, two HUD swaps, and GL-state restoration; requires a graphical EGL device. |
| `make run-thread-cpu-quota-probe` | Opt-in delegated cgroup topology probe. |
| `make run-thread-cpu-quota-controller-integration` | Opt-in live 50%→75%→off→60%→off quota reuse and cleanup test. |
| `make run-thread-cpu-quota-controller-integration-i386` | The same live reuse/handoff driven by the i386 client, plus test-only active cgroup-write failure, recovery, and controller interruption. |
| `make docs-hud-image` | Regenerate the README's 352x196 maximum-state HUD image from the production text, font, and vertex code. |

The coverage and cgroup targets can change the calling process's transient
user-scope topology. Run them only in a suitable delegated systemd user
session. The analyzer, sanitizer, coverage, and TSan targets start and finish
by removing the ignored `build/` directory so instrumented objects cannot
contaminate a later normal incremental build.

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
