/*
 * Backend-agnostic execution engine.
 *
 * Consumes a recorded display list and renders every op through the
 * renderer vtable.  No dependency on any specific graphics API.
 */
#include "internal.h"
#include "renderer/renderer.h"
#include "state/state.h"
#include "geometry/geometry.h"
#include "math/matrix.h"
#include "math/rect.h"
#include "math/arena.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- inline helpers ---- */

static inline const struct fx_renderer_vtbl *v(fx_renderer *r)
{
    return fx_renderer_vt(r);
}

static inline int32_t iround(float f) { return (int32_t)roundf(f); }
static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static void scissor_rect(fx_renderer *r, const fx_rect *rc,
                         uint32_t sw, uint32_t sh)
{
    int32_t x = iround(rc->x), y = iround(rc->y);
    int32_t w = iround(rc->w), h = iround(rc->h);
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int32_t)sw) w = (int32_t)sw - x;
    if (y + h > (int32_t)sh) h = (int32_t)sh - y;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    v(r)->scissor(r, x, y, (uint32_t)w, (uint32_t)h);
}

static void stencil_clr(fx_renderer *r, const fx_rect *rc,
                        uint32_t sw, uint32_t sh)
{
    int32_t x = iround(rc->x), y = iround(rc->y);
    int32_t w = iround(rc->w), h = iround(rc->h);
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int32_t)sw) w = (int32_t)sw - x;
    if (y + h > (int32_t)sh) h = (int32_t)sh - y;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    v(r)->stencil_clear(r, x, y, (uint32_t)w, (uint32_t)h);
}

/* ---- vertex helpers ---- */

static bool emit_solid_tris(fx_renderer *r, fx_arena *arena,
                            const fx_point *src, size_t n, fx_color c)
{
    fx_r_buffer *buf = nullptr;
    uint32_t first = 0;
    fx_solid_vertex *vx = v(r)->alloc_solid(r, n, &buf, &first);
    if (!vx) return false;
    for (size_t i = 0; i < n; i++)
        vx[i] = (fx_solid_vertex){ .pos = { src[i].x, src[i].y } };
    v(r)->draw_solid(r, buf, first, (uint32_t)n, c);
    return true;
}

static bool emit_gradient_tris(fx_renderer *r, fx_arena *arena,
                               const fx_point *src, size_t n,
                               const fx_gradient *g)
{
    fx_r_buffer *buf = nullptr;
    uint32_t first = 0;
    fx_solid_vertex *vx = v(r)->alloc_solid(r, n, &buf, &first);
    if (!vx) return false;
    for (size_t i = 0; i < n; i++)
        vx[i] = (fx_solid_vertex){ .pos = { src[i].x, src[i].y } };
    v(r)->draw_gradient(r, buf, first, (uint32_t)n, g);
    return true;
}

static bool emit_stencil_tris(fx_renderer *r, fx_arena *arena,
                              const fx_point *src, size_t n,
                              int fill_rule)
{
    fx_r_buffer *buf = nullptr;
    uint32_t first = 0;
    fx_solid_vertex *vx = v(r)->alloc_solid(r, n, &buf, &first);
    if (!vx) return false;
    for (size_t i = 0; i < n; i++)
        vx[i] = (fx_solid_vertex){ .pos = { src[i].x, src[i].y } };
    v(r)->stencil_fill(r, buf, first, (uint32_t)n, fill_rule);
    return true;
}

static bool draw_solid_rect(fx_renderer *r, const fx_rect *rc, fx_color c)
{
    fx_r_buffer *buf = nullptr;
    uint32_t first = 0;
    fx_solid_vertex *vx = v(r)->alloc_solid(r, 6, &buf, &first);
    if (!vx) return false;

    float x = rc->x, y = rc->y, x2 = rc->x + rc->w, y2 = rc->y + rc->h;
    vx[0] = (fx_solid_vertex){ .pos = { x,  y  } };
    vx[1] = (fx_solid_vertex){ .pos = { x2, y  } };
    vx[2] = (fx_solid_vertex){ .pos = { x2, y2 } };
    vx[3] = (fx_solid_vertex){ .pos = { x,  y  } };
    vx[4] = (fx_solid_vertex){ .pos = { x2, y2 } };
    vx[5] = (fx_solid_vertex){ .pos = { x,  y2 } };

    v(r)->draw_solid(r, buf, first, 6, c);
    return true;
}

