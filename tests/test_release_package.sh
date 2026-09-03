#!/bin/sh
set -eu

root=$(pwd -P)
version=$(sh packaging/read-version.sh VERSION)
package="frame-pacer-$version-linux-x86_64-multilib"
archive="$root/build/dist/$package.tar.xz"
checksum="$archive.sha256"
test_root=$(mktemp -d)

cleanup() {
    rm -rf -- "$test_root"
}
trap cleanup EXIT HUP INT TERM

fail() {
    printf 'release package test: %s\n' "$1" >&2
    exit 1
}

validate_archive_name() {
    candidate=$1
    base=${candidate##*/}
    embedded=$(tar -xOf "$candidate" "$package/VERSION" 2>/dev/null) || return 1
    [ "$base" = "frame-pacer-$embedded-linux-x86_64-multilib.tar.xz" ]
}

[ -f "$archive" ] || fail 'archive is missing'
[ -f "$checksum" ] || fail 'checksum is missing'
[ "$(wc -c <"$archive")" -le 52428800 ] || fail 'archive exceeds 50 MiB'
(
    cd "${archive%/*}"
    sha256sum -c "${checksum##*/}"
)
validate_archive_name "$archive" || fail 'archive and embedded versions disagree'

first_hash=$(sha256sum "$archive" | cut -d ' ' -f 1)
STRIP="${STRIP:-strip}" sh packaging/package.sh >/dev/null
second_hash=$(sha256sum "$archive" | cut -d ' ' -f 1)
[ "$first_hash" = "$second_hash" ] || fail 'repeated builds are not reproducible'

fake="$test_root/frame-pacer-9.9.9-linux-x86_64-multilib.tar.xz"
cp "$archive" "$fake"
if validate_archive_name "$fake"; then
    fail 'mismatched archive filename was accepted'
fi

extract="$test_root/extracted release"
mkdir -p "$extract"
tar -xJf "$archive" -C "$extract"
release="$extract/$package"
cmp "$release/VERSION" "$release/payload/VERSION" ||
    fail 'package versions disagree'

expected="$test_root/expected"
printf '%s\n' \
    "$package/" \
    "$package/CHANGELOG.md" \
    "$package/LICENSE" \
    "$package/README.md" \
    "$package/VERSION" \
    "$package/install.sh" \
    "$package/payload/" \
    "$package/payload/VERSION" \
    "$package/payload/VkLayer_frame_pacer_implicit.json.in" \
    "$package/payload/frame-pacer-thread-cpu-controller" \
    "$package/payload/i386/" \
    "$package/payload/i386/libVkLayer_frame_pacer.so" \
    "$package/payload/i386/libframe_pacer_gl.so" \
    "$package/payload/i386/libframe_pacer_gl_shim.so" \
    "$package/payload/x86_64/" \
    "$package/payload/x86_64/libVkLayer_frame_pacer.so" \
    "$package/payload/x86_64/libframe_pacer_gl.so" \
    "$package/payload/x86_64/libframe_pacer_gl_shim.so" \
    "$package/uninstall.sh" | sort >"$expected"
tar -tf "$archive" | sort >"$test_root/actual"
cmp "$expected" "$test_root/actual" || fail 'archive inventory differs'

if tar --numeric-owner -tvf "$archive" | awk '$2 != "0/0" { bad = 1 } END { exit bad }'; then
    :
else
    fail 'archive ownership is not numeric root/root'
fi
if find "$release" -type l -print -quit | grep -q .; then
    fail 'archive contains a symbolic link'
fi
if find "$release" -type f -perm /0022 -print -quit | grep -q .; then
    fail 'archive contains a group- or world-writable file'
fi
test "$(stat -c %a "$release/install.sh")" = 755
test "$(stat -c %a "$release/uninstall.sh")" = 755
test "$(stat -c %a "$release/README.md")" = 644

for architecture in x86_64 i386; do
    case "$architecture" in
        x86_64) class='ELF 64-bit' ;;
        i386) class='ELF 32-bit' ;;
    esac
    for library in \
        libVkLayer_frame_pacer.so \
        libframe_pacer_gl.so \
        libframe_pacer_gl_shim.so; do
        binary="$release/payload/$architecture/$library"
        file "$binary" | grep -q "$class" || fail "$architecture binary class is wrong"
        file "$binary" | grep -q 'stripped' || fail "$library is not stripped"
        if readelf -d "$binary" | grep -E 'RPATH|RUNPATH' >/dev/null; then
            fail "$library contains a runtime search path"
        fi
        if readelf -d "$binary" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' |
            grep -Ev '^libc\.so\.6$' | grep -q .; then
            fail "$library has an unexpected dynamic dependency"
        fi
    done
done
controller="$release/payload/frame-pacer-thread-cpu-controller"
file "$controller" | grep -q 'ELF 64-bit.*stripped' ||
    fail 'CPU controller is not a stripped x86_64 binary'
