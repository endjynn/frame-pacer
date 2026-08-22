# Repository-wide development instructions

These instructions apply to every future code modification throughout this
repository.

## Autonomy

- Complete all implementation and validation fully autonomously.
- Do not require manual or human testing for completion. Build an automated
  equivalent for every required completion check.
- Do not stop at a manual verification gate when further safe, automated work
  can complete the task.

## Refactoring authority

- Full and total destructive refactoring is authorized within this Git
  repository.
- Files, directories, modules, APIs, tests, documentation, build rules, and
  generated artifacts may be deleted, removed, renamed, replaced, reorganized,
  or otherwise modified when doing so produces the cleanest implementation.
- This authority is limited to the Git repository. It does not authorize
  package installation or modification of Steam, drivers, user configuration,
  system configuration, or other resources outside the repository.

## Clean implementation

- Leave zero legacy code after a change. Prefer a full, clean implementation
  over compatibility layering or parallel old and new paths.
- Remove every superseded implementation, dead branch, obsolete wrapper,
  transitional shim, unused symbol, stale test, outdated build rule, and
  inaccurate documentation affected by the change.
- Preserve required current functionality through the clean implementation
  and automated tests, not by retaining obsolete code.
