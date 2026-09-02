#!/bin/sh
set -eu

temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
version="$temporary/VERSION"
changelog="$temporary/CHANGELOG.md"
notes="$temporary/release-notes.md"

printf '%s\n' '1.2.3-beta.4' > "$version"
cat > "$changelog" <<'EOF'
# Changelog

## 1.2.3-beta.4 — 2026-09-02

- First direct change.
- Second change over
  two lines.

## 1.2.3-beta.3 — 2026-09-01

- Previous change.
EOF
sh ./.github/scripts/generate-release-notes.sh \
    "$version" "$changelog" "$notes"
cat > "$temporary/expected.md" <<'EOF'
Compatibility: Debian 12 (glibc 2.36), GCC 12, x86_64 and i386.

## What's Changed

- First direct change.
- Second change over
  two lines.
EOF
cmp "$temporary/expected.md" "$notes"

sed 's/2026-09-02/unreleased/' "$changelog" > "$temporary/unreleased.md"
if sh ./.github/scripts/generate-release-notes.sh \
    "$version" "$temporary/unreleased.md" "$notes" 2>/dev/null; then
    printf '%s\n' 'unreleased changelog unexpectedly accepted' >&2
    exit 1
fi
printf '%s\n' '9.9.9' > "$version"
if sh ./.github/scripts/generate-release-notes.sh \
    "$version" "$changelog" "$notes" 2>/dev/null; then
    printf '%s\n' 'missing changelog version unexpectedly accepted' >&2
    exit 1
fi

printf '%s\n' 'Release-note generation passed'
