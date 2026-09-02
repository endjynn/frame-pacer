#!/bin/sh
set -eu

root=$(pwd)
state=$(mktemp -d)
cleanup() { rm -rf -- "$state"; }
trap cleanup EXIT HUP INT TERM

mkdir -p "$state/frame-pacer"
printf 'global_fps_limit = off\nhud = off\n' > \
    "$state/frame-pacer/frame-pacer.conf"
chmod 600 "$state/frame-pacer/frame-pacer.conf"

echo 'Configuration hot paths:'
"$root/build/benchmark-pacer-limit"
echo 'GLX presentation path, uncapped with HUD and logging disabled:'
XDG_CONFIG_HOME="$state" XDG_STATE_HOME="$state" \
    LD_PRELOAD="$root/build/x86_64/libframe_pacer_gl.so" \
    "$root/build/benchmark-gl-present"
