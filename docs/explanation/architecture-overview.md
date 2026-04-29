# Architecture

This document describes the runtime shape of flux.

## Object model

```
┌───────────────────────┐
│ fx_context            │  one per process
│  • VkInstance         │  owns device selection and logging
│  • VkDevice           │  owns context-wide caches and upload path
│  • FT_Library         │  FreeType handle
│  • fx_atlas           │  dynamic GPU glyph atlas
│  • upload subsystem   │  staging buffer + cmd buffer + fence
└──────────┬────────────┘
           │ borrows
  ┌────────┴──────────────────────────────────┐
  │                                           │
┌─▼────────────────────┐         ┌────────────▼────────┐
│ fx_surface           │         │ fx_image, fx_font…  │
│  • VkSurfaceKHR      │         │ (resources, shared) │
│  • VkSwapchainKHR    │         └─────────────────────┘
│  • render pass       │   surface-lifetime
│  • pipelines, DSL,   │   (survive resize)
│    samplers          │
│  • readback staging  │   offscreen only
│  • frames[2]         │
│    – cmd buffer      │
│    – vbuf_pool       │
│    – desc_pool       │
│    – sync objects    │
│  • fx_canvas         │
│    – matrix stack    │
│    – recorded ops    │
└──────────────────────┘
```

- **`fx_context`** owns the core Vulkan instance, logical device, the
  **Dynamic Glyph Atlas**, and the **upload subsystem** (persistent
  staging buffer + reusable command buffer + fence) that every GPU
  upload flows through. It also holds the FreeType library handle.
- **`fx_surface`** manages a presentation window (Wayland swapchain)
  or an offscreen render target. Render pass, pipelines, descriptor
  set layout, and samplers are surface-lifetime: they are built once
  and reused across swapchain resizes. The swapchain, its images and
  framebuffers, the stencil attachment, and the two in-flight frame
  slots are torn down and rebuilt on every resize.
- **`fx_canvas`** is the primary recording interface. It maintains a
  3×3 transformation matrix stack and an operation list.
- **Resources** (`fx_image`, `fx_path`, `fx_font`, `fx_glyph_run`) are
  shared across surfaces. Image pixel uploads go through the
  context's upload subsystem (`src/vk/upload.c`).

## Per-frame Data Flow

flux uses a strict separation between **Recording** and **Execution**.

### 1. Recording (CPU-Immediate)
When drawing functions like `fx_fill_path` or `fx_draw_glyph_run` are called:
- **Transformations:** The current affine matrix is applied during recording on the CPU. Paths and path clips may be copied into transformed internal paths; glyph runs remain borrowed while their draw origin is recorded in device space.
- **Op Logging:** A `fx_op` is appended to the canvas's internal display list. If a transformation occurred, the canvas takes ownership of a new, transformed path object.
- **No GPU Work:** No Vulkan commands are issued during this phase.

### 2. Execution (GPU-Batched)
When `fx_surface_present` is called:
- **Tessellation:** Paths are flattened and tessellated into triangles.
- **Stroking:** Lines are expanded into geometry based on `fx_paint` (caps/joins).
- **Dynamic Atlas:** Missing glyphs are rasterized and uploaded to the GPU atlas.
- **Batching:** Sequential ops with identical paint properties are grouped into a single Vulkan draw call.
- **Vertex Upload:** Geometry is copied into the frame's dynamic linear buffer.
- **Submission:** A single primary command buffer is submitted to the graphics queue.

## Source layout

```
include/flux/           public headers
  flux.h                core types, matrix stack, paint, recorder
  flux_wayland.h        wayland surface constructor
  flux_vulkan.h         explicit Vulkan interop entry points

src/
  internal.h            shared internal declarations
  context.c             fx_context lifecycle, logging, FT initialization
  canvas.c              op recording, matrix stack, paint init
  image.c               GPU image creation and pixel upload
  path.c                path storage, matrix math, path transforms
  surface.c             fx_surface lifecycle, batching, VK execution
  stroker.c             SVG-grade line cap and join expansion
  tess.c                simple polygon triangulation
  text.c                FreeType/HarfBuzz glue, Dynamic Atlas manager
  vk/
    device.c            instance + device + queue selection
    memory.c            dynamic ring buffer allocator (vbuf_pool)
    upload.c            unified GPU upload (staging + cmd + fence)
    swapchain.c         swapchain, render pass, and pipeline creation
  shaders/
    solid.vert/frag     solid geometry shader
    image.vert/frag     textured quad shader
    text.frag           alpha-coverage text shader
    gradient.*          linear and radial gradients
    stencil.frag        stencil-only passes for path clips
    blur.frag           separable Gaussian blur

examples/               runnable demos (hello_rect)
tests/                  unit tests (transforms, stroker, foundation)
```

## Error Model

flux uses a "creation-time return, runtime-internal" error model:
- **Creation Failures:** (`fx_context_create`, etc.) return `NULL` and log the reason.
- **Resource Limits:** Dynamic growth (vbuf, atlas, upload staging) is handled internally. The glyph atlas in particular evicts all cached entries and reuses its texture when a new glyph does not fit; the event is logged as a warning.
- **Validation:** Integrated Vulkan validation layers route messages to the application's log handler.

## Surface Lifecycle

Surface-scoped state has two tiers with different lifetimes:

| Tier | Built by | Torn down by | Survives resize? |
|---|---|---|---|
| Swapchain, swapchain images, framebuffers, stencil attachment, per-frame sync objects | `fx_swapchain_build` | `fx_swapchain_destroy` | No |
| Render pass, descriptor set layout, pipeline layouts, pipelines, samplers, offscreen image and framebuffer | first `fx_swapchain_build` / `fx_surface_create_offscreen` | `fx_surface_destroy_pipelines` | Yes |

Resizing a swapchain surface calls `fx_swapchain_destroy` followed
by `fx_swapchain_build`; the second tier is preserved so a resize
does not pay the pipeline creation cost.

## Related decisions

- [ADR-0002: Keep flux as a low-level rendering substrate](../adr/0002-keep-flux-as-low-level-rendering-substrate.md)
