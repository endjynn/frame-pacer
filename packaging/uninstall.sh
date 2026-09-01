#!/bin/sh
set -eu

fail()
{
    printf 'frame-pacer uninstall: %s\n' "$1" >&2
    exit 1
}

validate_path()
{
    name=$1
    value=$2

    case "$value" in
        /*) ;;
        *) fail "$name must be an absolute path" ;;
    esac
    case "$value/" in
        *'/../'*|*'/./'*|*'//'*) fail "$name contains an unsafe path component" ;;
    esac
    if printf '%s' "$value" | LC_ALL=C grep -q '[[:cntrl:]]'; then
        fail "$name contains a control character"
    fi
}

prefix=${PREFIX:-"${HOME:-}/.local"}
destdir=${DESTDIR:-}

[ -n "$prefix" ] || fail 'PREFIX is empty and HOME is unavailable'
validate_path PREFIX "$prefix"
[ "$prefix" != / ] || fail 'PREFIX must not be the filesystem root'
if [ -n "$destdir" ]; then
    validate_path DESTDIR "$destdir"
    [ "$destdir" != / ] || destdir=
    destdir=${destdir%/}
fi

install_root="$destdir$prefix"
runtime_dir="$install_root/lib/frame-pacer"
manifest_dir="$install_root/share/vulkan/implicit_layer.d"

rm -f -- \
    "$manifest_dir/VkLayer_frame_pacer.x86_64.json" \
    "$manifest_dir/VkLayer_frame_pacer.i386.json" \
    "$runtime_dir/x86_64/libVkLayer_frame_pacer.so" \
    "$runtime_dir/i386/libVkLayer_frame_pacer.so" \
    "$runtime_dir/frame-pacer-thread-cpu-controller" \
    "$runtime_dir/VERSION" \
    "$runtime_dir/lib/libframe_pacer_gl.so" \
    "$runtime_dir/lib/libframe_pacer_gl_shim.so" \
    "$runtime_dir/lib32/libframe_pacer_gl.so" \
    "$runtime_dir/lib32/libframe_pacer_gl_shim.so" \
    "$runtime_dir/lib/x86_64-linux-gnu/libframe_pacer_gl.so" \
    "$runtime_dir/lib/x86_64-linux-gnu/libframe_pacer_gl_shim.so" \
    "$runtime_dir/lib/i386-linux-gnu/libframe_pacer_gl.so" \
    "$runtime_dir/lib/i386-linux-gnu/libframe_pacer_gl_shim.so"

rmdir \
    "$runtime_dir/lib/x86_64-linux-gnu" \
    "$runtime_dir/lib/i386-linux-gnu" \
    "$runtime_dir/x86_64" \
    "$runtime_dir/i386" \
    "$runtime_dir/lib32" \
    "$runtime_dir/lib" \
    "$runtime_dir" 2>/dev/null || true

printf 'Uninstalled frame-pacer from %s\n' "$prefix"
