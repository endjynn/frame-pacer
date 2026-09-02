# Build requirements and environment

This page is the package and environment reference for source builds. Most
users only need it while installing frame-pacer or diagnosing a failed build.

## Build requirements

Frame-pacer needs:

- GCC with x86_64 and i386 (`-m32`) support.
- C17 standard library development files for both architectures.
- Vulkan headers and loader development files for both architectures.
- OpenGL, GLX, and EGL headers and development files for both architectures.
- GNU Make, `jq`, `xxd`, and `file`.
- `glslangValidator` and `spirv-val`.
- Standard binary tools including `nm`, `readelf`, and `strings`.
- For release packages, GNU `tar`, `xz`, `sha256sum`, `strip`, Python 3, and
  PyYAML.

Common Debian/Ubuntu package names include:

| Package | Provides |
| --- | --- |
| `build-essential` | Compiler, standard development files, and Make |
| `gcc-multilib` | 32-bit compiler support |
| `libvulkan-dev`, `libvulkan-dev:i386` | Vulkan development files |
| `libegl1-mesa-dev` | OpenGL and EGL development files |
| `jq`, `xxd`, `file` | Build and validation utilities |
| `glslang-tools`, `spirv-tools` | Shader compiler and validator |

Package names and multiarch setup differ by distribution. If `make check`
reports a missing header, library, or command, install the matching package
for both 64-bit and 32-bit builds where your distribution separates them.

Live Vulkan, GLX, and EGL presentation probes additionally need X11
development files and access to a graphical session. They are development
checks and are not part of normal installation.

## Runtime environment

| Variable | Purpose |
| --- | --- |
| `ENABLE_FRAME_PACER=1` | Activates frame-pacer's installed Vulkan layer. |
| `DISABLE_FRAME_PACER=1` | Prevents the Vulkan loader from activating frame-pacer. Normal users should leave it unset. |
| `FRAME_PACER_LOG=1` | Enables diagnostic logging when the process creates/presents rendering work or hits an actionable initialization failure. |
| `XDG_CONFIG_HOME` | Changes the configuration base from `~/.config`. |
| `XDG_STATE_HOME` | Changes the runtime-state and log base from `~/.local/state`. |

The configuration path is `$XDG_CONFIG_HOME/frame-pacer/frame-pacer.conf`, or
`~/.config/frame-pacer/frame-pacer.conf` when `XDG_CONFIG_HOME` is unset.
Runtime logs and temporary controller state use `$XDG_STATE_HOME/frame-pacer`,
or `~/.local/state/frame-pacer` when `XDG_STATE_HOME` is unset.

## Installation variables

Package maintainers can override:

| Make variable | Purpose |
| --- | --- |
| `PREFIX` | Installation prefix; defaults to `~/.local`. |
| `DESTDIR` | Staging root for package builds. |
| `STRIP` | Binary stripping command used for release packages. |
| `SOURCE_DATE_EPOCH` | Optional normalized release timestamp; defaults to the release commit time. |
