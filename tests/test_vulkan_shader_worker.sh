#!/bin/sh
set -eu

root=$(pwd)
architecture=$1
probe=$2
state=$(mktemp -d)
trap 'rm -rf -- "$state"' EXIT HUP INT TERM
mkdir -p "$state/config/frame-pacer" "$state/logs"
printf 'global_fps_limit = 1\nhud = on\n\n[Shader worker]\nexecutable = "fossilize_replay"\nfps_limit = 1\nthread_cpu_limit = 50%%\n' > \
    "$state/config/frame-pacer/frame-pacer.conf"
chmod 600 "$state/config/frame-pacer/frame-pacer.conf"
cp "$probe" "$state/fossilize_replay"
for activation in explicit implicit; do
    if [ "$activation" = explicit ]; then
        layer=VK_LAYER_ENDJYNN_frame_pacer
        set -- env DISABLE_FRAME_PACER=1
    else
        layer=
        set -- env -u DISABLE_FRAME_PACER
    fi
    "$@" XDG_CONFIG_HOME="$state/config" XDG_STATE_HOME="$state/logs" \
        FRAME_PACER_LOG=1 ENABLE_FRAME_PACER=1 \
        VK_ADD_IMPLICIT_LAYER_PATH="$root/build/$architecture/implicit_layer" \
        VK_LAYER_PATH="$root/build/$architecture/layer" \
        VK_INSTANCE_LAYERS="$layer" \
        timeout 30 "$state/fossilize_replay"
    test "$(find "$state/logs" -type f | wc -l)" -eq 0
done
echo "Vulkan shader-worker bypass passed ($architecture)"
