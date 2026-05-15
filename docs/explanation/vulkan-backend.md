# Vulkan Backend

This document covers the Vulkan objects flux owns, the choices made in
structuring them, and the invariants that keep the codebase correct.

## Baseline

Vulkan 1.2. No 1.3 features are required.

## Memory Management

flux uses two allocators with different lifetimes.

### Per-Frame Linear Buffer Pool (`src/rhi/vulkan/vulkan_rhi.c`)

Each in-flight frame owns an `flux_vbuf_pool`: a persistently mapped
`HOST_VISIBLE | HOST_COHERENT` allocation used for vertices, indices, and
small uniforms written each frame.

- **Dynamic growth.** Starts at 4 MiB per frame. If a recording exceeds
  the current chunk, the pool allocates a new chunk (doubling the size)
  and chains it in. Old chunks are retired and destroyed only after the
  frame's fence signals, ensuring GPU safety.
- **Reset per frame.** `flux_vbuf_pool_reset` rewinds the write cursor
  when the frame's in-flight fence signals. Memory is not freed between
  frames.
- **Alignment.** All allocations are 16-byte aligned.

### Image Memory

User images and the Glyph Atlas are allocated in `DEVICE_LOCAL` memory
for maximum sampling performance via **VMA** (`vmaCreateImage`).
All `VkDeviceMemory` backing is suballocated by VMA, so flux does not
burn dedicated `VkDeviceMemory` slots per image.

## Upload Subsystem (`src/rhi/vulkan/vulkan_rhi.c`)

Every GPU upload in flux — image creation, `flux_image_update`, glyph
atlas inserts — goes through one path on `flux_context`:

- **Persistent staging buffer.** Host-visible, host-coherent,
  persistently mapped. Grows power-of-two on demand, never shrinks.
  Replaces the per-call `VkBuffer` + `VkDeviceMemory` create/destroy
  cycle that v0.0.1 and earlier used.
- **Reusable command buffer and fence.** One of each, both allocated
  at device init and destroyed at device shutdown. The upload path
  calls `vkResetCommandBuffer` + `vkResetFences` per upload rather
  than allocating new objects.
- **Layout-aware barriers.** `flux_upload_image` takes the caller's
  `old_layout` and emits an appropriate barrier (source stage and
  access mask) so that `UNDEFINED`, `TRANSFER_DST_OPTIMAL`, and
  `SHADER_READ_ONLY_OPTIMAL` all work as source layouts.
- **Synchronous.** The current API submits the upload and waits on
  the reusable fence before returning. This preserves the simple
  "image is usable as soon as the call returns" contract. Deferred
  submission (batching uploads into the next render submission) is a
  later optimization — see the known gaps in
  [release process](../dev/release-process.md).

The upload subsystem is private to `flux_context`; callers never touch
it directly. The public surface is `flux_image_create`,
`flux_image_update`, and internally `flux_atlas_ensure_glyph`.

## Draw Call Batching

The renderer automatically batches primitives to minimize Vulkan
overhead.

- **Grouping.** Sequential `flux_fill_path` / `flux_stroke_path` ops that
  share the same `flux_paint` color and pipeline are merged into a
  single `vkCmdDraw`.
- **Flushing.** A batch is flushed when:
    1. The paint color changes.
    2. An operation requires a different pipeline (Path → Image, clip
       state change, gradient).
    3. The vertex buffer pool rolls over to a new chunk.
    4. The recording ends.

## Pipeline Architecture

Pipelines are **context-shared**, not surface-lifetime. They are keyed
by `(format, samples)` and stored in `flux_context.pipeline_sets[]`.
A pipeline set is created on first use and reused across every surface
and resize. Only `flux_context_destroy` tears them down (via
`flux_pipeline_set_destroy_all`).

| Pipeline | Vertex Format | Push Constants | Descriptor Sets |
|---|---|---|---|
| **Solid** | `pos[2]` | `surface_size`, `color` | None |
| **Image** | `pos[2]`, `uv[2]` | `surface_size` | 1 (Sampler + Image) |
| **Text** | `pos[2]`, `uv[2]` | `surface_size`, `color` | 1 (Sampler + Atlas) |
| **Gradient** | `pos[2]` | `surface_size`, mode, stops, colors | None |
| **Cover** | `pos[2]` | `surface_size`, `color` | None |
| **Stencil** | `pos[2]` | `surface_size` | None |
| **Blur** | `pos[2]`, `uv[2]` | `surface_size`, direction, sigma | 1 (Sampler + Image) |
| **Fringe** | `pos[2]`, `coverage` | `surface_size`, `color` | None |

- **Topology.** All pipelines use `TRIANGLE_LIST`.
- **Blending.** Blend mode is set dynamically via
  `vkCmdSetColorBlendEquationEXT` from `VK_EXT_extended_dynamic_state3`.
  All 13 `flux_blend_mode` values are supported: the 12 Porter-Duff
  modes map to standard `VkBlendFactor` pairs, while `MULTIPLY`,
  `SCREEN`, and `OVERLAY` require `VK_EXT_blend_operation_advanced`
  and fall back to `SRC_OVER` when that extension is unavailable.
- **Push constants** are used for frequently changing per-draw state
  (viewport size, primary color, gradient stops). This avoids UBO
  binding churn.