static bool draw_gradient_rect(fx_renderer *r, const fx_rect *rc,
                               const fx_gradient *g)
{
    fx_r_buffer *buf = nullptr;
    uint32_t first = 0;
    fx_solid_vertex *vx = v(r)->alloc_solid(r, 6, &buf, &first);
    if (!vx) return false;

    float x = rc->x, y = rc->y, x2 = rc->x + rc->w, y2 = rc->y + rc->h;
    vx[0] = (fx_solid_vertex){ .pos = { x,  y  } };
    vx[1] = (fx_solid_vertex){ .pos = { x2, y  } };
    vx[2] = (fx_solid_vertex){ .pos = { x2, y2 } };
    vx[3] = (fx_solid_vertex){ .pos = { x,  y  } };
    vx[4] = (fx_solid_vertex){ .pos = { x2, y2 } };
    vx[5] = (fx_solid_vertex){ .pos = { x,  y2 } };

    v(r)->draw_gradient(r, buf, first, 6, g);
    return true;
}

static bool fill_tessellated(fx_renderer *r, fx_arena *arena,
                             const fx_point *poly, size_t n, fx_color c)
{
    fx_point *tris = nullptr;
    size_t tc = 0;
    if (!fx_tessellate_simple_polygon(poly, n, arena, &tris, &tc))
        return false;
    return emit_solid_tris(r, arena, tris, tc, c);
}

static bool fill_tessellated_gradient(fx_renderer *r, fx_arena *arena,
                                      const fx_point *poly, size_t n,
                                      const fx_gradient *g)
{
    fx_point *tris = nullptr;
    size_t tc = 0;
    if (!fx_tessellate_simple_polygon(poly, n, arena, &tris, &tc))
        return false;
    return emit_gradient_tris(r, arena, tris, tc, g);
}

/* ---- dash pattern subdivision ---- */

static size_t subdivide_dash(const fx_point *src, size_t n, bool closed,
                             const float *dashes, uint32_t dash_count,
                             float dash_phase, fx_arena *arena,
                             fx_point **out)
{
    if (!src || n < 2 || !dashes || dash_count < 2 || !out) {
        *out = (fx_point *)src;
        return n;
    }

    float total = 0.0f;
    for (uint32_t di = 0; di < dash_count; di++) total += dashes[di];
    if (total <= 0.0f) { *out = (fx_point *)src; return n; }

    /* Walk the polyline, emit segments for "on" parts */
    size_t cap = n * 2;
    fx_point *result = fx_arena_alloc(arena, cap * sizeof(fx_point));
    if (!result) { *out = (fx_point *)src; return n; }

    size_t out_n = 0;
    float dist = 0.0f;
    uint32_t di = 0;
    float remaining = dashes[0];
    bool on = true;
    float phase = dash_phase;

    /* Advance past negative phase */
    while (phase > 0.0f) {
        if (phase >= dashes[di]) {
            phase -= dashes[di];
            di = (di + 1) % dash_count;
            on = !on;
        } else {
            remaining = dashes[di] - phase;
            phase = 0.0f;
        }
    }

    fx_point start = src[0];
    for (size_t i = 0; i + 1 < n; i++) {
        float dx = src[i + 1].x - src[i].x;
        float dy = src[i + 1].y - src[i].y;
        float seg_len = sqrtf(dx * dx + dy * dy);
        if (seg_len == 0.0f) continue;

        float seg_pos = 0.0f;
        while (seg_pos < seg_len) {
            float take = seg_len - seg_pos;
            if (take > remaining) take = remaining;

            if (on) {
                if (out_n == 0) {
                    if (result) result[out_n++] = start;
                }
                float t = (seg_pos + take) / seg_len;
                fx_point end = {
                    .x = src[i].x + dx * t,
                    .y = src[i].y + dy * t,
                };
                result[out_n++] = end;
            } else {
                if (out_n > 0) {
                    /* Emit the "off" segment end as new start */
                    float t = (seg_pos + take) / seg_len;
                    start = (fx_point){
                        .x = src[i].x + dx * t,
                        .y = src[i].y + dy * t,
                    };
                    out_n = 0;
                }
            }

            seg_pos += take;
            dist += take;
            remaining -= take;
            if (remaining <= 0.0f) {
                di = (di + 1) % dash_count;
                remaining = dashes[di];
                on = !on;
                if (on) {
                    float t = seg_pos / seg_len;
                    start = (fx_point){
                        .x = src[i].x + dx * t,
                        .y = src[i].y + dy * t,
                    };
                }
            }
        }
    }

    /* Handle closed path: close the last segment */
    if (closed && out_n > 0) {
        result[out_n] = result[0];
        out_n++;
    }

    *out = result;
    return out_n;
}

