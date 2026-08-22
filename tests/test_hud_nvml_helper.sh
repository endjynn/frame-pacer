#!/bin/sh
set -eu

probe=$(pwd)/build/hud-nvml-helper-probe-i386
pci=

for render_node in /sys/class/drm/renderD*; do
    test -e "$render_node/device/vendor" || continue
    test "$(sed -n '1p' "$render_node/device/vendor")" = 0x10de || continue
    pci=$(basename "$(readlink -f "$render_node/device")")
    break
done

if test -z "$pci"; then
    echo 'NVML helper target-host test skipped: no NVIDIA render node'
    exit 0
fi

"$probe" "$pci"

runtime=
for candidate in \
    "$HOME/.steam/debian-installation/steamapps/common/SteamLinuxRuntime_4/_v2-entry-point" \
    "$HOME/.steam/root/steamapps/common/SteamLinuxRuntime_4/_v2-entry-point"
do
    if test -x "$candidate"; then
        runtime=$candidate
        break
    fi
done

if test -z "$runtime"; then
    echo 'NVML helper Steam Runtime test skipped: runtime 4 is not installed'
    exit 0
fi

"$runtime" --verb=run "$probe" "$pci"

if pgrep -f '^frame-pacer-nvml-helper\(deleted\)\|^frame-pacer-nvml-helper ' >/dev/null 2>&1; then
    echo 'transient NVML helper remained after probe teardown' >&2
    exit 1
fi

echo 'NVML helper target-host and Steam Runtime tests passed'
