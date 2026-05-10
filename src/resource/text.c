/* Minimal glyph run implementation. */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

fx_glyph_run *fx_glyph_run_create(size_t reserve)
{
    fx_glyph_run *run = calloc(1, sizeof(*run));
    if (!run) return nullptr;
    run->cap = reserve > 0 ? reserve : 16;
    run->glyphs = calloc(run->cap, sizeof(fx_glyph));
    if (!run->glyphs) { free(run); return nullptr; }
    return run;
}

void fx_glyph_run_destroy(fx_glyph_run *run)
{
    if (!run) return;
    free(run->glyphs);
    free(run);
}

void fx_glyph_run_reset(fx_glyph_run *run)
{
    if (!run) return;
    run->count = 0;
}

bool fx_glyph_run_append(fx_glyph_run *run, uint32_t glyph_id, float x, float y)
{
    if (!run) return false;
    if (run->count >= run->cap) {
        size_t nc = run->cap * 2;
        fx_glyph *ng = realloc(run->glyphs, nc * sizeof(fx_glyph));
        if (!ng) return false;
        run->glyphs = ng;
        run->cap = nc;
    }
    run->glyphs[run->count++] = (fx_glyph){ .glyph_id = glyph_id, .x = x, .y = y };
    return true;
}

size_t fx_glyph_run_count(const fx_glyph_run *run)
{
    return run ? run->count : 0;
}

const fx_glyph *fx_glyph_run_data(const fx_glyph_run *run)
{
    return run ? run->glyphs : nullptr;
}

bool fx_glyph_upload(fx_context *ctx, uint32_t glyph_id,
                     const uint8_t *bitmap, int w, int h,
                     int bearing_x, int bearing_y, int advance)
{
    (void)ctx; (void)glyph_id;
    (void)bitmap; (void)w; (void)h;
    (void)bearing_x; (void)bearing_y; (void)advance;
    return true;
}
