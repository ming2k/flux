# How to Draw Basic Shapes

This guide assumes you already have an `flux_canvas` for the current frame. See [How to record and present a frame](record-and-present-a-frame.md) for the frame lifecycle.

## When to use this

Use these patterns when application or toolkit code has already reduced scene data to explicit drawing commands.

## Draw a Line

flux draws lines as stroked paths:

```c
flux_canvas *c = flux_surface_acquire(surface);

flux_path *line = flux_path_create();
flux_path_move_to(line, 24.0f, 32.0f);
flux_path_line_to(line, 240.0f, 96.0f);

flux_paint stroke;
flux_paint_init(&stroke, flux_color_rgba(255, 230, 120, 255));
stroke.stroke_width = 3.0f;
stroke.line_cap = FLUX_CAP_ROUND;
stroke.line_join = FLUX_JOIN_ROUND;

flux_stroke_path(c, line, &stroke);

flux_surface_present(surface);
flux_path_destroy(line);
```

If the path is reused every frame, create it once, keep it alive while frames
are recorded, and destroy it when the scene object is destroyed.

## Draw a Circle

Use SVG-style arc commands or cubic curves to build a circle. This example uses
two 180-degree arcs:

```c
flux_canvas *c = flux_surface_acquire(surface);

flux_path *circle = flux_path_create();
float cx = 160.0f;
float cy = 120.0f;
float r = 48.0f;

flux_path_move_to(circle, cx + r, cy);
flux_path_arc_to(circle, r, r, 0.0f, false, true, cx - r, cy);
flux_path_arc_to(circle, r, r, 0.0f, false, true, cx + r, cy);
flux_path_close(circle);

flux_paint fill;
flux_paint_init(&fill, flux_color_rgba(90, 180, 255, 255));
flux_fill_path(c, circle, &fill);

flux_paint outline;
flux_paint_init(&outline, flux_color_rgba(240, 250, 255, 255));
outline.stroke_width = 2.0f;
flux_stroke_path(c, circle, &outline);

flux_surface_present(surface);
flux_path_destroy(circle);
```

## Fill a Rectangle

For axis-aligned rectangles, use the convenience helper:

```c
flux_canvas *c = flux_surface_acquire(surface);

flux_fill_rect(c, &(flux_rect){ 20.0f, 20.0f, 160.0f, 80.0f },
             flux_color_rgba(40, 120, 220, 255));

flux_surface_present(surface);
```

## Transform Drawing

Transforms are part of canvas recording:

```c
flux_canvas *c = flux_surface_acquire(surface);

flux_save(c);
flux_translate(c, 320.0f, 180.0f);
flux_rotate(c, 0.25f);
flux_scale(c, 2.0f, 2.0f);

flux_fill_rect(c, &(flux_rect){ -25.0f, -25.0f, 50.0f, 50.0f },
             flux_color_rgba(255, 120, 80, 255));

flux_restore(c);
flux_surface_present(surface);
```

## Gradient Fill

Attach a gradient to `flux_paint` and use the same path drawing API:

```c
flux_canvas *c = flux_surface_acquire(surface);

flux_path *rect = flux_path_create();
flux_path_add_rect(rect, &(flux_rect){ 40.0f, 40.0f, 240.0f, 120.0f });

flux_gradient *gradient = flux_gradient_create_linear(ctx, &(flux_linear_gradient_desc){
    .start = { 40.0f, 40.0f },
    .end = { 280.0f, 40.0f },
    .colors = {
        flux_color_rgba(255, 120, 60, 255),
        flux_color_rgba(60, 160, 255, 255),
    },
    .stops = { 0.0f, 1.0f },
    .stop_count = 2,
});

flux_paint paint;
flux_paint_init(&paint, flux_color_rgba(255, 255, 255, 255));
flux_paint_set_gradient(&paint, gradient);
flux_fill_path(c, rect, &paint);

flux_surface_present(surface);
flux_gradient_destroy(gradient);
flux_path_destroy(rect);
```

## Verification

Present the frame and inspect the output surface. For automated checks, render into an offscreen surface and compare the pixels that `flux_surface_read_pixels` returns.
