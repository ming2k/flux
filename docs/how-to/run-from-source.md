# How to Run flux from Source

Use this guide when you want to build and run flux directly from the repository without installing it system-wide.

## Prerequisites

- GCC 12+ or Clang 15+
- Meson 1.3+
- Ninja 1.10+
- Vulkan 1.2 headers and loader
- `glslangValidator`

For distribution-specific package names, see [Developer setup](../dev/setup.md).

## Build

Clone the repository and configure the build:

```sh
meson setup build
```

If the build directory already exists, reconfigure instead:

```sh
meson setup --reconfigure build
```

Compile the library and tests:

```sh
meson compile -C build
```

## Run tests

Run the unit test suite:

```sh
meson test -C build --suite unit
```

Run all tests including integration tests:

```sh
meson test -C build
```

All tests run offscreen and do not require a display server.

## Run from the build directory

Applications can link against the uninstalled shared library using the build directory:

```sh
cd build
cc ../examples/minimal.c -o minimal \
    -I../include $(pkg-config --cflags vulkan) \
    -Lsrc -lflux -Wl,-rpath,'$ORIGIN/src'
```

Or run examples directly if they were built:

```sh
meson compile -C build
./build/examples/minimal       # writes minimal.ppm
./build/examples/hello_rect    # writes hello_rect.ppm
```

## Quick iteration

For tight development loops, build only what changed and run affected tests:

```sh
ninja -C build && meson test -C build
```

To force a full rebuild from scratch:

```sh
meson setup --wipe build
meson compile -C build
```

## What's next

- [How to link flux](../how-to/link-flux.md)
- [How to draw basic shapes](../how-to/draw-basic-shapes.md)
- [Developer setup](../dev/setup.md)
