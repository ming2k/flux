#include "internal.h"
#include <math.h>

static bool ensure_op_capacity(vg_canvas *c, size_t extra)
{
    size_t need = c->op_count + extra;
    if (need <= c->op_cap) return true;

    size_t new_cap = c->op_cap ? c->op_cap : 16;
    while (new_cap < need) new_cap *= 2;

    vg_op *ops = realloc(c->ops, new_cap * sizeof(*ops));
    if (!ops) return false;

    c->ops = ops;
    c->op_cap = new_cap;
    return true;
}

static bool push_op(vg_canvas *c, const vg_op *op)
{
    if (!c || !op) return false;
    if (!ensure_op_capacity(c, 1)) return false;
    c->ops[c->op_count++] = *op;
    return true;
}

void vg_canvas_reset(vg_canvas *c)
{
    if (!c) return;

    for (size_t i = 0; i < c->op_count; ++i) {
        vg_op *op = &c->ops[i];
        if (op->kind == VG_OP_FILL_PATH && op->u.fill_path.owns_path) {
            vg_path_destroy((vg_path *)op->u.fill_path.path);
        } else if (op->kind == VG_OP_STROKE_PATH && op->u.stroke_path.owns_path) {
            vg_path_destroy((vg_path *)op->u.stroke_path.path);
        }
    }

    c->has_clear = false;
    c->clear_color = 0;
    c->op_count = 0;

    c->state_count = 0;
    vg_matrix_identity(&c->current_matrix);
}

void vg_canvas_dispose(vg_canvas *c)
{
    if (!c) return;
    vg_canvas_reset(c);
    free(c->ops);
    free(c->state_stack);
    memset(c, 0, sizeof(*c));
}

void vg_clear(vg_canvas *c, vg_color color)
{
    if (!c) return;
    c->clear_color = color;
    c->has_clear = true;
}

size_t vg_canvas_op_count(const vg_canvas *c)
{
    return c ? c->op_count : 0;
}

void vg_paint_init(vg_paint *paint, vg_color color)
{
    if (!paint) return;
    paint->color = color;
    paint->stroke_width = 1.0f;
    paint->miter_limit = 4.0f;
    paint->line_cap = VG_CAP_BUTT;
    paint->line_join = VG_JOIN_MITER;
}

bool vg_fill_path(vg_canvas *c, const vg_path *path, const vg_paint *paint)
{
    if (!c || !path || !paint) return false;

    vg_op op = {
        .kind = VG_OP_FILL_PATH,
        .u.fill_path = {
            .path = path,
            .paint = *paint,
            .owns_path = false,
        },
    };

    if (!vg_matrix_is_identity(&c->current_matrix)) {
        op.u.fill_path.path = vg_path_transform(path, &c->current_matrix);
        op.u.fill_path.owns_path = true;
        if (!op.u.fill_path.path) return false;
    }

    return push_op(c, &op);
}

bool vg_stroke_path(vg_canvas *c, const vg_path *path, const vg_paint *paint)
{
    if (!c || !path || !paint || paint->stroke_width <= 0.0f) return false;

    vg_op op = {
        .kind = VG_OP_STROKE_PATH,
        .u.stroke_path = {
            .path = path,
            .paint = *paint,
            .owns_path = false,
        },
    };

    if (!vg_matrix_is_identity(&c->current_matrix)) {
        op.u.stroke_path.path = vg_path_transform(path, &c->current_matrix);
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

bool vg_draw_image(vg_canvas *c, const vg_image *image,
                   const vg_rect *src, const vg_rect *dst)
{
    vg_rect full_src = { 0 };
    vg_op op = {
        .kind = VG_OP_DRAW_IMAGE,
        .u.draw_image = {
            .image = image,
            .src = { 0 },
            .dst = dst ? *dst : (vg_rect){ 0 },
        },
    };

    if (!image || !dst) return false;
    if (!vg_image_get_desc(image, NULL)) return false;

    if (!src) {
        vg_image_desc desc;
        if (!vg_image_get_desc(image, &desc)) return false;
        full_src = (vg_rect){
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

bool vg_draw_glyph_run(vg_canvas *c, const vg_font *font,
                       const vg_glyph_run *run,
                       float x, float y, const vg_paint *paint)
{
    if (!c || !font || !run || !paint || vg_glyph_run_count(run) == 0) return false;

    vg_matrix_transform_point(&c->current_matrix, &x, &y);

    vg_op op = {
        .kind = VG_OP_DRAW_GLYPHS,
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

void vg_save(vg_canvas *c)
{
    if (!c) return;

    if (c->state_count + 1 > c->state_cap) {
        size_t new_cap = c->state_cap ? c->state_cap * 2 : 4;
        vg_matrix *new_stack = realloc(c->state_stack, new_cap * sizeof(vg_matrix));
        if (!new_stack) return;
        c->state_stack = new_stack;
        c->state_cap = new_cap;
    }

    c->state_stack[c->state_count++] = c->current_matrix;
}

void vg_restore(vg_canvas *c)
{
    if (!c || c->state_count == 0) return;
    c->current_matrix = c->state_stack[--c->state_count];
}

void vg_translate(vg_canvas *c, float dx, float dy)
{
    if (!c) return;
    vg_matrix m;
    vg_matrix_identity(&m);
    m.m[4] = dx;
    m.m[5] = dy;
    vg_concat(c, &m);
}

void vg_scale(vg_canvas *c, float sx, float sy)
{
    if (!c) return;
    vg_matrix m;
    vg_matrix_identity(&m);
    m.m[0] = sx;
    m.m[3] = sy;
    vg_concat(c, &m);
}

void vg_rotate(vg_canvas *c, float radians)
{
    if (!c) return;
    float s = sinf(radians);
    float cc = cosf(radians);
    vg_matrix m;
    m.m[0] = cc;  m.m[2] = -s; m.m[4] = 0.0f;
    m.m[1] = s;   m.m[3] = cc; m.m[5] = 0.0f;
    vg_concat(c, &m);
}

void vg_concat(vg_canvas *c, const vg_matrix *m)
{
    if (!c || !m) return;
    vg_matrix next;
    vg_matrix_multiply(&next, &c->current_matrix, m);
    c->current_matrix = next;
}

void vg_set_matrix(vg_canvas *c, const vg_matrix *m)
{
    if (!c || !m) return;
    c->current_matrix = *m;
}

void vg_get_matrix(const vg_canvas *c, vg_matrix *out_m)
{
    if (!c || !out_m) return;
    *out_m = c->current_matrix;
}
