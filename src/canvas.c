#include "internal.h"
#include <math.h>

static bool ensure_op_capacity(fx_canvas *c, size_t extra)
{
    size_t need = c->op_count + extra;
    if (need <= c->op_cap) return true;

    size_t new_cap = c->op_cap ? c->op_cap : 16;
    while (new_cap < need) new_cap *= 2;

    fx_op *ops = realloc(c->ops, new_cap * sizeof(*ops));
    if (!ops) return false;

    c->ops = ops;
    c->op_cap = new_cap;
    return true;
}

static bool push_op(fx_canvas *c, const fx_op *op)
{
    if (!c || !op) return false;
    if (!ensure_op_capacity(c, 1)) return false;
    c->ops[c->op_count++] = *op;
    return true;
}

void fx_canvas_reset(fx_canvas *c)
{
    if (!c) return;

    for (size_t i = 0; i < c->op_count; ++i) {
        fx_op *op = &c->ops[i];
        if (op->kind == FX_OP_FILL_PATH && op->u.fill_path.owns_path) {
            fx_path_destroy((fx_path *)op->u.fill_path.path);
        } else if (op->kind == FX_OP_STROKE_PATH && op->u.stroke_path.owns_path) {
            fx_path_destroy((fx_path *)op->u.stroke_path.path);
        }
    }

    c->has_clear = false;
    c->clear_color = 0;
    c->op_count = 0;

    c->state_count = 0;
    fx_matrix_identity(&c->current_matrix);
}

void fx_canvas_dispose(fx_canvas *c)
{
    if (!c) return;
    fx_canvas_reset(c);
    free(c->ops);
    free(c->state_stack);
    memset(c, 0, sizeof(*c));
}

void fx_clear(fx_canvas *c, fx_color color)
{
    if (!c) return;
    c->clear_color = color;
    c->has_clear = true;
}

size_t fx_canvas_op_count(const fx_canvas *c)
{
    return c ? c->op_count : 0;
}

void fx_paint_init(fx_paint *paint, fx_color color)
{
    if (!paint) return;
    paint->color = color;
    paint->stroke_width = 1.0f;
    paint->miter_limit = 4.0f;
    paint->line_cap = FX_CAP_BUTT;
    paint->line_join = FX_JOIN_MITER;
}

bool fx_fill_rect(fx_canvas *c, const fx_rect *rect, fx_color color)
{
    if (!c || !rect) return false;
    fx_path *path = fx_path_create();
    if (!path) return false;
    fx_path_add_rect(path, rect);
    
    fx_paint p;
    fx_paint_init(&p, color);
    
    fx_op op = {
        .kind = FX_OP_FILL_PATH,
        .u.fill_path = {
            .path = path,
            .paint = p,
            .owns_path = true,
        },
    };

    if (!fx_matrix_is_identity(&c->current_matrix)) {
        fx_path *trans = fx_path_transform(path, &c->current_matrix);
        fx_path_destroy(path);
        if (!trans) return false;
        op.u.fill_path.path = trans;
    }

    return push_op(c, &op);
}

bool fx_fill_path(fx_canvas *c, const fx_path *path, const fx_paint *paint)
{
    if (!c || !path || !paint) return false;

    fx_op op = {
        .kind = FX_OP_FILL_PATH,
        .u.fill_path = {
            .path = path,
            .paint = *paint,
            .owns_path = false,
        },
    };

    if (!fx_matrix_is_identity(&c->current_matrix)) {
        op.u.fill_path.path = fx_path_transform(path, &c->current_matrix);
        op.u.fill_path.owns_path = true;
        if (!op.u.fill_path.path) return false;
    }

    return push_op(c, &op);
}

bool fx_stroke_path(fx_canvas *c, const fx_path *path, const fx_paint *paint)
{
    if (!c || !path || !paint || paint->stroke_width <= 0.0f) return false;

    fx_op op = {
        .kind = FX_OP_STROKE_PATH,
        .u.stroke_path = {
            .path = path,
            .paint = *paint,
            .owns_path = false,
        },
    };

    if (!fx_matrix_is_identity(&c->current_matrix)) {
        op.u.stroke_path.path = fx_path_transform(path, &c->current_matrix);
        op.u.stroke_path.owns_path = true;
        if (!op.u.stroke_path.path) return false;
        /* Note: stroke width is not transformed here; it follows
         * the convention of being in 'user space' but applied to
         * a path that was transformed. For Phase 1 we treat it as
         * constant in device space if transformed. A more complete
         * impl would scale the width too. */
    }

    return push_op(c, &op);
}

