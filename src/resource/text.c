/*
 * Glyph run: a small array of (glyph_id, x, y). The actual glyph atlas
 * is owned by the backend; flux_glyph_upload is the seam where bitmaps
 * are pushed into the atlas. Both are stubs at this layer; real
 * rasterisation happens in the RHI.
 */
#include "internal.h"

flux_result flux_glyph_upload(flux_context *ctx, uint32_t glyph_id,
                              const uint8_t *bitmap, int w, int h,
                              int bearing_x, int bearing_y, int advance)
{
    (void)glyph_id; (void)bitmap;
    (void)bearing_x; (void)bearing_y; (void)advance;
    if (!ctx) return FLUX_ERROR_INVALID_ARGUMENT;
    if (w <= 0 || h <= 0) return FLUX_ERROR_INVALID_ARGUMENT;
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
