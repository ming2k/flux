# Tutorial: Building a Complete Application

**Estimated time:** 30 minutes  
**Difficulty:** Intermediate  
**Prerequisites:** [Getting Started](01-getting-started.md), basic C, Vulkan desktop session

By the end of this tutorial you will have built a small interactive application that renders a responsive UI panel with text, icons, and clipping.

> **Note:** flux's hardware Vulkan backend is currently a stub. The code
> below shows the intended API for windowed applications; to run it today
> you would need an external Vulkan surface (GLFW, SDL, etc.) and a
> working swapchain present path. The in-tree examples (`examples/minimal.c`,
> `examples/hello_rect.c`) demonstrate offscreen rendering to PPM images
> with the fully-functional software backend.

## What you will build

A 400×300 window containing:
- A rounded header bar with a title
- A scrollable content area with clipped text
- A footer with status text

## Step 1: Project structure

Create a new directory for your project:

```bash
mkdir flux-tutorial-app && cd flux-tutorial-app
```

Create `meson.build`:

```meson
project('flux-tutorial-app', 'c',
  version : '0.1.0',
  default_options : ['warning_level=2', 'c_std=c23'])

flux_dep = dependency('flux', required : true)
Vulkan_client = dependency('Vulkan SDK', required : true)

executable('tutorial_app',
  'main.c',
  dependencies : [flux_dep, Vulkan_client],
  install : false)
```

Create `main.c` and set up a Vulkan surface with your platform toolkit
(GLFW, SDL, etc.). For brevity, this tutorial focuses on the
flux-specific drawing code. See `examples/hello_rect.c` for a complete
offscreen rendering example using the software backend.

## Step 2: Load assets at startup

Add a helper to load an icon image:

```c
#include <flux/flux.h>
#include <flux/flux_vulkan.h>

static flux_image *load_icon(flux_context *ctx, const char *path)
{
    /* In a real app, decode PNG/JPEG into a pixel buffer first.
     * Here we assume a 32×32 RGBA raw file for simplicity. */
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    uint8_t pixels[32 * 32 * 4];
    size_t n = fread(pixels, 1, sizeof(pixels), f);
    fclose(f);
    if (n != sizeof(pixels)) return NULL;

    flux_image_desc desc = {
        .width = 32,
        .height = 32,
        .format = FLUX_FMT_RGBA8_UNORM,
        .data = pixels,
        .stride = 32 * 4,
    };
    return flux_image_create(ctx, &desc);
}
```

## Step 3: Shape text with HarfBuzz

flux renders positioned glyph runs. Use HarfBuzz to shape UTF-8 text,
FreeType to rasterize glyphs, then upload bitmaps and build a run:

```c
#include <harfbuzz/hb.h>
#include <freetype/freetype.h>

static flux_glyph_run *shape_text(flux_context *ctx, FT_Face face,
                                hb_font_t *hb_font, const char *utf8)
{
    hb_buffer_t *buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, utf8, -1, 0, -1);
    hb_buffer_guess_segment_properties(buf);
    hb_shape(hb_font, buf, NULL, 0);

    unsigned int count;
    hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buf, &count);
    hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buf, &count);

    /* Upload each unique glyph bitmap to the flux atlas */
    for (unsigned int i = 0; i < count; ++i) {
        uint32_t gid = infos[i].codepoint;
        FT_Load_Glyph(face, gid, FT_LOAD_RENDER);
        FT_Bitmap *bm = &face->glyph->bitmap;
        flux_glyph_upload(ctx, gid,
                        bm->buffer,
                        (int)bm->width,
                        (int)bm->rows,
                        face->glyph->bitmap_left,
                        face->glyph->bitmap_top,
                        (int)(face->glyph->advance.x >> 6));
    }

    /* Build glyph run with shaped positions */
    flux_glyph_run *run = flux_glyph_run_create(count);
    float x = 0, y = 0;
    for (unsigned int i = 0; i < count; ++i) {
        flux_glyph_run_append(run, infos[i].codepoint,
                            x + positions[i].x_offset / 64.0f,
                            y + positions[i].y_offset / 64.0f);
        x += positions[i].x_advance / 64.0f;
        y += positions[i].y_advance / 64.0f;
    }
    hb_buffer_destroy(buf);
    return run;
}
```

## Step 4: Draw the UI

Inside your per-frame loop, after `flux_surface_acquire`:

```c
flux_canvas *c = flux_surface_acquire(vs);
if (!c) continue;

/* Colors */
flux_color bg = flux_color_rgba(245, 245, 250, 255);
flux_color header_bg = flux_color_rgba(50, 100, 200, 255);
flux_color text_color = flux_color_rgba(30, 30, 30, 255);
flux_color status_color = flux_color_rgba(120, 120, 120, 255);

/* Layout */
float w = a.width;
float h = a.height;
float header_h = 48.0f;
float footer_h = 32.0f;
float margin = 16.0f;

/* Background */
flux_clear(c, bg);

/* Header bar */
flux_rect header = { 0, 0, w, header_h };
flux_fill_rect(c, &header, header_bg);

/* Header title */
flux_paint paint;
flux_paint_init(&paint, flux_color_rgba(255, 255, 255, 255));
flux_draw_glyph_run(c, title_run, margin, 32.0f, &paint);

/* Content area with clipping */
flux_rect content = { margin, header_h + margin,
                    w - margin * 2, h - header_h - footer_h - margin * 2 };
flux_clip_rect(c, &content);

/* Icon */
flux_rect icon_dst = { margin, header_h + margin, 32, 32 };
flux_draw_image(c, icon_image, NULL, &icon_dst);

/* Body text */
flux_paint_init(&paint, text_color);
flux_draw_glyph_run(c, body_run, margin + 40, header_h + margin + 24, &paint);

flux_reset_clip(c);

/* Footer */
flux_rect footer = { 0, h - footer_h, w, footer_h };
flux_fill_rect(c, &footer, flux_color_rgba(230, 230, 235, 255));
flux_paint_init(&paint, status_color);
flux_draw_glyph_run(c, status_run, margin, h - 10.0f, &paint);

flux_surface_present(vs);
```

## Step 5: Handle resizing

Update the resize handler to adjust layout constants:

```c
if (a.width != last_w || a.height != last_h) {
    flux_surface_resize(vs, a.width, a.height);
    last_w = a.width;
    last_h = a.height;
    /* Layout is recomputed each frame, so no extra work needed. */
}
```

## Step 6: Build and run

```bash
meson setup build
meson compile -C build
VK_ICD_FILENAMES=Vulkan-1 ./build/tutorial_app
```

## What you learned

- Loading images once, then referencing them per frame.
- Using HarfBuzz to shape text and FreeType to rasterize glyphs, then feeding the result to `flux_glyph_run`.
- Using `flux_clip_rect` to restrict drawing to a content area.
- Computing layout each frame for responsive sizing.

## Exercises

1. **Rounded corners:** Build a path with `flux_path_arc_to` for the header bar.
2. **Gradient header:** Replace the solid header color with a linear gradient.
3. **Scroll offset:** Add a `scroll_y` variable and translate the content group with `flux_translate` before clipping.
4. **Dynamic text:** Update the status text every second using `strftime`.

## See also

- [How to draw basic shapes](../how-to/draw-basic-shapes.md)
- [How to render text](../how-to/render-text.md)
- [How to render SVG assets](../how-to/render-svg-assets.md)
- [How to optimize performance](../how-to/optimize-performance.md)
