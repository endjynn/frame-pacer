# Development

## Standard checks

Run the complete deterministic acceptance suite before submitting changes:

```sh
make check
```

This covers x86_64 and i386 Vulkan and GL builds, unit tests, shader assets,
manifests, installation, ABI, workflows, shell scripts, documentation, and the
generated HUD image. It does not start a game or intentionally modify cgroups.

Additional checks are available when relevant:

| Target | Purpose |
| --- | --- |
| `make check-unit-i386` | Run the isolated unit suite as i386. |
| `make check-analyzer` | Build the acceptance suite with GCC's static analyzer. |
| `make check-sanitize` | Run AddressSanitizer and UndefinedBehaviorSanitizer. |
| `make check-tsan` | Run ThreadSanitizer. |
| `make check-coverage` | Generate normalized GCC coverage and exercise available live integrations. |
| `make check-release-package` | Build and fully validate the reproducible release archive. |
| `make run-vulkan-present-probe` | Exercise real Vulkan presentation on x86_64 and i386. |
| `make run-glx-present-probe` | Exercise real GLX presentation on x86_64 and i386. |
| `make run-egl-present-probe` | Exercise real EGL presentation on x86_64. |
| `make run-nvml-helper-probe` | Exercise i386 NVIDIA telemetry against the host driver and Steam Runtime. |
| `make run-thread-cpu-quota-controller-integration` | Exercise live CPU-limit changes and cleanup. |
| `make run-thread-cpu-quota-controller-integration-i386` | Exercise the same lifecycle from an i386 client. |
| `make docs-hud-image` | Regenerate the documented HUD image. |

Analyzer, sanitizer, TSan, and coverage targets replace `build/` to avoid
mixing incompatible instrumentation. Live presentation and controller probes
need a suitable graphical or systemd user session and are not hosted-CI gates.

## Continuous integration

Pull requests and main-branch pushes run independent acceptance, i386,
analyzer, sanitizer, ThreadSanitizer, and release-package jobs on the fixed
Ubuntu runner. The release-package job uses the same digest-pinned Debian 12
environment as published binaries.

Pull-request jobs have read-only repository permissions, receive no repository
secrets, persist no checkout credential, and publish no routine artifact.
Every job has a timeout, and newer work cancels a superseded run.

## Release packages

`make release-package` runs the acceptance suite and writes a stripped,
reproducible archive and checksum below `build/dist`. The package includes only
the runtime payload, installer, uninstaller, version, license, changelog, and
README. `make check-release-package` additionally verifies its complete
inventory, privacy, ownership, architecture, dependencies, reproducibility,
installation, upgrade, and removal behavior.

The release toolchain is a compatibility boundary. Review any change to the
pinned container digest, compiler, or glibc baseline as a compatibility change.

## Publishing a version

1. Choose a [Semantic Version 2.0.0](https://semver.org/) value and write only
   that value to `VERSION`.
2. Update `CHANGELOG.md` and complete the normal pull-request checks.
3. After the change reaches `main`, deliberately push the matching `vVERSION`
   tag using the pseudonymous maintainer identity.

Only a matching upstream tag starts the release workflow. The build job has
read-only permissions and transfers exactly the tested archive and checksum.
A separate write-limited job revalidates those two files and publishes them
without checking out or executing repository code. Prerelease versions are
marked as prereleases automatically.

If a build or validation step fails, nothing is published. Correct the defect
through a normal reviewed change and use a new version; do not replace a
published tag or asset.

## Change requirements

- Keep one final presentation limiter per graphics API path.
- Preserve fail-open presentation behavior and automatic cleanup.
- Keep GLX/EGL preloading per-game; never make it global.
- Keep CPU control opt-in and isolated from Steam and unrelated processes.
- Add automated tests for supported behavior, failure paths, and cleanup.
- Cover both x86_64 and i386 when a change affects both architectures.
- Update user documentation when configuration or setup changes.

See [Technical details](technical-details.md) for the protected runtime
boundaries.
