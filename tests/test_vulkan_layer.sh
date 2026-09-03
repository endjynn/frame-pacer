#!/bin/sh
set -eu

root=$(pwd)
version=$(sh packaging/read-version.sh VERSION)
arch=${1:-x86_64}
state=$(mktemp -d)

case "$arch" in
    x86_64)
        instance_probe="$root/build/smoke"
        device_probe="$root/build/smoke-device"
        ;;
    i386)
        instance_probe="$root/build/smoke-i386"
        device_probe="$root/build/smoke-device-i386"
        ;;
    *) exit 2 ;;
esac

cleanup() {
    status=$?
    trap - EXIT HUP INT TERM
    if [ "$status" -ne 0 ] && [ -d "$state/frame-pacer" ]; then
        for log in "$state"/frame-pacer/frame-pacer-[0-9]*.log; do
            [ -f "$log" ] || continue
            printf '%s\n' "--- ${log##*/}"
            tail -n 40 "$log"
        done
    fi
    rm -rf -- "$state"
    exit "$status"
}
trap cleanup EXIT HUP INT TERM

run_probe() {
    XDG_STATE_HOME="$state" \
        FRAME_PACER_LOG=1 \
        VK_LAYER_PATH="$root/build/$arch/layer" \
        VK_INSTANCE_LAYERS=VK_LAYER_ENDJYNN_frame_pacer \
        "$1"
}

printf 'Vulkan instance lifecycle probe (%s)\n' "$arch"
run_probe "$instance_probe"
printf 'Vulkan device lifecycle probe (%s)\n' "$arch"
run_probe "$device_probe"

test "$(find "$state" -type f -name 'frame-pacer-[0-9]*.log' | wc -l)" -eq 0

if [ "$arch" = x86_64 ]; then
    failure_state="$state/failure"

    mkdir -p "$failure_state"
    XDG_STATE_HOME="$failure_state" FRAME_PACER_LOG=1 \
        "$root/build/test_frame_pacer_layer"
    failure_log=$(find "$failure_state/frame-pacer" -type f \
        -name 'frame-pacer-[0-9]*.log' -print -quit)
    test -n "$failure_log"
    sed -n '1p' "$failure_log" | grep -q \
        "startup version=$version backend=vulkan.*architecture=64-bit"
    grep -q 'queue present has no downstream target' "$failure_log"
fi
