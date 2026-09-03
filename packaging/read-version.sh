#!/bin/sh
set -eu

fail() {
    printf 'frame-pacer version: %s\n' "$1" >&2
    exit 1
}

file=${1:-VERSION}
lines=$(awk 'END { print NR }' "$file") || fail "cannot read $file"
[ "$lines" -eq 1 ] || fail 'VERSION must contain exactly one line'
version=$(sed -n '1p' "$file") || fail "cannot read $file"
semver='(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-((0|[1-9][0-9]*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*)(\.(0|[1-9][0-9]*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*))*))?(\+[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?'
printf '%s\n' "$version" | grep -Eq "^$semver$" ||
    fail 'VERSION is not a Semantic Version 2.0.0 value'
printf '%s\n' "$version"
