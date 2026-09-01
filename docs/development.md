# Development

## Standard workflow

Before making changes, inspect the working tree. After making changes, run:

```sh
make check
```

This builds and tests the x86_64 and i386 Vulkan and GL backends, unit tests,
shader assets, manifests, installation behavior, ABI, shell scripts, and the
generated HUD image. It does not start a game or intentionally modify cgroups.

Close games that use frame-pacer before replacing runtime libraries. Fully
close Steam before changing its launcher environment.

## Additional checks

Use the checks relevant to the change:

| Target | Purpose |
| --- | --- |
| `make check-unit-i386` | Run the isolated unit suite as i386. |
| `make check-analyzer` | Build the acceptance suite with GCC's static analyzer. |
| `make check-sanitize` | Run unit tests with AddressSanitizer and UndefinedBehaviorSanitizer. |
| `make check-tsan` | Run unit tests with ThreadSanitizer. |
| `make check-coverage` | Generate normalized GCC coverage and exercise available live integrations. |
| `make run-vulkan-present-probe` | Exercise real Vulkan presentation on x86_64 and i386. |
| `make run-glx-present-probe` | Exercise real GLX presentation on x86_64 and i386. |
| `make run-egl-present-probe` | Exercise real EGL presentation on x86_64. |
| `make run-nvml-helper-probe` | Exercise i386 NVIDIA telemetry against the host driver and Steam Runtime. |
| `make run-thread-cpu-quota-controller-integration` | Exercise live CPU-limit changes and cleanup. |
| `make run-thread-cpu-quota-controller-integration-i386` | Exercise the same controller lifecycle from an i386 client. |
| `make docs-hud-image` | Regenerate the documented HUD image. |

`make check-coverage` and the CPU-controller integrations create a transient
user scope and modify only their delegated test cgroup. Run them only in a
suitable systemd user session. Analyzer, sanitizer, TSan, and coverage builds
clean the `build` directory to avoid mixing instrumented and normal objects.

## Change requirements

- Keep one final presentation limiter per graphics API path.
- Preserve fail-open presentation behavior and automatic cleanup.
- Keep GLX/EGL preloading per-game; never make it global.
- Keep CPU control opt-in and isolated from Steam and unrelated processes.
- Add automated tests for supported behavior, failure paths, and cleanup.
- Cover both x86_64 and i386 when a change affects both architectures.
- Update user documentation when configuration or setup changes.

See [Technical details](technical-details.md) for the runtime boundaries that
these requirements protect.

## Diagnostic logs

Set `FRAME_PACER_LOG=1` only for the process being investigated. Logs are
written below `$XDG_STATE_HOME/frame-pacer`, or `~/.local/state/frame-pacer`.
Review logs for private paths or account information before sharing them.
