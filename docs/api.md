# API Reference

This document describes the public API of flux.

## Ownership

flux follows a simple own/borrow split:

- Objects returned by `*_create` are owned by the caller and destroyed with the matching `*_destroy`.
- `fx_canvas` is borrowed. It is valid only from `fx_surface_acquire(s)` until the matching `fx_surface_present(s)`.
- Recorded canvas ops borrow the resource objects they point at (`fx_image`, `fx_path`, `fx_font`, `fx_glyph_run`). You must keep them alive until the frame is presented.
- Internal copies: When recording paths or glyph runs under a non-identity transformation matrix, flux creates an internal transformed copy. The canvas manages the lifecycle of these internal copies automatically.

## Context and surfaces

```c
fx_context_desc desc = {
    .app_name = "shell-ui",
    .enable_validation = false,
};
fx_context *ctx = fx_context_create(&desc);
```

`fx_context` owns the Vulkan instance, device selection, and the context-wide **Glyph Atlas**.

Wayland surface creation:

```c
#include <flux/flux_wayland.h>

fx_surface *surface = fx_surface_create_wayland(ctx,
    wl_display,
    wl_surface,
    1280, 720,
    FX_CS_SRGB);
```

## Matrix Transformations

flux uses a 3x3 affine transformation stack. Transformations are applied on the CPU immediately during recording. This ensures that path flattening and stroking always happen at the final device resolution.

```c
fx_save(c);
fx_translate(c, 100, 100);
fx_rotate(c, M_PI / 4);
fx_scale(c, 2.0, 2.0);

// Draw something transformed...

fx_restore(c);
```

Available functions:
- `fx_save` / `fx_restore`: Push/pop the matrix stack.
- `fx_translate`, `fx_scale`, `fx_rotate`: Combine a new transform into the current matrix.
- `fx_concat`: Multiply the current matrix by a provided `fx_matrix`.
- `fx_set_matrix` / `fx_get_matrix`: Direct access to the current matrix.

## Paint System

The `fx_paint` object encapsulates all styling information for primitives.

```c
fx_paint paint;
fx_paint_init(&paint, fx_color_rgba(255, 128, 0, 255)); // Solid orange

paint.stroke_width = 4.0f;
paint.line_cap = FX_CAP_ROUND;
paint.line_join = FX_JOIN_ROUND;
paint.miter_limit = 4.0f;
```

Available enums:
- `fx_line_cap`: `FX_CAP_BUTT`, `FX_CAP_ROUND`, `FX_CAP_SQUARE`.
- `fx_line_join`: `FX_JOIN_MITER`, `FX_JOIN_ROUND`, `FX_JOIN_BEVEL`.

## Drawing Primitives

### Paths
`fx_path` is an explicit verb/point stream.

```c
fx_path *path = fx_path_create();
fx_path_move_to(path, 10.0f, 10.0f);
fx_path_line_to(path, 110.0f, 10.0f);
fx_path_close(path);

fx_fill_path(c, path, &paint);
fx_stroke_path(c, path, &paint);
```

### Images
`fx_image` handles GPU-resident pixel data.

```c
fx_image_desc img_desc = {
    .width  = 64,
    .height = 64,
    .format = FX_FMT_RGBA8_UNORM,
    .data   = pixels, // Optional initial CPU data
};

fx_image *img = fx_image_create(ctx, &img_desc);
fx_draw_image(c, img, NULL, &(fx_rect){ 0, 0, 64, 64 });
```

### Text
flux consumes positioned glyph runs. High-level shaping (UTF-8 to glyph-ids) should be performed via HarfBuzz.

```c
fx_font *font = fx_font_create(ctx, &(fx_font_desc){
    .family = "Noto Sans",
    .source_name = "/path/to/font.ttf",
    .size = 16.0f,
});

fx_glyph_run *run = fx_glyph_run_create(8);
fx_glyph_run_append(run, glyph_id, x, y);

fx_draw_glyph_run(c, font, run, 10, 20, &paint);
```

## Vulkan Implementation Status

- **Memory:** Per-frame dynamic ring buffers (`HOST_VISIBLE | HOST_COHERENT`).
- **Batching:** Automatic grouping of sequential primitives with identical paint properties.
- **Atlas:** Dynamic 2048x2048 `A8` glyph atlas.
- **Pipelines:** Specialized shaders for solid geometry, textured quads, and alpha-blended text.
- **Blending:** Full support for `SRC_OVER` alpha blending.
