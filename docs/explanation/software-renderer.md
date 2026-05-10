# Software Renderer

flux ships a CPU software renderer that implements the full `fx_renderer` vtable. It rasterises triangles to a pixel buffer in system memory, proving the renderer abstraction works without any GPU dependency.

## Capabilities

- Solid-colour triangle fill via scanline edge-walking with top-left fill convention
- Linear and radial gradient fill (up to 4 colour stops)
- Bilinear texture sampling for image blitting
- Glyph atlas text rendering (alpha channel × tint colour)
- Stencil buffer for multi-subpath path fills and arbitrary clip paths
- Premultiplied alpha blending (`ONE, ONE_MINUS_SRC_ALPHA`)
- Scissor rectangle clipping

## Limitations

- Single-threaded (all triangles rasterised on one CPU core)
- No anti-aliasing (hard edges)
- No sub-pixel precision (vertex positions truncated to integer)
- No SIMD acceleration (uses scalar float/int operations)
- Image rendering requires the image pixel data to be in CPU memory (no GPU texture sampling)

## Implementation

The software renderer is in `src/renderer/software.c` (~800 lines). It uses:

| Technique | Purpose |
|---|---|
| Edge function rasterisation | Triangle coverage testing (`w = A·x + B·y + C`) |
| Top-left fill convention | Watertight abutting triangles (no double-pixels) |
| Bounding box culling | Skip large empty areas outside the triangle |
| Stencil increment+clamp | Even-odd path fill for multi-subpath / hole paths |
| Linear interpolation | Gradient parameter evaluation and bilinear texture sampling |

## Architectural role

The software renderer proves that the `fx_renderer` vtable is independently implementable. The execution engine (`engine.c`) drives it through exactly the same vtable it would use for Vulkan — no special-casing, no `#ifdef` branches.

This means:
- The engine can be unit-tested without a GPU
- Offscreen rendering (headless servers, CI) works out of the box
- Porting flux to a new platform starts with getting the software renderer working, then adding a hardware backend

## See also

- [Architecture overview](architecture-overview.md) — the five-layer model.
- [ADR-0003: Renderer vtable abstraction](../adr/0003-renderer-vtable.md) — why we introduced the vtable.
- [Rendering pipeline](rendering-pipeline.md) — end-to-end data flow.
