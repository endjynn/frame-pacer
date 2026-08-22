# Environment and build requirements

Frame-pacer is currently built from source on a systemd-based Linux desktop.
`make check` requires a C17 compiler with x86_64 and i386 support, Vulkan and
GL/EGL development headers for both architectures, a Vulkan loader, `make`,
`jq`, `xxd`, `file`, `glslangValidator`, and `spirv-val`.

The optional real presentation probe additionally needs Xlib headers and
x86_64/i386 X11 runtime libraries. ABI and embedded-helper checks use `nm`,
`readelf`, `strings`, and `pgrep`; normalized coverage uses GCC's `gcov` and
`jq`.

On Debian/Ubuntu-family systems, the principal package names are commonly:

| Package | Purpose |
| --- | --- |
| `gcc-multilib` | 32-bit x86 compilation |
| `libvulkan-dev`, `libvulkan-dev:i386` | Vulkan headers and linker files |
| `libegl1-mesa-dev` | EGL, OpenGL, and GLX headers |
| `glslang-tools` | GLSL-to-SPIR-V compilation |
| `spirv-tools` | SPIR-V validation |

Package names and multiarch setup vary by distribution. Review package
transactions before installing anything.

## Runtime inputs and paths

| Input | Meaning |
| --- | --- |
| `XDG_CONFIG_HOME` | Configuration base. If unset, frame-pacer uses `~/.config`. |
| `XDG_STATE_HOME` | Runtime-state base for logs and the transient external CPU-controller protocol. If unset, frame-pacer uses `~/.local/state`. |
| `FRAME_PACER_LOG=1` | Enables PID-qualified diagnostic logs for the target process only. |
| `ENABLE_FRAME_PACER_HUD=1` | Enables the installed Vulkan implicit layer. |
| `DISABLE_FRAME_PACER_HUD=1` | Vulkan-loader disable control declared by the manifest. |

The configuration file is
`${XDG_CONFIG_HOME}/frame-pacer/frame-pacer.conf`, or
`~/.config/frame-pacer/frame-pacer.conf` when `XDG_CONFIG_HOME` is unset.
Logs are written below `${XDG_STATE_HOME}/frame-pacer`, or
`~/.local/state/frame-pacer` when `XDG_STATE_HOME` is unset.
When the Steam Runtime requires the external CPU controller, its private
command/status files use the same directory and are removed during teardown.

For an i386 process on an NVIDIA system, frame-pacer may execute its embedded
x86_64 NVML telemetry helper from a sealed anonymous file. The helper uses a
private socket, exits with the owning process, and creates no configuration,
state, installed executable, service, or other persistent runtime file.
