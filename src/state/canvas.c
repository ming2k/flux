/*
 * Canvas: records draw operations into a display list.
 *
 * Recording deep-copies paint state and dash arrays into each op so the
 * caller may mutate or release their flux_paint immediately. Paths are
 * *borrowed* — they must outlive the frame. Images and glyph runs are
 * retained by the canvas and released automatically when the op list is
 * cleared.
 */
#include "internal.h"
#include <math.h>

/* ------------------------------------------------------------------ */
/*  Storage                                                           */
/* ------------------------------------------------------------------ */

void flux_canvas_init(flux_canvas *c, flux_surface *owner)
{
    memset(c, 0, sizeof(*c));
    c->owner = owner;
    flux_matrix_identity(&c->current_matrix);
    c->dpr = 1.0f;
}

static bool ensure_op_capacity(flux_canvas *c, size_t extra)
{
    size_t need = c->op_count + extra;
    if (need <= c->op_cap) return true;

    size_t new_cap = c->op_cap ? c->op_cap : 64;
    while (new_cap < need) new_cap *= 2;

    flux_op *ops = flux_realloc(c->owner->ctx, c->ops,
                                c->op_cap * sizeof(*ops),
                                new_cap * sizeof(*ops));
    if (!ops) return false;
    c->ops = ops;
    c->op_cap = new_cap;
    return true;
}

bool flux_canvas_push_op(flux_canvas *c, const flux_op *op)
{
    if (!c || !op) return false;
    if (!ensure_op_capacity(c, 1)) return false;
    c->ops[c->op_count++] = *op;
    return true;
}

/* Release per-op owned storage (paint snapshots and owned paths). */
static void dispose_op(flux_op *op, flux_context *ctx)
{
    switch (op->kind) {
    case FLUX_OP_FILL_PATH:
        if (op->u.fill_path.owns_path)
            flux_path_release((flux_path *)op->u.fill_path.path);
        flux_paint_state_dispose(&op->u.fill_path.paint, ctx);
        break;
    case FLUX_OP_STROKE_PATH:
        if (op->u.stroke_path.owns_path)
            flux_path_release((flux_path *)op->u.stroke_path.path);
        flux_paint_state_dispose(&op->u.stroke_path.paint, ctx);
        break;
    case FLUX_OP_CLIP_PATH:
        if (op->u.clip_path.owns_path)
            flux_path_release((flux_path *)op->u.clip_path.path);
        break;
    case FLUX_OP_DRAW_GLYPHS:
        flux_glyph_run_release((flux_glyph_run *)op->u.draw_glyphs.run);
        flux_paint_state_dispose(&op->u.draw_glyphs.paint, ctx);
        break;
    case FLUX_OP_DRAW_IMAGE:
        flux_image_release((flux_image *)op->u.draw_image.image);
        break;
    default: break;
    }
}

void flux_canvas_reset(flux_canvas *c)
{
    if (!c) return;
    flux_context *ctx = c->owner ? c->owner->ctx : NULL;
    for (size_t i = 0; i < c->op_count; ++i) dispose_op(&c->ops[i], ctx);
    c->has_clear   = false;
    c->clear_color = 0;
    c->op_count    = 0;
    c->state_count = 0;
    flux_matrix_identity(&c->current_matrix);
}

void flux_canvas_dispose(flux_canvas *c)
{
    if (!c) return;
    flux_canvas_reset(c);
    flux_context *ctx = c->owner ? c->owner->ctx : NULL;
    flux_free(ctx, c->ops);
    flux_free(ctx, c->state_stack);
    memset(c, 0, sizeof(*c));
}

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

static flux_matrix canvas_transform(const flux_canvas *c)
{
    float dpr = (c->dpr > 0.0f) ? c->dpr : 1.0f;
    flux_matrix m = c->current_matrix;
    if (dpr != 1.0f) {
        flux_matrix dpr_m;
        flux_matrix_scaling(&dpr_m, dpr, dpr);
        flux_matrix combined;
        flux_matrix_multiply(&combined, &m, &dpr_m);
        m = combined;
    }
    return m;
}

static flux_rect scale_rect_by_dpr(const flux_rect *r, float dpr)
{
    if (dpr <= 0.0f || dpr == 1.0f) return *r;
    return (flux_rect){ r->x * dpr, r->y * dpr, r->w * dpr, r->h * dpr };
}

