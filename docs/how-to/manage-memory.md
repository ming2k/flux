# How to Manage Memory

flux uses explicit, predictable memory patterns. This guide explains the ownership model, allocation strategies, and how to avoid leaks or excessive churn.

## Ownership model

### Caller-owned objects

Objects created with `*_create` and destroyed with `*_destroy`:

| Object | Create | Destroy | Notes |
|---|---|---|---|
| `fx_context` | `fx_context_create` | `fx_context_destroy` | Owns Vulkan instance, device, glyph atlas, pipeline cache. |
| `fx_surface` | `fx_surface_create_*` | `fx_surface_destroy` | Vulkan, Vulkan, or offscreen. |
| `fx_image` | `fx_image_create` | `fx_image_destroy` | Retains a CPU-side pixel copy unless created without initial data. |
| `fx_path` | `fx_path_create` | `fx_path_destroy` | CPU-side verb/point array. |
| `fx_gradient` | `fx_gradient_create_*` | `fx_gradient_destroy` | Small CPU struct (no GPU resource). |
| `fx_glyph_run` | `fx_glyph_run_create` | `fx_glyph_run_destroy` | Dynamic glyph array. |

### Borrowed objects

- `fx_canvas` — borrowed from the surface. Valid only between `fx_surface_acquire` and `fx_surface_present`. Do not store the pointer across frames.
- Resource references in recorded ops — when you call `fx_fill_path(c, path, paint)`, the canvas records a pointer to `path`. You must keep `path` alive until `fx_surface_present` completes.

### Internal copies

When you record a path under a non-identity transformation matrix, flux creates an internal transformed copy:

```c
fx_translate(c, 100, 50);
fx_fill_path(c, path, &paint);  /* internal copy of path is made */
```

The canvas owns this copy and destroys it at the next `fx_surface_acquire` (via `fx_canvas_reset`). You do not need to manage it.

## Frame-local memory

Each surface frame owns two allocators that are reset every frame:

1. **Arena allocator** (`fx_arena`) — used for tessellation output, stroke expansion, and path flattening.
2. **Vertex buffer pool** (`fx_vbuf_pool`) — used for GPU vertex data.

These allocators grow to a steady-state size and then stop allocating. If you see continuous memory growth in a profiler, you are likely creating an unbounded number of unique paths or images per frame.

### Diagnosing arena growth

Enable trace logging (debug builds only):

```bash
FX_ENABLE_VALIDATION=1 ./your_app
```

Watch for repeated `fx_arena_alloc` calls with ever-larger sizes. Typical per-frame arena usage should plateau after the first few frames.

## Glyph atlas memory

The glyph atlas is a context-wide 2048×2048 `A8_UNORM` texture (2 MiB GPU memory) plus a CPU-side entry array. It grows on demand and evicts when full.

**Memory tip:** Each unique `(font, glyph_id)` pair consumes atlas space. Using 10 font sizes of the same typeface creates 10 distinct atlas entries. If memory is tight, limit the number of concurrent font/size combinations.

## Image memory

Images are stored in `DEVICE_LOCAL` GPU memory via VMA. By default, `fx_image_create` retains a CPU-side copy of the pixel data (accessible via `fx_image_data`). This doubles memory usage for static images.

If you do not need CPU readback, create the image without initial data and upload separately:

```c
fx_image_desc desc = {
    .width = 512,
    .height = 512,
    .format = FX_FMT_RGBA8_UNORM,
    .data = NULL,  /* no CPU copy retained */
};
fx_image *img = fx_image_create(ctx, &desc);
fx_image_update(img, pixels, stride);  /* upload to GPU, then free pixels */
```

## Cleanup order

Destroy objects in reverse dependency order:

```c
/* Good: surfaces before context, resources before surfaces */
fx_surface_destroy(surface);   /* waits on in-flight frames */
fx_image_destroy(image);       /* safe after surface is done with it */
fx_path_destroy(path);
fx_context_destroy(ctx);       /* destroys device, atlas, pipelines */
```

Destroying a context before its surfaces or images is undefined behavior.

## Memory leaks to watch for

1. **Forgotten glyph runs.** `fx_glyph_run_create` allocates heap memory. If you create a new run every frame and never destroy it, you leak.

2. **Path accumulation.** Creating a new `fx_path` every frame without destroying the old one leaks the verb/point arrays.

3. **Gradient leaks.** `fx_gradient` is small, but creating one per frame without destroying it still leaks.

4. **Image update without fence wait.** `fx_image_update` waits on the image's `last_use_fence` before overwriting, but if you update from a different thread than the render thread, you may need external synchronization. See [Thread safety](thread-safety.md).

## Valgrind / ASan

flux is compatible with AddressSanitizer and Valgrind (for CPU allocations). GPU memory allocated through VMA is not tracked by Valgrind.

```bash
meson setup build -Db_sanitize=address
meson compile -C build
meson test -C build --suite unit
```

## See also

- [Thread safety](thread-safety.md)
- [How to optimize performance](optimize-performance.md)
- [API reference](api.md)
