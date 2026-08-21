#!/bin/sh
set -eu

root=$(pwd)
state=$(mktemp -d)

cleanup() { rm -rf -- "$state"; }
trap cleanup EXIT HUP INT TERM

if [ -z "${DISPLAY:-}" ]; then
    echo 'EGL presentation probe skipped: DISPLAY is unset'
    exit 77
fi

mkdir -p "$state/config/frame-pacer" "$state/runtime"
printf 'global_fps_limit = 999\nhud = on\n' > \
    "$state/config/frame-pacer/frame-pacer.conf"
chmod 600 "$state/config/frame-pacer/frame-pacer.conf"

XDG_CONFIG_HOME="$state/config" \
    XDG_STATE_HOME="$state/runtime" \
    FRAME_PACER_LOG=1 \
    LD_PRELOAD="$root/build/x86_64/libframe_pacer_gl_shim.so" \
    "$root/build/egl-present-probe"
log=$(find "$state/runtime/frame-pacer" -type f \
    -name 'frame-pacer-gl-[0-9]*.log')
test -n "$log"
grep -q 'GL interposer init.*hud=1' "$log"
grep -q 'GL HUD context version=' "$log"
grep -q 'GL HUD coordinates=native' "$log"
test "$(grep -c 'eglSwapBuffers .*cap=999' "$log")" -eq 2
grep -q 'GL shutdown swaps=2' "$log"

echo 'EGL presentation probe passed for x86_64'