bool fx_draw_image(fx_canvas *c, const fx_image *image,
                   const fx_rect *src, const fx_rect *dst)
{
    return fx_draw_image_ex(c, image, src, dst);
}

bool fx_draw_image_ex(fx_canvas *c, const fx_image *image,
                      const fx_rect *src, const fx_rect *dst)
{
    fx_rect full_src = { 0 };
    fx_op op = {
        .kind = FX_OP_DRAW_IMAGE,
        .u.draw_image = {
            .image = image,
            .src = { 0 },
            .dst = dst ? *dst : (fx_rect){ 0 },
        },
    };

    if (!image || !dst) return false;
    if (!fx_image_get_desc(image, NULL)) return false;

    if (!src) {
        fx_image_desc desc;
        if (!fx_image_get_desc(image, &desc)) return false;
        full_src = (fx_rect){
            .x = 0.0f,
            .y = 0.0f,
            .w = (float)desc.width,
            .h = (float)desc.height,
        };
        op.u.draw_image.src = full_src;
    } else {
        op.u.draw_image.src = *src;
    }

    return push_op(c, &op);
}

bool fx_draw_glyph_run(fx_canvas *c, const fx_font *font,
                       const fx_glyph_run *run,
                       float x, float y, const fx_paint *paint)
{
    if (!c || !font || !run || !paint || fx_glyph_run_count(run) == 0) return false;

    fx_matrix_transform_point(&c->current_matrix, &x, &y);

    fx_op op = {
        .kind = FX_OP_DRAW_GLYPHS,
        .u.draw_glyphs = {
            .font = font,
            .run = run,
            .x = x,
            .y = y,
            .paint = *paint,
        },
    };
    return push_op(c, &op);
}

/* ---------- canvas state & transforms ---------- */

void fx_save(fx_canvas *c)
{
    if (!c) return;

    if (c->state_count + 1 > c->state_cap) {
        size_t new_cap = c->state_cap ? c->state_cap * 2 : 4;
        fx_matrix *new_stack = realloc(c->state_stack, new_cap * sizeof(fx_matrix));
        if (!new_stack) return;
        c->state_stack = new_stack;
        c->state_cap = new_cap;
    }

    c->state_stack[c->state_count++] = c->current_matrix;
}

void fx_restore(fx_canvas *c)
{
    if (!c || c->state_count == 0) return;
    c->current_matrix = c->state_stack[--c->state_count];
}

void fx_translate(fx_canvas *c, float dx, float dy)
{
    if (!c) return;
    fx_matrix m;
    fx_matrix_identity(&m);
    m.m[4] = dx;
    m.m[5] = dy;
    fx_concat(c, &m);
}

void fx_scale(fx_canvas *c, float sx, float sy)
{
    if (!c) return;
    fx_matrix m;
    fx_matrix_identity(&m);
    m.m[0] = sx;
    m.m[3] = sy;
    fx_concat(c, &m);
}

void fx_rotate(fx_canvas *c, float radians)
{
    if (!c) return;
    float s = sinf(radians);
    float cc = cosf(radians);
    fx_matrix m;
    m.m[0] = cc;  m.m[2] = -s; m.m[4] = 0.0f;
    m.m[1] = s;   m.m[3] = cc; m.m[5] = 0.0f;
    fx_concat(c, &m);
}

void fx_concat(fx_canvas *c, const fx_matrix *m)
{
    if (!c || !m) return;
    fx_matrix next;
    fx_matrix_multiply(&next, &c->current_matrix, m);
    c->current_matrix = next;
}

void fx_set_matrix(fx_canvas *c, const fx_matrix *m)
{
    if (!c || !m) return;
    c->current_matrix = *m;
}

void fx_get_matrix(const fx_canvas *c, fx_matrix *out_m)
{
    if (!c || !out_m) return;
    *out_m = c->current_matrix;
}
