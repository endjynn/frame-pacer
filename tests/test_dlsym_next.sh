#!/bin/sh
set -eu

root=$(pwd)
arch=${1:-x86_64}
case "$arch" in x86_64 | i386) ;; *) exit 2 ;; esac
base="$root/build/$arch"
chain="$base/liblookup-first.so:$base/liblookup-second.so:$base/liblookup-provider.so"
for preload in "$chain" \
    "$base/libframe_pacer_gl_shim.so:$chain" \
    "$base/libframe_pacer_gl_shim-no-tail.so:$chain" \
    "$base/libframe_pacer_gl.so:$chain"; do
    timeout -k 1 10 env LD_PRELOAD="$preload" "$base/test-dlsym-next" "$base/liblookup-local.so"
done
