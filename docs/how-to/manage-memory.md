# How to Manage Memory

flux uses explicit, predictable memory patterns. This guide explains the ownership model, allocation strategies, and how to avoid leaks or excessive churn.

## Ownership model

### Caller-owned objects

Objects created with `*_create` and destroyed with `*_destroy`:

| Object | Create | Destroy | Notes |
|---|---|---|---|
| `flux_context` | `flux_context_create` | `flux_context_destroy` | Owns Vulkan instance, device, glyph atlas, pipeline cache. |
| `flux_surface` | `flux_surface_create_*` | `flux_surface_destroy` | Vulkan, Vulkan, or offscreen. |
| `flux_image` | `flux_image_create` | `flux_image_destroy` | Retains a CPU-side pixel copy unless created without initial data. |
| `flux_path` | `flux_path_create` | `flux_path_destroy` | CPU-side verb/point array. |
| `flux_gradient` | `flux_gradient_create_*` | `flux_gradient_destroy` | Small CPU struct (no GPU resource). |
| `flux_glyph_run` | `flux_glyph_run_create` | `flux_glyph_run_destroy` | Dynamic glyph array. |

### Borrowed objects

- `flux_canvas` — borrowed from the surface. Valid only between `flux_surface_acquire` and `flux_surface_present`. Do not store the pointer across frames.
- Resource references in recorded ops — when you call `flux_fill_path(c, path, paint)`, the canvas records a pointer to `path`. You must keep `path` alive until `flux_surface_present` completes.

### Internal copies

When you record a path under a non-identity transformation matrix, flux creates an internal transformed copy:

```c
flux_translate(c, 100, 50);
flux_fill_path(c, path, &paint);  /* internal copy of path is made */
```

The canvas owns this copy and destroys it at the next `flux_surface_acquire` (via `flux_canvas_reset`). You do not need to manage it.

## Frame-local memory

Each surface frame owns two allocators that are reset every frame:

1. **Arena allocator** (`flux_arena`) — used for tessellation output, stroke expansion, and path flattening.
2. **Vertex buffer pool** (`flux_vbuf_pool`) — used for GPU vertex data.

These allocators grow to a steady-state size and then stop allocating. If you see continuous memory growth in a profiler, you are likely creating an unbounded number of unique paths or images per frame.

### Diagnosing arena growth

Enable trace logging (debug builds only):

```bash
FLUX_ENABLE_VALIDATION=1 ./your_app
```

Watch for repeated `flux_arena_alloc` calls with ever-larger sizes. Typical per-frame arena usage should plateau after the first few frames.

## Glyph atlas memory

The glyph atlas is a context-wide 2048×2048 `A8_UNORM` texture (4 MiB CPU memory) plus a CPU-side entry array. It grows on demand; when full, `flux_glyph_upload` returns `FLUX_ERROR_OUT_OF_MEMORY`.

**Memory tip:** Each unique `(font, glyph_id)` pair consumes atlas space. Using 10 font sizes of the same typeface creates 10 distinct atlas entries. If memory is tight, limit the number of concurrent font/size combinations.

## Image memory

Images are stored in `DEVICE_LOCAL` GPU memory via VMA. By default, `flux_image_create` retains a CPU-side copy of the pixel data (accessible via `flux_image_data`). This doubles memory usage for static images.

If you do not need CPU readback, create the image without initial data and upload separately:

```c
flux_image_desc desc = {
    .width = 512,
    .height = 512,
    .format = FLUX_FMT_RGBA8_UNORM,
    .data = NULL,  /* no CPU copy retained */
};
flux_image *img = flux_image_create(ctx, &desc);
flux_image_update(img, pixels, stride);  /* upload to GPU, then free pixels */
```

## Cleanup order

Destroy objects in reverse dependency order:

```c
/* Good: surfaces before context, resources before surfaces */
flux_surface_destroy(surface);   /* waits on in-flight frames */
flux_image_destroy(image);       /* safe after surface is done with it */
flux_path_destroy(path);
flux_context_destroy(ctx);       /* destroys device, atlas, pipelines */
```

Destroying a context before its surfaces or images is undefined behavior.

## Memory leaks to watch for

1. **Forgotten glyph runs.** `flux_glyph_run_create` allocates heap memory. If you create a new run every frame and never destroy it, you leak.

2. **Path accumulation.** Creating a new `flux_path` every frame without destroying the old one leaks the verb/point arrays.

3. **Gradient leaks.** `flux_gradient` is small, but creating one per frame without destroying it still leaks.

4. **Image update without fence wait.** `flux_image_update` waits on the image's `last_use_fence` before overwriting, but if you update from a different thread than the render thread, you may need external synchronization. See [Thread safety](thread-safety.md).

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
