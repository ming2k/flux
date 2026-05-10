# Explanation

Conceptual documentation for understanding why flux is shaped the way it is.

- [Positioning](positioning.md) - Mission, non-goals, and relationship to neighboring projects.
- [Capability model](capability-model.md) - What flux owns and what stays above it.
- [Responsibility boundaries](responsibility-boundaries.md) - API and module boundary rules.
- [Application architecture](application-architecture.md) - Recommended layering around flux.
- [Architecture overview](architecture-overview.md) - Runtime object model and data flow.
- [Rendering pipeline](rendering-pipeline.md) - End-to-end data flow from record to present.
- [Rendering](rendering.md) - Tessellation, batching, text, shaders, and anti-aliasing.
- [Why GPUs use triangles](why-triangles.md) - Why 2D rendering commands often become triangle meshes.
- [Text rendering pipeline](text-rendering-pipeline.md) - How text becomes pixels, and where flux sits in the stack.
- [Bézier curves](bezier-curves.md) - Quadratic and cubic curves, flattening, and GPU conversion.
- [Vulkan backend](vulkan-backend.md) - Backend-owned Vulkan objects and invariants.
- [Graphics foundations](graphics-foundations.md) - Prerequisite concepts from memory to the GPU pipeline.
- [Software renderer](software-renderer.md) - CPU scanline rasteriser and the renderer vtable.
- [Roadmap](roadmap.md) - Shipped and planned work.

## Reference

- [Glossary](../reference/glossary.md) - Definitions of terms used throughout flux.

## Related decisions

- [ADR-0001: Record architecture decisions](../adr/0001-record-architecture-decisions.md)
- [ADR-0002: Keep flux as a low-level rendering substrate](../adr/0002-keep-flux-as-low-level-rendering-substrate.md)
- [ADR-0003: Renderer vtable abstraction](../adr/0003-renderer-vtable.md)