/* ---- stroke ---- */

static bool stroke_rect_aa(fx_renderer *r, const fx_rect *rc,
                           float width, fx_color c)
{
    float hw = width * 0.5f;
    fx_rect o = { rc->x - hw, rc->y - hw, rc->w + width, rc->h + width };
    fx_rect i = { rc->x + hw, rc->y + hw, rc->w - width, rc->h - width };

    if (i.w <= 0.0f || i.h <= 0.0f) return draw_solid_rect(r, &o, c);

    bool ok = true;
    ok &= draw_solid_rect(r, &(fx_rect){ o.x, o.y, o.w, i.y - o.y }, c);
    ok &= draw_solid_rect(r, &(fx_rect){ o.x, i.y + i.h, o.w, (o.y + o.h) - (i.y + i.h) }, c);
    ok &= draw_solid_rect(r, &(fx_rect){ o.x, i.y, i.x - o.x, i.h }, c);
    ok &= draw_solid_rect(r, &(fx_rect){ i.x + i.w, i.y, (o.x + o.w) - (i.x + i.w), i.h }, c);
    return ok;
}

/* ---- stencil path fill (multi-subpath / holes) ---- */

static bool fill_stencil_path(fx_renderer *r, fx_arena *arena,
                              const fx_path *path, fx_color color,
                              const fx_gradient *grad,
                              uint32_t sw, uint32_t sh,
                              int fill_rule,
                              bool has_clip, const fx_path *clip_path)
{
    fx_rect b;
    if (!fx_path_get_bounds(path, &b)) return false;
    uint32_t subn = (uint32_t)fx_path_subpath_count(path);

    scissor_rect(r, &b, sw, sh);
    stencil_clr(r, &b, sw, sh);

    for (uint32_t si = 0; si < subn; si++) {
        fx_point *flat = nullptr;
        size_t pc = 0;
        bool closed = false;
        if (!fx_path_flatten_subpath(path, si, 0.25f, arena, &flat, &pc, &closed))
            continue;
        if (pc < 3) continue;

        fx_point *tris = nullptr;
        size_t tc = 0;
        if (!fx_tessellate_simple_polygon(flat, pc, arena, &tris, &tc))
            continue;
        emit_stencil_tris(r, arena, tris, tc, fill_rule);
    }

    /* Cover quad */
    fx_r_buffer *cb = nullptr;
    uint32_t cf = 0;
    fx_solid_vertex *cv = v(r)->alloc_solid(r, 6, &cb, &cf);
    if (!cv) return false;

    cv[0] = (fx_solid_vertex){ .pos = { b.x, b.y } };
    cv[1] = (fx_solid_vertex){ .pos = { b.x + b.w, b.y } };
    cv[2] = (fx_solid_vertex){ .pos = { b.x + b.w, b.y + b.h } };
    cv[3] = (fx_solid_vertex){ .pos = { b.x, b.y } };
    cv[4] = (fx_solid_vertex){ .pos = { b.x + b.w, b.y + b.h } };
    cv[5] = (fx_solid_vertex){ .pos = { b.x, b.y + b.h } };

    v(r)->stencil_ref(r, 1);
    if (grad) v(r)->cover_gradient(r, cb, cf, 6, grad);
    else      v(r)->cover_solid(r, cb, cf, 6, color);

    if (has_clip && clip_path) {
        stencil_clr(r, &b, sw, sh);
        const fx_point *cp = nullptr;
        size_t cc = 0;
        fx_point *cflat = nullptr;
        if (fx_path_get_line_loop(clip_path, &cp, &cc) ||
            fx_path_flatten_line_loop(clip_path, 0.25f, arena, &cflat, &cc)) {
            if (cflat) cp = cflat;
            if (cp && cc >= 3) {
                emit_stencil_tris(r, arena, cp, cc, 0);
            }
        }
    }

    return true;
}

