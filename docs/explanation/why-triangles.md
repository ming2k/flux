# Why GPUs Use Triangles

This document explains why a 2D renderer still needs triangle-shaped geometry, even though triangles are often associated with 3D graphics.

## The short version

Applications usually think in higher-level shapes:

- Rectangles
- Rounded rectangles
- Circles
- Paths
- Text
- Images
- Gradients

GPUs usually execute lower-level drawing work:

- Vertex processing
- Primitive assembly
- Rasterization
- Fragment shading
- Blending

Triangles are the common bridge between those two worlds. A 2D API can expose friendly drawing commands, but before the GPU can shade pixels, those commands often become triangle meshes.

## Triangles are not only for 3D

The difference between 2D and 3D is mostly about the coordinate system, camera model, depth testing, and lighting model. It is not about whether the GPU can use triangles.

In 3D, triangles describe visible surfaces in space.

In 2D, triangles describe flat screen-space regions.

For example, a 2D rectangle is commonly represented as two triangles:

```text
A ----- B
|     / |
|   /   |
| /     |
D ----- C
```

The renderer can draw that rectangle as:

```text
triangle 1: A, B, D
triangle 2: B, C, D
```

There is no perspective or 3D camera required. The triangle vertices can already be in pixel coordinates.

## Why triangles work so well

### A triangle is always planar

Any three points define a flat surface. That makes triangles stable for rasterization.

Quads and general polygons can be ambiguous. Four points might not lie on the same plane in 3D, and complex polygons can be concave, self-intersecting, or contain holes. Triangles avoid most of that ambiguity.

Even in a pure 2D renderer, this simplicity matters because the GPU wants a predictable primitive before it decides which pixels are covered.

### A triangle has simple coverage rules

Rasterization answers one central question:

> Which pixels are inside this shape?

For a triangle, the GPU can answer that efficiently using edge equations. Each edge splits the screen into an inside and outside half-plane. A pixel is covered when it lies on the inside side of all three edges.

That makes triangle coverage fast, regular, and easy to parallelize.

### Attributes interpolate naturally

Once a pixel is inside a triangle, the GPU often needs interpolated values:

- Color
- Texture coordinates
- Gradient coordinates
- Alpha coverage
- Depth
- Custom shader inputs

Triangles support this directly. Values attached to the three vertices can be interpolated across the covered pixels.

This is useful in both 2D and 3D. A textured 2D image quad, for example, is usually two triangles with UV coordinates at each corner.

## What happens to 2D shapes

Most 2D drawing commands are not triangles at the API boundary. They become triangles during rendering.

### Rectangles

A filled rectangle becomes two triangles.

For images, the same two triangles carry texture coordinates. The fragment shader samples from the image using those coordinates.

### Text

Text is often drawn as one textured rectangle per glyph, where the texture is a glyph atlas.

Each glyph rectangle becomes two triangles:

```text
glyph bitmap in atlas
        |
        v
textured quad
        |
        v
two triangles
        |
        v
pixels
```

The fragment shader samples the glyph alpha mask and multiplies it by the text color.

### Paths

Paths are more complex because they may contain lines, quadratic Bezier curves, cubic Bezier curves, arcs, and closed shapes.

A typical path pipeline is:

```text
path commands
    |
    v
flatten curves into line segments
    |
    v
build one or more polygons
    |
    v
triangulate polygons
    |
    v
draw triangles
```

This process is called tessellation. It converts a higher-level shape into a mesh the GPU can rasterize.

## Does the CPU or GPU make the triangles?

Either is possible.

Some renderers tessellate on the CPU, then upload triangle vertices to the GPU. This is straightforward and predictable.

Other renderers use more advanced GPU-side techniques, such as compute shaders, stencil fills, signed distance fields, or analytic fragment shaders. Even then, triangles often remain part of the drawing setup: they may define a bounding region where the shader runs.

For flux, path flattening, stroking, and simple polygon triangulation are currently CPU-side work. The resulting vertices are batched and submitted to Vulkan as GPU draw calls.

## Why not draw circles and curves directly?

The GPU can shade pixels with arbitrary math, but it still needs a region of the screen to run that shader over.

For a circle, a renderer could draw two triangles covering the circle's bounding box and let the fragment shader discard pixels outside the analytic circle equation. That can be useful for some shapes.

But for general paths, text, clipping, gradients, images, and batching, triangle meshes are a practical universal representation. They give the renderer one common format for many different drawing commands.

## The 2D mental model is still valid

As an application developer, it is reasonable to think in 2D terms:

```c
fx_fill_rect(c, &rect, color);
fx_fill_path(c, path, &paint);
fx_draw_image(c, image, &src, &dst);
fx_draw_glyph_run(c, run, x, y, &paint);
```

Those are the right abstractions at the API level.

Triangles are an implementation detail below that API. They are not a sign that the renderer has become a 3D engine. They are simply the format the GPU is especially good at consuming.

## Summary

2D renderers use triangles because GPUs rasterize triangles efficiently and predictably.

The renderer can expose rectangles, paths, text, and images to the application while internally converting them into triangle meshes. This keeps the public API expressive and 2D-focused while still using the GPU's strongest execution path.

In short:

```text
2D API shape -> tessellation -> triangles -> GPU rasterization -> pixels
```
