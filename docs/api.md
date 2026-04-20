# API Reference

This document describes the public API of vgfx.

## Ownership

vgfx follows a simple own/borrow split:

- Objects returned by `*_create` are owned by the caller and destroyed with the matching `*_destroy`.
- `vg_canvas` is borrowed. It is valid only from `vg_surface_acquire(s)` until the matching `vg_surface_present(s)`.
- Recorded canvas ops borrow the resource objects they point at (`vg_image`, `vg_path`, `vg_font`, `vg_glyph_run`). You must keep them alive until the frame is presented.
- Internal copies: When recording paths or glyph runs under a non-identity transformation matrix, vgfx creates an internal transformed copy. The canvas manages the lifecycle of these internal copies automatically.

## Context and surfaces

```c
vg_context_desc desc = {
    .app_name = "shell-ui",
    .enable_validation = false,
};
vg_context *ctx = vg_context_create(&desc);
```

`vg_context` owns the Vulkan instance, device selection, and the context-wide **Glyph Atlas**.

Wayland surface creation:

```c
#include <vgfx/vgfx_wayland.h>

vg_surface *surface = vg_surface_create_wayland(ctx,
    wl_display,
    wl_surface,
    1280, 720,
    VG_CS_SRGB);
```

## Matrix Transformations

vgfx uses a 3x3 affine transformation stack. Transformations are applied on the CPU immediately during recording. This ensures that path flattening and stroking always happen at the final device resolution.

```c
vg_save(c);
vg_translate(c, 100, 100);
vg_rotate(c, M_PI / 4);
vg_scale(c, 2.0, 2.0);

// Draw something transformed...

vg_restore(c);
```

Available functions:
- `vg_save` / `vg_restore`: Push/pop the matrix stack.
- `vg_translate`, `vg_scale`, `vg_rotate`: Combine a new transform into the current matrix.
- `vg_concat`: Multiply the current matrix by a provided `vg_matrix`.
- `vg_set_matrix` / `vg_get_matrix`: Direct access to the current matrix.

## Paint System

The `vg_paint` object encapsulates all styling information for primitives.

```c
vg_paint paint;
vg_paint_init(&paint, vg_color_rgba(255, 128, 0, 255)); // Solid orange

paint.stroke_width = 4.0f;
paint.line_cap = VG_CAP_ROUND;
paint.line_join = VG_JOIN_ROUND;
paint.miter_limit = 4.0f;
```

Available enums:
- `vg_line_cap`: `VG_CAP_BUTT`, `VG_CAP_ROUND`, `VG_CAP_SQUARE`.
- `vg_line_join`: `VG_JOIN_MITER`, `VG_JOIN_ROUND`, `VG_JOIN_BEVEL`.

## Drawing Primitives

### Paths
`vg_path` is an explicit verb/point stream.

```c
vg_path *path = vg_path_create();
vg_path_move_to(path, 10.0f, 10.0f);
vg_path_line_to(path, 110.0f, 10.0f);
vg_path_close(path);

vg_fill_path(c, path, &paint);
vg_stroke_path(c, path, &paint);
```

### Images
`vg_image` handles GPU-resident pixel data.

```c
vg_image_desc img_desc = {
    .width  = 64,
    .height = 64,
    .format = VG_FMT_RGBA8_UNORM,
    .data   = pixels, // Optional initial CPU data
};

vg_image *img = vg_image_create(ctx, &img_desc);
vg_draw_image(c, img, NULL, &(vg_rect){ 0, 0, 64, 64 });
```

### Text
vgfx consumes positioned glyph runs. High-level shaping (UTF-8 to glyph-ids) should be performed via HarfBuzz.

```c
vg_font *font = vg_font_create(ctx, &(vg_font_desc){
    .family = "Noto Sans",
    .source_name = "/path/to/font.ttf",
    .size = 16.0f,
});

vg_glyph_run *run = vg_glyph_run_create(8);
vg_glyph_run_append(run, glyph_id, x, y);

vg_draw_glyph_run(c, font, run, 10, 20, &paint);
```

## Vulkan Implementation Status

- **Memory:** Per-frame dynamic ring buffers (`HOST_VISIBLE | HOST_COHERENT`).
- **Batching:** Automatic grouping of sequential primitives with identical paint properties.
- **Atlas:** Dynamic 2048x2048 `A8` glyph atlas.
- **Pipelines:** Specialized shaders for solid geometry, textured quads, and alpha-blended text.
- **Blending:** Full support for `SRC_OVER` alpha blending.
