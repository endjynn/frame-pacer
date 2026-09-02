#!/bin/sh
set -eu

fail()
{
    printf 'frame-pacer package: %s\n' "$1" >&2
    exit 1
}

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
version=$(sh "$root/packaging/read-version.sh" "$root/VERSION")

payload="$root/build/install-payload"
for required in \
    x86_64/libVkLayer_frame_pacer.so \
    i386/libVkLayer_frame_pacer.so \
    x86_64/libframe_pacer_gl.so \
    i386/libframe_pacer_gl.so \
    x86_64/libframe_pacer_gl_shim.so \
    i386/libframe_pacer_gl_shim.so \
    frame-pacer-thread-cpu-controller \
    VkLayer_frame_pacer_implicit.json.in \
    VERSION
do
    [ -f "$payload/$required" ] && [ ! -L "$payload/$required" ] ||
        fail "install payload is missing $required"
done

epoch=${SOURCE_DATE_EPOCH:-}
if [ -z "$epoch" ]; then
    epoch=$(git -c safe.directory="$root" -C "$root" log -1 --format=%ct 2>/dev/null) ||
        fail 'SOURCE_DATE_EPOCH is unset and the release commit is unavailable'
fi
case "$epoch" in
    ''|*[!0-9]*) fail 'SOURCE_DATE_EPOCH must be a non-negative integer' ;;
esac

package="frame-pacer-$version-linux-x86_64-multilib"
stage="$root/build/release-stage"
package_dir="$stage/$package"
dist="$root/build/dist"
archive="$dist/$package.tar.xz"
checksum="$archive.sha256"

rm -rf -- "$stage"
install -d -m 0755 \
    "$package_dir/payload/x86_64" \
    "$package_dir/payload/i386" \
    "$dist"
install -m 0644 \
    "$root/LICENSE" \
    "$root/README.md" \
    "$root/CHANGELOG.md" \
    "$root/VERSION" \
    "$package_dir/"
install -m 0755 "$root/packaging/install.sh" "$package_dir/install.sh"
install -m 0755 "$root/packaging/uninstall.sh" "$package_dir/uninstall.sh"
install -m 0644 "$payload/VkLayer_frame_pacer_implicit.json.in" \
    "$package_dir/payload/VkLayer_frame_pacer_implicit.json.in"
install -m 0644 "$payload/VERSION" "$package_dir/payload/VERSION"
install -m 0755 "$payload/frame-pacer-thread-cpu-controller" \
    "$package_dir/payload/frame-pacer-thread-cpu-controller"

for architecture in x86_64 i386
do
    for library in \
        libVkLayer_frame_pacer.so \
        libframe_pacer_gl.so \
        libframe_pacer_gl_shim.so
    do
        install -m 0755 "$payload/$architecture/$library" \
            "$package_dir/payload/$architecture/$library"
        "${STRIP:-strip}" --strip-unneeded \
            "$package_dir/payload/$architecture/$library"
    done
done
"${STRIP:-strip}" --strip-unneeded \
    "$package_dir/payload/frame-pacer-thread-cpu-controller"

if find "$package_dir" -type l -print -quit | grep -q .; then
    fail 'release tree contains a symbolic link'
fi
if find "$package_dir" -type f -perm /0022 -print -quit | grep -q .; then
    fail 'release tree contains a group- or world-writable file'
fi

find "$package_dir" -exec touch -h -d "@$epoch" {} +
temporary="$archive.tmp-$$"
rm -f -- "$temporary" "$checksum.tmp-$$"
tar --sort=name --format=gnu --owner=0 --group=0 --numeric-owner \
    --mtime="@$epoch" --mode='u+rwX,go+rX,go-w' \
    -C "$stage" -cf - "$package" |
    xz -9e --threads=1 > "$temporary"
[ "$(wc -c < "$temporary")" -le 52428800 ] ||
    fail 'release archive exceeds 50 MiB'
mv -f -- "$temporary" "$archive"
(
    cd "$dist"
    sha256sum "${archive##*/}" > "${checksum##*/}.tmp-$$"
    mv -f -- "${checksum##*/}.tmp-$$" "${checksum##*/}"
)

printf '%s\n' "$archive" "$checksum"
