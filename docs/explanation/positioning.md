# Positioning

flux is a low-level 2D graphics substrate built on Vulkan.

## Stack position

```text
widgets / app chrome / shell policy
text layout, shaping, fallback (Pango / HarfBuzz / custom)
-----------------------------------------------
flux: images, paths, glyph runs, command recording
-----------------------------------------------
Vulkan, FreeType, HarfBuzz
```

## Mission

- Provide a small, explicit GPU graphics core for 2D rendering.
- Keep the public model close to the machine: surfaces, images, path verbs, glyph IDs, command lists, and explicit presentation.
- Share one backend for image work, vector work, and text rendering.

## Design center

- Vulkan as the execution backend.
- Personal shell and desktop software where low latency, predictable ownership, and direct control matter more than cross-platform reach.
- Composition of upstream building blocks instead of rebuilding them all inside one monolith.
- Platform-agnostic: the caller provides the `VkSurfaceKHR` or uses offscreen rendering.

## What flux is

- A surface and frame orchestration layer over Vulkan.
- A home for GPU-oriented 2D resources: images, path geometry, glyph runs, atlases, pipelines, transient buffers.
- A low-level recorder that a shell, immediate-mode UI, or retained scene layer can target.

## What flux is not

- Not a widget toolkit.
- Not a layout engine.
- Not a text shaping engine.
- Not a browser engine.
- Not an SVG document parser.
- Not a general 3D renderer.
- Not a platform windowing library.

## Relationship to existing projects

- **Skia**: reference point for breadth and GPU-first 2D rendering. flux follows the idea of a unified image/vector/text backend, but stays narrower and more explicit.
- **Cairo**: reference point for a stable drawing vocabulary. flux borrows explicit path construction, but targets Vulkan and modern GPU execution instead of a broad backend matrix.
- **Pango / HarfBuzz**: belong above flux. They shape text and choose fonts; flux consumes the resulting glyph stream and renders it.
- **GLFW / SDL**: belong above flux. They create windows and platform surfaces; flux consumes the resulting `VkSurfaceKHR` and renders to it.

## Why the abstraction stays low

The project is an engine layer for 2D rendering, not an all-purpose UI platform. Once the abstraction climbs too high, it starts deciding layout, fallback, caching policy, or widget behavior for the caller. flux deliberately stops below that line.

## See also

- [Capability model](capability-model.md) — what flux can and cannot do.
- [Responsibility boundaries](responsibility-boundaries.md) — API and module boundary contract.
- [ADR-0002: Keep flux as a low-level rendering substrate](../adr/0002-keep-flux-as-low-level-rendering-substrate.md)
