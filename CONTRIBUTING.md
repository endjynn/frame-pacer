# Contributing

Thanks for helping improve frame-pacer. Please open an issue before proposing
a large architectural change.

## Before a pull request

1. Keep changes narrowly scoped and explain the affected final presentation
   boundary.
2. Add or update focused tests.
3. Run `make check` on x86_64 and i386.
4. Document any live-game validation and any known limitation.
5. Do not add global Steam preloads, wrappers, permanent services, privileged
   helpers, or title-specific behavior without a reproducible justification.

Do not copy code from another project unless its license permits it and the
required attribution and notices are included.

## Reporting regressions

Include distribution, desktop session, GPU/driver, Steam Runtime/Proton
version, game executable basename, backend, exact configuration, and a short
reproduction. Attach a `FRAME_PACER_LOG=1` log only after reviewing it for
private paths or account information.
