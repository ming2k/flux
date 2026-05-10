# Capability Model

flux is a rendering substrate. The application owns scene objects, layout, input, invalidation, animation, asset loading, text shaping, and glyph rasterization. flux owns the final step: turning explicit 2D drawing commands into GPU work.

## Feature status

| Feature | Status | Notes |
|---|---|---|
| **Surfaces** | Implemented | Offscreen CPU-readable, caller-provided `VkSurfaceKHR` |
| **Frame lifecycle** | Implemented | Acquire canvas, record commands, present |
| **Command recording** | Implemented | `fx_canvas`: clear, fill, stroke, clip, transform, images, glyph runs |
| **Paths** | Implemented | Move, line, quad, cubic, arc, close, rect, bounds |
| **Path transforms** | Implemented | 2D affine matrix applied during recording |
| **Fill** | Implemented | Solid color and gradients; even-odd and non-zero fill rules |
| **Stroke** | Implemented | Caps, joins, miter limit, dash patterns |
| **Clipping** | Implemented | Scissor rects; stencil-based path clips compiled but not yet dispatched |
| **Images** | Implemented | Upload CPU pixels, draw textured quads |
| **Gradients** | Implemented | 2–4 stop linear and radial, converted to push constants |
| **Text** | Implemented | Caller-uploaded glyph bitmaps, dynamic GPU atlas, positioned glyph run rendering |
| **Glyph rasterization** | Not implemented | Caller rasterizes with FreeType/stb_truetype; flux accepts bitmaps |
| **Anti-aliasing** | Partial | Text AA via caller-provided coverage masks; path AA via high-precision flattening, fringe AA planned |
| **Blur** | Implemented | Separable Gaussian via `fx_mask_blur`; software backend fully functional, Vulkan pipeline compiled |
| **Render-to-texture** | Implemented | `fx_image_create_from_surface`: zero-copy offscreen→image in software backend |
| **Layers** | Partial | Offscreen surfaces + render-to-texture enable manual layer composition |
| **Compute** | Not implemented | No compute shaders |

## Translation model

```text
app/toolkit state → layout/shaping/assets → explicit flux draw calls → GPU
```

| Upstream output | flux input |
|---|---|
| Pixel buffer / decoded image | `fx_image` + `fx_draw_image` |
| Vector outlines | `fx_path` + `fx_fill_path` / `fx_stroke_path` |
| Rasterized glyph bitmaps | `fx_glyph_upload` + `fx_glyph_run` + `fx_draw_glyph_run` |
| Gradient stops and geometry | `fx_gradient` + `fx_paint` |

## What stays above flux

- Widget tree, layout engine, style system, input routing, focus handling, accessibility tree.
- UTF-8 paragraph shaping, line breaking, bidirectional text, font discovery, fallback policy.
- Glyph rasterization: flux accepts bitmaps, does not parse font files or render outlines.
- SVG/XML parser, CSS parser, image file decoder, icon theme resolver.
- Application-owned Vulkan resources (flux creates its own instance/device; interop is opt-in).

## See also

- [Responsibility boundaries](responsibility-boundaries.md) — module-level ownership contract.
- [ADR-0002: Keep flux as a low-level rendering substrate](../adr/0002-keep-flux-as-low-level-rendering-substrate.md)
