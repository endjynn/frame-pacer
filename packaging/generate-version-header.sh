#!/bin/sh
set -eu

input=$1
output=$2
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd -P)
version=$(sh "$root/packaging/read-version.sh" "$input")
temporary="$output.tmp-$$"
trap 'rm -f -- "$temporary"' EXIT HUP INT TERM

{
    printf '%s\n' '#ifndef FRAME_PACER_VERSION_H'
    printf '%s\n' '#define FRAME_PACER_VERSION_H'
    printf '#define FRAME_PACER_VERSION "%s"\n' "$version"
    printf '%s\n' '#endif'
} >"$temporary"

if [ -f "$output" ] && cmp -s "$temporary" "$output"; then
    rm -f -- "$temporary"
else
    mv -f -- "$temporary" "$output"
fi
trap - EXIT HUP INT TERM
