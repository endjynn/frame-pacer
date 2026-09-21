#!/bin/sh
set -eu

state=$(mktemp -d)
trap 'rm -rf -- "$state"' EXIT HUP INT TERM
mkdir -p "$state/logs"
# A copy changes /proc/self/exe; a symlink or argv[0] does not establish identity.
cp "$1" "$state/fossilize_replay"
XDG_STATE_HOME="$state/logs" FRAME_PACER_LOG=1 \
    "$state/fossilize_replay" --shader-worker
test "$(find "$state/logs" -type f | wc -l)" -eq 0
echo 'Shader-worker dispatch bypass passed'
