# CPU thread limiter

`thread_cpu_limit` is an opt-in, per-thread cgroup-v2 CPU ceiling. It replaces
the abandoned aggregate CPU-quota idea: it never applies one shared cap to the
whole game process.

## Configuration and live changes

Set it only in a selected executable rule:

```ini
[Example game]
executable = "ExampleGame.exe"
fps_limit = 60
thread_cpu_limit = 50%
```

Accepted values are `1%` through `100%` and `off`. The setting is never global.
`thread_cpu_quota` is obsolete and rejected. Configuration is polled at most
once per second, so changing `50%` to `75%`, or to `off`, takes effect live.
An absent, replaced, or invalid configuration disables the active ceiling.

## Safety model

The controller creates a uniquely named, delegated transient user scope for
the target process. Below that exact scope it creates a private threaded
subtree, with one owned child per discovered thread. Each child receives its
own `cpu.max` value, for example `50000 100000` for 50%.

Before changing the hierarchy, it verifies the process cgroup path and scope
ownership. It moves only the target's verified threads through `cgroup.threads`
and never changes a parent cgroup, `app.slice`, Steam, or an unrelated process.
New threads are reconciled in bounded background work, outside Vulkan present
and GLX/EGL swap paths. A second frame-pacer backend for the same process
adopts the same controller identity rather than creating a competing subtree.

Some Steam Runtime containers expose a read-only cgroup view to the game. In
that case frame-pacer uses a short-lived, user-owned external controller tied
to the game process. It normally runs as a transient systemd user service. If
the client architecture cannot load libsystemd—for example an i386 game on a
host with only the native library—the native helper bootstraps the same
transient scope directly. Neither path installs a permanent service or changes
system configuration. `libsystemd.so.0` is loaded at runtime; frame-pacer has
no build-time systemd library dependency. Source-checkout backends use
`build/frame-pacer-thread-cpu-controller`; `make install` places the same
helper beside the architecture library directories so installed Vulkan layers
retain this fallback.

The controller exchanges private command/status files below
`${XDG_STATE_HOME}/frame-pacer`, or `~/.local/state/frame-pacer` when
`XDG_STATE_HOME` is unset. Frame-pacer creates the fallback state hierarchy on
first use and requires the protocol directory to be owned by the current user
with no group or other access.

On `off`, configuration failure, controller failure, or backend unload, the
controller immediately withdraws confirmation, returns only verified surviving
owned threads to its threaded domain, and removes only its empty children and
private root. The helper and user manager collect the transient scope after
game exit; reboot also clears it. No root access is required.

## HUD confirmation

The purple `THR` HUD row is shown only while a valid, active
`thread_cpu_limit` is configured. Its quota field is numeric only after the
controller has verified every currently discovered thread in its own owned
child with the exact requested `cpu.max`; otherwise it reads `N/A`. See
[HUD](hud.md) for the user-facing metric semantics.

## Verification

Normal builds do not create or modify cgroups:

```sh
make check
```

The two dedicated cgroup checks are deliberately opt-in and never part of
`make check` or installation:

```sh
make run-thread-cpu-quota-probe
make run-thread-cpu-quota-controller-integration
```

They exercise a delegated user-scope topology and controller cleanup. Run them
only on a system where creating a transient user scope is appropriate.
`make check-coverage` also runs the controller integration to collect its
instrumented lifecycle data and therefore has the same requirement.
