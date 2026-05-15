# API Reference

This document is the canonical companion to `<flux/flux.h>`. The
header is the source of truth; this page explains the conventions and
the relationships between the moving parts.

For application-level integration recipes, see the [How-to
guides](../how-to/).

## Conventions

flux is a C foundation library. Every callable, type, and macro uses
the `flux_` / `FLUX_` prefix and follows the rules below.

| Convention | Rule |
|---|---|
| Naming | `flux_*` for symbols, `FLUX_*` for macros and enum members. |
| Handles | Opaque, refcounted. Never dereference. |
| Constructors | Always `_create*`, take ctx + desc, return `flux_result` and write to `out_*`. |
| Destructors | Always `_release`. NULL-safe. |
| Sharing | `_retain` increments the refcount; chainable; NULL-safe. |
| Errors | `flux_result`. Compare with `FLUX_OK`. `flux_result_string(r)` for diagnostics. |
| Descriptors | Every `*_desc` starts with `uint32_t size`; set it to `sizeof(*desc)`. |
| Thread model | One context per thread. Resources may not be shared between contexts. |

### Borrowed vs owned

flux is explicit about who owns what.

- **Owned.** Anything returned from `_create*` is owned by the caller
  and must be released. `flux_paint`, `flux_path`, `flux_gradient`,
  `flux_image`, `flux_glyph_run`.
- **Borrowed.** `flux_canvas` returned from `flux_surface_acquire` is
  valid until the matching `flux_surface_present`. Do not retain it.
- **Borrowed inside ops.** `flux_canvas_fill_path`, `_stroke_path`,
  `_clip_path`, `_draw_image`, `_draw_glyph_run` borrow their resource
  arguments. The caller must keep them alive until present.
- **Snapshotted.** `flux_paint` is deep-copied (including dash array
  and gradient retain) at record time, so the caller may release or
  mutate the paint immediately.

## Context

```c
flux_context_desc desc = {
    .size          = sizeof(desc),
    .min_log_level = FLUX_LOG_INFO,
    /* .allocator left zero-initialised → libc malloc */
};
flux_context *ctx = NULL;
flux_result r = flux_context_create(&desc, &ctx);
```

`desc` may be `NULL` for "all defaults."

### Allocator

To route allocations through your own allocator, fill in
`desc.allocator`:

```c
flux_context_desc desc = {
    .size      = sizeof(desc),
    .allocator = {
        .alloc   = my_alloc,
        .realloc = my_realloc,
        .free    = my_free,
        .user    = &my_arena,
    },
};
```

If any of `alloc` / `realloc` / `free` is non-NULL, all three must be.
The allocator's user pointer is forwarded verbatim. The allocator
must outlive the context and every resource derived from it.

### Logger

Set `desc.log` to a `flux_log_fn` to capture diagnostics. The default
sink writes to `stderr` with timestamps and `file:line` annotations.

| Level | Macro (internal) | Elided in `NDEBUG`? |
|---|---|---|
| `FLUX_LOG_TRACE` | `FLUX_LOGT` | yes |
| `FLUX_LOG_DEBUG` | `FLUX_LOGD` | yes |
| `FLUX_LOG_INFO`  | `FLUX_LOGI` | no  |
| `FLUX_LOG_WARN`  | `FLUX_LOGW` | no  |
| `FLUX_LOG_ERROR` | `FLUX_LOGE` | no  |

Filter at runtime with `desc.min_log_level`.

### Capabilities

```c
flux_capabilities caps = { .size = sizeof(caps) };
flux_get_capabilities(&caps);
```

Reports `has_vulkan`, `has_software`, `has_stencil`, `has_msaa`,
`max_gradient_stops`, `max_image_size`, `max_surface_size`. The struct
is sized for forward compatibility.

## Math

### Matrix

`flux_matrix` stores six floats column-major:

```
[ m[0]  m[2]  m[4] ]
[ m[1]  m[3]  m[5] ]
[  0     0     1   ]
```

