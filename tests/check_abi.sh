#!/bin/sh
set -eu

libraries='libVkLayer_frame_pacer.so libframe_pacer_gl.so libframe_pacer_gl_shim.so'
architectures='x86_64 i386'

temporary_directory=$(mktemp -d)
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM

for library in $libraries; do
    baseline="tests/abi/$library.symbols"

    for architecture in $architectures; do
        actual="$temporary_directory/$library.$architecture.symbols"
        nm -D --defined-only "build/$architecture/$library" |
            awk '$3 !~ /^__gcov_/ && $3 != "mangle_path" { print $2, $3 }' > "$actual"

        if ! diff -u "$baseline" "$actual"; then
            echo "ABI mismatch: build/$architecture/$library" >&2
            exit 1
        fi
    done
done

echo 'ABI symbol baselines passed for x86_64 and i386'
