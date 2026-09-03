#!/bin/sh
set -eu

test_root=$(mktemp -d)
prefix="$test_root/install & upgrade"
layer_dir="$prefix/share/vulkan/implicit_layer.d"
runtime_dir="$prefix/lib/frame-pacer"
config="$test_root/config/frame-pacer/frame-pacer.conf"

cleanup() {
    rm -rf -- "$test_root"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "${config%/*}" "$runtime_dir"
printf 'global_fps_limit = 60\n' >"$config"
printf 'keep\n' >"$runtime_dir/unrelated.txt"

if make install PREFIX=/ >/dev/null 2>&1; then
    echo 'install accepted the filesystem root as PREFIX' >&2
    exit 1
fi
if make install PREFIX=relative/path >/dev/null 2>&1; then
    echo 'install accepted a relative PREFIX' >&2
    exit 1
fi

make install PREFIX="$prefix"

test "$(stat -c %a build/frame-pacer-thread-cpu-controller)" = 755
test -x "$runtime_dir/x86_64/libVkLayer_frame_pacer.so"
test -x "$runtime_dir/i386/libVkLayer_frame_pacer.so"
test -x "$runtime_dir/frame-pacer-thread-cpu-controller"
test ! -e "$runtime_dir/frame-pacer-nvml-helper"
test "$(stat -c %a "$runtime_dir/frame-pacer-thread-cpu-controller")" = 755
test "$(cat "$runtime_dir/VERSION")" = "$(cat VERSION)"

for path in \
    lib/libframe_pacer_gl.so \
    lib/libframe_pacer_gl_shim.so \
    lib/x86_64-linux-gnu/libframe_pacer_gl.so \
    lib/x86_64-linux-gnu/libframe_pacer_gl_shim.so; do
    test -x "$runtime_dir/$path"
    file "$runtime_dir/$path" | grep -q 'ELF 64-bit'
done
for path in \
    lib32/libframe_pacer_gl.so \
    lib32/libframe_pacer_gl_shim.so \
    lib/i386-linux-gnu/libframe_pacer_gl.so \
    lib/i386-linux-gnu/libframe_pacer_gl_shim.so; do
    test -x "$runtime_dir/$path"
    file "$runtime_dir/$path" | grep -q 'ELF 32-bit'
done

jq -e '.layer.name == "VK_LAYER_ENDJYNN_frame_pacer_x86_64" and
       .layer.library_path == $PREFIX + "/lib/frame-pacer/x86_64/libVkLayer_frame_pacer.so"' \
    --arg PREFIX "$prefix" "$layer_dir/VkLayer_frame_pacer.x86_64.json" >/dev/null
jq -e '.layer.name == "VK_LAYER_ENDJYNN_frame_pacer_i386" and
       .layer.library_path == $PREFIX + "/lib/frame-pacer/i386/libVkLayer_frame_pacer.so"' \
    --arg PREFIX "$prefix" "$layer_dir/VkLayer_frame_pacer.i386.json" >/dev/null

# Reinstallation replaces owned runtime files but preserves configuration and
# unrelated neighboring content.
printf 'old\n' >"$runtime_dir/x86_64/libVkLayer_frame_pacer.so"
make install PREFIX="$prefix"
file "$runtime_dir/x86_64/libVkLayer_frame_pacer.so" | grep -q 'ELF 64-bit'
test "$(cat "$config")" = 'global_fps_limit = 60'
test "$(cat "$runtime_dir/unrelated.txt")" = keep

make uninstall PREFIX="$prefix"

test ! -e "$layer_dir/VkLayer_frame_pacer.x86_64.json"
test ! -e "$layer_dir/VkLayer_frame_pacer.i386.json"
test ! -e "$runtime_dir/x86_64/libVkLayer_frame_pacer.so"
test ! -e "$runtime_dir/i386/libVkLayer_frame_pacer.so"
test ! -e "$runtime_dir/frame-pacer-thread-cpu-controller"
test ! -e "$runtime_dir/lib/libframe_pacer_gl_shim.so"
test ! -e "$runtime_dir/lib32/libframe_pacer_gl_shim.so"
test "$(cat "$config")" = 'global_fps_limit = 60'
test "$(cat "$runtime_dir/unrelated.txt")" = keep

stage="$test_root/stage with spaces"
make install DESTDIR="$stage" PREFIX=/usr/local

jq -e '.layer.library_path == "/usr/local/lib/frame-pacer/x86_64/libVkLayer_frame_pacer.so"' \
    "$stage/usr/local/share/vulkan/implicit_layer.d/VkLayer_frame_pacer.x86_64.json" >/dev/null
jq -e '.layer.library_path == "/usr/local/lib/frame-pacer/i386/libVkLayer_frame_pacer.so"' \
    "$stage/usr/local/share/vulkan/implicit_layer.d/VkLayer_frame_pacer.i386.json" >/dev/null
test -x "$stage/usr/local/lib/frame-pacer/frame-pacer-thread-cpu-controller"
test -x "$stage/usr/local/lib/frame-pacer/lib/libframe_pacer_gl_shim.so"
test -x "$stage/usr/local/lib/frame-pacer/lib32/libframe_pacer_gl_shim.so"
test ! -e "$stage/usr/local/lib/frame-pacer/frame-pacer-nvml-helper"

make uninstall DESTDIR="$stage" PREFIX=/usr/local

test ! -e "$stage/usr/local/share/vulkan/implicit_layer.d/VkLayer_frame_pacer.x86_64.json"
test ! -e "$stage/usr/local/lib/frame-pacer/x86_64/libVkLayer_frame_pacer.so"
test ! -e "$stage/usr/local/lib/frame-pacer/frame-pacer-thread-cpu-controller"
