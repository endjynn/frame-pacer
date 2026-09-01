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
chmod 700 "$state/frame-pacer"
printf '%s\n' 'global_fps_limit = 70' > "$state/frame-pacer/frame-pacer.conf"
chmod 600 "$state/frame-pacer/frame-pacer.conf"

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
test "$(grep -c 'glXSwapBuffers .*cap=70' "$log")" -eq 6
test "$(grep -c 'eglSwapBuffers .*cap=70' "$log")" -eq 4
test "$(grep -c 'eglSwapBuffersWithDamageKHR .*cap=70' "$log")" -eq 2
test "$(grep -c 'eglSwapBuffersWithDamageEXT .*cap=70' "$log")" -eq 2

# A missing configuration must leave every GL presentation path uncapped.
mkdir -p "$state/uncapped"
XDG_CONFIG_HOME="$state/missing-config" XDG_STATE_HOME="$state/uncapped" \
    LD_LIBRARY_PATH="$root/build/$arch" \
    FRAME_PACER_LOG=1 \
    LD_PRELOAD="$root/build/\${LIB}/libframe_pacer_gl_shim.so" \
    "$test_program"
uncapped_log=$(find "$state/uncapped/frame-pacer" -type f \
    -name 'frame-pacer-gl-*.log' -print -quit)
test -n "$uncapped_log"
test "$(grep -c 'glXSwapBuffers .*cap=0' "$uncapped_log")" -eq 6
test "$(grep -c 'eglSwapBuffers .*cap=0' "$uncapped_log")" -eq 4
test "$(grep -c 'eglSwapBuffersWithDamageKHR .*cap=0' "$uncapped_log")" -eq 2
test "$(grep -c 'eglSwapBuffersWithDamageEXT .*cap=0' "$uncapped_log")" -eq 2

# A launcher may stage only a symlink to the shim. dladdr then names the staged
# path while /proc/self/maps identifies the adjacent backend's canonical path.
mkdir -p "$state/staged" "$state/mapped"
ln -s "$root/build/$arch/libframe_pacer_gl_shim.so" "$state/staged/preload.so"
XDG_CONFIG_HOME="$state" XDG_STATE_HOME="$state/mapped" \
    LD_LIBRARY_PATH="$root/build/$arch" \
    FRAME_PACER_LOG=1 \
    LD_PRELOAD="$state/staged/preload.so" \
    "$test_program"
