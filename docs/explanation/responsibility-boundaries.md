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

flux backend integration
Vulkan instance/device/swapchain execution, offscreen render targets,
GPU memory, descriptor and pipeline management

system libraries and drivers
Vulkan, allocators, GPU driver
```

## What flux owns

- Frame orchestration: acquire a frame-local `fx_canvas`, record commands, submit/present with `fx_surface_present`.
- 2D drawing vocabulary: paths, fills, strokes, clips, images, gradients, and positioned glyph runs.
- Resource handles needed by the renderer: `fx_image`, `fx_path`, `fx_gradient`, and `fx_glyph_run`.
- CPU-side geometry preparation: path flattening, tessellation, stroke expansion, glyph atlas packing, transient vertex upload.
- Backend execution: Vulkan device selection, queues, command buffers, render passes, swapchains, pipelines, descriptor pools, offscreen readback.
- Ownership rules: created objects are caller-owned; recorded ops borrow caller resources until present; the canvas owns any internal transformed copies it creates.

## What flux does not own

- Widgets, controls, styling systems, input routing, focus policy, shell policy, accessibility trees.
- Layout: flex/grid/box layout, paragraph layout, line breaking, text wrapping, baseline alignment, animation/invalidation policy.
- Text shaping, fallback policy, and glyph rasterization. flux accepts pre-rasterized glyph bitmaps via `fx_glyph_upload` and renders positioned glyph runs; callers decide how Unicode text becomes bitmaps.
- Document or asset parsing: SVG parsing, CSS parsing, image file decoding, font discovery, icon theme lookup, resource loading from disk.
- General Vulkan ownership for the application. flux owns the Vulkan objects it creates; callers that pass a `VkSurfaceKHR` or use `VkInstance` interop keep responsibility for their own external objects and synchronization.
- General 3D rendering, compute workloads, cross-backend abstraction, or compositor/window-manager behavior.

## Module responsibilities

| Module | Source | Responsibility |
|---|---|---|
| Context | `src/context.c` | Lifetime, logging, backend initialization, context-wide caches |
| Canvas | `src/canvas.c` | Command recording only: paint defaults, transform stack, clip commands, append-only display-list ops. No GPU work. |
| Path | `src/path.c` | Path storage, bounds, transforms, flattening entry points |
| Tessellation | `src/tess.c` | Simple polygon triangulation |
| Stroker | `src/stroker.c` | Stroke expansion: caps, joins, miter limit |
| Image | `src/image.c` | Image descriptors, CPU pixel copies, GPU upload |
| Text | `src/text.c` | Glyph atlas management, glyph upload, glyph-run containers. Does not rasterize or shape. |
| Surface | `src/surface.c` | Surface lifetime, frame lifecycle, op execution, batching, clipping state, presentation, offscreen readback |
| Vulkan backend | `src/vk/*` | Instance/device selection, swapchain, pipelines, render passes, memory pools, upload staging |

## Public header boundaries

- `flux/flux.h` — Core graphics API. Backend-neutral; does not require Vulkan or other platform headers.
- `flux/flux_vulkan.h` — Vulkan interop. Exposes `fx_context_get_instance` and `fx_surface_create_vulkan` for intentional Vulkan integration.
- New backend-specific APIs live in their own integration headers instead of expanding `flux.h`.

## Change rules

- If an API needs layout, shaping, rasterization, widgets, file formats, or scene policy, it belongs above flux unless it can be expressed as explicit drawing commands.
- If an API exposes backend/platform types, put it behind a named interop header.
- If state affects rendering, it must be either public and recordable on `fx_canvas` or fully internal to execution. Do not keep planned feature state in core structs before the public contract exists.
- Recording functions validate inputs before touching canvas or resource state.
- Recording remains CPU-only. GPU commands are emitted only from surface present paths.

## See also

- [Capability model](capability-model.md) — feature inventory.
- [Architecture overview](architecture-overview.md) — runtime object model and data flow.
- [ADR-0002: Keep flux as a low-level rendering substrate](../adr/0002-keep-flux-as-low-level-rendering-substrate.md)
