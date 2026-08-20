# Steam integration

Frame-pacer has two intentionally separate activation paths. Keep them
separate: Vulkan can be enabled for Steam globally; the GLX/EGL shim is
per-game only.

## Vulkan implicit layer

After building, point Steam's environment at both generated manifest
directories:

```text
VK_ADD_IMPLICIT_LAYER_PATH=/absolute/path/to/frame-pacer/build/x86_64/implicit_layer:/absolute/path/to/frame-pacer/build/i386/implicit_layer
ENABLE_FRAME_PACER_HUD=1
```

Use your desktop environment's normal Steam launcher configuration to set
these two variables. Do not add `LD_PRELOAD`, `FRAME_PACER_LOG`, a wrapper, or
any other frame-pacer variable globally. The manifests declare
`DISABLE_FRAME_PACER_HUD` for Vulkan-loader compliance; leave it unset.

Changing a desktop launcher requires Steam to be fully stopped first. Some
desktop environments cache launcher entries, so log out and back in before
trusting an edited launcher.

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

To disable frame-pacer, remove the two Vulkan environment variables from the
Steam launcher and remove the GL preload option from affected games. Then fully
restart Steam. Removing `thread_cpu_limit` or setting it to `off` also removes
the CPU controller live; no permanent systemd service is installed.
