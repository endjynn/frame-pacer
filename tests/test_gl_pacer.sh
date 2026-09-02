#!/bin/sh
set -eu

root=$(pwd)
version=$(sh packaging/read-version.sh VERSION)
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
# Loading the backend with logging requested still creates no file until an
# intercepted presentation attempt or actionable failure occurs.
mkdir -p "$state/no-render"
XDG_CONFIG_HOME="$state" XDG_STATE_HOME="$state/no-render" \
    FRAME_PACER_LOG=1 \
    LD_PRELOAD="$root/build/\${LIB}/libframe_pacer_gl_shim.so" "$noop_program"
if find "$state/no-render" -type f -name 'frame-pacer-gl-*.log' \
    -print -quit | grep -q .; then
    exit 1
fi

# A presentation entry point without a downstream target is retained as a
# bounded diagnostic even though no frame can be forwarded.
mkdir -p "$state/missing-present"
XDG_CONFIG_HOME="$state" XDG_STATE_HOME="$state/missing-present" \
    FRAME_PACER_LOG=1 FRAME_PACER_TEST_MISSING_PRESENT=glx \
    LD_PRELOAD="$root/build/\${LIB}/libframe_pacer_gl_shim.so" "$noop_program"
missing_log=$(find "$state/missing-present/frame-pacer" -type f \
    -name 'frame-pacer-gl-*.log' -print -quit)
test -n "$missing_log"
sed -n '1p' "$missing_log" | grep -q \
    "startup version=$version backend=opengl"
grep -q 'glXSwapBuffers has no downstream target' "$missing_log"

# Exercise every GLX/EGL presentation entry point with the backend loaded and
# logging disabled. No runtime log may be created.
XDG_CONFIG_HOME="$state" XDG_STATE_HOME="$state" \
    LD_LIBRARY_PATH="$root/build/$arch" \
    LD_PRELOAD="$root/build/\${LIB}/libframe_pacer_gl_shim.so" \
    "$test_program"
if find "$state/frame-pacer" -type f -name 'frame-pacer-gl-*.log' -print -quit | grep -q .; then
    exit 1
fi
XDG_CONFIG_HOME="$state" XDG_STATE_HOME="$state" \
    LD_LIBRARY_PATH="$root/build/$arch" \
    FRAME_PACER_LOG=1 \
    FRAME_PACER_TEST_RELOAD_PATH="$state/frame-pacer/frame-pacer.conf" \
    LD_PRELOAD="$root/build/\${LIB}/libframe_pacer_gl_shim.so" \
    "$test_program"
log=$(find "$state/frame-pacer" -type f -name 'frame-pacer-gl-*.log' -print -quit)
test -n "$log"
grep -q "startup version=$version backend=opengl.*dlsym=1 glx=1 egl=1" "$log"
sed -n '1p' "$log" | grep -q \
    "startup version=$version backend=opengl"
test "$(grep -c 'effective-config revision=1 trigger=startup backend=glx.*config=valid.*fps=70 fps_source=global.*reason=no-per-game-rules' "$log")" -eq 1
test "$(grep -c 'effective-config revision=1 trigger=startup backend=egl.*config=valid.*fps=70 fps_source=global.*reason=no-per-game-rules' "$log")" -eq 1
test "$(grep -c 'effective-config revision=2 trigger=reload backend=glx.*config=valid.*fps=71 fps_source=global.*hud=off hud_source=global.*reason=no-per-game-rules' "$log")" -eq 1
test "$(grep -c 'effective-config revision=2 trigger=reload backend=egl.*config=valid.*fps=71 fps_source=global.*hud=off hud_source=global.*reason=no-per-game-rules' "$log")" -eq 1
grep -q 'GL shutdown swaps=16' "$log"
if grep -Eq ' first=| now=| deadline=| eintr=| cap=' "$log"; then
    echo 'OpenGL log contains routine per-frame output' >&2
    grep -En ' first=| now=| deadline=| eintr=| cap=' "$log" >&2
    exit 1
fi

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
grep -q 'effective-config revision=1 trigger=startup backend=glx.*config=missing.*fps=off fps_source=default.*reason=missing-file' "$uncapped_log"
grep -q 'effective-config revision=1 trigger=startup backend=egl.*config=missing.*fps=off fps_source=default.*reason=missing-file' "$uncapped_log"
grep -q 'GL shutdown swaps=14' "$uncapped_log"
if grep -Eq ' first=| now=| deadline=| eintr=| cap=' "$uncapped_log"; then
    echo 'Uncapped OpenGL log contains routine per-frame output' >&2
    grep -En ' first=| now=| deadline=| eintr=| cap=' "$uncapped_log" >&2
    exit 1
fi

# A launcher may stage only a symlink to the shim. dladdr then names the staged
# path while /proc/self/maps identifies the adjacent backend's canonical path.
printf '%s\n' 'global_fps_limit = 70' > "$state/frame-pacer/frame-pacer.conf"
chmod 600 "$state/frame-pacer/frame-pacer.conf"
mkdir -p "$state/staged" "$state/mapped"
ln -s "$root/build/$arch/libframe_pacer_gl_shim.so" "$state/staged/preload.so"
XDG_CONFIG_HOME="$state" XDG_STATE_HOME="$state/mapped" \
    LD_LIBRARY_PATH="$root/build/$arch" \
    FRAME_PACER_LOG=1 \
    LD_PRELOAD="$state/staged/preload.so" \
    "$test_program"
