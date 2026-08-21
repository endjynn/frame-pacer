#!/bin/sh
set -eu

root=$(pwd)
state=$(mktemp -d)

cleanup() { rm -rf -- "$state"; }
trap cleanup EXIT HUP INT TERM

if [ -z "${DISPLAY:-}" ]; then
    echo 'Vulkan presentation probe skipped: DISPLAY is unset'
    exit 77
fi

run_probe()
{
    architecture=$1
    executable=$2
    architecture_state="$state/$architecture"

    mkdir -p "$architecture_state"
    XDG_STATE_HOME="$architecture_state" \
        FRAME_PACER_LOG=1 \
        VK_LAYER_PATH="$root/build/$architecture/layer" \
        VK_INSTANCE_LAYERS=VK_LAYER_ENDJYNN_frame_pacer \
        "$executable"
    log=$(find "$architecture_state/frame-pacer" -type f \
        -name 'frame-pacer-[0-9]*.log')
    test -n "$log"
    grep -q 'HUD image resources ready' "$log"
    grep -q 'HUD command resources ready' "$log"
    grep -q 'HUD overlay submitted' "$log"
    grep -q 'shutdown presents=2' "$log"
}

run_probe x86_64 "$root/build/vulkan-present-probe"
run_probe i386 "$root/build/vulkan-present-probe-i386"
echo 'Vulkan presentation probes passed for x86_64 and i386'
