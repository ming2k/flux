# How to Troubleshoot Common Issues

This guide assumes you have already tried [Getting Started](../tutorials/01-getting-started.md).

## When to use this

Use this page when a local build, test run, or example launch fails. If you are changing source code, also read [Developer setup](../dev/setup.md) and [Testing](../dev/testing.md).

## Prerequisites

- A configured Meson build directory
- The dependency packages listed in [Developer setup](../dev/setup.md)

## Common issues

- **`meson setup build` says the directory already exists**: reuse it with `meson compile -C build`, or reconfigure with `meson configure build <options>`.
- **Meson cannot find Wayland dependencies**: configure with `-Dwayland=disabled` for offscreen-only builds, or install `wayland-client`, `wayland-protocols`, and `wayland-scanner`.
- **`hello_rect` does not build**: examples require Wayland support; check that `-Dexamples=true` and `-Dwayland=enabled` or `auto` are active.
- **`hello_rect` cannot connect to a display**: run it inside an active Wayland compositor session with `WAYLAND_DISPLAY` set.
- **Vulkan validation layers are missing**: install `vulkan-validation-layers` or run without `FX_ENABLE_VALIDATION=1`.
- **Integration tests fail on a machine without a GPU**: run the unit suite first with `meson test -C build --suite unit`, or configure CI with a software Vulkan ICD such as lavapipe.

## Verification

After fixing the issue, rerun the smallest relevant check:

```bash
meson compile -C build
meson test -C build --suite unit
```
