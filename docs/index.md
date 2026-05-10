# flux Documentation

flux is a C23 library that exposes an explicit Vulkan-backed 2D drawing API. This documentation is organized by what you are trying to do.

## Start here

| If you want to...                            | Read...                     |
|----------------------------------------------|-----------------------------|
| Learn flux by running it locally             | [Tutorials](tutorials/)     |
| Complete a specific integration task         | [How-to guides](how-to/)    |
| Look up API, build, or configuration details | [Reference](reference/)     |
| Understand the design and trade-offs         | [Explanation](explanation/) |
| Review architecture decisions                | [ADRs](adr/)                |
| Modify the codebase                          | [Developer docs](dev/)      |

## Common paths

- New to the project: [Getting Started](tutorials/01-getting-started.md)
- Understanding scope: [Capability model](explanation/capability-model.md)
- Understanding text rendering: [Text rendering pipeline](explanation/text-rendering-pipeline.md)
- Drawing with the API: [How to draw basic shapes](how-to/draw-basic-shapes.md)
- Public API lookup: [API reference](reference/api.md)
- Thread safety rules: [Thread safety](reference/thread-safety.md)
- Source tree tour: [Project layout](dev/project-layout.md)

## What flux is

A pure 2D graphics library focused on drawing: paths, fills, strokes, text, images, gradients, and clipping. The caller provides the Vulkan surface (from GLFW, SDL, or raw platform code) and rasterizes glyph bitmaps; flux handles the rendering.

## What flux is not

- Not a windowing toolkit (use GLFW, SDL, or platform APIs)
- Not a layout engine
- Not a text shaper or rasterizer (use HarfBuzz + FreeType)
- Not an SVG parser