/* ------------------------------------------------------------------ */
/*  Frame state                                                       */
/* ------------------------------------------------------------------ */

flux_result flux_canvas_clear(flux_canvas *c, flux_color color)
{
    if (!c) return FLUX_ERROR_INVALID_ARGUMENT;
    c->clear_color = color;
    c->has_clear   = true;
    return FLUX_OK;
}

size_t flux_canvas_op_count(const flux_canvas *c) { return c ? c->op_count : 0; }

/* ------------------------------------------------------------------ */
/*  Transform stack                                                   */
/* ------------------------------------------------------------------ */

flux_result flux_canvas_save(flux_canvas *c)
{
    if (!c) return FLUX_ERROR_INVALID_ARGUMENT;
    if (c->state_count + 1 > c->state_cap) {
        size_t new_cap = c->state_cap ? c->state_cap * 2 : 8;
        flux_matrix *st = flux_realloc(c->owner->ctx, c->state_stack,
                                       c->state_cap * sizeof(flux_matrix),
                                       new_cap * sizeof(flux_matrix));
        if (!st) return FLUX_ERROR_OUT_OF_MEMORY;
        c->state_stack = st;
        c->state_cap   = new_cap;
    }
    c->state_stack[c->state_count++] = c->current_matrix;
    return FLUX_OK;
}

flux_result flux_canvas_restore(flux_canvas *c)
{
    if (!c) return FLUX_ERROR_INVALID_ARGUMENT;
    if (c->state_count == 0) return FLUX_ERROR_INVALID_STATE;
    c->current_matrix = c->state_stack[--c->state_count];
    return FLUX_OK;
}

flux_result flux_canvas_concat(flux_canvas *c, const flux_matrix *m)
{
    if (!c || !m) return FLUX_ERROR_INVALID_ARGUMENT;
    flux_matrix next;
    flux_matrix_multiply(&next, &c->current_matrix, m);
    c->current_matrix = next;
    return FLUX_OK;
}

flux_result flux_canvas_translate(flux_canvas *c, float dx, float dy)
{
    flux_matrix t; flux_matrix_translation(&t, dx, dy);
    return flux_canvas_concat(c, &t);
}

flux_result flux_canvas_scale(flux_canvas *c, float sx, float sy)
{
    flux_matrix s; flux_matrix_scaling(&s, sx, sy);
    return flux_canvas_concat(c, &s);
}

flux_result flux_canvas_rotate(flux_canvas *c, float radians)
{
    flux_matrix r; flux_matrix_rotation(&r, radians);
    return flux_canvas_concat(c, &r);
}

flux_result flux_canvas_set_matrix(flux_canvas *c, const flux_matrix *m)
{
    if (!c || !m) return FLUX_ERROR_INVALID_ARGUMENT;
    c->current_matrix = *m;
    return FLUX_OK;
}

flux_result flux_canvas_get_matrix(const flux_canvas *c, flux_matrix *out_m)
{
    if (!c || !out_m) return FLUX_ERROR_INVALID_ARGUMENT;
    *out_m = c->current_matrix;
    return FLUX_OK;
}

/* ------------------------------------------------------------------ */
/*  Drawing ops                                                       */
/* ------------------------------------------------------------------ */

