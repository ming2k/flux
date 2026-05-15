# How to Record and Present a Frame

This guide assumes you already have an `fx_surface`. For surface construction, see the [API reference](../reference/api.md).

## When to use this

Use this pattern for every visible or offscreen frame rendered by flux.

Every visible frame follows the same shape: acquire a canvas, record commands,
then present. The returned `fx_canvas` is frame-local and must not be kept after
`fx_surface_present`.

```c
#include <flux/flux.h>

void draw_frame(fx_surface *surface)
{
    fx_canvas *c = fx_surface_acquire(surface);
    if (!c) return;

    fx_clear(c, fx_color_rgba(18, 20, 24, 255));

    /* Record draw calls here. */

    fx_surface_present(surface);
}
```

The application owns the event loop. On Vulkan, process Vulkan events, resize
the flux surface when the window size changes, acquire a flux canvas, record the
frame, and present.

Recorded operations borrow resources such as `fx_path`, `fx_image`,
and `fx_glyph_run` until present. Destroy or mutate those objects only after the
frame has been presented unless the object is not referenced by the current
frame.

## Verification

For offscreen surfaces, call `fx_surface_read_pixels` after presenting and check that the output buffer contains the expected pixels. For Vulkan surfaces, verify that the compositor displays the updated frame.
