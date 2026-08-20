#!/bin/sh
set -eu

test_root=$(mktemp -d)
prefix="$test_root/install"
layer_dir="$prefix/share/vulkan/implicit_layer.d"

cleanup() {
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

make install PREFIX="$prefix"

test -x "$prefix/lib/frame-pacer/x86_64/libVkLayer_frame_pacer.so"
test -x "$prefix/lib/frame-pacer/i386/libVkLayer_frame_pacer.so"
jq -e '.layer.name == "VK_LAYER_ENDJYNN_frame_pacer_x86_64" and
       .layer.library_path == $PREFIX + "/lib/frame-pacer/x86_64/libVkLayer_frame_pacer.so"' \
    --arg PREFIX "$prefix" "$layer_dir/VkLayer_frame_pacer.x86_64.json" >/dev/null
jq -e '.layer.name == "VK_LAYER_ENDJYNN_frame_pacer_i386" and
       .layer.library_path == $PREFIX + "/lib/frame-pacer/i386/libVkLayer_frame_pacer.so"' \
    --arg PREFIX "$prefix" "$layer_dir/VkLayer_frame_pacer.i386.json" >/dev/null

make uninstall PREFIX="$prefix"

test ! -e "$layer_dir/VkLayer_frame_pacer.x86_64.json"
test ! -e "$layer_dir/VkLayer_frame_pacer.i386.json"
test ! -e "$prefix/lib/frame-pacer/x86_64/libVkLayer_frame_pacer.so"
test ! -e "$prefix/lib/frame-pacer/i386/libVkLayer_frame_pacer.so"

stage="$test_root/stage"
make install DESTDIR="$stage" PREFIX=/usr/local

jq -e '.layer.library_path == "/usr/local/lib/frame-pacer/x86_64/libVkLayer_frame_pacer.so"' \
    "$stage/usr/local/share/vulkan/implicit_layer.d/VkLayer_frame_pacer.x86_64.json" >/dev/null
jq -e '.layer.library_path == "/usr/local/lib/frame-pacer/i386/libVkLayer_frame_pacer.so"' \
    "$stage/usr/local/share/vulkan/implicit_layer.d/VkLayer_frame_pacer.i386.json" >/dev/null

make uninstall DESTDIR="$stage" PREFIX=/usr/local

test ! -e "$stage/usr/local/share/vulkan/implicit_layer.d/VkLayer_frame_pacer.x86_64.json"
test ! -e "$stage/usr/local/lib/frame-pacer/x86_64/libVkLayer_frame_pacer.so"
