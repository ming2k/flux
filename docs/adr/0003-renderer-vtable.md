# ADR-0003: Renderer Vtable Abstraction

**Status:** Accepted  
**Date:** 2026-05-07

## Context

flux originally coupled the rendering execution engine directly to Vulkan. Every draw call in the execution loop (record_ops in surface.c) dispatched to vkCmd* functions. Internal types (fx_context, fx_surface, fx_image) embedded Vulkan handles (VkInstance, VkDevice, VkImage) directly. Adding a new backend (OpenGL, Metal, or a software rasteriser) would require rewriting the entire rendering engine.

## Decision

We introduced a `fx_renderer` vtable interface that abstracts all backend-specific rendering operations behind a single C function-table struct. Every pixel-related operation — vertex allocation, draw submission, scissor and stencil state, texture management, and frame lifecycle — goes through the vtable.

The execution engine (`engine.c`) calls only vtable methods. It has no dependency on any specific graphics API.

### The vtable

```c
typedef struct fx_renderer_vtbl {
    void (*destroy)(fx_renderer *r);
    void (*surface_extent)(fx_renderer *r, uint32_t *w, uint32_t *h);

    void (*begin_frame)(fx_renderer *r);
    void (*begin_pass)(fx_renderer *r, fx_color clear);
    void (*end_pass)(fx_renderer *r);
    void (*submit)(fx_renderer *r);
    bool (*read_pixels)(fx_renderer *r, void *data, size_t stride);

    fx_solid_vertex *(*alloc_solid)(fx_renderer *r, size_t count, fx_r_buffer **buf, uint32_t *first);
    fx_image_vertex *(*alloc_image)(fx_renderer *r, size_t count, fx_r_buffer **buf, uint32_t *first);

    void (*draw_solid)(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count, fx_color color);
    void (*flush_solid)(fx_renderer *r);
    void (*draw_image)(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count, fx_r_texture *tex);
    void (*draw_text)(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count, fx_color color);
    void (*draw_gradient)(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count, const fx_gradient *grad);

    void (*scissor)(fx_renderer *r, int32_t x, int32_t y, uint32_t w, uint32_t h);
    void (*stencil_clear)(fx_renderer *r, int32_t x, int32_t y, uint32_t w, uint32_t h);
    void (*stencil_fill)(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count);
    void (*stencil_ref)(fx_renderer *r, uint32_t ref);
    void (*cover_solid)(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count, fx_color color);
    void (*cover_gradient)(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t count, const fx_gradient *grad);

    fx_r_texture *(*texture_alloc)(fx_renderer *r, uint32_t w, uint32_t h, fx_pixel_format fmt, const void *data, size_t stride);
    void (*texture_free)(fx_renderer *r, fx_r_texture *tex);
    void (*texture_update)(fx_renderer *r, fx_r_texture *tex, const void *data, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
} fx_renderer_vtbl;
```

### Layered architecture

The vtable anchors a five-layer architecture:

```
API Layer       — flux.h, surface.c (user-facing)
State Layer     — state/ (recording: display lists, transforms)
Geometry Layer  — geometry/ (paths, tessellation, strokes)
     │
     ▼
Renderer Layer  — renderer/ (vtable + Software + Vulkan backends)
     │
Math Layer      — math/ (matrices, rects, arena)
```

## Consequences

### Positive

- **Multi-backend support.** A `SoftwareRenderer` (~800 lines of C) proves the vtable is implementable without Vulkan. A `VulkanRenderer` can be added behind the same interface.
- **Testability.** The execution engine can be tested against the software renderer without a GPU.
- **Clean separation.** The geometry, state, and API layers have zero knowledge of any graphics API.
- **Future portability.** Porting to OpenGL, Metal, or Direct3D means implementing one vtable — not rewriting the entire engine.

### Negative

- **Vtable call overhead.** Every draw goes through an indirect function call. For typical 2D workloads (dozens to low hundreds of draw calls per frame) this is negligible compared to the actual rendering work. For pathologically fine granularity it could matter; mitigation is batched draw entry points.
- **Interface stability.** The vtable has ~20 methods. Adding a new rendering feature (e.g. blur, blend modes) requires adding vtable entries. The alternative is "capability query" + fallback, which is more complex.
- **Existing Vulkan code.** The Vulkan backend (swapchain, pipelines, command buffers) was deeply coupled to the old internal.h types. Porting it behind the vtable is ongoing work.

## Alternatives considered

### Adapter pattern with backend-specific interfaces

Each backend exposes its own API, and higher layers branch on backend type. Rejected because it leaks abstraction — every piece of code that touches rendering must know the backend.

### Macro/template dispatch

Use compile-time `#ifdef FX_BACKEND_VULKAN` / `#ifdef FX_BACKEND_SOFTWARE` to select implementations at compile time. Rejected because it prevents a single binary from using multiple backends (e.g. offscreen software + windowed Vulkan).

### Direct backend registration (OpenGL-style)

A global "current backend" pointer that the engine dereferences. This is functionally identical to the vtable but less explicit about the contract. The vtable struct makes the contract visible in one place.

## See also

- [Architecture overview](../explanation/architecture-overview.md) — updated with the five-layer model.
- [ADR-0002: Keep flux as a low-level rendering substrate](0002-keep-flux-as-low-level-rendering-substrate.md) — the vtable keeps flux "low" by pushing backend specifics behind an interface.
