#!/bin/sh
set -eu

root=$(pwd)
state=$(mktemp -d)
arch=${1:-x86_64}
case "$arch" in
    x86_64)
        test_program="$root/build/test-gl-pacer"
        noop_program="$root/build/test-gl-shim-noop"
        ;;
    i386)
        test_program="$root/build/test-gl-pacer-i386"
        noop_program="$root/build/test-gl-shim-noop-i386"
        ;;
    *) exit 2 ;;
esac
cleanup() { rm -rf -- "$state"; }
trap cleanup EXIT HUP INT TERM
mkdir -p "$state/frame-pacer"
printf '%s\n' 'global_fps_limit = 70' > "$state/frame-pacer/frame-pacer.conf"

XDG_CONFIG_HOME="$state" XDG_STATE_HOME="$state" \
    LD_PRELOAD="$root/build/\${LIB}/libframe_pacer_gl_shim.so" "$noop_program"
if find "$state/frame-pacer" -type f -name 'frame-pacer-gl-*.log' -print -quit | grep -q .; then
    exit 1
fi
XDG_CONFIG_HOME="$state" XDG_STATE_HOME="$state" \
    LD_LIBRARY_PATH="$root/build/$arch" \
    FRAME_PACER_LOG=1 \
    LD_PRELOAD="$root/build/\${LIB}/libframe_pacer_gl_shim.so" \
    "$test_program"
log=$(find "$state/frame-pacer" -type f -name 'frame-pacer-gl-*.log' -print -quit)
test -n "$log"
grep -q 'GL interposer init.*dlsym=1 glx=1 egl=1' "$log"
test "$(grep -c 'glXSwapBuffers .*cap=70' "$log")" -eq 3
test "$(grep -c 'eglSwapBuffers .*cap=70' "$log")" -eq 2
test "$(grep -c 'eglSwapBuffersWithDamageKHR .*cap=70' "$log")" -eq 1
