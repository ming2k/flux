# How to Record and Present a Frame

This guide assumes you already have an `flux_surface`. For surface construction, see the [API reference](../reference/api.md).

## When to use this

Use this pattern for every visible or offscreen frame rendered by flux.

Every visible frame follows the same shape: acquire a canvas, record commands,
then present. The returned `flux_canvas` is frame-local and must not be kept after
`flux_surface_present`.

```c
#include <flux/flux.h>

void draw_frame(flux_surface *surface)
{
    flux_canvas *c = flux_surface_acquire(surface);
    if (!c) return;

    flux_clear(c, flux_color_rgba(18, 20, 24, 255));

    /* Record draw calls here. */

    flux_surface_present(surface);
}
```

The application owns the event loop. On Vulkan, process Vulkan events, resize
the flux surface when the window size changes, acquire a flux canvas, record the
frame, and present.

Recorded operations borrow resources such as `flux_path`, `flux_image`,
and `flux_glyph_run` until present. Destroy or mutate those objects only after the
frame has been presented unless the object is not referenced by the current
frame.

## Verification

For offscreen surfaces, call `flux_surface_read_pixels` after presenting and check that the output buffer contains the expected pixels. For Vulkan surfaces, verify that the compositor displays the updated frame.
