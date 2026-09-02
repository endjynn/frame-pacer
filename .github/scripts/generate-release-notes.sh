#!/bin/sh
set -eu

fail()
{
    printf 'release notes: %s\n' "$1" >&2
    exit 1
}

[ "$#" -eq 3 ] || fail 'expected VERSION, CHANGELOG, and output paths'
version_file=$1
changelog=$2
output=$3
script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_root=$(CDPATH= cd -- "$script_directory/../.." && pwd)
version=$(sh "$repository_root/packaging/read-version.sh" "$version_file") ||
    fail 'invalid version'
prefix="## $version "
heading=$(awk -v prefix="$prefix" 'index($0, prefix) == 1 { print; exit }' \
    "$changelog") || fail "cannot read $changelog"
[ -n "$heading" ] || fail "CHANGELOG has no $version section"
case "$heading" in
    *unreleased*) fail "$version is still marked unreleased" ;;
esac
section=$(awk -v prefix="$prefix" '
    index($0, prefix) == 1 { found = 1; next }
    found && /^## / { exit }
    found { print }
    END { if (!found) exit 1 }
' "$changelog") || fail "cannot extract $version from CHANGELOG"
printf '%s\n' "$section" | grep -q '^- ' ||
    fail "$version has no changelog entries"
[ ! -L "$output" ] || fail 'output must not be a symbolic link'
umask 077
{
    printf '%s\n' \
        'Compatibility: Debian 12 (glibc 2.36), GCC 12, x86_64 and i386.'
    printf '\n## What'"'"'s Changed\n'
    printf '%s\n' "$section"
} > "$output" || fail "cannot write $output"
