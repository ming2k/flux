# Text Rendering Pipeline

This document explains how text becomes pixels in flux, where the library sits in the stack, and why the pipeline is split the way it is.

## The full pipeline

```text
Caller layer
------------
UTF-8 text string
      |
      v
[Font discovery]        <-- Fontconfig, system APIs
      |
      v
[Shaping]               <-- HarfBuzz
      |  UTF-8 → glyph IDs + positions + advances
      v
[Rasterization]         <-- FreeType / stb_truetype / caller's rasterizer
      |  Glyph outlines → 8-bit alpha masks
      v
[Glyph upload]          <-- fx_glyph_upload (boundary)

flux layer
----------
      |
      v
[Atlas packing]         <-- flux shelf allocator
      |  Masks → GPU texture (A8_UNORM)
      v
[GPU execution]         <-- flux Vulkan backend
      |  Textured quads → blended fragments
      v
Pixels on screen
```

## What the caller owns

flux stops at the glyph bitmap boundary. Everything upstream belongs to the application or a text layout library:

| Stage | Responsibility | Typical library |
|---|---|---|
| **Font discovery** | Find the right font file for a given family, weight, and style. Handle fallback when the primary font lacks a glyph. | Fontconfig, system APIs |
| **Itemization** | Split text by script, direction, and font. A single paragraph may contain Latin, Arabic, and emoji — each needs different fonts and shaping parameters. | HarfBuzz, Pango |
| **Shaping** | Convert character sequences into positioned glyph IDs. This includes kerning, ligatures, and complex-script reordering. | HarfBuzz |
| **Layout** | Line breaking, paragraph alignment, text wrapping, and baseline computation. | Pango, custom layout engine |
| **Rasterization** | Convert glyph outlines into 8-bit alpha masks at the requested size. | FreeType, stb_truetype |
| **Caching** | Deciding when to re-rasterize text, how long to keep glyph runs alive, and invalidation on font or content changes. | Application |

## What flux owns

Once the caller has rasterized a glyph to a bitmap, flux takes over:

| Stage | Responsibility | Implementation |
|---|---|---|
| **Atlas management** | Pack glyph bitmaps into a 2048×2048 `A8_UNORM` GPU texture. Evict and reuse when full. | `src/text.c` |
| **Rendering** | Draw each glyph as a textured quad. The fragment shader multiplies the paint color by the atlas alpha. | `src/surface.c` + text.frag |

## The atlas in detail

The glyph atlas is the key to fast text rendering:

1. **Upload** — When a glyph is needed, the caller rasterizes it and uploads via `fx_glyph_upload`. The bitmap is copied into the atlas texture and uploaded to the GPU.
2. **Cache hit** — Subsequent draws of the same glyph ID reuse the atlas entry. No CPU rasterization, no GPU upload.
3. **Eviction** — When the atlas fills, flux logs a warning, waits for in-flight frames to finish, clears every entry, and starts fresh. The next frame must re-upload any glyphs that are still needed.
4. **Rejection** — Glyphs larger than 2046 pixels are rejected upfront. This prevents a single giant glyph from evicting the entire atlas.

The atlas is shared across all surfaces in a context. Two surfaces drawing the same glyph ID share one atlas entry.

## Concrete scenario: a button label

Imagine a UI toolkit rendering a button with the label "Save":

```text
Application layer:
  1. Fontconfig resolves "Inter Medium" → /usr/share/fonts/inter/Inter-Medium.ttf
  2. HarfBuzz shapes "Save" → glyph IDs [55, 68, 83, 72]
  3. FreeType rasterizes each glyph at 16px → 8-bit alpha masks
  4. Layout engine positions the run at (24.0, 40.0)

Boundary:
  5. Toolkit uploads each glyph bitmap via fx_glyph_upload(ctx, glyph_id, ...)
  6. Toolkit creates fx_glyph_run with the four positioned glyphs

flux layer:
  7. fx_draw_glyph_run(c, run, 24.0f, 40.0f, &paint)
  8. Canvas records the draw op (no GPU work yet)
  9. fx_surface_present triggers execution:
     a. Check atlas for glyph IDs 55, 68, 83, 72
     b. Emit four textured quads (one per glyph)
     c. GPU blends the text color with atlas alpha
```

## Why this split exists

flux could theoretically own font loading and rasterization. It does not, for three reasons:

1. **Policy surface** — Text layout involves many application-specific decisions: wrapping rules, truncation, alignment, RTL handling, emoji policy. A graphics library should not force these choices on callers.

2. **Dependency weight** — Full text rendering requires font parsing, Unicode data, and rasterization. flux keeps its dependency list small: Vulkan only.

3. **Flexibility** — Callers can use any rasterizer they prefer (FreeType, stb_truetype, etc.) or skip rasterization entirely for pre-baked font textures.

The practical contract is: **rasterize above, pack and render below**. The caller produces `(glyph_id, bitmap, x, y)` tuples; flux turns them into antialiased pixels.

## Performance notes

- **Atlas warm-up** — The first frame that uses a new glyph pays the upload cost. Subsequent frames are cache hits.
- **Batching** — Consecutive `fx_draw_glyph_run` calls with the same paint color are batched into a single Vulkan draw call.
- **Memory** — The atlas texture is 4 MB (2048 × 2048 × 1 byte). One atlas per context, shared across all surfaces.
- **Glyph ID space** — The caller owns the glyph ID namespace. Different fonts/sizes should use distinct ID ranges.

## See also

- [How to render text](../how-to/render-text.md) — code examples for uploading and drawing.
- [Rendering](rendering.md) — general rendering architecture including the text pipeline.
- [Responsibility boundaries](responsibility-boundaries.md) — why text rasterization stays above flux.
