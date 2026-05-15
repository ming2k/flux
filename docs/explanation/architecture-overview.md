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
│  RHI Layer        (rhi/)                  │  Backend vtable + Software + Vulkan backends
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

The recording half of the two-phase architecture. Builds a display list of ops and manages the transform stack during `flux_surface_acquire`:

| Module | Contents |
|---|---|
| `canvas.c` | Op recording: fill rect, fill path, stroke path, draw image, draw glyphs, clip ops |
| `state.h` | `flux_op`, `flux_op_kind`, and canvas helpers |

### RHI layer (`src/rhi/`)

**This is the architectural keystone.** The `flux_rhi_device` vtable abstracts all backend-specific rendering behind a single interface:

```
flux_rhi_vtbl {
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
| **Software** | `rhi/software/software_rhi.c` | CPU scanline rasteriser with stencil buffer (~800 LOC). Proves the vtable fully. |
| **Vulkan** | `rhi/vulkan/` | Stub ready for porting existing Vulkan code behind the vtable. |

### Execution engine (`engine.c`)

Backend-agnostic op executor. Consumes a recorded display list and calls the RHI vtable for every op. Handles:

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
flux_surface_acquire() → flux_canvas*
    │
    ├── flux_fill_rect()        → append FLUX_OP_FILL_RECT
    ├── flux_fill_path()        → append FLUX_OP_FILL_PATH
    ├── flux_stroke_path()      → append FLUX_OP_STROKE_PATH
    ├── flux_draw_image()       → append FLUX_OP_DRAW_IMAGE
    ├── flux_draw_glyph_run()   → append FLUX_OP_DRAW_GLYPHS
    ├── flux_clip_rect()        → append FLUX_OP_CLIP_RECT
    └── flux_clip_path()        → append FLUX_OP_CLIP_PATH

Phase 2 — Execute (at flux_surface_present)
==========================================
flux_engine_execute(canvas, rhi)
    │
    ├── For each op:
    │   ├── Tessellate / flatten paths (geometry layer)
    │   ├── Allocate vertices via rhi->alloc_solid/alloc_image
    │   ├── Issue draws via rhi->draw_solid/draw_image/etc.
    │   └── Manage clip state via rhi->scissor/stencil_*
    │
    └── rhi->submit()    (present to screen or make pixels readable)
```

## Key design decisions

1. **Vtable at the RHI boundary** — the execution engine never calls a graphics API directly. Every pixel-related operation goes through `flux_rhi_vt`. This is the separation that enables multiple backends.

2. **Opaque resource handles** — `flux_r_buffer` and `flux_r_texture` are interpreted only by the RHI. The engine allocates vertices through the RHI without knowing if they live in a Vulkan VkBuffer or a malloc'd array.

3. **Batching lives in the RHI** — consecutive same-color solid draws are batched by the RHI internally. The engine just issues `draw_solid()` calls; the RHI decides when to flush.

4. **Two-phase recording/execution preserved** — the existing architectural strength (CPU records while GPU renders) is maintained. The RHI vtable simply makes the "execute" phase backend-swappable.

## See also

- [Rendering pipeline](rendering-pipeline.md) — detailed op-by-op walkthrough.
- [ADR-0003: Renderer vtable abstraction](../adr/0003-renderer-vtable.md) — decision record.
- [Graphics foundations](graphics-foundations.md) — prerequisite hardware concepts.
