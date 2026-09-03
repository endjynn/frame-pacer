#!/bin/sh
set -eu

root=$(pwd)
version=$(sh packaging/read-version.sh VERSION)
state=$(mktemp -d)

cleanup() { rm -rf -- "$state"; }
trap cleanup EXIT HUP INT TERM

if [ -z "${DISPLAY:-}" ]; then
    echo 'GLX presentation probe skipped: DISPLAY is unset'
    exit 77
fi

mkdir -p "$state/config/frame-pacer"
printf 'global_fps_limit = 999\nhud = on\n' > \
    "$state/config/frame-pacer/frame-pacer.conf"
chmod 600 "$state/config/frame-pacer/frame-pacer.conf"

run_probe() {
    architecture=$1
    executable=$2
    architecture_state="$state/$architecture"

    mkdir -p "$architecture_state"
    XDG_CONFIG_HOME="$state/config" \
        XDG_STATE_HOME="$architecture_state" \
        FRAME_PACER_LOG=1 \
        LD_PRELOAD="$root/build/$architecture/libframe_pacer_gl_shim.so" \
        "$executable"
    log=$(find "$architecture_state/frame-pacer" -type f \
        -name 'frame-pacer-gl-[0-9]*.log')
    test -n "$log"
    sed -n '1p' "$log" | grep -q \
        "startup version=$version backend=opengl"
    grep -q "startup version=$version backend=opengl.*hud=1" "$log"
    grep -q 'effective-config revision=1 trigger=startup backend=glx.*config=valid.*fps=999 fps_source=global.*reason=no-per-game-rules' "$log"
    grep -q 'GL HUD context version=' "$log"
    grep -q 'GL HUD coordinates=native' "$log"
    grep -q 'GL shutdown swaps=2' "$log"
    if grep -Eq ' first=| now=| deadline=| eintr=| cap=' "$log"; then
        echo 'GLX log contains routine per-frame output' >&2
        exit 1
    fi
}

run_probe x86_64 "$root/build/glx-present-probe"
run_probe i386 "$root/build/glx-present-probe-i386"
echo 'GLX presentation probes passed for x86_64 and i386'