if readelf -d "$controller" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' |
    grep -Ev '^libc\.so\.6$' | grep -q .; then
    fail 'CPU controller has an unexpected dynamic dependency'
fi

python3 tests/check_release_privacy.py "$release"

stage="$test_root/install stage"
prefix='/opt/frame pacer & beta'
runtime="$stage$prefix/lib/frame-pacer"
config="$stage/config/frame-pacer/frame-pacer.conf"
mkdir -p "${config%/*}" "$runtime"
printf 'hud = off\n' >"$config"
printf 'neighbor\n' >"$runtime/unrelated.txt"

DESTDIR="$stage" PREFIX="$prefix" "$release/install.sh"
jq -e '.layer.library_path == $PREFIX + "/lib/frame-pacer/x86_64/libVkLayer_frame_pacer.so"' \
    --arg PREFIX "$prefix" \
    "$stage$prefix/share/vulkan/implicit_layer.d/VkLayer_frame_pacer.x86_64.json" >/dev/null
jq -e '.layer.library_path == $PREFIX + "/lib/frame-pacer/i386/libVkLayer_frame_pacer.so"' \
    --arg PREFIX "$prefix" \
    "$stage$prefix/share/vulkan/implicit_layer.d/VkLayer_frame_pacer.i386.json" >/dev/null
printf 'previous version\n' >"$runtime/x86_64/libVkLayer_frame_pacer.so"
DESTDIR="$stage" PREFIX="$prefix" "$release/install.sh"
file "$runtime/x86_64/libVkLayer_frame_pacer.so" | grep -q 'ELF 64-bit'

DESTDIR="$stage" PREFIX="$prefix" "$release/uninstall.sh"
test ! -e "$runtime/x86_64/libVkLayer_frame_pacer.so"
test ! -e "$runtime/lib/libframe_pacer_gl_shim.so"
test ! -e "$runtime/lib32/libframe_pacer_gl_shim.so"
test "$(cat "$runtime/unrelated.txt")" = neighbor
test "$(cat "$config")" = 'hud = off'

smoke_stage="$test_root/smoke"
smoke_prefix=/opt/frame-pacer-beta
smoke_runtime="$smoke_stage$smoke_prefix/lib/frame-pacer"
DESTDIR="$smoke_stage" PREFIX="$smoke_prefix" "$release/install.sh"
smoke_config="$test_root/smoke-config/frame-pacer"
mkdir -p "$smoke_config"
printf 'global_fps_limit = 999\nhud = on\n' >"$smoke_config/frame-pacer.conf"
chmod 600 "$smoke_config/frame-pacer.conf"
for architecture in x86_64 i386; do
    case "$architecture" in
        x86_64)
            gl_directory=lib
            gl_probe=./build/test-gl-pacer
            vulkan_probe=./build/smoke
            ;;
        i386)
            gl_directory=lib32
            gl_probe=./build/test-gl-pacer-i386
            vulkan_probe=./build/smoke-i386
            ;;
    esac
    gl_state="$test_root/staged-gl-$architecture"
    mkdir -p "$gl_state"
    XDG_CONFIG_HOME="$test_root/smoke-config" XDG_STATE_HOME="$gl_state" \
        LD_LIBRARY_PATH="$root/build/$architecture" FRAME_PACER_LOG=1 \
        LD_PRELOAD="$smoke_runtime/$gl_directory/libframe_pacer_gl_shim.so" \
        "$gl_probe"
    gl_log=$(find "$gl_state/frame-pacer" -type f \
        -name 'frame-pacer-gl-[0-9]*.log' -print -quit)
    grep -q "startup version=$version backend=opengl" "$gl_log" ||
        fail "$architecture staged OpenGL runtime reports the wrong version"

    layer_directory="$test_root/staged-vulkan-$architecture/layer"
    vulkan_state="$test_root/staged-vulkan-$architecture/state"
    mkdir -p "$layer_directory" "$vulkan_state"
    sed "s|\"../libVkLayer_frame_pacer.so\"|\"$smoke_runtime/$architecture/libVkLayer_frame_pacer.so\"|" \
        VkLayer_frame_pacer.json.in >"$layer_directory/VkLayer_frame_pacer.json"
    XDG_CONFIG_HOME="$test_root/smoke-config" XDG_STATE_HOME="$vulkan_state" \
        FRAME_PACER_LOG=1 VK_LAYER_PATH="$layer_directory" \
        VK_INSTANCE_LAYERS=VK_LAYER_ENDJYNN_frame_pacer "$vulkan_probe"
    if find "$vulkan_state" -type f -name 'frame-pacer-[0-9]*.log' \
        -print -quit | grep -q .; then
        fail "$architecture staged Vulkan load-only probe created a log"
    fi
done
DESTDIR="$smoke_stage" PREFIX="$smoke_prefix" "$release/uninstall.sh"

printf 'Release package tests passed\n'
