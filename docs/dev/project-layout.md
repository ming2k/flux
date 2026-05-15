# Project Layout

This document is for contributors who need a quick map of the source tree.

| Path | Purpose |
|---|---|
| `include/flux/` | Public C headers. `flux.h` is the core API; `flux_vulkan.h` is the optional Vulkan interop header. |
| `src/resource/context.c` | Context lifecycle, logging, backend initialization entry points, and context-wide caches. |
| `src/state/canvas.c` | CPU-only command recording, paint defaults, transform stack, clip commands, and display-list ops. |
| `src/resource/image.c` | Image descriptors, CPU pixel copies, and GPU upload for renderer-owned images. |
| `src/geometry/path.c` | Path storage, bounds, matrix math, and path transforms. |
| `src/geometry/tess.c` | Polygon tessellation. |
| `src/geometry/stroker.c` | Stroke expansion for caps and joins. |
| `src/resource/text.c` | Glyph-run containers and glyph atlas population. |
| `src/surface.c` | Surface lifecycle, frame lifecycle, op execution, batching, clipping state, presentation, and offscreen readback. |
| `src/rhi/` | Render Hardware Interface (RHI) — the abstract vtable that decouples 2D logic from GPU APIs. |
| `src/rhi/software/` | Software (CPU) RHI implementation. |
| `src/rhi/vulkan/` | Vulkan RHI implementation. |
| `src/shaders/` | GLSL shaders compiled to SPIR-V at build time. |
| `examples/` | Offscreen example programs (`minimal.c`, `hello_rect.c`). |
| `tests/` | Unit and integration tests. |
| `build/` | Meson build output (git-ignored). |
| `docs/` | User, reference, design, ADR, and contributor documentation. |

## See also

- [Architecture overview](../explanation/architecture-overview.md)
- [Responsibility boundaries](../explanation/responsibility-boundaries.md)
- [Code style](code-style.md)
