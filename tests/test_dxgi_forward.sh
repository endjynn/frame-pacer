#!/bin/sh
set -eu
root=$(pwd)
proton_root=${FRAME_PACER_PROTON_ROOT:-"$HOME/.steam/debian-installation/steamapps/common/Proton - Experimental"}
wine="$proton_root/files/bin/wine"
test -x "$wine" || {
    echo "set FRAME_PACER_PROTON_ROOT to the Proton runtime to test" >&2
    exit 77
}
prefix="$root/build/dxgi-proxy-probe-prefix"
path=$(printf '%s' "$root/build/windows/x86_64" | sed 's|/|\\\\|g; s|^|Z:|')
WINEPREFIX="$prefix" WINEARCH=win64 WINEDEBUG=-all WINEPATH="$path" WINEDLLOVERRIDES='dxgi=n,b' "$wine" "$root/build/windows/dxgi-proxy-client/dxgi_proxy_probe.exe"