```c
flux_matrix m;
flux_matrix_identity(&m);
flux_matrix_translation(&m, 100.0f, 50.0f);
flux_matrix_rotation(&m, 1.5707963f);

flux_matrix product;
flux_matrix_multiply(&product, &a, &b);

flux_matrix inv;
if (!flux_matrix_invert(&m, &inv)) { /* singular */ }

float x = 0, y = 0;
flux_matrix_transform_point(&m, &x, &y);

flux_rect rect_out;
flux_matrix_transform_rect(&m, &rect_in, &rect_out);
```

### Color

```c
flux_color c    = flux_color_rgba(255, 128, 0, 200);   // premultiplies
flux_color cpre = flux_color_rgba_premul(160, 80, 0, 200); // already premul

uint8_t r, g, b, a;
flux_color_unpack(c, &r, &g, &b, &a);                  // undoes premul
```

`flux_color` is `0xAARRGGBB` premultiplied. The pack helpers are
`static inline`; `unpack` is out-of-line.

## Surface

### Offscreen

```c
flux_surface *s = NULL;
flux_surface_create_offscreen(ctx, 256, 256,
    FLUX_FMT_RGBA8_UNORM, FLUX_CS_SRGB, &s);
```

### Vulkan

```c
#include <flux/flux_vulkan.h>

flux_vulkan_device dev = { .size = sizeof(dev) };
flux_vulkan_device_create(&desc, exts, ext_count, &dev);

flux_surface *s = NULL;
flux_surface_create_vulkan(ctx, &dev, vk_surface,
    width, height, FLUX_CS_SRGB, &s);
```

flux borrows the device and the `VkSurfaceKHR` for the lifetime of
the `flux_surface`. Both must outlive `flux_surface_release`.

### Frame loop

```c
flux_canvas *c = flux_surface_acquire(s);
flux_canvas_clear(c, 0xFF202020u);
/* ... drawing ops ... */
flux_surface_present(s);
```

### Resize, DPR, readback

```c
flux_surface_resize(s, 1920, 1080);
flux_surface_set_dpr(s, 2.0f);

uint8_t pixels[256 * 256 * 4];
flux_surface_read_pixels(s, pixels, 0);   /* offscreen only */
```

`flux_surface_get_size`, `_get_format`, `_get_dpr` give read-only
access to the current state.

## Canvas

The canvas records a display list during a frame. All operations are
queued and replayed at present time.

### Transform stack

```c
flux_canvas_save(c);
flux_canvas_translate(c, 100, 100);
flux_canvas_rotate(c, 0.785f);
flux_canvas_scale(c, 2.0f, 2.0f);
/* ... */
flux_canvas_restore(c);
```

`flux_canvas_set_matrix` / `_get_matrix` / `_concat` give direct
access. `_restore` on an empty stack returns `FLUX_ERROR_INVALID_STATE`.

### Clipping

```c
flux_canvas_clip_rect(c, &(flux_rect){ 10, 10, 100, 100 });
flux_canvas_clip_path(c, my_path);
flux_canvas_reset_clip(c);
```

Rectangle clips are scissor-bound; path clips use the stencil buffer.

### Drawing

```c
flux_canvas_fill_rect (c, &(flux_rect){ 0, 0, 100, 50 }, 0xFF0000FFu);
flux_canvas_fill_path (c, path, paint);
flux_canvas_stroke_path(c, path, paint);
flux_canvas_draw_image(c, image, /*src=*/NULL, /*dst=*/&dst_rect);
flux_canvas_draw_glyph_run(c, run, x, y, paint);
flux_canvas_apply_blur(c, 4.0f);   /* offscreen only */
```

## Paint

`flux_paint` is opaque; setters and getters mirror each other.

```c
flux_paint *p = NULL;
flux_paint_create(ctx, &p);
flux_paint_set_color       (p, 0xFFAABBCCu);
flux_paint_set_stroke_width(p, 2.0f);
flux_paint_set_line_cap    (p, FLUX_CAP_ROUND);
flux_paint_set_line_join   (p, FLUX_JOIN_ROUND);
flux_paint_set_miter_limit (p, 4.0f);
flux_paint_set_fill_rule   (p, FLUX_FILL_NON_ZERO);
flux_paint_set_blend_mode  (p, FLUX_BLEND_MULTIPLY);
flux_paint_set_gradient    (p, my_gradient);
flux_paint_set_dash        (p, (float[]){5, 2, 5}, 3, 0.0f);

float w = flux_paint_get_stroke_width(p);
flux_color c = flux_paint_get_color(p);
```

