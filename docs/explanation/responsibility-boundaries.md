# Responsibility Boundaries

This document defines what flux owns and what must stay above or below it. It is the contract for public API additions and internal refactors.

## Layer model

```text
application / shell / toolkit
widgets, layout, input policy, animation policy, scene invalidation

text and asset preparation
font fallback, paragraph layout, shaping, rasterization, SVG parsing, image decoding

flux core API
surfaces, frame lifecycle, command recording, paths, paints, images,
gradients, glyph atlas, positioned glyph runs

flux RHI abstraction
`flux_rhi_vtbl` interface: backend-neutral execution engine calls vtable
ops; concrete backends implement drawing + presentation.

flux backend integration (Vulkan)
Vulkan swapchain/present, offscreen render targets,
GPU memory, descriptor and pipeline management. Full GPU rasterisation
via recorded command buffers.

system libraries and drivers
Vulkan, allocators, GPU driver
```

## What flux owns

- Frame orchestration: acquire a frame-local `flux_canvas`, record commands, submit/present with `flux_surface_present`.
- 2D drawing vocabulary: paths, fills, strokes, clips, images, gradients, and positioned glyph runs.
- Resource handles needed by the renderer: `flux_image`, `flux_path`, `flux_gradient`, and `flux_glyph_run`.
- CPU-side geometry preparation: path flattening, tessellation, stroke expansion, glyph atlas packing, transient vertex upload.
- Backend execution via the RHI vtable: device selection, queues, command buffers, render passes, swapchains, pipelines, descriptor pools, offscreen readback.
- Software rasteriser: CPU scanline fill with stencil buffer, used as the offscreen fallback.
- Ownership rules: created objects are caller-owned; recorded ops borrow caller resources until present; the canvas owns any internal transformed copies it creates.

## What flux does not own

- Widgets, controls, styling systems, input routing, focus policy, shell policy, accessibility trees.
- Layout: flex/grid/box layout, paragraph layout, line breaking, text wrapping, baseline alignment, animation/invalidation policy.
- Text shaping, fallback policy, and glyph rasterization. flux accepts pre-rasterized glyph bitmaps via `flux_glyph_upload` and renders positioned glyph runs; callers decide how Unicode text becomes bitmaps.
- Document or asset parsing: SVG parsing, CSS parsing, image file decoding, font discovery, icon theme lookup, resource loading from disk.
- General Vulkan ownership for the application. flux owns the Vulkan objects it creates; callers that pass a `VkSurfaceKHR` or use `VkInstance` interop keep responsibility for their own external objects and synchronization.
- General 3D rendering, compute workloads, cross-backend abstraction, or compositor/window-manager behavior.

## Module responsibilities

| Module | Source | Responsibility |
|---|---|---|
| Context | `src/resource/context.c` | Lifetime, logging, backend initialization, context-wide caches |
| Canvas | `src/state/canvas.c` | Command recording only: paint defaults, transform stack, clip commands, append-only display-list ops. No GPU work. |
| Path | `src/geometry/path.c` | Path storage, bounds, transforms, flattening entry points |
| Tessellation | `src/geometry/tess.c` | Simple polygon triangulation |
| Stroker | `src/geometry/stroker.c` | Stroke expansion: caps, joins, miter limit |
| Image | `src/resource/image.c` | Image descriptors, CPU pixel copies, GPU upload |
| Text | `src/resource/text.c` | Glyph atlas management, glyph upload, glyph-run containers. Does not rasterize or shape. |
| Surface | `src/surface.c` | Surface lifetime, frame lifecycle, op execution, batching, clipping state, presentation, offscreen readback |
| RHI abstraction | `src/rhi/rhi.h` | `flux_rhi_vtbl` interface: backend-neutral ops for drawing, clipping, and presentation |
| Software RHI | `src/rhi/software/software_rhi.c` | CPU scanline rasteriser with stencil buffer; offscreen fallback |
| Vulkan RHI | `src/rhi/vulkan/vulkan_rhi.c` | Vulkan backend: device, swapchain, memory, upload, pipelines, and presentation |
| Vulkan context | `src/resource/context.c` | Instance/device/queue initialization, physical device selection |

## Public header boundaries

- `flux/flux.h` — Core graphics API. Backend-neutral; does not require Vulkan or other platform headers.
- `flux/flux_vulkan.h` — Vulkan interop. Exposes `flux_surface_create_vulkan` for intentional Vulkan integration. Callers provide a complete `flux_vulkan_device` and `VkSurfaceKHR`; flux owns only the swapchain and render-time objects.
- New backend-specific APIs live in their own integration headers instead of expanding `flux.h`.

## Change rules

- If an API needs layout, shaping, rasterization, widgets, file formats, or scene policy, it belongs above flux unless it can be expressed as explicit drawing commands.
- If an API exposes backend/platform types, put it behind a named interop header.
- If state affects rendering, it must be either public and recordable on `flux_canvas` or fully internal to execution. Do not keep planned feature state in core structs before the public contract exists.
- Recording functions validate inputs before touching canvas or resource state.
- Recording remains CPU-only. GPU commands are emitted only from RHI `submit()` paths.
- New backends implement the `flux_rhi_vtbl` interface; no engine or surface code may call a graphics API directly.

## See also

- [Capability model](capability-model.md) — feature inventory.
- [Architecture overview](architecture-overview.md) — runtime object model and data flow.
- [ADR-0002: Keep flux as a low-level rendering substrate](../adr/0002-keep-flux-as-low-level-rendering-substrate.md)
