# How to Debug GPU Issues

When rendering looks wrong, crashes, or validation errors appear, use this guide to diagnose the problem systematically.

## Enable validation layers

Validation layers catch API misuse before it becomes a crash or visual glitch.

```bash
# Runtime
FX_ENABLE_VALIDATION=1 ./your_app

# Or at context creation
fx_context_desc desc = {
    .enable_validation = true,
};
```

Requirements:
- Compiled with `-Dvalidation=enabled` or `auto` (default).
- `vulkan-validation-layers` package installed.

Validation adds 2–5× CPU overhead. Never ship with it enabled.

## Read the log

flux logs to stderr by default with ISO-8601 timestamps and source locations:

```
[2026-04-25T14:32:01.123Z][flux E][src/vk/device.c:142] vkCreateDevice failed => VkResult -3
```

Set a custom logger to forward messages to your application's logging system:

```c
void my_log(fx_log_level lvl, const char *file, int line,
            const char *fmt, const char *msg, void *user)
{
    /* file:line points to the call site in flux source */
    fprintf(stderr, "[%s:%d] %s\n", file, line, msg);
}

fx_context_desc desc = { .log = my_log };
```

Log levels (in order of severity):
- `FX_LOG_TRACE` — internal bookkeeping, vertex counts, batch stats
- `FX_LOG_DEBUG` — pipeline creation, swapchain details
- `FX_LOG_INFO` — surface creation, atlas events, general status
- `FX_LOG_WARN` — atlas eviction, fallback paths, performance hints
- `FX_LOG_ERROR` — Vulkan errors, out-of-memory, invalid parameters

Trace and debug are compiled out in `NDEBUG` builds (`FX_LOGD` and `FX_LOGT` expand to nothing).

## Common symptoms

### Black screen or nothing rendered

1. **Did you call `fx_surface_present`?** The canvas is only a recorder; nothing reaches the GPU until present.
2. **Is the clear color opaque?** A clear with alpha=0 on a swapchain with premultiplied alpha may appear black.
3. **Check validation output.** A missing descriptor set or unbound pipeline usually prints an error.
4. **Verify the surface extent.** If the window is 0×0, the viewport is empty.

### Garbled or missing text

1. **Did you shape the text?** `fx_draw_glyph_run` requires glyph IDs and positions. Passing unshaped character codes will not render.
2. **Check the atlas log.** If you see `"glyph atlas full: evicting N entries"`, the atlas is thrashing. Reduce font/size variety or pre-load glyphs.
3. **Verify the font loaded.** `fx_font_create` returns `NULL` if FreeType cannot open the file. Check the error log.

### Flickering or tearing

1. **Are you acquiring and presenting from the same thread?** Cross-thread surface use causes synchronization errors.
2. **Check the swapchain present mode.** Some compositors force mailbox mode, which can drop frames. This is usually correct behavior.
3. **Validate fence timing.** If you manually call `vkQueueSubmit` on flux-owned queues, you may interfere with frame pacing.

### Validation error: "Image layout does not match"

This usually means an image was not transitioned to `SHADER_READ_ONLY_OPTIMAL` before sampling. flux transitions images automatically at creation and during upload, but if you access `fx_context_get_instance` and submit your own command buffers that sample flux-owned images, you must manage layouts yourself.

### Validation error: "Stencil buffer not cleared"

Path clipping and stencil-based fills clear the stencil region before use. If you see this error, it may indicate a driver bug or an uninitialized stencil attachment. File an issue with `vulkaninfo` output.

## GPU debugging tools

### RenderDoc

[RenderDoc](https://renderdoc.org/) is the recommended tool for frame capture and analysis.

1. Launch your application through RenderDoc.
2. Capture a frame (F12 or trigger key).
3. Inspect:
   - **Texture viewer** — verify atlas, images, and output attachments.
   - **Mesh viewer** — inspect generated vertex data for paths and strokes.
   - **Pipeline state** — confirm shader constants, scissor, and stencil state.
   - **Event browser** — trace the sequence of draw calls.

### Vulkan Info

```bash
vulkaninfo --summary
```

Collect this output when reporting GPU-specific bugs.

### Mesa environment variables (Linux)

```bash
# Enable Mesa debug output
MESA_DEBUG=flush ./your_app

# Force software rendering for comparison
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json ./your_app
```

If a bug reproduces on lavapipe but not a physical GPU, it is likely a driver bug. If it reproduces on both, it is likely a flux bug.

## Capturing a minimal reproduction

When reporting a rendering bug, include:

1. A single-frame capture (RenderDoc `.rdc` if possible).
2. The smallest C file that reproduces the issue.
3. `vulkaninfo --summary` output.
4. Validation layer output (if any).
5. Expected vs. actual screenshot.

## Performance debugging

If frames are slow but correct:

1. **Profile with `bench_ui`.**
   ```bash
   ./build/bench/bench_ui
   ```
2. **Check for atlas eviction.** Eviction triggers `vkDeviceWaitIdle` and stalls the GPU.
3. **Count draw calls.** Each batch break, gradient switch, or image draw emits a new draw call. Group solid-color ops together.
4. **Check staging buffer growth.** Large image uploads that exceed the current staging buffer size cause reallocation. Keep upload sizes stable.

## See also

- [How to troubleshoot common issues](troubleshooting.md)
- [How to optimize performance](optimize-performance.md)
- [Testing](../dev/testing.md)
