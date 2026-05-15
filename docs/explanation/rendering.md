# Rendering

This document describes the rendering subsystem: tessellation, batching, shaders, and anti-aliasing. For the end-to-end pipeline from recording to presentation, see [Rendering pipeline](rendering-pipeline.md).

## Core Design: Mesh-based Tessellation

flux uses a **mesh-based tessellation** approach. Integrated GPUs benefit from low fragment costs and consistent driver behavior. Anti-aliasing is achieved through geometry-based coverage rather than MSAA.

## Curves and Flattening

Quadratic and cubic Béziers are flattened to line segments by recursive midpoint subdivision.
- **Scale-Aware:** Flattening happens in device space after the current transformation matrix is applied.
- **Precision:** The error bound is fixed at `0.25px` in device coordinates, ensuring curves remain smooth regardless of the logical scale.

See [Bézier curves](bezier-curves.md) for the full algorithm.

## Strokes

Strokes are expanded CPU-side into fill polygons by the stroker (`src/geometry/stroker.c`).
- **Caps:** `BUTT`, `SQUARE`, and `ROUND`.
- **Joins:** `MITER`, `ROUND`, and `BEVEL`.
- **Miter Limit:** Implemented with automatic fallback to Bevel joins to prevent geometry spikes at sharp angles.

## Text Rendering

Text is rendered as textured quads from a dynamic glyph atlas:

1. **Caller rasterization:** The caller rasterizes glyph outlines into 8-bit alpha masks using FreeType, stb_truetype, or any rasterizer.
2. **Upload:** `fx_glyph_upload` copies the bitmap into a 2048×2048 `A8_UNORM` GPU atlas texture.
3. **Cache hit:** Subsequent draws of the same glyph ID reuse the atlas entry.
4. **Execution:** Each glyph in a run is drawn as a textured quad. The fragment shader multiplies the paint color by the atlas alpha channel.

See [Text rendering pipeline](text-rendering-pipeline.md) for the full stack.

## Memory and Batching

### Per-Frame Ring Buffers

flux uses a per-frame linear allocator (`fx_vbuf_pool`) for vertex data.
- **No Static Limits:** The buffer grows dynamically by doubling its size if the current frame complexity exceeds its capacity.
- **Zero Stall:** Two frames are kept in flight; while the GPU is rendering frame N, the CPU can begin command recording for frame N+1.

### Automatic Batching

Sequential operations are grouped into the minimum possible number of Vulkan draw calls.
- **Grouping Criteria:** Sequential ops that share the same `fx_paint` color and pipeline type (e.g., multiple `fill_path` calls with the same color).
- **Flushing:** A batch is flushed when a pipeline state change is required (e.g., switching from Path to Image) or when the paint properties change.

## Pipelines and Shaders

| Pipeline | Purpose | Blend Mode |
|---|---|---|
| **Solid** | Standard path fills and strokes. | SRC_OVER |
| **Image** | Textured quads for `fx_image`. | SRC_OVER |
| **Text** | Alpha-blended glyph quads. | SRC_OVER |
| **Gradient** | Linear and radial gradients. | SRC_OVER |

Shaders are written in GLSL and compiled to SPIR-V at build time. No runtime compilation is required.

## Anti-Aliasing (AA)

- **Text AA:** Provided by caller rasterization (FreeType coverage masks).
- **Path AA:** Currently implemented through high-precision flattening (0.25px tolerance). Future versions will implement "fringe" geometry AA for paths to achieve sub-pixel smoothness without MSAA.

## Status

As of **v0.1.0**, the solid-color, image, text, gradient, and stencil pipelines are compiled. Solid, image, text, and gradient are wired into the recorder. Path flattening, stroking, and simple polygon triangulation are performed on the CPU, with the resulting meshes batched and submitted to the GPU. Clipping is scissor-bound; stencil-based exact path clip is compiled but not yet dispatched.

## See also

- [Rendering pipeline](rendering-pipeline.md) — end-to-end data flow.
- [Why GPUs use triangles](why-triangles.md) — why 2D drawing commands often become triangle meshes.
- [Bézier curves](bezier-curves.md) — curve mathematics.
- [Text rendering pipeline](text-rendering-pipeline.md) — text stack.
- [ADR-0002: Keep flux as a low-level rendering substrate](../adr/0002-keep-flux-as-low-level-rendering-substrate.md)
