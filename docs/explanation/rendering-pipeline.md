# Rendering Pipeline

This document describes the end-to-end rendering pipeline: how drawing commands flow from application code to pixels on screen.

## Two-phase architecture

flux splits rendering into distinct **CPU recording** and **GPU execution** phases:

```text
Application
    |
    v
[Record]    <-- CPU, immediate
    flux_surface_acquire → flux_fill_path/flux_draw_image/etc. → flux_surface_present
    |
    v
[Execute]   <-- GPU, deferred
    Tessellate → Batch → Upload → Submit → Present
    |
    v
Pixels
```

## Phase 1: Recording (CPU)

Recording happens on the calling thread, synchronously, with no GPU work:

```c
flux_canvas *c = flux_surface_acquire(surface);
flux_fill_rect(c, &rect, color);        /* appends one flux_op */
flux_fill_path(c, path, &paint);        /* appends one flux_op */
flux_draw_image(c, img, &src, &dst);   /* appends one flux_op */
flux_draw_glyph_run(c, run, x, y, &paint); /* appends one flux_op */
```

### What happens during recording

1. **Transform application** — The current matrix is applied to paths and glyph positions on the CPU. Transformed paths may be copied into internal canvas-owned paths.
2. **Op append** — Each call appends one `flux_op` to the canvas display list. This is an O(1) array append.
3. **No validation** — Recording does not check if images are uploaded, glyphs are in the atlas, or paths are valid. Validation happens at present time.
4. **Resource borrowing** — The canvas stores pointers to `flux_path`, `flux_image`, `flux_glyph_run`. The caller must keep these alive until `flux_surface_present`.

### Recording performance

- Typical cost: ~50–200 ns per op (just struct initialization and array append).
- No allocations during recording (ops are written into a pre-grown array).
- Transform matrix application adds ~200–500 ns for paths with many points.

## Phase 2: Execution (GPU)

When `flux_surface_present` is called, the recorded ops are converted into GPU commands:

```text
flux_surface_present
    |
    +-- Acquire next frame slot (semaphore + fence)
    |
    +-- Tessellate paths (CPU)
    |       Move/Line/Quad/Cubic → flattened polylines
    |       Polyline → triangles (ear-clipping)
    |
    +-- Expand strokes (CPU)
    |       Polyline + cap/join style → triangle mesh
    |
    +-- Build batches (CPU)
    |       Group consecutive ops with same pipeline + paint
    |
    +-- Allocate vertex memory (CPU)
    |       Write triangle vertices into per-frame ring buffer
    |
    +-- Upload dynamic data (GPU)
    |       Vertex buffer → GPU (DMA)
    |       Glyph atlas updates (if any new glyphs uploaded)
    |
    +-- Record command buffer (CPU → GPU)
    |       vkCmdBeginRenderPass
    |       for each batch:
    |           vkCmdBindPipeline
    |           vkCmdPushConstants (paint color, transforms)
    |           vkCmdBindVertexBuffers
    |           vkCmdDraw
    |       vkCmdEndRenderPass
    |
    +-- Submit and present (GPU)
            vkQueueSubmit → vkQueuePresentKHR
```

### Tessellation

Paths are converted to triangle meshes on the CPU during present:

| Path verb | Conversion |
|---|---|
| Move | Start new subpath |
| Line | Direct segment |
| Quad | Recursive de Casteljau flattening → line segments |
| Cubic | Recursive de Casteljau flattening → line segments |
| Arc | Approximated as 1–4 cubic Bézier segments, then flattened |
| Close | Join last point to first |

After flattening, simple polygons are triangulated using ear-clipping. The resulting triangles are written into the per-frame vertex buffer.

### Batching

Consecutive ops that share the same **pipeline** and **paint** are merged into a single `vkCmdDraw`:

```text
Ops: [fill_rect red] [fill_rect red] [fill_path red] [draw_image] [fill_rect blue]
     |<--- batch 1 --->|  |<-batch 2->|  |<-batch 3->|  |<--batch 4-->|
     Pipeline: Solid      Pipeline: Solid  Pipeline: Image Pipeline: Solid
     Paint: red           Paint: red       (texture)      Paint: blue
     Draw calls: 1        Draw calls: 1    Draw calls: 1  Draw calls: 1
```

**Batch break conditions:**
- Pipeline change (solid → image → text → gradient)
- Paint change (different color, gradient, or stroke style)
- Clip state change (scissor rect or stencil path)
- Clear command (resets all batching state)

### Vertex layout

All geometry is expressed as `flux_image_vertex` (pos + UV):

```c
typedef struct {
    float pos[2];   /* screen-space position in pixels */
    float uv[2];    /* texture coordinates, or (0,0) for solid */
} flux_image_vertex;
```

