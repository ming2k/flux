# Glyph Atlas

The glyph atlas is the single data structure that makes fast text rendering possible in flux. It bridges the gap between CPU-rasterized glyph bitmaps and GPU-drawn textured quads. This document explains how the atlas is laid out, how it packs glyphs, how it stays synchronized with the GPU, and the trade-offs baked into its design.

## What the atlas is

A glyph atlas is one large texture that holds many small alpha masks. Instead of creating a separate GPU texture for every glyph, flux packs all uploaded glyphs into a single 2048×2048 `A8_UNORM` image. At draw time each glyph becomes a quad whose UV coordinates point to the correct sub-rectangle inside the atlas.

This has two consequences:

1. **One texture bind per text draw call.** All glyphs in a run share the same atlas texture, so the GPU never has to switch textures mid-run.
2. **Cache reuse.** Once a glyph is uploaded, every subsequent draw of the same `glyph_id` reuses the same atlas entry with no CPU work and no GPU upload.

## Where the atlas lives

The atlas is owned by `flux_context`, not by any individual surface. Its concrete type is `flux_glyph_atlas` (defined in `src/internal.h`) and it is created lazily on the first call to `flux_glyph_upload`.

```c
typedef struct flux_glyph_atlas {
    uint8_t  *pixels;      /* A8, owned by context allocator */
    uint32_t  width;       /* 2048 */
    uint32_t  height;      /* 2048 */
    uint32_t  row_y;       /* current shelf y */
    uint32_t  row_h;       /* current shelf height */
    uint32_t  row_x;       /* current x in shelf */
    flux_glyph_slot *slots;
    size_t    slot_count;
    size_t    slot_cap;
    uint64_t  revision;    /* incremented on every upload */
} flux_glyph_atlas;
```