flux_result flux_canvas_fill_rect(flux_canvas *c, const flux_rect *rect, flux_color color)
{
    if (!c || !rect) return FLUX_ERROR_INVALID_ARGUMENT;
    flux_rect r = scale_rect_by_dpr(rect, c->dpr);

    flux_matrix m = canvas_transform(c);
    if (flux_matrix_is_identity(&m)) {
        flux_op op = {
            .kind = FLUX_OP_FILL_RECT,
            .u.fill_rect = { .rect = r, .color = color },
        };
        return flux_canvas_push_op(c, &op) ? FLUX_OK : FLUX_ERROR_OUT_OF_MEMORY;
    }

    /* Non-identity transform: emit a path fill on a transformed rect. */
    flux_path *path = NULL;
    flux_result rr = flux_path_create(c->owner->ctx, &path);
    if (rr != FLUX_OK) return rr;

    float x0 = r.x,         y0 = r.y;
    float x1 = r.x + r.w,   y1 = r.y;
    float x2 = r.x + r.w,   y2 = r.y + r.h;
    float x3 = r.x,         y3 = r.y + r.h;
    flux_matrix_transform_point(&m, &x0, &y0);
    flux_matrix_transform_point(&m, &x1, &y1);
    flux_matrix_transform_point(&m, &x2, &y2);
    flux_matrix_transform_point(&m, &x3, &y3);

    if (flux_path_move_to(path, x0, y0) != FLUX_OK ||
        flux_path_line_to(path, x1, y1) != FLUX_OK ||
        flux_path_line_to(path, x2, y2) != FLUX_OK ||
        flux_path_line_to(path, x3, y3) != FLUX_OK ||
        flux_path_close  (path)         != FLUX_OK) {
        flux_path_release(path);
        return FLUX_ERROR_OUT_OF_MEMORY;
    }

    flux_op op = {
        .kind = FLUX_OP_FILL_PATH,
        .u.fill_path = {
            .path      = path,
            .owns_path = true,
            .paint = (flux_paint_state){
                .color      = color,
                .blend_mode = FLUX_BLEND_SRC_OVER,
                .fill_rule  = FLUX_FILL_EVEN_ODD,
            },
        },
    };
    if (!flux_canvas_push_op(c, &op)) {
        flux_path_release(path);
        return FLUX_ERROR_OUT_OF_MEMORY;
    }
    return FLUX_OK;
}

flux_result flux_canvas_fill_path(flux_canvas *c, const flux_path *path, const flux_paint *paint)
{
    if (!c || !path || !paint) return FLUX_ERROR_INVALID_ARGUMENT;

    flux_paint_state snap;
    flux_result r = flux_paint_snapshot(paint, &snap);
    if (r != FLUX_OK) return r;

    const flux_path *use = path;
    bool owns = false;
    flux_matrix m = canvas_transform(c);
    if (!flux_matrix_is_identity(&m)) {
        flux_path *tx = NULL;
        r = flux_path_transform(path, &m, &tx);
        if (r != FLUX_OK) {
            flux_paint_state_dispose(&snap, c->owner->ctx);
            return r;
        }
        use = tx;
        owns = true;
    }

    flux_op op = {
        .kind = FLUX_OP_FILL_PATH,
        .u.fill_path = { .path = use, .owns_path = owns, .paint = snap },
    };
    if (!flux_canvas_push_op(c, &op)) {
        if (owns) flux_path_release((flux_path *)use);
        flux_paint_state_dispose(&snap, c->owner->ctx);
        return FLUX_ERROR_OUT_OF_MEMORY;
    }
    return FLUX_OK;
}

flux_result flux_canvas_stroke_path(flux_canvas *c, const flux_path *path, const flux_paint *paint)
{
    if (!c || !path || !paint) return FLUX_ERROR_INVALID_ARGUMENT;
    if (paint->state.stroke_width <= 0.0f) return FLUX_ERROR_INVALID_ARGUMENT;

    flux_paint_state snap;
    flux_result r = flux_paint_snapshot(paint, &snap);
    if (r != FLUX_OK) return r;

    const flux_path *use = path;
    bool owns = false;
    flux_matrix m = canvas_transform(c);
    if (!flux_matrix_is_identity(&m)) {
        flux_path *tx = NULL;
        r = flux_path_transform(path, &m, &tx);
        if (r != FLUX_OK) {
            flux_paint_state_dispose(&snap, c->owner->ctx);
            return r;
        }
        use = tx;
        owns = true;
    }

    flux_op op = {
        .kind = FLUX_OP_STROKE_PATH,
        .u.stroke_path = { .path = use, .owns_path = owns, .paint = snap },
    };
    if (!flux_canvas_push_op(c, &op)) {
        if (owns) flux_path_release((flux_path *)use);
        flux_paint_state_dispose(&snap, c->owner->ctx);
        return FLUX_ERROR_OUT_OF_MEMORY;
    }
    return FLUX_OK;
}