- **Solid fills:** UV = (0,0), color from push constants.
- **Images:** UV mapped to source rectangle.
- **Text:** UV mapped to glyph atlas sub-rectangle.
- **Gradients:** UV = gradient parameters encoded as coordinates.

## Shader architecture

All shaders are compiled from GLSL to SPIR-V at build time and embedded into the library binary.

### Pipeline types

| Pipeline | Vertex Shader | Fragment Shader | Input |
|---|---|---|---|
| **Solid** | Passthrough + transform | Solid color | `flux_color` via push constants |
| **Image** | Passthrough + transform | Texture sample | `flux_image` via descriptor set |
| **Text** | Passthrough + transform | Atlas alpha × color | Atlas texture via descriptor set |
| **Gradient** | Passthrough + transform | Gradient interpolation | Stops + geometry via push constants |
| **Stencil** | Passthrough | Write stencil ref | (mask only, no color output) |
| **Blur** | Passthrough + transform | Separable Gaussian | Source texture + kernel params |

### Push constants

Per-draw paint data is passed via Vulkan push constants (fast, no descriptor set needed):

```c
typedef struct {
    float surface_size[2];   /* viewport dimensions for pixel-snapping */
    uint32_t mode;           /* 0 = solid, 1 = gradient, etc. */
    float color[4];          /* RGBA premultiplied */
} flux_solid_color_pc;
```

Push constants are updated between batches using `vkCmdPushConstants`, which has negligible overhead.

## Frame lifecycle

```text
Frame N-1          Frame N              Frame N+1
   |                  |                     |
   v                  v                     v
[GPU rendering]   [CPU recording]      [CPU recording]
   |                  |                     |
   |                  +-- Acquire canvas    +-- Acquire canvas (blocked if N not done)
   |                  |   Record ops         |
   |                  |   flux_surface_present |
   |                  |                     |
   +-- Present        +-- Submit cmd buf    +-- Submit cmd buf
   |                  |   Signal fence      |   Signal fence
   |                  |                     |
   +-- Fence signaled +-- Fence signaled    +-- ...
        (safe to          (safe to
         reuse buffers)    reuse buffers)
```

- **Double buffering:** Two frame slots rotate. While GPU renders frame N, CPU records frame N+1.
- **Automatic throttling:** `flux_surface_acquire` waits if both frames are in flight (backpressure from GPU).
- **No CPU stall:** Recording never blocks unless the GPU is more than one frame behind.

## Per-frame memory

Each frame owns:

| Resource | Size | Growth |
|---|---|---|
| Vertex buffer | 1 MB initial | Doubles on overflow |
| Arena allocator | 64 KB initial | Doubles on overflow |
| Descriptor pool | 16 sets | Fixed |
| Command buffer | 4 KB initial | Reallocated by Vulkan |

All per-frame memory is reset (not freed) at the start of each frame. No allocation happens on the steady-state hot path.

## Performance characteristics

| Operation | Typical cost | Notes |
|---|---|---|
| `flux_fill_rect` | 50 ns | Just struct init + array append |
| `flux_fill_path` (simple) | 200 ns | Bounds check + struct init |
| `flux_fill_path` (complex) | 1–5 µs | Matrix transform + path copy |
| Tessellation (simple rect) | 100 ns | 2 triangles |
| Tessellation (bezier) | 1–5 µs | Flattening + ear-clipping |
| Stroke expansion | 2–10 µs | Per-vertex geometry generation |
| Batch flush | 200 ns | `vkCmdPushConstants` + `vkCmdDraw` |
| Frame submit | 10–50 µs | `vkQueueSubmit` + `vkQueuePresent` |
| GPU frame time | 0.5–4 ms | Depends on scene complexity |

## Clipping

Two clipping mechanisms are implemented:

### Scissor rects

- **Implementation:** `vkCmdSetScissor` during command buffer recording.
- **Limitation:** Axis-aligned rectangles only.
- **Cost:** Near-zero (one Vulkan command per clip change).

### Stencil paths

- **Implementation:** Two-pass rendering:
  1. Fill clip path into stencil buffer (increment stencil on front faces).
  2. Render scene with stencil test enabled (`stencil_ref > 0`).
- **Limitation:** Requires stencil attachment on surface.
- **Cost:** One extra draw call per clipped batch.

## Gradient rendering

Gradients are evaluated in the fragment shader using push constants:

```glsl
// Linear gradient
float t = dot(pos - start, end - start) / dot(end - start, end - start);
t = clamp(t, 0.0, 1.0);

// Find segment between two stops
// Interpolate color
```

Up to 4 color stops are packed into push constants. More stops would require a texture lookup.

## See also

- [Bézier curves](bezier-curves.md) — curve flattening algorithm.
- [Text rendering pipeline](text-rendering-pipeline.md) — how text becomes pixels.
- [Architecture overview](architecture-overview.md) — runtime object model.
- [Performance tuning](../how-to/optimize-performance.md) — practical optimization tips.
