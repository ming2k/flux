# How to Render SVG Assets

This guide assumes you already have an `flux_context`, an `flux_surface`, and an SVG library or parser above flux.

## When to use this

Use this page when application or toolkit code needs to display SVG assets through flux.

flux does not parse SVG. SVG support belongs above flux because SVG includes
XML parsing, CSS, units, transforms, paint servers, text, clipping, masks, and
filter semantics. The final output can still land in flux in two practical
ways.

## Workflow overview

```text
SVG file (XML + CSS + transforms + filters + text)
      |
      v
[Parse and resolve]  <-- Application layer (librsvg, usvg, or custom parser)
      |
      +-- Option 1: Full rasterization
      |       |
      |       v
      |   [RGBA pixel buffer]
      |       |
      |       v
      |   flux_image_create → flux_draw_image  <-- flux layer
      |
      +-- Option 2: Path extraction
              |
              v
          [Path commands: M, L, Q, C, A, Z]
              |
              v
          flux_path_move_to / flux_path_line_to / etc.  <-- flux layer
              |
              v
          flux_fill_path / flux_stroke_path
```

## Rasterize SVG, Then Draw an Image

Use a complete SVG renderer such as librsvg when fidelity matters. The SVG
library rasterizes into an RGBA buffer; flux uploads that buffer as `flux_image`
and draws it:

```c
uint32_t width = 128;
uint32_t height = 128;
uint8_t *rgba = render_svg_with_librsvg("icon.svg", width, height);

flux_image *icon = flux_image_create(ctx, &(flux_image_desc){
    .width = width,
    .height = height,
    .format = FLUX_FMT_RGBA8_UNORM,
    .data = rgba,
    .stride = width * 4,
});

flux_canvas *c = flux_surface_acquire(surface);
flux_draw_image(c, icon, nullptr, &(flux_rect){ 24.0f, 24.0f, 128.0f, 128.0f });
flux_surface_present(surface);
```

This path is the best default for complex SVG files, filters, masks, text, CSS,
or assets from icon themes.

## Parse SVG Paths, Then Draw Vector Paths

Use a lightweight parser such as NanoSVG, usvg, or a custom path parser when
the SVG subset is simple and should remain vector in flux. Convert each parsed
path command into `flux_path` calls:

```c
flux_path *p = flux_path_create();

/* Pseudocode: commands come from an SVG path parser, not from flux. */
for (size_t i = 0; i < svg_command_count; ++i) {
    switch (commands[i].kind) {
    case SVG_MOVE_TO:
        flux_path_move_to(p, commands[i].x, commands[i].y);
        break;
    case SVG_LINE_TO:
        flux_path_line_to(p, commands[i].x, commands[i].y);
        break;
    case SVG_QUAD_TO:
        flux_path_quad_to(p, commands[i].cx, commands[i].cy,
                        commands[i].x, commands[i].y);
        break;
    case SVG_CUBIC_TO:
        flux_path_cubic_to(p, commands[i].cx0, commands[i].cy0,
                         commands[i].cx1, commands[i].cy1,
                         commands[i].x, commands[i].y);
        break;
    case SVG_ARC_TO:
        flux_path_arc_to(p, commands[i].rx, commands[i].ry,
                       commands[i].rotation,
                       commands[i].large_arc,
                       commands[i].sweep,
                       commands[i].x, commands[i].y);
        break;
    case SVG_CLOSE:
        flux_path_close(p);
        break;
    }
}

flux_canvas *c = flux_surface_acquire(surface);

flux_paint paint;
flux_paint_init(&paint, flux_color_rgba(40, 190, 120, 255));
flux_fill_path(c, p, &paint);

flux_surface_present(surface);
flux_path_destroy(p);
```

This path is appropriate for icons or generated vector assets where the caller
can limit the SVG feature subset. Complex fill rules, text, filters, masks, and
CSS should be resolved above flux before drawing.

## Where Dependencies Live

Do not add librsvg, NanoSVG, usvg, XML, or CSS parsing as required flux
dependencies unless flux intentionally grows an optional asset-ingestion layer.
Keep those libraries in the application/toolkit layer and hand flux the
resolved result: pixels, paths, paints, images, or glyph runs.

## Verification

Render the asset into an offscreen surface and compare the output against a known-good image, or display the asset in a Vulkan surface and inspect it manually.