- **Dynamic state.** `VK_DYNAMIC_STATE_VIEWPORT`,
  `VK_DYNAMIC_STATE_SCISSOR`, `VK_DYNAMIC_STATE_STENCIL_REFERENCE`,
  and `VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT` are dynamic so
  pipelines survive viewport, clip, and blend-mode changes without
  pipeline variants.

## Path Anti-Aliasing

Path fills use **stencil-then-cover** with an additional **fringe** pass:

1. **Stencil.** Tessellated path triangles increment the stencil buffer.
2. **Cover.** A bounding-box quad fills pixels where stencil == 1.
3. **Fringe.** Extra triangles expand each path edge outward by 0.5 px.
   Per-vertex coverage (1.0 on the original edge, 0.0 on the outer
   boundary) is interpolated in the fragment shader and multiplied
   with the draw colour before blending.

This gives smooth silhouette edges without MSAA or analytic coverage.

## Glyph Atlas

The **Glyph Atlas** is a context-global resource managed in
`src/resource/text.c`.

- **Format.** 2048×2048 `A8_UNORM` single-channel alpha.
- **Algorithm.** Shelf-packing allocator with 2-pixel inter-glyph
  padding to prevent bilinear filtering bleed.
- **Updates.** Callers rasterize glyphs externally (e.g., FreeType) and
  upload bitmaps via `flux_glyph_upload`. The atlas packs them into the
  GPU texture through the shared upload subsystem.
- **Overflow.** When the atlas cannot fit a new glyph, `flux_glyph_upload`
  returns `FLUX_ERROR_OUT_OF_MEMORY`. The caller decides what to do next:
  skip the glyph, re-rasterize at a smaller size, or clear and rebuild.
  flux does not perform automatic eviction; keeping the atlas stateless
  and predictable is a deliberate simplicity bound. A true LRU eviction
  policy is a later optimization once there is a workload that needs it.

## Descriptor Management

flux uses a **per-frame descriptor pool** strategy:

- Each in-flight frame has its own `VkDescriptorPool`.
- Pools are reset at the start of each frame
  (`vkResetDescriptorPool`).
- Descriptor sets for images and the glyph atlas are allocated
  on-the-fly during recording. This avoids complex recycling and
  eliminates CPU/GPU synchronization stalls.

Per-draw descriptor allocation is mitigated by a **per-frame descriptor
cache**: each `flux_frame` keeps a small array of `(image_view, sampler) →
VkDescriptorSet` entries. Reusing the same image in a frame hits the
cache instead of allocating a new set. The cache is cleared at the
start of each frame when the descriptor pool is reset.

## Readback

`flux_surface_read_pixels` (offscreen surfaces only) uses a persistent
per-surface staging buffer — host-visible, host-coherent,
persistently mapped — that grows to fit the largest requested
`stride × height`. It is created on first call and freed with the
surface.

Synchronization: `flux_surface_read_pixels` calls
`flux_surface_wait_idle` before submitting its copy, which waits on
every in-flight frame fence. `flux_surface_present` itself no longer
blocks the caller on the offscreen path — ownership of "wait before
touching the image" belongs to the consumers (read_pixels,
subsequent acquire, surface destroy).

## Synchronization

flux runs a two-frame in-flight pipeline:

- **Frame fences (`in_flight`).** Ensure the CPU does not start
  recording frame N until the GPU has finished executing the previous
  iteration of frame N. On swapchain surfaces these are waited in
  `flux_surface_acquire`.
- **Swapchain semaphores.** `image_available` and `render_finished`
  coordinate the swapchain acquire → graphics submit → present
  handoff on windowed surfaces.
- **Upload fence.** A single `VkFence` on `flux_context` waited
  synchronously by every GPU upload. See "Upload Subsystem" above.
- **Offscreen path.** No presentation engine, no swapchain
  semaphores. `flux_surface_present` submits with the frame fence and
  returns. Callers synchronize via `flux_surface_read_pixels`,
  `flux_surface_wait_idle`, or `flux_surface_destroy` before touching
  the rendered image.

## Physical Device Selection

`flux_device_init` scores devices: Discrete > Integrated > Virtual. It
requires:

1. `VK_KHR_swapchain` support.
2. A queue family supporting graphics (and presentation to the target
   surface, if one is provided).
3. `A8_UNORM` and `RGBA8_UNORM` with `SAMPLED_IMAGE` feature support.

The context keeps the selected device's `VkPhysicalDeviceProperties`
and `VkPhysicalDeviceMemoryProperties` cached on `flux_context` so that
later allocators and pipeline builders do not re-query the driver.

## Destroy Order

When `flux_surface_destroy` runs:

1. `flux_surface_wait_idle` waits on every frame fence.
2. `vkDeviceWaitIdle` ensures no queue work remains.
3. Swapchain-scoped resources tear down
   (`flux_swapchain_destroy` for swapchain surfaces; an inline block
   for offscreen surfaces: framebuffer, readback buffer,
   offscreen image, stencil attachment, per-frame sync).
4. Surface-scoped resources tear down
   (`flux_surface_destroy_pipelines`: sampler, render pass).
5. `VkSurfaceKHR` (swapchain surfaces only) is destroyed.
6. The canvas display list is disposed and the surface is freed.

## Related decisions

- [ADR-0002: Keep flux as a low-level rendering substrate](../adr/0002-keep-flux-as-low-level-rendering-substrate.md)
