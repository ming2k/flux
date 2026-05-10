# Project Layout

This document is for contributors who need a quick map of the source tree.

| Path | Purpose |
|---|---|
| `include/flux/` | Public C headers. `flux.h` is the core API; `flux_vulkan.h` is the optional Vulkan interop header. |
| `src/context.c` | Context lifecycle, logging, backend initialization entry points, FreeType initialization, and context-wide caches. |
| `src/canvas.c` | CPU-only command recording, paint defaults, transform stack, clip commands, and display-list ops. |
| `src/image.c` | Image descriptors, CPU pixel copies, and GPU upload for renderer-owned images. |
| `src/path.c` | Path storage, bounds, matrix math, and path transforms. |
| `src/tess.c` | Polygon tessellation. |
| `src/stroker.c` | Stroke expansion for caps and joins. |
| `src/text.c` | Font handles, glyph-run containers, FreeType rasterization, and glyph atlas population. |
| `src/surface.c` | Surface lifecycle, frame lifecycle, op execution, batching, clipping state, presentation, and offscreen readback. |
| `src/vk/` | Vulkan helpers for device selection, memory, upload, swapchain, pipelines, render passes, and transient buffers. |
| `src/shaders/` | GLSL shaders compiled to SPIR-V at build time. |
| `examples/` | Example programs (none yet). |
| `tests/` | Unit and integration tests. |
| `scripts/` | Build and release helper scripts. |
| `docs/` | User, reference, design, ADR, and contributor documentation. |

## See also

- [Architecture overview](../explanation/architecture-overview.md)
- [Responsibility boundaries](../explanation/responsibility-boundaries.md)
- [Code style](code-style.md)
