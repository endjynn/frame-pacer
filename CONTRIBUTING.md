# Contributing

Thanks for helping improve frame-pacer. Bug reports, compatibility reports,
documentation improvements, and focused code changes are welcome.

Open an issue before starting a large architectural change so its scope can be
discussed first.

## Pull requests

Before submitting a pull request:

1. Keep the change focused and explain the user-visible result.
2. Add or update automated tests.
3. Run `make check`; the same checks will run on GitHub.
4. Run any additional checks relevant to the change, including
   `make check-release-package` for installation, packaging, or release work; see
   [Development](docs/development.md).
5. Update documentation when setup, configuration, or behavior changes.
6. Describe known limitations and any optional live-game observations.

Do not copy code or assets from another project unless their license permits
it and all required attribution and notices are included.

## Bug reports

Include:

- Linux distribution and desktop session.
- GPU and driver version.
- Steam Runtime and Proton version, when applicable.
- Game executable and graphics backend.
- Exact frame-pacer configuration.
- Reproduction steps and the expected result.

Diagnostic logs can contain private paths or account information. Review them
before attaching them to a public issue. See
[Troubleshooting](docs/troubleshooting.md) for logging instructions.
