#include "internal.h"

static constexpr int ATLAS_SIZE = 2048;

static bool ensure_atlas_image(fx_context *ctx)
{
    if (ctx->atlas.image) return true;

    fx_image_desc desc = {
        .width = ATLAS_SIZE,
        .height = ATLAS_SIZE,
        .format = FX_FMT_A8_UNORM,
    };
    ctx->atlas.image = fx_image_create(ctx, &desc);
    if (!ctx->atlas.image) return false;

    ctx->atlas.shelf_y = 2;
    ctx->atlas.shelf_x = 2;
    ctx->atlas.shelf_h = 0;
    return true;
}

static fx_atlas_entry *find_atlas_entry(fx_context *ctx, uint32_t glyph_id)
{
    for (size_t i = 0; i < ctx->atlas.entry_count; ++i) {
        if (ctx->atlas.entries[i].glyph_id == glyph_id) {
            return &ctx->atlas.entries[i];
        }
    }
    return nullptr;
}

static void atlas_reset(fx_context *ctx)
{
    ctx->atlas.entry_count = 0;
    ctx->atlas.shelf_x = 2;
    ctx->atlas.shelf_y = 2;
    ctx->atlas.shelf_h = 0;
}

bool fx_glyph_upload(fx_context *ctx, uint32_t glyph_id,
                     const uint8_t *bitmap,
                     int w, int h,
                     int bearing_x, int bearing_y,
                     int advance)
{
    if (!ctx || w <= 0 || h <= 0) return false;
    if (!ensure_atlas_image(ctx)) return false;

    /* Check if already uploaded */
    if (find_atlas_entry(ctx, glyph_id)) return true;

    if (w + 2 > ATLAS_SIZE || h + 2 > ATLAS_SIZE) {
        FX_LOGE(ctx, "glyph %u is larger than atlas (%dx%d, atlas %d)",
                glyph_id, w, h, ATLAS_SIZE);
        return false;
    }

    if (ctx->atlas.shelf_x + w + 2 > ATLAS_SIZE) {
        ctx->atlas.shelf_x = 2;
        ctx->atlas.shelf_y += ctx->atlas.shelf_h + 2;
        ctx->atlas.shelf_h = 0;
    }

    if (ctx->atlas.shelf_y + h + 2 > ATLAS_SIZE) {
        FX_LOGW(ctx, "glyph atlas full: evicting %zu entries",
                ctx->atlas.entry_count);
        atlas_reset(ctx);
        vkDeviceWaitIdle(ctx->device);
    }

    if (h > ctx->atlas.shelf_h) ctx->atlas.shelf_h = h;

    int x = ctx->atlas.shelf_x;
    int y = ctx->atlas.shelf_y;

    if (bitmap) {
        if (!fx_upload_image(ctx, ctx->atlas.image->vk_image,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             x, y, (uint32_t)w, (uint32_t)h,
                             bitmap, (size_t)w, 1)) {
            FX_LOGE(ctx, "glyph upload failed (glyph=%u)", glyph_id);
            return false;
        }
    }

    if (ctx->atlas.entry_count + 1 > ctx->atlas.entry_cap) {
        size_t new_cap = ctx->atlas.entry_cap ? ctx->atlas.entry_cap * 2 : 256;
        fx_atlas_entry *new_entries = realloc(ctx->atlas.entries, new_cap * sizeof(fx_atlas_entry));
        if (!new_entries) return false;
        ctx->atlas.entries = new_entries;
        ctx->atlas.entry_cap = new_cap;
    }

    fx_atlas_entry *e = &ctx->atlas.entries[ctx->atlas.entry_count++];
    e->glyph_id = glyph_id;
    e->w = w;
    e->h = h;
    e->u0 = (float)x / (float)ATLAS_SIZE;
    e->v0 = (float)y / (float)ATLAS_SIZE;
    e->u1 = (float)(x + w) / (float)ATLAS_SIZE;
    e->v1 = (float)(y + h) / (float)ATLAS_SIZE;
    e->bearing_x = bearing_x;
    e->bearing_y = bearing_y;
    e->advance = advance;

    ctx->atlas.shelf_x += w + 2;
    return true;
}

/* Internal helper for drawing glyphs */
bool fx_atlas_ensure_glyph(fx_context *ctx, uint32_t glyph_id, fx_atlas_entry *out_entry)
{
    if (!ensure_atlas_image(ctx)) return false;
    fx_atlas_entry *e = find_atlas_entry(ctx, glyph_id);
    if (e) {
        *out_entry = *e;
        return true;
    }
    return false;  /* Caller must upload first */
}

static bool ensure_glyph_capacity(fx_glyph_run *run, size_t extra)
{
    size_t need = run->count + extra;
    if (need <= run->cap) return true;
    size_t new_cap = run->cap ? run->cap * 2 : 16;
    while (new_cap < need) new_cap *= 2;
    fx_glyph *glyphs = realloc(run->glyphs, new_cap * sizeof(*glyphs));
    if (!glyphs) return false;
    run->glyphs = glyphs;
    run->cap = new_cap;
    return true;
}

fx_glyph_run *fx_glyph_run_create(size_t reserve_glyphs)
{
    fx_glyph_run *run = calloc(1, sizeof(*run));
    if (!run) return nullptr;
    if (reserve_glyphs && !ensure_glyph_capacity(run, reserve_glyphs)) {
        free(run);
        return nullptr;
    }
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
    if (!ensure_glyph_capacity(run, 1)) return false;
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
