#!/bin/sh
set -eu

root=$(pwd)
version=$(sh packaging/read-version.sh VERSION)
state=$(mktemp -d)

cleanup() { rm -rf -- "$state"; }
trap cleanup EXIT HUP INT TERM

if [ -z "${DISPLAY:-}" ]; then
    echo 'Vulkan presentation probe skipped: DISPLAY is unset'
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
    XDG_CONFIG_HOME="$state/config" XDG_STATE_HOME="$architecture_state" \
        FRAME_PACER_LOG=1 \
        VK_LAYER_PATH="$root/build/$architecture/layer" \
        VK_INSTANCE_LAYERS=VK_LAYER_ENDJYNN_frame_pacer \
        "$executable"
    log=$(find "$architecture_state/frame-pacer" -type f \
        -name 'frame-pacer-[0-9]*.log')
    test -n "$log"
    sed -n '1p' "$log" | grep -q \
        "startup version=$version backend=vulkan"
    grep -q "startup version=$version backend=vulkan" "$log"
    grep -q 'effective-config revision=1 trigger=startup backend=vulkan.*config=valid.*fps=999 fps_source=global.*reason=no-per-game-rules' "$log"
    grep -q 'HUD image resources ready' "$log"
    grep -q 'HUD command resources ready' "$log"
    grep -q 'HUD overlay submitted' "$log"
    python3 tests/check_vulkan_lifecycle_log.py "$log"
    grep -q 'shutdown presents=2' "$log"
    if grep -Eq ' first=| now=| deadline=| eintr=| cap=|present result=|fallback count=' "$log"; then
        echo 'Vulkan log contains routine per-frame output' >&2
        exit 1
    fi
}

run_probe x86_64 "$root/build/vulkan-present-probe"
run_probe i386 "$root/build/vulkan-present-probe-i386"
for architecture in x86_64 i386; do
    executable="$root/build/vulkan-present-probe"
    if [ "$architecture" = i386 ]; then
        executable="$executable-i386"
    fi
    overlap_state="$state/overlap-$architecture"
    mkdir -p "$overlap_state"
    XDG_CONFIG_HOME="$state/config" XDG_STATE_HOME="$overlap_state" \
        FRAME_PACER_LOG=1 FRAME_PACER_TEST_OVERLAP=1 \
        VK_LAYER_PATH="$root/build/$architecture/layer" \
        VK_INSTANCE_LAYERS=VK_LAYER_ENDJYNN_frame_pacer "$executable"
    log=$(find "$overlap_state/frame-pacer" -type f -name 'frame-pacer-[0-9]*.log')
    python3 tests/check_vulkan_overlap_log.py "$log"
    grep -q 'shutdown presents=10 ' "$log"
done
echo 'Vulkan presentation probes passed for x86_64 and i386'