Because the atlas lives in the context, every surface that shares the same context also shares the same CPU-side pixel buffer and slot table. Each surface still maintains its own GPU texture copy (see [GPU synchronization](#gpu-synchronization)), but the authoritative bitmap data is centralized.

## The shelf allocator

flux packs glyphs with a **shelf allocator** (sometimes called a skyline allocator). The atlas is treated as a stack of horizontal shelves.

### Algorithm

1. Each shelf has a `y` origin (`row_y`) and a fixed height (`row_h`).
2. Glyphs are placed left-to-right along the current shelf (`row_x`).
3. If a glyph does not fit in the remaining shelf width, the allocator commits the current shelf and starts a new one at `row_y + row_h + padding`.
4. Every glyph is surrounded by a 2-pixel padding (`ATLAS_PAD`) to avoid bleeding between neighboring entries when the GPU sampler strays slightly outside the intended UV rectangle.

```c
static bool alloc_atlas_rect(flux_glyph_atlas *a, uint16_t w, uint16_t h,
                             uint16_t *out_x, uint16_t *out_y)
{
    uint16_t pw = w + ATLAS_PAD;
    uint16_t ph = h + ATLAS_PAD;
    if (a->row_x + pw > a->width) {
        /* New shelf */
        a->row_y += a->row_h + ATLAS_PAD;
        a->row_x = ATLAS_PAD;
        a->row_h = 0;
    }
    if (a->row_y + ph > a->height) {
        return false; /* atlas full */
    }
    *out_x = (uint16_t)a->row_x;
    *out_y = (uint16_t)a->row_y;
    a->row_x += pw;
    if (ph > a->row_h) a->row_h = ph;
    return true;
}
```

### Why a shelf allocator?

The shelf allocator is small, predictable, and fast. It does not need complex data structures or binning heuristics. For UI text at typical densities (thousands of glyphs, not millions) the fragmentation is acceptable. The trade-off is that a single unusually tall glyph can waste the rest of its shelf; in practice this is mitigated by the 2048-pixel height budget.

### Atlas full behavior

When the allocator runs out of room it returns `false`, and `flux_glyph_upload` forwards that as `FLUX_ERROR_OUT_OF_MEMORY`. The caller must then decide what to do: skip the glyph, rasterize at a smaller size, or clear and rebuild the atlas. flux does **not** currently perform automatic eviction; that policy is left to the application layer.

## Slot table and lookup

Each uploaded glyph is recorded in a dynamically-growing array of `flux_glyph_slot`:

```c
typedef struct flux_glyph_slot {
    uint32_t glyph_id;
    uint16_t atlas_x, atlas_y;
    uint16_t w, h;
    int16_t  bearing_x, bearing_y;
    uint16_t advance;
} flux_glyph_slot;
```

The slot stores everything needed to place the glyph on screen: its sub-rectangle inside the atlas, its bitmap dimensions, and its FreeType-style bearing and advance metrics.

Lookup is a linear scan:

```c
flux_glyph_slot *flux_glyph_atlas_find(flux_glyph_atlas *a, uint32_t glyph_id)
{
    for (size_t i = 0; i < a->slot_count; i++) {
        if (a->slots[i].glyph_id == glyph_id)
            return &a->slots[i];
    }
    return NULL;
}
```

Linear search is sufficient because the slot count is expected to stay in the low thousands for typical application text. A hash table would shave off a few dozen nanoseconds per lookup but would add memory overhead and allocator complexity that the current design avoids.

Upload is idempotent: calling `flux_glyph_upload` with a `glyph_id` that already exists in the slot table returns immediately with `FLUX_OK`.

## GPU synchronization

The CPU-side atlas and the GPU do not share memory. Instead, each surface lazily creates its own GPU texture from the CPU atlas whenever the atlas changes.

### Revision tracking

`flux_glyph_atlas.revision` starts at zero and increments once for every successful upload. Each surface remembers the last revision it copied to the GPU in `flux_surface.glyph_atlas_revision`.

During `flux_surface_present`, the engine checks:

```c
if (!s->glyph_atlas_tex || s->glyph_atlas_revision != a->revision) {
    if (s->glyph_atlas_tex)
        vt(r)->texture_free(r, s->glyph_atlas_tex);
    s->glyph_atlas_tex = vt(r)->texture_alloc(r, a->width, a->height,
                                               FLUX_FMT_A8_UNORM,
                                               a->pixels, a->width);
    s->glyph_atlas_revision = a->revision;
}
```

If the revision has changed, the old texture is destroyed and a new one is allocated with the current pixel buffer as initial data. This means:

- **Batch your uploads.** Uploading one glyph, drawing, uploading another glyph, and drawing again causes two full texture allocations and copies. It is better to upload all needed glyphs before the first `flux_surface_present`.
- **Per-surface copies.** Two surfaces in the same context each get their own GPU texture copy. This is necessary because different surfaces may use different RHI backends (e.g. one Vulkan window and one software offscreen target) and because texture handles are backend-specific.
- **No incremental updates.** flux currently re-uploads the entire 4 MB atlas on every change. For the 2048×2048 size this is cheap enough that sub-region updates are not yet implemented.

### Texture format

The atlas uses `FLUX_FMT_A8_UNORM`: one byte per pixel interpreted as normalized alpha. The fragment shader samples this alpha and multiplies it by the paint color to produce the final text color. No RGB channels are stored for glyphs because text is monochrome in flux's model; color is supplied at draw time.

## From atlas entry to screen pixel

When the engine encounters a `FLUX_OP_DRAW_GLYPHS` operation, it does the following for every glyph in the run:

1. Look up the glyph ID in the atlas slot table. If not found, the glyph is silently skipped.
2. Compute screen-space position using the glyph run's `(x, y)` plus the slot's `bearing_x` and `bearing_y`.
3. Compute normalized UV coordinates from the slot's `atlas_x`, `atlas_y`, `w`, and `h`.
4. Allocate six vertices (two triangles) and emit a `draw_image` command with the surface's atlas texture and the paint color.

```c
float x = base_x + g->x + slot->bearing_x;
float y = base_y + g->y + slot->bearing_y;
float u0 = slot->atlas_x / atlas_w;
float v0 = slot->atlas_y / atlas_h;
/* ... six vertices ... */
vt(r)->draw_image(r, gb, gfirst, 6, s->glyph_atlas_tex, gp->color);
```

The backend's `draw_image` path handles bilinear filtering, blending, and scissor clipping exactly the same way it handles regular image draws. Text is therefore not a special GPU primitive; it is just a textured quad with an alpha-mask texture.

## Design trade-offs

| Decision | Rationale | Cost |
|---|---|---|
| **Fixed 2048×2048 size** | Large enough for thousands of typical UI glyphs; small enough to re-upload in full. | Giant glyphs (e.g. 512 px emoji) consume disproportionate shelf space. |
| **Shelf allocator** | Simple, no fragmentation metadata, easy to reason about. | Can waste space when glyph heights vary wildly. |
| **Linear slot lookup** | Avoids hash-table memory overhead and rehashing logic. | O(n) lookup; acceptable for n < ~5000. |
| **CPU-side pixel buffer** | Backend-agnostic; easy to read back for debugging. | Extra 4 MB per context; requires full GPU upload on change. |
| **No automatic eviction** | Keeps the atlas stateless and predictable; caller controls cache policy. | Application must handle `FLUX_ERROR_OUT_OF_MEMORY` from `flux_glyph_upload`. |
| **Per-surface GPU textures** | Lets different backends coexist; no shared GPU state between surfaces. | Redundant VRAM when many surfaces draw the same glyphs. |

## Relationship to the rest of the text pipeline

The atlas sits at the boundary between caller-owned text logic and flux-owned rendering. The caller produces `(glyph_id, bitmap, metrics)` tuples via `flux_glyph_upload`; flux stores, packs, and draws them. Everything upstream—font discovery, shaping, rasterization, and cache eviction policy—remains the caller's responsibility.

For a higher-level view of how text becomes pixels, see [Text Rendering Pipeline](text-rendering-pipeline.md). For concrete API usage, see [How to render text](../how-to/render-text.md).