flux_result flux_canvas_draw_image(flux_canvas *c, const flux_image *image,
                                   const flux_rect *src, const flux_rect *dst)
{
    if (!c || !image || !dst) return FLUX_ERROR_INVALID_ARGUMENT;

    flux_rect scaled_dst = scale_rect_by_dpr(dst, c->dpr);

    flux_rect src_rect;
    if (src) {
        src_rect = *src;
    } else {
        uint32_t w = 0, h = 0;
        if (flux_image_get_size(image, &w, &h) != FLUX_OK)
            return FLUX_ERROR_INVALID_ARGUMENT;
        src_rect = (flux_rect){ 0.0f, 0.0f, (float)w, (float)h };
    }

    flux_op op = {
        .kind = FLUX_OP_DRAW_IMAGE,
        .u.draw_image = { .image = image, .src = src_rect, .dst = scaled_dst },
    };
    if (!flux_canvas_push_op(c, &op)) return FLUX_ERROR_OUT_OF_MEMORY;
    (void)flux_image_retain((flux_image *)image);
    return FLUX_OK;
}

flux_result flux_canvas_draw_glyph_run(flux_canvas *c, const flux_glyph_run *run,
                                       float x, float y, const flux_paint *paint)
{
    if (!c || !run || !paint) return FLUX_ERROR_INVALID_ARGUMENT;
    if (flux_glyph_run_count(run) == 0) return FLUX_ERROR_INVALID_ARGUMENT;

    flux_paint_state snap;
    flux_result r = flux_paint_snapshot(paint, &snap);
    if (r != FLUX_OK) return r;

    x *= c->dpr;
    y *= c->dpr;
    flux_matrix_transform_point(&c->current_matrix, &x, &y);

    flux_op op = {
        .kind = FLUX_OP_DRAW_GLYPHS,
        .u.draw_glyphs = { .run = run, .x = x, .y = y, .paint = snap },
    };
    if (!flux_canvas_push_op(c, &op)) {
        flux_paint_state_dispose(&snap, c->owner->ctx);
        return FLUX_ERROR_OUT_OF_MEMORY;
    }
    (void)flux_glyph_run_retain((flux_glyph_run *)run);
    return FLUX_OK;
}

flux_result flux_canvas_apply_blur(flux_canvas *c, float sigma)
{
    if (!c) return FLUX_ERROR_INVALID_ARGUMENT;
    if (sigma <= 0.0f) return FLUX_OK;
    flux_op op = { .kind = FLUX_OP_APPLY_BLUR, .u.apply_blur = { .sigma = sigma } };
    return flux_canvas_push_op(c, &op) ? FLUX_OK : FLUX_ERROR_OUT_OF_MEMORY;
}

/* ------------------------------------------------------------------ */
/*  Clipping                                                          */
/* ------------------------------------------------------------------ */

flux_result flux_canvas_clip_rect(flux_canvas *c, const flux_rect *rect)
{
    if (!c || !rect) return FLUX_ERROR_INVALID_ARGUMENT;
    flux_rect r = scale_rect_by_dpr(rect, c->dpr);

    flux_matrix m = canvas_transform(c);
    if (!flux_matrix_is_identity(&m)) {
        flux_rect tx;
        flux_matrix_transform_rect(&m, &r, &tx);
        r = tx;
    }

    flux_op op = { .kind = FLUX_OP_CLIP_RECT, .u.clip_rect = { .rect = r } };
    return flux_canvas_push_op(c, &op) ? FLUX_OK : FLUX_ERROR_OUT_OF_MEMORY;
}

flux_result flux_canvas_reset_clip(flux_canvas *c)
{
    if (!c) return FLUX_ERROR_INVALID_ARGUMENT;
    flux_op op = { .kind = FLUX_OP_RESET_CLIP };
    return flux_canvas_push_op(c, &op) ? FLUX_OK : FLUX_ERROR_OUT_OF_MEMORY;
}

flux_result flux_canvas_clip_path(flux_canvas *c, const flux_path *path)
{
    if (!c || !path) return FLUX_ERROR_INVALID_ARGUMENT;

    const flux_path *use = path;
    bool owns = false;
    flux_matrix m = canvas_transform(c);
    if (!flux_matrix_is_identity(&m)) {
        flux_path *tx = NULL;
        flux_result r = flux_path_transform(path, &m, &tx);
        if (r != FLUX_OK) return r;
        use = tx;
        owns = true;
    }

    flux_op op = {
        .kind = FLUX_OP_CLIP_PATH,
        .u.clip_path = { .path = use, .owns_path = owns },
    };
    if (!flux_canvas_push_op(c, &op)) {
        if (owns) flux_path_release((flux_path *)use);
        return FLUX_ERROR_OUT_OF_MEMORY;
    }
    return FLUX_OK;
}
