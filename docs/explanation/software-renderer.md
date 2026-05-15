# Software Renderer

flux ships a CPU software renderer in `src/rhi/software/software_rhi.c`. It implements the full `flux_rhi_vtbl` without any GPU dependency, making it useful for headless servers, CI, debugging, and as a pixel-accurate reference for new backends.

## Capabilities

The software backend supports every vtable entry:

- Solid-colour triangle fill via barycentric edge-function rasterisation
- 13 blend modes (`SRC_OVER`, `PLUS`, `MULTIPLY`, `SCREEN`, `OVERLAY`, and the full Porter-Duff set)
- Linear and radial gradients (sampled per-pixel)
- Bilinear texture sampling for images and glyph quads
- Stencil-based even-odd path fill for multi-subpath shapes with holes
- Fringe anti-aliasing for path edges (0.5 px outward coverage ramp)
- Rectangular scissor clipping
- A8, BGRA8, and RGBA8 pixel formats with correct channel swizzle

## Architecture

The renderer maintains:

| Field | Purpose |
|---|---|
| `fb.pixels` | 32-bit RGBA colour buffer |
| `fb.stencil` | 8-bit stencil buffer for path fills |
| `pool` | Linear arena for per-frame vertex data |
| `textures` | Linked list of `sw_texture` objects |

All vertex data lives in the linear pool. `alloc_solid` and `alloc_image` simply bump a pointer. At the start of each frame the pool cursor is reset to zero — no individual frees are needed.

## Rasterisation pipeline

### Solid triangles

`raster_solid` walks the bounding box of each triangle and uses barycentric edge functions to decide whether a pixel centre is inside. If so, the pixel is blended with the draw colour using the current blend mode.

### Image / glyph quads

`draw_image` receives six vertices (two triangles) and a `sw_texture`. `raster_image` interpolates UV coordinates across the triangle, performs bilinear sampling, applies the tint colour, and blends the result into the framebuffer.

For `A8_UNORM` textures (glyph atlas), the single alpha channel is replicated to RGB before tint multiplication. This lets the same `draw_image` path serve both regular images and text glyphs.

### Path fill (stencil)

`sw_stencil_fill` increments the stencil value for every triangle that covers a pixel. After all path triangles are submitted, `sw_cover_solid` fills pixels where the stencil value is odd — the even-odd rule. The stencil is then cleared.

After the cover pass, `sw_draw_fringe` rasterises extra triangles that expand each path edge outward by 0.5 px. Vertex coverage is barycentrically interpolated from 1.0 (on the original edge) to 0.0 (on the outer boundary). The resulting alpha ramp is blended over the framebuffer, smoothing hard silhouette edges without affecting the interior.

## Role in the project

The software renderer is the **reference implementation**. When a new backend (e.g. Metal, WebGPU) is added, its output is validated against the software backend using golden-image tests. If the pixels differ, the new backend is wrong until proven otherwise.

Because the engine drives it through the same vtable as Vulkan, no engine code changes are needed to support it. This also means that offscreen rendering works on any machine, even inside Docker containers without `/dev/dri`.

## Limitations

- Single-threaded (no SIMD or multi-core parallelism yet)
- GPU images must be copied to CPU memory before sampling

These are acceptable for the intended use cases (CI, debugging, headless) but would be blockers for a production game engine.

## See also

- [RHI design](rhi-design.md) — why the vtable abstraction exists.
- [Algorithms](algorithms.md) — barycentric rasterisation, bilinear sampling, and stencil fill in detail.
- [Vulkan backend](vulkan-backend.md) — the GPU-accelerated counterpart.
- [ADR-0003: Renderer vtable abstraction](../adr/0003-renderer-vtable.md)
