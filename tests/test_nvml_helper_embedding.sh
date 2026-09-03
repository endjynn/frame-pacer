#!/bin/sh
set -eu

helper=build/frame-pacer-nvml-helper

file "$helper" | grep -q 'ELF 64-bit.*x86-64'
test "$(stat -c %s "$helper")" -le 65536
if readelf -S "$helper" | grep -q '\.debug'; then
    echo 'production NVML helper contains debug sections' >&2
    exit 1
fi

for library in \
    build/i386/libVkLayer_frame_pacer.so \
    build/i386/libframe_pacer_gl.so; do
    strings "$library" | grep -q 'frame-pacer-nvml-helper'
done

for library in \
    build/x86_64/libVkLayer_frame_pacer.so \
    build/x86_64/libframe_pacer_gl.so \
    build/i386/libframe_pacer_gl_shim.so \
    build/x86_64/libframe_pacer_gl_shim.so; do
    if strings "$library" | grep -q 'frame-pacer-nvml-helper'; then
        echo "unexpected embedded NVML helper in $library" >&2
        exit 1
    fi
done

echo 'NVML helper architecture, size, and embedding checks passed'
