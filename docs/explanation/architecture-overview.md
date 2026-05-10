# Architecture Overview

flux is organised into five layers, each with a clear responsibility:

```
┌─────────────────────────────────────┐
│  API Layer       (flux.h, surface.c)│  User-facing entry points
├─────────────────────────────────────┤
│  State Layer     (state/)           │  Recording: display lists, transforms, clip
├─────────────────────────────────────┤
│  Geometry Layer  (geometry/)        │  Paths, Bézier flattening, tessellation, strokes
├─────────────────────────────────────┤
│  Renderer Layer  (renderer/)        │  Backend vtable + Software + Vulkan backends
├─────────────────────────────────────┤
│  Math Layer      (math/)            │  Matrices, rectangles, arena allocator
└─────────────────────────────────────┘
```

## Layer responsibilities

### Math layer (`src/math/`)

Pure computational primitives with no knowledge of rendering or geometry:

| Module | Contents |
|---|---|
| `matrix.h/c` | 2D affine transform: multiply, transform point, identity check |
| `rect.h` | Rectangle helpers: area check, point containment |
| `arena.h/c` | Bump allocator for per-frame temporary geometry |

### Geometry layer (`src/geometry/`)

Path construction and decomposition. Depends on the math layer, but not on any rendering state or backend:

| Module | Contents |
|---|---|
| `path.c` | Verb/point stream, Bézier flattening, arc-to-cubic, subpath iteration |
| `tess.c` | Ear-clipping polygon triangulation |
| `stroker.c` | Polyline → triangle mesh with caps and joins |

Exports through `geometry/geometry.h`.

### State layer (`src/state/`)

The recording half of the two-phase architecture. Builds a display list of ops and manages the transform stack during `fx_surface_acquire`:

| Module | Contents |
|---|---|
| `canvas.c` | Op recording: fill rect, fill path, stroke path, draw image, draw glyphs, clip ops |
| `state.h` | `fx_op`, `fx_op_kind`, and canvas helpers |

### Renderer layer (`src/renderer/`)

**This is the architectural keystone.** The `fx_renderer` vtable abstracts all backend-specific rendering behind a single interface:

```
fx_renderer_vtbl {
    begin_frame, begin_pass, end_pass, submit
    alloc_solid, alloc_image
    draw_solid, flush_solid, draw_image, draw_text, draw_gradient
    scissor, stencil_clear, stencil_fill, stencil_ref
    cover_solid, cover_gradient
    texture_alloc, texture_free, texture_update
    read_pixels
}
```

With this vtable, the execution engine (`engine.c`) has **zero** knowledge of Vulkan or software pixel buffers. Adding a new backend means implementing the vtable — the entire stack above it remains unchanged.

Current backends:

| Backend | File | Description |
|---|---|---|
| **Software** | `renderer/software.c` | CPU scanline rasteriser with stencil buffer (~800 LOC). Proves the vtable fully. |
| **Vulkan** | `renderer/vulkan/` | Stub ready for porting existing Vulkan code behind the vtable. |

### Execution engine (`engine.c`)

Backend-agnostic op executor. Consumes a recorded display list and calls the renderer vtable for every op. Handles:

- Path → polyline flattening (delegates to geometry layer)
- Polygon triangulation (delegates to geometry layer)
- Stroke expansion (delegates to geometry layer)
- Stencil-based multi-subpath path fill (renders fill-stencil + cover through vtable)
- Clipping (scissor + stencil through vtable)

### Resource layer (`src/resource/`)

Lifecycle-managed objects: context, images, glyph runs.

## Data flow: two-phase architecture

```
Phase 1 — Recording (CPU, immediate)
=====================================
fx_surface_acquire() → fx_canvas*
    │
    ├── fx_fill_rect()        → append FX_OP_FILL_RECT
    ├── fx_fill_path()        → append FX_OP_FILL_PATH
    ├── fx_stroke_path()      → append FX_OP_STROKE_PATH
    ├── fx_draw_image()       → append FX_OP_DRAW_IMAGE
    ├── fx_draw_glyph_run()   → append FX_OP_DRAW_GLYPHS
    ├── fx_clip_rect()        → append FX_OP_CLIP_RECT
    └── fx_clip_path()        → append FX_OP_CLIP_PATH

Phase 2 — Execute (at fx_surface_present)
==========================================
fx_engine_execute(canvas, renderer)
    │
    ├── For each op:
    │   ├── Tessellate / flatten paths (geometry layer)
    │   ├── Allocate vertices via renderer->alloc_solid/alloc_image
    │   ├── Issue draws via renderer->draw_solid/draw_image/etc.
    │   └── Manage clip state via renderer->scissor/stencil_*
    │
    └── renderer->submit()    (present to screen or make pixels readable)
```

## Key design decisions

1. **Vtable at the renderer boundary** — the execution engine never calls a graphics API directly. Every pixel-related operation goes through `fx_renderer_vt`. This is the separation that enables multiple backends.

2. **Opaque resource handles** — `fx_r_buffer` and `fx_r_texture` are interpreted only by the renderer. The engine allocates vertices through the renderer without knowing if they live in a Vulkan VkBuffer or a malloc'd array.

3. **Batching lives in the renderer** — consecutive same-color solid draws are batched by the renderer internally. The engine just issues `draw_solid()` calls; the renderer decides when to flush.

4. **Two-phase recording/execution preserved** — the existing architectural strength (CPU records while GPU renders) is maintained. The renderer vtable simply makes the "execute" phase backend-swappable.

## See also

- [Rendering pipeline](rendering-pipeline.md) — detailed op-by-op walkthrough.
- [ADR-0003: Renderer vtable abstraction](../adr/0003-renderer-vtable.md) — decision record.
- [Graphics foundations](graphics-foundations.md) — prerequisite hardware concepts.
