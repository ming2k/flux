# How to Render Text

This guide assumes you already have an `flux_context`, an `flux_surface`, and an active frame. See [Getting Started](../tutorials/01-getting-started.md) and [How to record and present a frame](record-and-present-a-frame.md) if needed.

## When to use this

Use this page when your application has text content and needs to convert it into positioned glyph runs that flux can render.

flux consumes positioned glyph runs and pre-rasterized glyph bitmaps. It does not discover system fonts, shape UTF-8 text, or rasterize glyphs. Keep font discovery, shaping, rasterization, paragraph layout, bidirectional text, and line breaking above flux.

## Dependency Split

**Caller responsibilities (above flux):**

- **Font discovery** (Fontconfig, system API): find the right font file.
- **Shaping** (HarfBuzz): convert UTF-8 text into glyph IDs and positioned advances.
- **Rasterization** (FreeType, stb_truetype, etc.): convert glyph outlines into 8-bit alpha masks.

**flux responsibilities:**

- Pack uploaded glyph bitmaps into a GPU atlas texture.
- Render positioned glyph runs as textured quads.

## Glyph ID Namespace

flux does not interpret glyph IDs — they are opaque integers owned by the caller. Different fonts, sizes, or styles must use distinct ID ranges. For example:

```c
/* Encode font + size into the high bits */
#define GLYPH_ID(font_idx, size_idx, glyph_idx) \
    (((font_idx) & 0xFF) << 24 | \
     ((size_idx) & 0xFF) << 16 | \
     ((glyph_idx) & 0xFFFF))
```

## Upload Glyph Bitmaps

Before drawing a glyph, upload its bitmap to the flux atlas. This example uses FreeType, but any rasterizer works:

```c
/* Rasterize a glyph using FreeType (caller-side) */
FT_Load_Glyph(face, glyph_id, FT_LOAD_RENDER);
FT_Bitmap *bm = &face->glyph->bitmap;

/* Upload to flux atlas */
bool ok = flux_glyph_upload(ctx, glyph_id,
                          bm->buffer,
                          (int)bm->width,
                          (int)bm->rows,
                          face->glyph->bitmap_left,
                          face->glyph->bitmap_top,
                          (int)(face->glyph->advance.x >> 6));
```

**Parameters explained:**

- `glyph_id` — caller-defined identifier (see namespace note above).
- `bitmap` — row-major 8-bit alpha mask with top-left origin. `nullptr` is allowed (reserves atlas space without uploading data).
- `w, h` — bitmap dimensions in pixels. Both must be > 0.
- `bearing_x, bearing_y` — offset from glyph origin to bitmap left/top edge, in pixels.
- `advance` — horizontal advance in pixels.

**Upload is idempotent.** Calling `flux_glyph_upload` with the same `glyph_id` twice returns true without duplicating the atlas entry.

## Draw a Glyph Run

Once glyphs are uploaded, build a run and draw it:

```c
flux_glyph_run *run = flux_glyph_run_create(8);
flux_glyph_run_append(run, 43, 0.0f, 0.0f);   /* glyph id, x, y */
flux_glyph_run_append(run, 72, 12.0f, 0.0f);

flux_canvas *c = flux_surface_acquire(surface);

flux_paint text_paint;
flux_paint_init(&text_paint, flux_color_rgba(245, 245, 240, 255));
flux_draw_glyph_run(c, run, 32.0f, 64.0f, &text_paint);

flux_surface_present(surface);
flux_glyph_run_destroy(run);
```

**Important:** A glyph that has not been uploaded is silently skipped during rendering. Always upload before drawing.

## Complete Example: Shape, Rasterize, Upload, Draw

This example uses HarfBuzz for shaping and FreeType for rasterization. Substitute your own libraries as needed:

```c
#include <harfbuzz/hb.h>
#include <freetype/freetype.h>

flux_glyph_run *prepare_text(flux_context *ctx, FT_Face face,
                           hb_font_t *hb_font, const char *utf8)
{
    /* Shape text into glyph IDs and positions */
    hb_buffer_t *buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, utf8, -1, 0, -1);
    hb_buffer_guess_segment_properties(buf);
    hb_shape(hb_font, buf, nullptr, 0);

    unsigned int count = 0;
    hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buf, &count);
    hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buf, &count);

    /* Rasterize and upload each unique glyph */
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
    float x = 0.0f;
    float y = 0.0f;
    for (unsigned int i = 0; i < count; ++i) {
        flux_glyph_run_append(run, infos[i].codepoint,
                            x + pos[i].x_offset / 64.0f,
                            y - pos[i].y_offset / 64.0f);
        x += pos[i].x_advance / 64.0f;
        y -= pos[i].y_advance / 64.0f;
    }

    hb_buffer_destroy(buf);
    return run;
}
```

Render:

```c
flux_glyph_run *run = prepare_text(ctx, face, hb_font, "Hello flux");
flux_canvas *c = flux_surface_acquire(surface);

flux_paint paint;
flux_paint_init(&paint, flux_color_rgba(255, 255, 255, 255));
flux_draw_glyph_run(c, run, 40.0f, 80.0f, &paint);

flux_surface_present(surface);
flux_glyph_run_destroy(run);
```

## Atlas Overflow

The glyph atlas is a fixed 2048×2048 texture. When it fills, `flux_glyph_upload` returns `FLUX_ERROR_OUT_OF_MEMORY`. flux does not automatically evict or clear the atlas; the caller must handle the error.

Common strategies:

- **Skip the glyph.** Drop the new glyph and continue rendering.
- **Reduce size.** Re-rasterize the glyph at a smaller pixel size and retry.
- **Rebuild.** If your application manages its own glyph cache, clear the cache, reset the atlas by creating a new context, and re-upload only the glyphs currently needed.

**Best practice:** Upload all glyphs you need at the start of a frame, before recording draw commands. Batch uploads minimize GPU texture reallocation and prevent unexpected overflow mid-frame.

## Production Text Layer

For production UI text, keep these policies outside flux:

- Font discovery and fallback through Fontconfig or the toolkit's font layer.
- Paragraph layout, wrapping, truncation, and alignment.
- Bidirectional text handling and script itemization.
- Emoji fallback and color glyph policy.
- Text caching and invalidation.
- Glyph rasterization with FreeType, stb_truetype, or a custom rasterizer.
- Glyph ID namespace management.

After those steps, upload bitmaps and submit final positioned glyph runs to flux.

## Verification

Render a known string into an offscreen or Vulkan surface. If no glyphs appear:

1. Confirm `flux_glyph_upload` returned true for each glyph.
2. Confirm the glyph IDs in the run match the uploaded IDs exactly.
3. Confirm the bitmap data is valid (non-zero alpha for visible pixels).
4. Confirm the glyph run stays alive until `flux_surface_present`.
5. Confirm `flux_glyph_upload` did not return `FLUX_ERROR_OUT_OF_MEMORY` for any glyph.

## See also

- [Text rendering pipeline](../explanation/text-rendering-pipeline.md) — how text becomes pixels.
- [Capability model](../explanation/capability-model.md) — what flux owns vs. caller owns.
