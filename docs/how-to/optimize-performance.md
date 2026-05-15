# How to Optimize Performance

This guide covers the practical techniques for keeping flux rendering fast. Targets: < 4 ms GPU for a full-screen 4K scene of ~200 ops on integrated graphics.

## Quick wins

### 1. Batch same-colored operations

flux automatically batches consecutive `fx_fill_rect` and polygon-fill ops that share the same solid color into a single draw call. To take advantage of this, group your drawing by color where possible:

```c
/* Good: all red rects together, then all blue rects */
fx_paint red, blue;
fx_paint_init(&red, 0xFFFF0000);
fx_paint_init(&blue, 0xFF0000FF);

for (size_t i = 0; i < n_red; ++i)
    fx_fill_rect(c, &red_rects[i], red.color);
for (size_t i = 0; i < n_blue; ++i)
    fx_fill_rect(c, &blue_rects[i], blue.color);
```

Switching paint state (color, gradient, image) breaks the batch and emits a new draw call.

### 2. Prefer `fx_fill_rect` over `fx_fill_path` for rectangles

`fx_fill_rect` records a dedicated `FX_OP_FILL_RECT` op that routes directly to the batched solid-rect backend without allocating a temporary `fx_path`. Transformed (non-axis-aligned) rectangles still fall back to path fill automatically, but the common case is allocation-free.

### 3. Reuse paths and images across frames

Creating and destroying GPU resources every frame is expensive. Build your paths and images during initialization or level load, then reference them each frame.

```c
/* Initialization */
fx_path *star = fx_path_create();
build_star_path(star);

/* Per frame */
fx_fill_path(c, star, &paint);
```

### 4. Minimize canvas state changes

`fx_save` / `fx_restore`, `fx_clip_rect`, and `fx_reset_clip` are cheap on the CPU but can introduce GPU-side state changes (scissor updates, stencil clears). Avoid redundant clip operations:

```c
/* Avoid: clip, draw, reset, clip again, draw, reset */
fx_clip_rect(c, &panel);
draw_panel_contents(c);
fx_reset_clip(c);
fx_clip_rect(c, &panel);  /* redundant if you never left the panel */
draw_more_contents(c);
fx_reset_clip(c);

/* Better: clip once, draw everything, then reset */
fx_clip_rect(c, &panel);
draw_panel_contents(c);
draw_more_contents(c);
fx_reset_clip(c);
```

## Memory and allocation

### Frame-local arenas

Each surface frame owns an arena allocator (default block size 64 KiB). Tessellation, stroking, and path flattening allocate from this arena. The arena is reset (not freed) at the start of each frame, so the steady-state allocation cost is zero.

If you see arena growth across frames, you are likely creating unbounded unique paths per frame. Cache and reuse paths instead.

### Glyph atlas

The glyph atlas is a single 2048×2048 `A8_UNORM` texture shared across the context. It uses shelf packing and automatically evicts when full. Atlas eviction triggers `vkDeviceWaitIdle`, which stalls the GPU pipeline.

**Avoid eviction stalls:**
- Pre-populate the atlas with your common glyph sets during initialization.
- Limit the number of unique font/size combinations in a single scene.
- Avoid rendering extremely large glyphs (> 2046 px) — they are rejected upfront.

## GPU upload

### Image uploads

`fx_image_create` and `fx_image_update` use a unified upload path with a persistent staging buffer. The first upload for a given image size allocates staging memory; subsequent uploads of the same or smaller size reuse it.

**Best practice:** Create images with their final size at load time. If you must update them dynamically, keep the size stable to avoid staging reallocation.

### Double-buffer dynamic images

If you need to update an image every frame (e.g., video frames, camera feed), create two `fx_image` objects and alternate:

```c
fx_image *frames[2] = { create_image(w, h), create_image(w, h) };
uint32_t idx = 0;

while (running) {
    update_pixels(cpu_buffer);
    fx_image_update(frames[idx], cpu_buffer, stride);
    fx_draw_image(c, frames[idx], &src, &dst);
    idx ^= 1;
}
```

This avoids the implicit fence wait in `fx_image_update` because the previous frame has already finished sampling the alternate image.

## Surface and swapchain

### Offscreen surfaces

Offscreen surfaces are ideal for headless rendering, automated testing, and texture pre-rendering. They skip swapchain overhead but still create a full render pass and framebuffer.

**Tip:** If you only need a small render target, create the offscreen surface at that exact size. Do not allocate a 4K offscreen surface to render a 256×256 icon.

### Resize handling

Window resizes trigger swapchain recreation. In flux, pipeline sets are cached by `(format, samples)` and survive resizes, so shader recompilation does not occur. However, the framebuffer, stencil attachment, and swapchain images are recreated.

**Best practice:** Debounce resize events. Do not call `fx_surface_resize` for every intermediate size during a live resize drag.

## Validation layers

Validation layers add significant CPU overhead (sometimes 2–5× frame time). Always ship with validation disabled:

```c
fx_context_desc desc = {
    .enable_validation = false,
};
```

Use validation only during development and CI.

## Benchmarking

Use `bench/bench_ui` to measure your changes:

```bash
./build/bench/bench_ui
```

The benchmark reports p50/p95/p99/peak frame times. Compare before and after numbers for the same GPU. See [Testing](dev/testing.md) for the benchmark methodology.

## Checklist

- [ ] Group solid-color fills together to maximize batching.
- [ ] Use `fx_fill_rect` for rectangles instead of building a path.
- [ ] Cache `fx_path` and `fx_image` objects across frames.
- [ ] Pre-load common glyphs to avoid atlas eviction.
- [ ] Double-buffer dynamically updated images.
- [ ] Debounce window resizes.
- [ ] Disable validation in release builds.
- [ ] Profile with `bench_ui` after significant changes.

## See also

- [How to record and present a frame](record-and-present-a-frame.md)
- [Testing](../dev/testing.md)
- [Vulkan backend explanation](../explanation/vulkan-backend.md)
