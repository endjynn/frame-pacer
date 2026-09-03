#!/bin/sh
set -eu

fail() {
    printf 'frame-pacer install: %s\n' "$1" >&2
    exit 1
}

validate_path() {
    name=$1
    value=$2

    case "$value" in
        /*) ;;
        *) fail "$name must be an absolute path" ;;
    esac
    case "$value/" in
        *'/../'* | *'/./'* | *'//'*) fail "$name contains an unsafe path component" ;;
    esac
    if printf '%s' "$value" | LC_ALL=C grep -q '[[:cntrl:]]'; then
        fail "$name contains a control character"
    fi
}

atomic_install() {
    atomic_source=$1
    atomic_destination=$2
    atomic_mode=$3
    atomic_temporary="$atomic_destination.tmp-$$"
    current_temporary=$atomic_temporary

    install -d -m 0755 "${atomic_destination%/*}"
    rm -f -- "$atomic_temporary"
    install -m "$atomic_mode" "$atomic_source" "$atomic_temporary"
    mv -f -- "$atomic_temporary" "$atomic_destination"
    current_temporary=
}

write_manifest() {
    manifest_destination=$1
    manifest_library_path=$2
    manifest_layer_name=$3
    manifest_temporary="$manifest_destination.tmp-$$"
    current_temporary=$manifest_temporary
    manifest_json_path=$(printf '%s' "$manifest_library_path" |
        sed -e 's/\\/\\\\/g' -e 's/"/\\"/g')
    manifest_replacement=$(printf '%s' "$manifest_json_path" |
        sed 's/[\\&|]/\\&/g')

    rm -f -- "$manifest_temporary"
    sed -e "s|@LIBRARY_PATH@|$manifest_replacement|" \
        -e "s|@LAYER_NAME@|$manifest_layer_name|" \
        "$payload_dir/VkLayer_frame_pacer_implicit.json.in" >"$manifest_temporary"
    chmod 0644 "$manifest_temporary"
    mv -f -- "$manifest_temporary" "$manifest_destination"
    current_temporary=
}

cleanup() {
    if [ -n "${current_temporary:-}" ]; then
        rm -f -- "$current_temporary"
    fi
}

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
payload_dir=${FRAME_PACER_PAYLOAD_DIR:-"$script_dir/payload"}
prefix=${PREFIX:-"${HOME:-}/.local"}
destdir=${DESTDIR:-}
current_temporary=
trap cleanup EXIT
trap 'cleanup; exit 1' HUP INT TERM

[ -n "$prefix" ] || fail 'PREFIX is empty and HOME is unavailable'
validate_path PREFIX "$prefix"
[ "$prefix" != / ] || fail 'PREFIX must not be the filesystem root'
if [ -n "$destdir" ]; then
    validate_path DESTDIR "$destdir"
    [ "$destdir" != / ] || destdir=
    destdir=${destdir%/}
fi

for required in \
    x86_64/libVkLayer_frame_pacer.so \
    i386/libVkLayer_frame_pacer.so \
    x86_64/libframe_pacer_gl.so \
    i386/libframe_pacer_gl.so \
    x86_64/libframe_pacer_gl_shim.so \
    i386/libframe_pacer_gl_shim.so \
    frame-pacer-thread-cpu-controller \
    VkLayer_frame_pacer_implicit.json.in \
    VERSION; do
    [ -f "$payload_dir/$required" ] && [ ! -L "$payload_dir/$required" ] ||
        fail "payload is missing $required"
done

install_root="$destdir$prefix"
runtime_dir="$install_root/lib/frame-pacer"
manifest_dir="$install_root/share/vulkan/implicit_layer.d"
runtime_prefix="$prefix/lib/frame-pacer"

umask 022
install -d -m 0755 \
    "$runtime_dir/x86_64" \
    "$runtime_dir/i386" \
    "$runtime_dir/lib" \
    "$runtime_dir/lib32" \
    "$runtime_dir/lib/x86_64-linux-gnu" \
    "$runtime_dir/lib/i386-linux-gnu" \
    "$manifest_dir"

atomic_install "$payload_dir/x86_64/libVkLayer_frame_pacer.so" \
    "$runtime_dir/x86_64/libVkLayer_frame_pacer.so" 0755
atomic_install "$payload_dir/i386/libVkLayer_frame_pacer.so" \
    "$runtime_dir/i386/libVkLayer_frame_pacer.so" 0755
atomic_install "$payload_dir/frame-pacer-thread-cpu-controller" \
    "$runtime_dir/frame-pacer-thread-cpu-controller" 0755
atomic_install "$payload_dir/VERSION" "$runtime_dir/VERSION" 0644

for destination in lib lib/x86_64-linux-gnu; do
    atomic_install "$payload_dir/x86_64/libframe_pacer_gl.so" \
        "$runtime_dir/$destination/libframe_pacer_gl.so" 0755
    atomic_install "$payload_dir/x86_64/libframe_pacer_gl_shim.so" \
        "$runtime_dir/$destination/libframe_pacer_gl_shim.so" 0755
done
for destination in lib32 lib/i386-linux-gnu; do
    atomic_install "$payload_dir/i386/libframe_pacer_gl.so" \
        "$runtime_dir/$destination/libframe_pacer_gl.so" 0755
    atomic_install "$payload_dir/i386/libframe_pacer_gl_shim.so" \
        "$runtime_dir/$destination/libframe_pacer_gl_shim.so" 0755
done

write_manifest "$manifest_dir/VkLayer_frame_pacer.x86_64.json" \
    "$runtime_prefix/x86_64/libVkLayer_frame_pacer.so" \
    VK_LAYER_ENDJYNN_frame_pacer_x86_64
write_manifest "$manifest_dir/VkLayer_frame_pacer.i386.json" \
    "$runtime_prefix/i386/libVkLayer_frame_pacer.so" \
    VK_LAYER_ENDJYNN_frame_pacer_i386

printf 'Installed frame-pacer %s in %s\n' \
    "$(sed -n '1p' "$payload_dir/VERSION")" "$prefix"
