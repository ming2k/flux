# flux

flux is a low-level C11 2D graphics library for applications and shells that want explicit Vulkan-backed drawing without adopting a full UI toolkit.

## Quick Start

```bash
meson setup build -Dwayland=disabled
meson compile -C build
meson test -C build --suite unit
# Expected output: all unit tests pass
```

## Documentation

- [Full documentation](docs/index.md)
- [Getting Started Tutorial](docs/tutorials/01-getting-started.md)
- [API Reference](docs/reference/api.md)
- [Configuration Reference](docs/reference/configuration.md)
- [Architecture Overview](docs/explanation/architecture-overview.md)
- [Contributing](CONTRIBUTING.md)

## When to use this project

flux is a good fit if you need an explicit rendering substrate for paths, images, gradients, glyph runs, Wayland surfaces, Vulkan interop, or offscreen CPU-readable render targets.

Consider alternatives like Skia if you need a mature multi-backend 2D engine, Cairo if you need a stable broad-platform drawing API, or a retained UI toolkit if you need widgets, layout, accessibility, styling, and input policy.

## Status

Current version: **0.1.0**. flux is still pre-1.0: public APIs may change on minor releases, and golden-image tests plus performance baselines remain 1.0 work. See [release process](docs/dev/release-process.md) and [CHANGELOG.md](CHANGELOG.md).

## License

Apache-2.0 - see [LICENSE](LICENSE).
