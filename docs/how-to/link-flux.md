# How to Link flux

This guide assumes you have already built flux once. See [Getting Started](../tutorials/01-getting-started.md) if you need a first local build.

## When to use this

Use this page when an application needs to consume flux as a shared library, static library, or Meson subproject.

## Dynamic Link

The default Meson build produces a shared library:

```sh
meson setup build
meson compile -C build
meson install -C build
```

Compile an application against an installed flux:

```sh
cc app.c -o app $(pkg-config --cflags --libs flux)
```

At runtime the dynamic loader must be able to find `libflux.so`, either through
the install prefix, `ldconfig`, `LD_LIBRARY_PATH`, or an rpath chosen by the
application build.

## Static Link

Build and install a static flux library:

```sh
meson setup build-static -Ddefault_library=static
meson compile -C build-static
meson install -C build-static
```

Compile with static dependency flags:

```sh
cc app.c -o app $(pkg-config --cflags --libs --static flux)
```

Static linking pulls more transitive dependencies into the application link
line, including Vulkan, FreeType, HarfBuzz, pthreads, math, and optional
Wayland libraries when Wayland support is enabled.

## Meson Subproject

If flux is vendored as a Meson subproject, consume its dependency object:

```meson
flux_proj = subproject('flux')
flux_dep = flux_proj.get_variable('flux_dep')

executable('app', 'app.c', dependencies : [flux_dep])
```

Use this when the application wants to control whether flux is built as static
or shared from the top-level `default_library` option.

## Verification

Compile a small application that includes `flux/flux.h` and creates an `fx_context`. The compile and link step should complete without unresolved `fx_*` symbols.
