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

# Exercise the real policy that rejects anonymous memfd exec. Do not load or
# modify a profile; hosts without this optional policy still run the baseline.
if command -v aa-exec >/dev/null 2>&1 &&
    aa-exec -p bwrap -- /bin/true 2>/dev/null; then
    aa-exec -p bwrap -- "$probe" "$pci"
    for test in build/test-hud-nvml-client-i386 \
        build/test-hud-nvml-client-retry-i386 \
        build/test-hud-nvml-client-target-exit-i386; do
        aa-exec -p bwrap -- "$test"
    done
else
    echo 'NVML helper AppArmor test skipped: bwrap profile unavailable'
fi

runtime=
for candidate in \
    "$HOME/.local/share/umu/steamrt4/_v2-entry-point" \
    "$HOME/.steam/debian-installation/steamapps/common/SteamLinuxRuntime_4/_v2-entry-point" \
    "$HOME/.steam/root/steamapps/common/SteamLinuxRuntime_4/_v2-entry-point"; do
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

if pgrep -f '^frame-pacer-nvml-helper\(deleted\)\|^frame-pacer-nvml-helper \|^/lib64/ld-linux-x86-64.so.2 /proc/self/fd/3 --pid ' >/dev/null 2>&1; then
    echo 'transient NVML helper remained after probe teardown' >&2
    exit 1
fi

echo 'NVML helper target-host and Steam Runtime tests passed'
