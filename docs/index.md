# flux Documentation

flux is a C11 library that exposes an explicit Vulkan-backed 2D drawing API. This documentation is organized by what you are trying to do.

## Start here

| If you want to... | Read... |
|---|---|
| Learn flux by running it locally | [Tutorials](tutorials/) |
| Complete a specific integration task | [How-to guides](how-to/) |
| Look up API, build, or configuration details | [Reference](reference/) |
| Understand the design and trade-offs | [Explanation](explanation/) |
| Review architecture decisions | [ADRs](adr/) |
| Modify the codebase | [Developer docs](dev/) |

## Common paths

- New to the project: [Getting Started](tutorials/01-getting-started.md)
- Linking an application: [How to link flux](how-to/link-flux.md)
- Drawing with the API: [How to draw basic shapes](how-to/draw-basic-shapes.md)
- Public API lookup: [API reference](reference/api.md)
- Runtime scope: [Capability model](explanation/capability-model.md)
- Source tree tour: [Project layout](dev/project-layout.md)

## Status at a glance

flux 0.1.0 is a pre-1.0 development release. It includes vector primitives, gradients, clipping, image upload, text rendering through positioned glyph runs, offscreen rendering, and batched Vulkan execution. See [roadmap](explanation/roadmap.md), [release process](dev/release-process.md), and [CHANGELOG](../CHANGELOG.md) for current release status.
