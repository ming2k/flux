# flux

A 2D graphics foundation library written in C23 with software and
Vulkan backends. The public API is opaque-handle, refcounted, sized-
descriptor, allocator-pluggable, with first-class matrix/color/path
helpers and an immediate-mode canvas.

> **Status:** v0.2.0 — first shape-stable public surface. Pre-1.0;
> minor bumps may still introduce breaking changes, but only with
> notice. See [API stability](docs/reference/api-stability.md).

## Hello, flux

```c
#include <flux/flux.h>

int main(void) {
    flux_context *ctx = NULL;
    flux_context_create(NULL, &ctx);

    flux_surface *s = NULL;
    flux_surface_create_offscreen(ctx, 256, 256,
        FLUX_FMT_RGBA8_UNORM, FLUX_CS_SRGB, &s);

    flux_canvas *c = flux_surface_acquire(s);
    flux_canvas_clear(c, flux_color_rgba(0, 100, 200, 255));
    flux_surface_present(s);

    flux_surface_release(s);
    flux_context_release(ctx);
}
```

See [`examples/`](examples/) for the full programs (`minimal`,
`hello_rect`, `windowed` with Vulkan + GLFW).

## Quick start

```bash
meson setup build
meson compile -C build
meson test -C build --suite unit       # 13 unit tests, all green
./build/examples/minimal               # writes minimal.ppm
./build/examples/hello_rect            # writes hello_rect.ppm
```

## Documentation

- [Full documentation index](docs/index.md)
- [Getting started](docs/tutorials/01-getting-started.md)
- [Building an application](docs/tutorials/02-building-an-application.md)
- [API reference](docs/reference/api.md)
- [API stability and deprecation policy](docs/reference/api-stability.md)
- [Thread safety](docs/reference/thread-safety.md)
- [Architecture overview](docs/explanation/architecture-overview.md)
- [Contributing](CONTRIBUTING.md)

## License

GPL-3.0.
