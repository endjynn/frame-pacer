#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf -- "$temporary"' EXIT HUP INT TERM

expect_failure()
{
    if sh "$root/packaging/read-version.sh" "$1" >/dev/null 2>&1; then
        printf 'accepted invalid VERSION fixture: %s\n' "$1" >&2
        exit 1
    fi
}

printf '%s\n' '0.1.0-beta.2' > "$temporary/valid"
[ "$(sh "$root/packaging/read-version.sh" "$temporary/valid")" = '0.1.0-beta.2' ]
printf '%s\n' '1.2.3' > "$temporary/release"
printf '%s\n' '1.2.3-rc.1+build.7' > "$temporary/metadata"
[ "$(sh "$root/packaging/read-version.sh" "$temporary/release")" = '1.2.3' ]
[ "$(sh "$root/packaging/read-version.sh" "$temporary/metadata")" = \
    '1.2.3-rc.1+build.7' ]
printf '%s\n%s\n' '1.0.0' '2.0.0' > "$temporary/two-lines"
printf '%s\n' '01.0.0' > "$temporary/leading-zero"
printf '%s\n' 'not-a-version' > "$temporary/invalid"
printf '%s\n' '1.0.0" -DBAD=1' > "$temporary/injection"
printf '1.0.0\001\n' > "$temporary/control"
: > "$temporary/empty"
expect_failure "$temporary/two-lines"
expect_failure "$temporary/leading-zero"
expect_failure "$temporary/invalid"
expect_failure "$temporary/injection"
expect_failure "$temporary/control"
expect_failure "$temporary/empty"

sh "$root/packaging/generate-version-header.sh" "$temporary/valid" "$temporary/version.h"
grep -Fx '#define FRAME_PACER_VERSION "0.1.0-beta.2"' "$temporary/version.h" >/dev/null
printf '%s\n' '1.2.3' > "$temporary/valid"
sh "$root/packaging/generate-version-header.sh" "$temporary/valid" "$temporary/version.h"
grep -Fx '#define FRAME_PACER_VERSION "1.2.3"' "$temporary/version.h" >/dev/null

grep -F 'packaging/read-version.sh' "$root/Makefile" >/dev/null
grep -F 'packaging/read-version.sh' "$root/packaging/package.sh" >/dev/null
grep -F 'packaging/read-version.sh' "$root/.github/workflows/release.yml" >/dev/null
