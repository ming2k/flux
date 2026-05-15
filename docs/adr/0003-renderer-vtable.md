# ADR-0003: Renderer Vtable Abstraction

**Status:** Accepted  
**Date:** 2026-05-07

## Context

flux originally coupled the rendering execution engine directly to Vulkan. Every draw call in the execution loop (record_ops in surface.c) dispatched to vkCmd* functions. Internal types (flux_context, flux_surface, flux_image) embedded Vulkan handles (VkInstance, VkDevice, VkImage) directly. Adding a new backend (OpenGL, Metal, or a software rasteriser) would require rewriting the entire rendering engine.

## Decision

We introduced a `flux_rhi_vtbl` interface that abstracts all backend-specific rendering operations behind a single C function-table struct. Every pixel-related operation — vertex allocation, draw submission, scissor and stencil state, texture management, and frame lifecycle — goes through the vtable.

The execution engine (`engine.c`) calls only vtable methods. It has no dependency on any specific graphics API.

### The vtable

```c
typedef struct flux_rhi_vtbl {
    void (*destroy)(flux_rhi_device *r);
    void (*surface_extent)(flux_rhi_device *r, uint32_t *w, uint32_t *h);

    void (*begin_frame)(flux_rhi_device *r);
    void (*begin_pass)(flux_rhi_device *r, flux_color clear);
    void (*end_pass)(flux_rhi_device *r);
    void (*submit)(flux_rhi_device *r);
    bool (*read_pixels)(flux_rhi_device *r, void *data, size_t stride);

    flux_solid_vertex *(*alloc_solid)(flux_rhi_device *r, size_t count, flux_r_buffer **buf, uint32_t *first);
    flux_image_vertex *(*alloc_image)(flux_rhi_device *r, size_t count, flux_r_buffer **buf, uint32_t *first);

    void (*draw_solid)(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, flux_color color);
    void (*flush_solid)(flux_rhi_device *r);
    void (*draw_image)(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, flux_r_texture *tex, flux_color tint);
    void (*draw_text)(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, flux_color color);
    void (*draw_gradient)(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, const flux_gradient *grad);

    void (*scissor)(flux_rhi_device *r, int32_t x, int32_t y, uint32_t w, uint32_t h);
    void (*stencil_clear)(flux_rhi_device *r, int32_t x, int32_t y, uint32_t w, uint32_t h);
    void (*stencil_fill)(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, int fill_rule);
    void (*stencil_ref)(flux_rhi_device *r, uint32_t ref);
    void (*cover_solid)(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, flux_color color);
    void (*cover_gradient)(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, const flux_gradient *grad);
    void (*draw_fringe)(flux_rhi_device *r, flux_r_buffer *buf, uint32_t first, uint32_t count, flux_color color);

    flux_r_texture *(*texture_alloc)(flux_rhi_device *r, uint32_t w, uint32_t h, flux_pixel_format fmt, const void *data, size_t stride);
    void (*texture_free)(flux_rhi_device *r, flux_r_texture *tex);
    void (*texture_update)(flux_rhi_device *r, flux_r_texture *tex, const void *data, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
} flux_rhi_vtbl;
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

Use compile-time `#ifdef FLUX_BACKEND_VULKAN` / `#ifdef FLUX_BACKEND_SOFTWARE` to select implementations at compile time. Rejected because it prevents a single binary from using multiple backends (e.g. offscreen software + windowed Vulkan).

### Direct backend registration (OpenGL-style)

A global "current backend" pointer that the engine dereferences. This is functionally identical to the vtable but less explicit about the contract. The vtable struct makes the contract visible in one place.

## See also

- [Architecture overview](../explanation/architecture-overview.md) — updated with the five-layer model.
- [ADR-0002: Keep flux as a low-level rendering substrate](0002-keep-flux-as-low-level-rendering-substrate.md) — the vtable keeps flux "low" by pushing backend specifics behind an interface.
