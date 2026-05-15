# Glossary

Definitions of terms used throughout flux documentation and source code.

## A

**Affine transformation**  
A 2D geometric transformation preserving parallel lines, represented as a 3×2 matrix in flux (`fx_matrix`). Supports translation, scaling, rotation, and skew.

**Arena allocator**  
A bump-pointer allocator that hands out memory from large pre-allocated blocks. flux uses per-frame arenas for tessellation and stroke output. Arenas are reset (not freed) between frames.

**Atlas (glyph atlas)**  
A single GPU texture (`A8_UNORM`) that caches rasterized glyph masks. All text in a context shares one 2048×2048 atlas.

## B

**Batching**  
Grouping multiple draw operations with identical state into a single GPU draw call. flux automatically batches consecutive solid-color fills.

## C

**Canvas**  
A frame-local command recorder. You acquire a canvas with `fx_surface_acquire`, record drawing operations, and submit with `fx_surface_present`. The canvas pointer is only valid for one frame.

**Clipping**  
Restricting drawing to a sub-region of the surface. flux supports scissor-based rectangular clipping (`fx_clip_rect`) and stencil-based arbitrary path clipping (`fx_clip_path`).

**Context**  
The top-level `fx_context` object. Owns the Vulkan instance, device, glyph atlas, pipeline cache, and upload subsystem. One context can own many surfaces.

## D

**Descriptor set**  
A Vulkan object that binds a texture (image + sampler) to a shader. flux caches descriptor sets per frame to avoid allocating one per image draw call.

**Device pixel ratio (DPR)**  
The ratio of physical pixels to logical pixels. Set with `fx_surface_set_dpr` to render at HiDPI resolutions. The canvas automatically scales coordinates by DPR.

## E

**Even-odd fill rule**  
The winding rule used by `fx_fill_path`. A pixel is inside the path if a ray from the pixel crosses the path boundary an odd number of times. Supports holes and self-intersecting paths.

## F

**Fill**  
Painting the interior of a path or rectangle. Contrasts with **stroke**, which paints the outline.

**Flattening**  
Converting curved path segments (quadratics, cubics, arcs) into sequences of line segments. flux flattens at a 0.25-pixel tolerance.

**Frame**  
One complete acquire-record-present cycle. A surface typically double- or triple-buffers frames.

## G

**Glyph run**  
An array of `(glyph_id, x, y)` tuples produced by a text shaper (e.g., HarfBuzz). flux consumes glyph runs via `fx_draw_glyph_run`.

**GPU upload**  
Transferring pixel data from CPU memory to GPU memory. flux uses a unified upload subsystem with a persistent staging buffer to avoid per-call allocations.

## H

**HarfBuzz**  
An open-source text shaping library commonly used above flux to convert UTF-8 text into positioned glyph IDs. flux does not link HarfBuzz; callers shape text externally and submit glyph runs via `fx_glyph_run` and `fx_glyph_upload`.

## I

**ICD (Installable Client Driver)**  
A Vulkan driver implementation. Set `VK_ICD_FILENAMES` to select a specific ICD, e.g., lavapipe for software rendering.

## L

**Lavapipe**  
Mesa's software Vulkan implementation. Used in CI for headless testing without a physical GPU.

## O

**Offscreen surface**  
A surface without a swapchain or display connection. Renders to a GPU image that can be read back to CPU memory with `fx_surface_read_pixels`. Useful for testing and texture generation.

**Op**  
A single recorded drawing command on the canvas (e.g., fill rect, draw image, clip rect).

## P

**Paint**  
The rendering state applied to a path or glyph run: color, stroke width, line caps/joins, and optional gradient.

**Path**  
A sequence of verbs (move, line, quad, cubic, arc, close) and control points. Paths are CPU-side objects; tessellation happens at present time.

**Pipeline**  
A compiled GPU program (vertex + fragment shaders) plus fixed-function state. flux creates pipelines for solid fills, images, text, gradients, and stencil operations.

**Pipeline cache**  
A Vulkan object that stores compiled shader binaries. Shared across all surfaces in a context to eliminate recompilation on surface creation.

**Pipeline set**  
A collection of pipelines keyed by `(color_format, sample_count)`. flux caches up to 4 pipeline sets per context.

**Premultiplied alpha**  
A color representation where the RGB channels are multiplied by the alpha channel. flux `fx_color` values are premultiplied.

## R

**Rendering substrate**  
flux's self-description: a low-level library that turns explicit 2D commands into GPU work. Does not own layout, widgets, or input. See [Capability model](capability-model.md).

## S

**Scissor**  
A GPU feature that discards fragments outside an axis-aligned rectangle. Used by `fx_clip_rect`.

**Shelf packing**  
The 2D texture allocation strategy used by the glyph atlas. Glyphs are placed in horizontal rows (shelves). When a shelf fills, a new shelf begins below it.

**Stencil buffer**  
An auxiliary framebuffer channel used for path clipping and complex fills. flux uses an `S8_UINT` stencil attachment.

**Stroke**  
Painting the outline of a path with a given width, cap, and join style. Implemented by expanding the path into a triangle strip.

**Surface**
A render target: either a swapchain (from a caller-provided `VkSurfaceKHR`), or an offscreen image.

**Swapchain**  
A Vulkan object that manages a queue of images for presentation to a display. Recreated on window resize.

## T

**Tessellation**  
Converting a polygon into triangles. flux uses ear-clipping for simple polygons. Concave polygons and holes are supported.

## V

**Validation layers**  
Optional Vulkan debugging facilities that check API usage. Enabled at runtime with `FX_ENABLE_VALIDATION=1` or at context creation with `enable_validation = true`. Adds significant CPU overhead.

**Vertex buffer pool**  
A per-frame growable buffer pool for GPU vertex data. Reset every frame.

**VMA (Vulkan Memory Allocator)**  
The library used by flux for GPU memory allocation. Replaces manual `VkDeviceMemory` management.

**Vulkan**  
The low-level graphics API that flux targets. Minimum supported version is 1.2.

## See also

- [Capability model](../explanation/capability-model.md)
- [Architecture overview](../explanation/architecture-overview.md)
- [Vulkan backend](../explanation/vulkan-backend.md)
