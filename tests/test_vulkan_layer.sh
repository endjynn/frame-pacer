#!/bin/sh
set -eu

root=$(pwd)
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

cleanup() { rm -rf -- "$state"; }
trap cleanup EXIT HUP INT TERM

run_probe()
{
    XDG_STATE_HOME="$state" \
        FRAME_PACER_LOG=1 \
        VK_LAYER_PATH="$root/build/$arch/layer" \
        VK_INSTANCE_LAYERS=VK_LAYER_ENDJYNN_frame_pacer \
        "$1"
}

run_probe "$instance_probe"
run_probe "$device_probe"

test "$(find "$state/frame-pacer" -type f -name 'frame-pacer-[0-9]*.log' | wc -l)" -eq 2
for log in "$state"/frame-pacer/frame-pacer-[0-9]*.log; do
    grep -q 'layer init.*hud=enabled' "$log"
done
