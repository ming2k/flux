# flux

`flux` is a pure 2D graphics core library.

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
- [Building an Application](docs/tutorials/02-building-an-application.md)
- [API Reference](docs/reference/api.md)
- [Configuration Reference](docs/reference/configuration.md)
- [Thread Safety](docs/reference/thread-safety.md)
- [Platform Support](docs/reference/platforms.md)
- [Architecture Overview](docs/explanation/architecture-overview.md)
- [Contributing](CONTRIBUTING.md)

