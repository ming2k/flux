/*
 * Glyph run: a small array of (glyph_id, x, y). The actual glyph atlas
 * is owned by the context; flux_glyph_upload packs bitmaps into a CPU-side
 * A8 atlas. Surfaces lazy-create a backend texture from this atlas.
 */
#include "internal.h"

#include <string.h>

#define ATLAS_W 2048
#define ATLAS_H 2048
#define ATLAS_PAD 2

static flux_glyph_atlas *ensure_atlas(flux_context *ctx)
{
    if (ctx->atlas) return ctx->atlas;
    flux_glyph_atlas *a = flux_calloc(ctx, 1, sizeof(*a));
    if (!a) return NULL;
    a->width  = ATLAS_W;
    a->height = ATLAS_H;
    a->pixels = flux_calloc(ctx, (size_t)ATLAS_W * ATLAS_H, 1);
    if (!a->pixels) {
        flux_free(ctx, a);
        return NULL;
    }
    a->row_y = ATLAS_PAD;
    a->row_x = ATLAS_PAD;
    a->row_h = 0;
    a->slot_cap = 256;
    a->slots = flux_calloc(ctx, a->slot_cap, sizeof(flux_glyph_slot));
    if (!a->slots) {
        flux_free(ctx, a->pixels);
        flux_free(ctx, a);
        return NULL;
    }
    ctx->atlas = a;
    return a;
}

flux_glyph_slot *flux_glyph_atlas_find(flux_glyph_atlas *a, uint32_t glyph_id)
{
    for (size_t i = 0; i < a->slot_count; i++) {
        if (a->slots[i].glyph_id == glyph_id)
            return &a->slots[i];
    }
    return NULL;
}

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

flux_result flux_glyph_upload(flux_context *ctx, uint32_t glyph_id,
                              const uint8_t *bitmap, int w, int h,
                              int bearing_x, int bearing_y, int advance)
{
    if (!ctx || !bitmap) return FLUX_ERROR_INVALID_ARGUMENT;
    if (w <= 0 || h <= 0) return FLUX_ERROR_INVALID_ARGUMENT;

    flux_glyph_atlas *a = ensure_atlas(ctx);
    if (!a) return FLUX_ERROR_OUT_OF_MEMORY;

    if (flux_glyph_atlas_find(a, glyph_id)) return FLUX_OK; /* already uploaded */

    uint16_t ax = 0, ay = 0;
    if (!alloc_atlas_rect(a, (uint16_t)w, (uint16_t)h, &ax, &ay))
        return FLUX_ERROR_OUT_OF_MEMORY; /* atlas full */

    /* Copy bitmap into atlas */
    for (int row = 0; row < h; row++) {
        uint8_t *dst = a->pixels + (size_t)(ay + row) * a->width + ax;
        const uint8_t *src = bitmap + (size_t)row * w;
        memcpy(dst, src, (size_t)w);
    }

    if (a->slot_count >= a->slot_cap) {
        size_t nc = a->slot_cap * 2;
        flux_glyph_slot *ns = flux_realloc(ctx, a->slots,
                                           a->slot_cap * sizeof(flux_glyph_slot),
                                           nc * sizeof(flux_glyph_slot));
        if (!ns) return FLUX_ERROR_OUT_OF_MEMORY;
        a->slots = ns;
        a->slot_cap = nc;
    }

    a->slots[a->slot_count++] = (flux_glyph_slot){
        .glyph_id   = glyph_id,
        .atlas_x    = ax,
        .atlas_y    = ay,
        .w          = (uint16_t)w,
        .h          = (uint16_t)h,
        .bearing_x  = (int16_t)bearing_x,
        .bearing_y  = (int16_t)bearing_y,
        .advance    = (uint16_t)advance,
    };
    a->revision++;
    return FLUX_OK;
}

flux_result flux_glyph_run_create(flux_context *ctx, size_t reserve,
                                  flux_glyph_run **out_run)
{
    if (!ctx || !out_run) return FLUX_ERROR_INVALID_ARGUMENT;

    flux_glyph_run *run = flux_calloc(ctx, 1, sizeof(*run));
    if (!run) return FLUX_ERROR_OUT_OF_MEMORY;

    flux_ref_init(&run->ref_count);
    run->ctx = flux_context_retain(ctx);
    run->cap = reserve > 0 ? reserve : 16;
    run->glyphs = flux_calloc(ctx, run->cap, sizeof(flux_glyph));
    if (!run->glyphs) {
        flux_context_release(run->ctx);
        flux_free(ctx, run);
        return FLUX_ERROR_OUT_OF_MEMORY;
    }

    *out_run = run;
    return FLUX_OK;
}

flux_glyph_run *flux_glyph_run_retain(flux_glyph_run *run)
{
    if (run) flux_ref_retain(&run->ref_count);
    return run;
}

void flux_glyph_run_release(flux_glyph_run *run)
{
    if (!run) return;
    if (flux_ref_release(&run->ref_count) == 0) {
        flux_context *ctx = run->ctx;
        flux_free(ctx, run->glyphs);
        flux_free(ctx, run);
        flux_context_release(ctx);
    }
}

void flux_glyph_run_clear(flux_glyph_run *run)
{
    if (run) run->count = 0;
}

flux_result flux_glyph_run_append(flux_glyph_run *run, uint32_t glyph_id,
                                  float x, float y)
{
    if (!run) return FLUX_ERROR_INVALID_ARGUMENT;
    if (run->count >= run->cap) {
        size_t nc = run->cap * 2;
        flux_glyph *ng = flux_realloc(run->ctx, run->glyphs,
                                      run->cap * sizeof(flux_glyph),
                                      nc * sizeof(flux_glyph));
        if (!ng) return FLUX_ERROR_OUT_OF_MEMORY;
        run->glyphs = ng;
        run->cap = nc;
    }
    run->glyphs[run->count++] = (flux_glyph){ .glyph_id = glyph_id, .x = x, .y = y };
    return FLUX_OK;
}

size_t flux_glyph_run_count(const flux_glyph_run *run)
{
    return run ? run->count : 0;
}

const flux_glyph *flux_glyph_run_data(const flux_glyph_run *run)
{
    if (!run || run->count == 0) return NULL;
    return run->glyphs;
}