Negative stroke width and miter limits below 1.0 are rejected with
`FLUX_ERROR_INVALID_ARGUMENT`.

### Blend modes

`SRC_OVER`, `DST_OVER`, `SRC_IN`, `DST_IN`, `SRC_OUT`, `DST_OUT`,
`SRC_ATOP`, `DST_ATOP`, `XOR`, `PLUS`, `MULTIPLY`, `SCREEN`, `OVERLAY`.

## Path

`flux_path` is a verb/point stream.

```c
flux_path *p = NULL;
flux_path_create(ctx, &p);

flux_path_move_to (p, 10, 10);
flux_path_line_to (p, 50, 10);
flux_path_quad_to (p, 50, 30, 30, 30);
flux_path_cubic_to(p, 20, 30, 10, 20, 10, 10);
flux_path_close   (p);

/* Convenience shapes. */
flux_path_add_rect      (p, &(flux_rect){ 0, 0, 100, 80 });
flux_path_add_round_rect(p, &(flux_rect){ 0, 0, 100, 50 }, 10.0f);
flux_path_add_circle    (p, 50, 50, 25);
flux_path_add_ellipse   (p, 50, 50, 30, 20);

/* SVG arc. */
flux_path_arc_to(p, rx, ry, rotation, large_arc, sweep, x, y);

/* Queries. */
flux_rect bounds;
flux_path_get_bounds(p, &bounds);
size_t verbs  = flux_path_verb_count(p);
size_t points = flux_path_point_count(p);

/* Transform — returns a new path. */
flux_path *moved = NULL;
flux_path_transform(p, &matrix, &moved);
```

## Gradient

```c
flux_color colors[3] = { 0xFFFF0000, 0xFF00FF00, 0xFF0000FF };
float      stops [3] = { 0.0f, 0.5f, 1.0f };

flux_linear_gradient_desc lg = {
    .size       = sizeof(lg),
    .start      = { 0, 0 },
    .end        = { 100, 0 },
    .colors     = colors,
    .stops      = stops,
    .stop_count = 3,
    .extend     = FLUX_EXTEND_PAD,
};
flux_gradient *g = NULL;
flux_gradient_create_linear(ctx, &lg, &g);

flux_paint_set_gradient(paint, g);
```

`stop_count` must be in `[2, FLUX_MAX_GRADIENT_STOPS]`. Stops must be
monotonically non-decreasing in `[0, 1]`. Radial gradients use
`flux_radial_gradient_desc` with `center` and `radius`.

## Image

```c
flux_image_desc desc = {
    .size   = sizeof(desc),
    .width  = 64, .height = 64,
    .format = FLUX_FMT_RGBA8_UNORM,
    .data   = pixels, .stride = 64 * 4,    // optional
};
flux_image *img = NULL;
flux_image_create(ctx, &desc, &img);

flux_image_update       (img, new_pixels, 64 * 4);                /* full */
flux_image_update_region(img, x, y, w, h, new_pixels, w * 4);     /* sub-region */

uint32_t w, h;
flux_image_get_size(img, &w, &h);
flux_pixel_format fmt = flux_image_get_format(img);
```

Wrap a presented offscreen surface as an image:

```c
flux_image *snap = NULL;
flux_image_create_from_surface(s, &snap);
```

The image holds a refcount on the surface; release the image first if
you tear both down.

## Glyph run

flux consumes positioned glyph runs. Shaping (UTF-8 → glyph IDs) and
rasterisation are the caller's responsibility (HarfBuzz + FreeType is
the typical pairing).

```c
flux_glyph_upload(ctx, glyph_id, bitmap, w, h, bx, by, advance);

flux_glyph_run *run = NULL;
flux_glyph_run_create(ctx, /* reserve */ 16, &run);
flux_glyph_run_append(run, glyph_id, x, y);

flux_canvas_draw_glyph_run(c, run, origin_x, origin_y, paint);
```

## See also

- [`<flux/flux.h>`](../../include/flux/flux.h) — the canonical contract.
- [API stability and deprecation policy](api-stability.md).
- [Thread safety](thread-safety.md).
- [Examples](../../examples/) — minimal, hello_rect, windowed.