/* ---- main execute loop ---- */

size_t fx_engine_execute(fx_canvas *canvas, fx_renderer *r)
{
    uint32_t sw = 0, sh = 0;
    v(r)->surface_extent(r, &sw, &sh);

    fx_arena arena;
    fx_arena_init(&arena, 65536);

    size_t executed = 0;
    const fx_path *active_clip_path = nullptr;

    v(r)->begin_pass(r, canvas->has_clear ? canvas->clear_color : 0);
    v(r)->scissor(r, 0, 0, sw, sh);
    v(r)->stencil_ref(r, 0);

    for (size_t i = 0; i < canvas->op_count; i++) {
        const fx_op *op = &canvas->ops[i];

        switch (op->kind) {

        case FX_OP_CLIP_RECT:
            v(r)->flush_solid(r);
            scissor_rect(r, &op->u.clip_rect.rect, sw, sh);
            continue;

        case FX_OP_RESET_CLIP:
            v(r)->flush_solid(r);
            v(r)->scissor(r, 0, 0, sw, sh);
            v(r)->stencil_clear(r, 0, 0, sw, sh);
            active_clip_path = nullptr;
            v(r)->stencil_ref(r, 0);
            continue;

        case FX_OP_CLIP_PATH: {
            v(r)->flush_solid(r);
            const fx_path *cp = op->u.clip_path.path;
            fx_rect cr;
            if (fx_path_get_bounds(cp, &cr)) {
                scissor_rect(r, &cr, sw, sh);
                stencil_clr(r, &cr, sw, sh);
            }

            const fx_point *cpts = nullptr;
            size_t ccn = 0;
            fx_point *cflat = nullptr;
            if (fx_path_get_line_loop(cp, &cpts, &ccn) ||
                fx_path_flatten_line_loop(cp, 0.25f, &arena, &cflat, &ccn)) {
                if (cflat) cpts = cflat;
                if (ccn >= 3) emit_stencil_tris(r, &arena, cpts, ccn, 0);
            }
            active_clip_path = cp;
            v(r)->stencil_ref(r, 1);
            continue;
        }

        case FX_OP_FILL_RECT:
            draw_solid_rect(r, &op->u.fill_rect.rect, op->u.fill_rect.color);
            executed++;
            continue;

        case FX_OP_FILL_PATH: {
            const fx_path *fp = op->u.fill_path.path;
            const fx_gradient *gr = op->u.fill_path.paint.gradient;
            fx_rect rr;

            if (fx_path_is_axis_aligned_rect(fp, &rr)) {
                if (gr) draw_gradient_rect(r, &rr, gr);
                else    draw_solid_rect(r, &rr, op->u.fill_path.paint.color);
                executed++;
                continue;
            }

            uint32_t sn = (uint32_t)fx_path_subpath_count(fp);
            const fx_point *pts = nullptr;
            size_t pn = 0;
            fx_point *flat = nullptr;

            if (sn == 1 && fx_path_get_line_loop(fp, &pts, &pn)) {
                if (gr) fill_tessellated_gradient(r, &arena, pts, pn, gr);
                else    fill_tessellated(r, &arena, pts, pn, op->u.fill_path.paint.color);
                executed++;
                continue;
            }
            if (sn == 1 &&
                fx_path_flatten_line_loop(fp, 0.25f, &arena, &flat, &pn)) {
                if (gr) fill_tessellated_gradient(r, &arena, flat, pn, gr);
                else    fill_tessellated(r, &arena, flat, pn, op->u.fill_path.paint.color);
                executed++;
                continue;
            }

            if (fill_stencil_path(r, &arena, fp, op->u.fill_path.paint.color,
                                  gr, sw, sh,
                                  (int)op->u.fill_path.paint.fill_rule,
                                  active_clip_path != nullptr, active_clip_path))
                executed++;
            continue;
        }

        case FX_OP_STROKE_PATH: {
            const fx_path *sp = op->u.stroke_path.path;
            const fx_paint *p = &op->u.stroke_path.paint;
            fx_rect rr;
            uint32_t sn = (uint32_t)fx_path_subpath_count(sp);

            if (!p->dash_array && sn == 1 && p->line_join == FX_JOIN_MITER &&
                fx_path_is_axis_aligned_rect(sp, &rr)) {
                stroke_rect_aa(r, &rr, p->stroke_width, p->color);
                executed++;
                continue;
            }

            bool any = false;
            for (uint32_t si = 0; si < sn; si++) {
                fx_point *flat = nullptr;
                size_t pc = 0;
                bool closed = false;
                if (!fx_path_flatten_subpath(sp, si, 0.25f, &arena,
                                             &flat, &pc, &closed))
                    continue;

                /* Apply dash pattern if present */
                if (p->dash_array && p->dash_count >= 2) {
                    fx_point *dashed = nullptr;
                    pc = subdivide_dash(flat, pc, closed,
                                       p->dash_array, p->dash_count,
                                       p->dash_phase, &arena, &dashed);
                    flat = dashed;
                    if (pc < 2) continue;
                }

                fx_point *st = nullptr;
                size_t sc = 0;
                if (fx_stroke_polyline(flat, pc, closed, p, &arena, &st, &sc)) {
                    any |= emit_solid_tris(r, &arena, st, sc, p->color);
                }
            }
            if (any) executed++;
            continue;
        }

        case FX_OP_DRAW_IMAGE: {
            const fx_draw_image_op *io = &op->u.draw_image;
            v(r)->flush_solid(r);

            uint32_t iw, ih;
            if (io->image && fx_image_get_desc(io->image, nullptr)) {
                fx_image_desc d;
                fx_image_get_desc(io->image, &d);
                iw = d.width; ih = d.height;
            } else {
                iw = sw; ih = sh;
            }

            /* Build image quad */
            fx_r_buffer *ib = nullptr;
            uint32_t ifirst = 0;
            fx_image_vertex *iv = v(r)->alloc_image(r, 6, &ib, &ifirst);
            if (!iv) continue;

            fx_rect src = io->src, dst = io->dst;
            float u0 = src.x / (float)iw, v0 = src.y / (float)ih;
            float u1 = (src.x + src.w) / (float)iw, v1 = (src.y + src.h) / (float)ih;
            iv[0] = (fx_image_vertex){ { dst.x, dst.y }, { u0, v0 } };
            iv[1] = (fx_image_vertex){ { dst.x + dst.w, dst.y }, { u1, v0 } };
            iv[2] = (fx_image_vertex){ { dst.x + dst.w, dst.y + dst.h }, { u1, v1 } };
            iv[3] = (fx_image_vertex){ { dst.x, dst.y }, { u0, v0 } };
            iv[4] = (fx_image_vertex){ { dst.x + dst.w, dst.y + dst.h }, { u1, v1 } };
            iv[5] = (fx_image_vertex){ { dst.x, dst.y + dst.h }, { u0, v1 } };

            fx_r_texture *tex = io->image ? io->image->rtex : v(r)->surface_texture(r);
            v(r)->draw_image(r, ib, ifirst, 6, tex);
            executed++;
            continue;
        }

        case FX_OP_MASK_BLUR:
            v(r)->flush_solid(r);
            v(r)->blur(r, op->u.mask_blur.sigma);
            executed++;
            continue;

        case FX_OP_DRAW_GLYPHS:
            v(r)->flush_solid(r);
            executed++;
            continue;
        }
    }

    v(r)->flush_solid(r);
    v(r)->end_pass(r);
    v(r)->submit(r);

    fx_arena_destroy(&arena);
    return executed;
}
