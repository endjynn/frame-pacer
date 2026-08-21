# Steam integration

Frame-pacer has two intentionally separate activation paths. Keep them
separate: Vulkan can be enabled for Steam globally; the GLX/EGL shim is
per-game only.

## Vulkan implicit layer

After `make install`, set this in Steam's environment:

```text
ENABLE_FRAME_PACER_HUD=1
```

The user-local install puts both manifests in the Vulkan loader's standard
search path, so no layer-path variable is needed. Use your desktop
environment's normal Steam launcher configuration to set this variable. Do not
add `LD_PRELOAD`, `FRAME_PACER_LOG`, a wrapper, or any other frame-pacer
variable globally. The manifests declare
`DISABLE_FRAME_PACER_HUD` for Vulkan-loader compliance; leave it unset.

Changing a desktop launcher requires Steam to be fully stopped first. GNOME
caches desktop-launcher state, so after changing Steam's global launcher
environment, log out of GNOME and back in before validation.

## GLX/EGL shim

Only add this to the launch options of a game already known to present through
GLX or EGL:

```text
LD_PRELOAD=/absolute/path/to/frame-pacer/build/${LIB}/libframe_pacer_gl_shim.so %command%
```

Steam expands `${LIB}` to the correct architecture directory. Never set this
preload globally: even a lazy shim would still be mapped into unrelated Steam
processes. Add `FRAME_PACER_LOG=1` to the same per-game option only while
collecting diagnostics.

Steam caches per-game launch options. Fully exit Steam before changing one,
then restart it. A desktop-session re-login is not normally required for a
per-game-only edit.

## Rollback

To disable frame-pacer, remove the Vulkan environment variable from the Steam
launcher and remove the GL preload option from affected games. Then fully
restart Steam and run `make uninstall` to remove the installed Vulkan layer.
Removing `thread_cpu_limit` or setting it to `off` also removes the CPU
controller live; no permanent systemd service is installed.
