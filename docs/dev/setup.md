# Developer Setup

This document is for contributors who will modify flux source code. If you only want to use flux, see the [Getting Started tutorial](../tutorials/01-getting-started.md) instead.

## Prerequisites

| Tool | Minimum | Why |
|---|---|---|---|
| GCC or Clang | GCC 15 / Clang 18 | C23 |
| Meson | 1.3 | build system |
| Ninja | 1.10 | build backend |
| Vulkan SDK | 1.2 | headers + loader |
| glslangValidator | any recent | shader compilation |
| python3 | 3.8 | bin2c script |

On Arch Linux:

```
pacman -S vulkan-devel glslang python meson ninja
```

On Ubuntu / Debian:

```
apt install libvulkan-dev glslang-tools \
            python3 meson ninja-build
```

## Build

```sh
meson setup build
ninja -C build
```

Useful option combinations:

```sh
# release build
meson setup build --buildtype=release

# build only the library, no examples
meson setup build -Dexamples=false

# compile-in Vulkan validation layer support
meson setup build -Dvalidation=enabled
```

Re-configuring without wiping the build directory:

```sh
meson configure build -Dexamples=true
ninja -C build
```

## Running tests

```sh
meson test -C build
```

All tests run offscreen and do not require a display server.

## Validation layers

```sh
FX_ENABLE_VALIDATION=1 meson test -C build
```

This enables `VK_LAYER_KHRONOS_validation` and a debug messenger that
routes validation messages through the library's log sink to `stderr`.
Any `ERROR`-severity validation message indicates a driver-contract
violation in the library.

Make sure the validation layers are installed:

```sh
# Arch
pacman -S vulkan-validation-layers

# Ubuntu
apt install vulkan-validationlayers
```

If the layer is not installed, flux logs at `INFO` level that
validation was requested but the layer was not found, then continues
without it.

## Debugging

### Verbose logging

`FX_LOGD` and `FX_LOGT` are elided in `NDEBUG` builds. To see debug or
trace output, build with `--buildtype=debug` (which disables `NDEBUG`)
and set `min_log_level` to `FX_LOG_DEBUG` or `FX_LOG_TRACE` in
`fx_context_desc`:

```c
fx_context_desc desc = {
    .min_log_level = FX_LOG_DEBUG,
};
```

The default `stderr` sink includes ISO-8601 timestamps and
`file:line` locations.

### GDB / LLDB

Both work normally — flux is a standard shared library. Build with
`--buildtype=debug` for full symbols and no optimisation:

```sh
meson setup build-debug --buildtype=debug
ninja -C build-debug
gdb --args build-debug/tests/test_foundation
```

### RenderDoc

RenderDoc can capture Vulkan frames from any flux application. Launch
via the RenderDoc GUI or:

```sh
renderdoccmd capture -c capture.rdc build/tests/test_offscreen
```

RenderDoc is particularly useful for inspecting the render pass clear
colour, the tessellated geometry, and atlas textures.

### Vulkan layers beyond validation

- `VK_LAYER_LUNARG_api_dump` — logs every Vulkan call to stdout.
  Useful for verifying submit ordering.
- `VK_LAYER_MESA_overlay` — GPU frame time overlay on the rendered
  output (Mesa only).

Activate any layer via the environment:

```sh
VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_api_dump meson test -C build
```

## Incremental development loop

For tight iteration on a single file:

```sh
ninja -C build src/libflux.so && meson test -C build
```

Ninja's dependency graph means only the modified TU recompiles.

## Shader development

Shaders live in `src/shaders/`. After editing a `.glsl` file:

```sh
ninja -C build       # runs glslangValidator and bin2c automatically
```

SPIR-V files land in `build/src/shaders/`. The `bin2c.py` script
embeds them into `shaders_embedded.c` as `static const uint32_t[]`
arrays.

To inspect a compiled shader:

```sh
spirv-dis build/src/shaders/solid.frag.spv
```

## Install

```sh
meson setup build --prefix=/usr/local
ninja -C build install
```

This installs `libflux.so`, the public headers under `include/flux/`,
and `flux.pc` for pkg-config. After installation:

```sh
pkg-config --cflags --libs flux
```

## Project layout

See [project-layout.md](project-layout.md) for a tour of the source tree.

## Before submitting a PR

- [ ] Relevant tests pass.
- [ ] Code style follows [code-style.md](code-style.md).
- [ ] Documentation is updated for user-visible behavior changes.
- [ ] `CHANGELOG.md` is updated when release notes are warranted.
- [ ] An ADR is added for significant architecture changes.
