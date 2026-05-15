# RHI Design

flux renders through a small **Render Hardware Interface** (RHI) abstraction layer. This document explains why the abstraction exists, what it hides, and how to add a new backend.

## Why an abstraction layer?

flux supports two rendering paths:

| Backend | Use case |
|---|---|
| **Software** | Headless servers, CI, debugging, portability fallback |
| **Vulkan** | GPU-accelerated windowed and offscreen rendering |

Both paths share the same **frontend** — the canvas, path builder, paint state, and display list — but their **backends** are completely different. The RHI is the seam between them.

Without an abstraction layer, every new feature (text, gradients, clipping) would need to be implemented twice, and the engine would be full of `#ifdef` branches. The RHI solves this by forcing both backends to speak the same language.

## The vtable

The RHI is a single C vtable struct (`flux_rhi_vtbl` in `src/rhi/rhi.h`). Every backend fills this table with its own functions:

```c
struct flux_rhi_vtbl {
    void (*destroy)(flux_rhi_device *r);
    bool (*resize)(flux_rhi_device *r, uint32_t w, uint32_t h);

    /* Vertex allocation */
    flux_solid_vertex  *(*alloc_solid)(flux_rhi_device *, size_t count,
                                        flux_r_buffer **buf, uint32_t *first);
    flux_image_vertex  *(*alloc_image)(flux_rhi_device *, size_t count,
                                        flux_r_buffer **buf, uint32_t *first);

    /* Draw commands */
    void (*draw_solid)(flux_rhi_device *, flux_r_buffer *, uint32_t first,
                       uint32_t count, flux_color color);
    void (*draw_image)(flux_rhi_device *, flux_r_buffer *, uint32_t first,
                       uint32_t count, flux_r_texture *tex, flux_color tint);
    void (*draw_gradient)(...);
    void (*draw_text)(...);

    /* State */
    void (*set_blend_mode)(flux_rhi_device *, flux_blend_mode);
    void (*scissor)(flux_rhi_device *, int32_t x, int32_t y,
                    uint32_t w, uint32_t h);

    /* Texture management */
    flux_r_texture *(*texture_alloc)(...);
    void (*texture_free)(flux_rhi_device *, flux_r_texture *);

    /* Frame lifecycle */
    void (*begin_frame)(flux_rhi_device *);
    void (*flush_solid)(flux_rhi_device *);
    flux_r_texture *(*surface_texture)(flux_rhi_device *);
    bool (*read_pixels)(flux_rhi_device *, void *data, size_t stride);
};
```

**Key design choices:**

- **No polymorphic objects.** `flux_rhi_device` is an opaque pointer. The backend casts it to its own struct (`sw_renderer *` or `vk_renderer *`). This is plain C — no vptrs, no RTTI.
- **Buffer tagging.** `flux_r_buffer` is not a real struct; it is an opaque handle (usually an integer index into a backend array). This lets the software backend use a linear arena and the Vulkan backend use `VkBuffer` handles without the engine knowing the difference.
- **Immediate-mode recording.** The engine calls `alloc_*` to get writable vertex memory, fills it, then calls `draw_*` in the same frame. There is no retained mesh data between frames.

## What the engine sees

`src/engine.c` is backend-agnostic. It walks the canvas's display list and calls vtable functions:

```c
/* Engine sees only the vtable */
vt(r)->draw_solid(r, buf, first, count, paint.color);
```

The engine does not know whether `draw_solid` writes to a CPU pixel buffer or submits a `vkCmdDraw`. This means:

- A bug fix in path tessellation fixes both backends simultaneously.
- A new blend mode only needs to be implemented in each backend's `draw_solid` / `draw_image` path.

## What the backend owns

Each backend is responsible for everything below the vtable:

### Software backend (`src/rhi/software/software_rhi.c`)

- A CPU pixel buffer (`uint8_t *`) and a matching stencil buffer (`uint8_t *`)
- A linear memory pool for vertex data
- A linked list of `sw_texture` objects
- Scanline rasterizers for solid triangles, image quads, and gradients

### Vulkan backend (`src/rhi/vulkan/`)

- `VkInstance`, `VkDevice`, `VkQueue` (borrowed from the application)
- `VkSwapchainKHR` and per-frame sync objects (fences, semaphores)
- `VkPipelineCache` persisted to disk (`~/.cache/flux/pipeline_cache.bin`)
- Per-frame linear vertex buffer pools
- Descriptor pools and a small per-frame descriptor cache
- A persistent staging buffer for image uploads

## Adding a new backend

To port flux to a new GPU API (Metal, Direct3D 12, WebGPU):

1. **Create a new subdirectory** under `src/rhi/` (e.g. `src/rhi/metal/`).
2. **Define your device struct** containing all API-specific state:
   ```c
   typedef struct metal_renderer {
       flux_rhi_vtbl *vtbl;   /* must be first field */
       id<MTLDevice> device;
       /* ... */
   } metal_renderer;
   ```
3. **Implement every vtable entry.** Start with `alloc_solid`, `draw_solid`, `flush_solid`, and `set_blend_mode` — this is enough to run `test_offscreen`.
4. **Add a surface constructor** in `src/surface.c` (or keep it private until the backend stabilizes).
5. **Add golden-image tests** to prove pixel correctness against the software backend.

The software backend is the reference implementation. If your new backend produces different pixels for the same input, the software renderer is considered correct and the new backend has a bug.

## Related reading

- [Software renderer](software-renderer.md) — the reference CPU backend.
- [Vulkan backend](vulkan-backend.md) — the production GPU backend.
- [ADR-0003: Renderer vtable abstraction](../adr/0003-renderer-vtable.md) — why we chose a vtable over callbacks or macros.
