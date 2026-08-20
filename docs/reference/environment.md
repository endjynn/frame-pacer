# Build requirements

Frame-pacer is currently built from source on a systemd-based Linux desktop.
`make check` requires a C17 compiler with x86_64 and i386 support, Vulkan and
GL/EGL development headers for both architectures, a Vulkan loader, `make`,
`jq`, `xxd`, `file`, `glslangValidator`, and `spirv-val`.

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
