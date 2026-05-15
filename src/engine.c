/*
 * Backend-agnostic execution engine.
 *
 * Walks a recorded display list and dispatches each op through the RHI
 * vtable. Knows nothing about Vulkan, software pixel buffers, or any
 * specific backend.
 */
#include "internal.h"
#include "rhi/rhi.h"
#include "state/state.h"
#include "geometry/geometry.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static inline const struct flux_rhi_vtbl *vt(flux_rhi_device *r)
{
    return flux_rhi_vt(r);
}

static inline int32_t iround(float f) { return (int32_t)roundf(f); }

static void scissor_rect(flux_rhi_device *r, const flux_rect *rc,
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
    vt(r)->scissor(r, x, y, (uint32_t)w, (uint32_t)h);
}

static void stencil_clr(flux_rhi_device *r, const flux_rect *rc,
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
    vt(r)->stencil_clear(r, x, y, (uint32_t)w, (uint32_t)h);
}

/* ---- vertex emitters ---- */

static bool emit_solid_tris(flux_rhi_device *r, const flux_point *src, size_t n, flux_color c)
{
    flux_r_buffer *buf = NULL;
    uint32_t first = 0;
    flux_solid_vertex *vx = vt(r)->alloc_solid(r, n, &buf, &first);
    if (!vx) return false;
    for (size_t i = 0; i < n; i++)
        vx[i] = (flux_solid_vertex){ .pos = { src[i].x, src[i].y } };
    vt(r)->draw_solid(r, buf, first, (uint32_t)n, c);
    return true;
}

static bool emit_stencil_tris(flux_rhi_device *r, const flux_point *src, size_t n, int fill_rule)
{
    flux_r_buffer *buf = NULL;
    uint32_t first = 0;
    flux_solid_vertex *vx = vt(r)->alloc_solid(r, n, &buf, &first);
    if (!vx) return false;
    for (size_t i = 0; i < n; i++)
        vx[i] = (flux_solid_vertex){ .pos = { src[i].x, src[i].y } };
    vt(r)->stencil_fill(r, buf, first, (uint32_t)n, fill_rule);
    return true;
}

static bool emit_fringe_tris(flux_rhi_device *r, const flux_point *src, size_t n, flux_color c)
{
    if (n < 3) return true;

    /* Compute signed area to determine outward normal direction. */
    float area = 0.0f;
    for (size_t i = 0; i < n; i++) {
        size_t j = (i + 1) % n;
        area += src[i].x * src[j].y - src[j].x * src[i].y;
    }
    float sign = (area > 0.0f) ? 1.0f : -1.0f;
    float width = 0.5f;

    size_t max_verts = n * 6;
    flux_r_buffer *buf = NULL;
    uint32_t first = 0;
    flux_fringe_vertex *vx = vt(r)->alloc_fringe(r, max_verts, &buf, &first);
    if (!vx) return false;

    size_t vi = 0;
    for (size_t i = 0; i < n; i++) {
        size_t j = (i + 1) % n;
        float dx = src[j].x - src[i].x;
        float dy = src[j].y - src[i].y;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 1e-3f) continue;

        float nx = (-dy / len) * sign * width;
        float ny = ( dx / len) * sign * width;

        flux_fringe_vertex v0 = { { src[i].x,           src[i].y           }, 1.0f, 0.0f };
        flux_fringe_vertex v1 = { { src[j].x,           src[j].y           }, 1.0f, 0.0f };
        flux_fringe_vertex v2 = { { src[i].x + nx,      src[i].y + ny      }, 0.0f, 0.0f };
        flux_fringe_vertex v3 = { { src[j].x + nx,      src[j].y + ny      }, 0.0f, 0.0f };

        vx[vi++] = v0; vx[vi++] = v1; vx[vi++] = v2;
        vx[vi++] = v1; vx[vi++] = v3; vx[vi++] = v2;
    }

    if (vi > 0)
        vt(r)->draw_fringe(r, buf, first, (uint32_t)vi, c);
    return true;
}

static bool draw_solid_rect(flux_rhi_device *r, const flux_rect *rc, flux_color c)
{
    flux_r_buffer *buf = NULL;
    uint32_t first = 0;
    flux_solid_vertex *vx = vt(r)->alloc_solid(r, 6, &buf, &first);
    if (!vx) return false;

    float x = rc->x, y = rc->y, x2 = rc->x + rc->w, y2 = rc->y + rc->h;
    vx[0] = (flux_solid_vertex){ { x,  y  } };
    vx[1] = (flux_solid_vertex){ { x2, y  } };
    vx[2] = (flux_solid_vertex){ { x2, y2 } };
    vx[3] = (flux_solid_vertex){ { x,  y  } };
    vx[4] = (flux_solid_vertex){ { x2, y2 } };
    vx[5] = (flux_solid_vertex){ { x,  y2 } };

    vt(r)->draw_solid(r, buf, first, 6, c);
    return true;
}

static bool draw_gradient_rect(flux_rhi_device *r, const flux_rect *rc,
                               const flux_gradient *g)
{
    flux_r_buffer *buf = NULL;
    uint32_t first = 0;
    flux_solid_vertex *vx = vt(r)->alloc_solid(r, 6, &buf, &first);
    if (!vx) return false;

    float x = rc->x, y = rc->y, x2 = rc->x + rc->w, y2 = rc->y + rc->h;
    vx[0] = (flux_solid_vertex){ { x,  y  } };
    vx[1] = (flux_solid_vertex){ { x2, y  } };
    vx[2] = (flux_solid_vertex){ { x2, y2 } };
    vx[3] = (flux_solid_vertex){ { x,  y  } };
    vx[4] = (flux_solid_vertex){ { x2, y2 } };
    vx[5] = (flux_solid_vertex){ { x,  y2 } };

    vt(r)->draw_gradient(r, buf, first, 6, g);
    return true;
}

/* ---- dash subdivision ---- */

static size_t subdivide_dash(const flux_point *src, size_t n, bool closed,
                             const float *dashes, uint32_t dash_count,
                             float dash_phase, flux_arena *arena,
                             flux_point **out)
{
    if (!src || n < 2 || !dashes || dash_count < 2 || !out) {
        *out = (flux_point *)src;
        return n;
    }
    float total = 0.0f;
    for (uint32_t di = 0; di < dash_count; di++) total += dashes[di];
    if (total <= 0.0f) { *out = (flux_point *)src; return n; }

    float total_len = 0.0f;
    for (size_t i = 0; i + 1 < n; i++) {
        float dx = src[i + 1].x - src[i].x;
        float dy = src[i + 1].y - src[i].y;
        total_len += sqrtf(dx * dx + dy * dy);
    }
    float min_dash = dashes[0];
    for (uint32_t di = 1; di < dash_count; di++)
        if (dashes[di] < min_dash) min_dash = dashes[di];
    size_t cap = (size_t)(total_len / min_dash + 1.0f) * 4 + n;
    flux_point *result = flux_arena_alloc(arena, cap * sizeof(flux_point));
    if (!result) { *out = (flux_point *)src; return n; }

    size_t out_n = 0;
    uint32_t di = 0;
    float remaining = dashes[0];
    bool on = true;
    float phase = dash_phase;

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

    flux_point start = src[0];
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
                if (out_n == 0) result[out_n++] = start;
                float t = (seg_pos + take) / seg_len;
                flux_point end = { src[i].x + dx * t, src[i].y + dy * t };
                result[out_n++] = end;
            } else if (out_n > 0) {
                float t = (seg_pos + take) / seg_len;
                start = (flux_point){ src[i].x + dx * t, src[i].y + dy * t };
                out_n = 0;
            }

            seg_pos += take;
            remaining -= take;
            if (remaining <= 0.0f) {
                di = (di + 1) % dash_count;
                remaining = dashes[di];
                on = !on;
                if (on) {
                    float t = seg_pos / seg_len;
                    start = (flux_point){ src[i].x + dx * t, src[i].y + dy * t };
                }
            }
        }
    }

    if (closed && out_n > 0) result[out_n++] = result[0];
    *out = result;
    return out_n;
}

/* ---- AA rectangle stroke fast path ---- */

static bool stroke_rect_aa(flux_rhi_device *r, const flux_rect *rc, float width, flux_color c)
{
    float hw = width * 0.5f;
    flux_rect o = { rc->x - hw, rc->y - hw, rc->w + width, rc->h + width };
    flux_rect i = { rc->x + hw, rc->y + hw, rc->w - width, rc->h - width };

    if (i.w <= 0.0f || i.h <= 0.0f) return draw_solid_rect(r, &o, c);

    bool ok = true;
    ok &= draw_solid_rect(r, &(flux_rect){ o.x, o.y, o.w, i.y - o.y }, c);
    ok &= draw_solid_rect(r, &(flux_rect){ o.x, i.y + i.h, o.w, (o.y + o.h) - (i.y + i.h) }, c);
    ok &= draw_solid_rect(r, &(flux_rect){ o.x, i.y, i.x - o.x, i.h }, c);
    ok &= draw_solid_rect(r, &(flux_rect){ i.x + i.w, i.y, (o.x + o.w) - (i.x + i.w), i.h }, c);
    return ok;
}

/* ---- stencil-buffer path fill ---- */

/* Convention: any op handler that mutates `scissor` or `stencil_ref`
 * must restore them to the defaults (full-surface scissor, stencil_ref=0)
 * before returning, unless `has_clip` is true — in which case the active
 * clip path owns the state and handlers must leave it alone. */

static bool fill_stencil_path(flux_rhi_device *r, flux_arena *arena,
                              const flux_path *path, flux_color color,
                              const flux_gradient *grad,
                              uint32_t sw, uint32_t sh, int fill_rule,
                              bool has_clip, const flux_path *clip_path)
{
    flux_rect b;
    if (flux_path_get_bounds(path, &b) != FLUX_OK) return false;
    if (has_clip && clip_path) {
        flux_rect cb;
        if (flux_path_get_bounds(clip_path, &cb) == FLUX_OK) {
            if (cb.x > b.x) { b.w -= (cb.x - b.x); b.x = cb.x; }
            if (cb.y > b.y) { b.h -= (cb.y - b.y); b.y = cb.y; }
            if (cb.x + cb.w < b.x + b.w) b.w = (cb.x + cb.w) - b.x;
            if (cb.y + cb.h < b.y + b.h) b.h = (cb.y + cb.h) - b.y;
            if (b.w <= 0 || b.h <= 0) return true;
        }
    }
    uint32_t subn = (uint32_t)flux_path_subpath_count(path);

    /* Temporary array to hold flattened subpaths for fringe AA. */
    typedef struct { flux_point *flat; size_t pc; } flat_entry;
    flat_entry *flats = (flat_entry *)flux_arena_alloc(arena, subn * sizeof(flat_entry));
    if (!flats) return false;
    uint32_t flat_count = 0;

    scissor_rect(r, &b, sw, sh);
    stencil_clr(r, &b, sw, sh);

    for (uint32_t si = 0; si < subn; si++) {
        flux_point *flat = NULL;
        size_t pc = 0;
        bool closed = false;
        if (flux_path_flatten_subpath(path, si, 0.25f, arena, &flat, &pc, &closed) != FLUX_OK)
            continue;
        if (pc < 3) continue;

        flux_point *tris = NULL;
        size_t tc = 0;
        if (flux_tessellate_simple_polygon(flat, pc, arena, &tris, &tc) != FLUX_OK)
            continue;
        emit_stencil_tris(r, tris, tc, fill_rule);

        if (closed) {
            flats[flat_count].flat = flat;
            flats[flat_count].pc = pc;
            flat_count++;
        }
    }

    /* Cover quad. */
    flux_r_buffer *cb = NULL;
    uint32_t cf = 0;
    flux_solid_vertex *cv = vt(r)->alloc_solid(r, 6, &cb, &cf);
    if (!cv) return false;
    cv[0] = (flux_solid_vertex){ { b.x, b.y } };
    cv[1] = (flux_solid_vertex){ { b.x + b.w, b.y } };
    cv[2] = (flux_solid_vertex){ { b.x + b.w, b.y + b.h } };
    cv[3] = (flux_solid_vertex){ { b.x, b.y } };
    cv[4] = (flux_solid_vertex){ { b.x + b.w, b.y + b.h } };
    cv[5] = (flux_solid_vertex){ { b.x, b.y + b.h } };

    vt(r)->stencil_ref(r, 1);
    if (grad) vt(r)->cover_gradient(r, cb, cf, 6, grad);
    else      vt(r)->cover_solid(r, cb, cf, 6, color);

    /* Fringe AA pass (solid color only for now). */
    if (!grad) {
        for (uint32_t i = 0; i < flat_count; i++) {
            emit_fringe_tris(r, flats[i].flat, flats[i].pc, color);
        }
    }

    /* Restore default scissor + stencil ref so the next op (FILL_RECT,
     * DRAW_GLYPHS, etc.) sees clean state. The stencil pipeline tests
     * EQUAL against the cleared stencil buffer (0); leaving ref=1 here
     * silently discards every following fragment outside this path's
     * bounds. When a clip is active the engine's op handlers own the
     * scissor/stencil state, so we leave it alone in that case. */
    if (!has_clip) {
        vt(r)->scissor(r, 0, 0, sw, sh);
        vt(r)->stencil_ref(r, 0);
    }

    return true;
}

/* ------------------------------------------------------------------ */
/*  Main execute loop                                                 */
/* ------------------------------------------------------------------ */

flux_result flux_engine_execute(flux_canvas *canvas, flux_rhi_device *r)
{
    if (!canvas || !r) return FLUX_ERROR_INVALID_ARGUMENT;

    uint32_t sw = 0, sh = 0;
    vt(r)->surface_extent(r, &sw, &sh);

    flux_arena arena;
    flux_arena_init(&arena, 65536);

    const flux_path *active_clip_path = NULL;

    vt(r)->begin_pass(r, canvas->has_clear ? canvas->clear_color : 0);
    vt(r)->scissor(r, 0, 0, sw, sh);
    vt(r)->stencil_ref(r, 0);

    for (size_t i = 0; i < canvas->op_count; i++) {
        const flux_op *op = &canvas->ops[i];

        switch (op->kind) {

        case FLUX_OP_CLIP_RECT:
            vt(r)->flush_solid(r);
            scissor_rect(r, &op->u.clip_rect.rect, sw, sh);
            continue;

        case FLUX_OP_RESET_CLIP:
            vt(r)->flush_solid(r);
            vt(r)->scissor(r, 0, 0, sw, sh);
            vt(r)->stencil_clear(r, 0, 0, sw, sh);
            active_clip_path = NULL;
            vt(r)->stencil_ref(r, 0);
            continue;

        case FLUX_OP_CLIP_PATH: {
            vt(r)->flush_solid(r);
            const flux_path *cp = op->u.clip_path.path;
            flux_rect cr;
            if (flux_path_get_bounds(cp, &cr) == FLUX_OK) {
                scissor_rect(r, &cr, sw, sh);
                stencil_clr(r, &cr, sw, sh);
            }
            const flux_point *cpts = NULL;
            size_t ccn = 0;
            flux_point *cflat = NULL;
            if (flux_path_get_line_loop(cp, &cpts, &ccn) ||
                (flux_path_flatten_line_loop(cp, 0.25f, &arena, &cflat, &ccn) == FLUX_OK)) {
                if (cflat) cpts = cflat;
                if (ccn >= 3) emit_stencil_tris(r, cpts, ccn, 0);
            }
            active_clip_path = cp;
            vt(r)->stencil_ref(r, 1);
            continue;
        }

        case FLUX_OP_FILL_RECT:
            vt(r)->set_blend_mode(r, FLUX_BLEND_SRC_OVER);
            draw_solid_rect(r, &op->u.fill_rect.rect, op->u.fill_rect.color);
            continue;

        case FLUX_OP_FILL_PATH: {
            const flux_path *fp = op->u.fill_path.path;
            const flux_paint_state *p = &op->u.fill_path.paint;
            vt(r)->set_blend_mode(r, p->blend_mode);
            const flux_gradient *gr = p->gradient;
            flux_rect rr;

            if (flux_path_is_axis_aligned_rect(fp, &rr)) {
                if (gr) draw_gradient_rect(r, &rr, gr);
                else    draw_solid_rect(r, &rr, p->color);
                continue;
            }

            fill_stencil_path(r, &arena, fp, p->color, gr,
                              sw, sh, (int)p->fill_rule,
                              active_clip_path != NULL, active_clip_path);
            continue;
        }

        case FLUX_OP_STROKE_PATH: {
            const flux_path *sp = op->u.stroke_path.path;
            const flux_paint_state *p = &op->u.stroke_path.paint;
            vt(r)->set_blend_mode(r, p->blend_mode);
            flux_stroke_style style = {
                .line_cap     = p->line_cap,
                .line_join    = p->line_join,
                .stroke_width = p->stroke_width,
                .miter_limit  = p->miter_limit,
            };
            flux_rect rr;
            uint32_t sn = (uint32_t)flux_path_subpath_count(sp);

            if (!p->dash_array && sn == 1 && p->line_join == FLUX_JOIN_MITER &&
                flux_path_is_axis_aligned_rect(sp, &rr)) {
                stroke_rect_aa(r, &rr, p->stroke_width, p->color);
                continue;
            }

            for (uint32_t si = 0; si < sn; si++) {
                flux_point *flat = NULL;
                size_t pc = 0;
                bool closed = false;
                if (flux_path_flatten_subpath(sp, si, 0.25f, &arena, &flat, &pc, &closed) != FLUX_OK)
                    continue;

                if (p->dash_array && p->dash_count >= 2) {
                    flux_point *dashed = NULL;
                    pc = subdivide_dash(flat, pc, closed, p->dash_array, p->dash_count,
                                        p->dash_phase, &arena, &dashed);
                    flat = dashed;
                    if (pc < 2) continue;
                }

                flux_point *st = NULL;
                size_t sc = 0;
                if (flux_stroke_polyline(flat, pc, closed, &style, &arena, &st, &sc) == FLUX_OK)
                    emit_solid_tris(r, st, sc, p->color);
            }
            continue;
        }

        case FLUX_OP_DRAW_IMAGE: {
            vt(r)->set_blend_mode(r, FLUX_BLEND_SRC_OVER);
            const flux_draw_image_op *io = &op->u.draw_image;
            vt(r)->flush_solid(r);

            uint32_t iw = sw, ih = sh;
            if (io->image) {
                uint32_t w = 0, h = 0;
                if (flux_image_get_size(io->image, &w, &h) == FLUX_OK) {
                    iw = w; ih = h;
                }
            }

            flux_r_buffer *ib = NULL;
            uint32_t ifirst = 0;
            flux_image_vertex *iv = vt(r)->alloc_image(r, 6, &ib, &ifirst);
            if (!iv) continue;

            flux_rect src = io->src, dst = io->dst;
            float u0 = src.x / (float)iw, v0 = src.y / (float)ih;
            float u1 = (src.x + src.w) / (float)iw, v1 = (src.y + src.h) / (float)ih;
            iv[0] = (flux_image_vertex){ { dst.x,         dst.y         }, { u0, v0 } };
            iv[1] = (flux_image_vertex){ { dst.x + dst.w, dst.y         }, { u1, v0 } };
            iv[2] = (flux_image_vertex){ { dst.x + dst.w, dst.y + dst.h }, { u1, v1 } };
            iv[3] = (flux_image_vertex){ { dst.x,         dst.y         }, { u0, v0 } };
            iv[4] = (flux_image_vertex){ { dst.x + dst.w, dst.y + dst.h }, { u1, v1 } };
            iv[5] = (flux_image_vertex){ { dst.x,         dst.y + dst.h }, { u0, v1 } };

            flux_r_texture *tex = io->image ? io->image->rtex : vt(r)->surface_texture(r);
            vt(r)->draw_image(r, ib, ifirst, 6, tex, 0xFFFFFFFFu);
            continue;
        }

        case FLUX_OP_APPLY_BLUR:
            vt(r)->flush_solid(r);
            vt(r)->blur(r, op->u.apply_blur.sigma);
            continue;

        case FLUX_OP_DRAW_GLYPHS: {
            const flux_draw_glyphs_op *go = &op->u.draw_glyphs;
            const flux_glyph_run *gr = go->run;
            const flux_paint_state *gp = &go->paint;
            flux_context *ctx = canvas->owner ? canvas->owner->ctx : NULL;
            flux_glyph_atlas *a = ctx ? ctx->atlas : NULL;
            if (!a || !gr || gr->count == 0) continue;

            flux_surface *s = canvas->owner;
            if (!s->glyph_atlas_tex || s->glyph_atlas_revision != a->revision) {
                if (s->glyph_atlas_tex)
                    vt(r)->texture_free(r, s->glyph_atlas_tex);
                s->glyph_atlas_tex = vt(r)->texture_alloc(r, a->width, a->height,
                                                           FLUX_FMT_A8_UNORM,
                                                           a->pixels, a->width);
                s->glyph_atlas_revision = a->revision;
            }
            if (!s->glyph_atlas_tex) continue;

            vt(r)->flush_solid(r);
            vt(r)->set_blend_mode(r, gp->blend_mode);

            float base_x = go->x;
            float base_y = go->y;
            float atlas_w = (float)a->width;
            float atlas_h = (float)a->height;

            for (size_t gi = 0; gi < gr->count; gi++) {
                const flux_glyph *g = &gr->glyphs[gi];
                flux_glyph_slot *slot = flux_glyph_atlas_find(a, g->glyph_id);
                if (!slot) continue;

                float x = base_x + g->x + slot->bearing_x;
                float y = base_y + g->y - slot->bearing_y;
                float w = slot->w;
                float h = slot->h;
                float u0 = slot->atlas_x / atlas_w;
                float v0 = slot->atlas_y / atlas_h;
                float u1 = (slot->atlas_x + slot->w) / atlas_w;
                float v1 = (slot->atlas_y + slot->h) / atlas_h;

                flux_r_buffer *gb = NULL;
                uint32_t gfirst = 0;
                flux_image_vertex *gv = vt(r)->alloc_image(r, 6, &gb, &gfirst);
                if (!gv) continue;

                gv[0] = (flux_image_vertex){ { x,     y     }, { u0, v0 } };
                gv[1] = (flux_image_vertex){ { x + w, y     }, { u1, v0 } };
                gv[2] = (flux_image_vertex){ { x + w, y + h }, { u1, v1 } };
                gv[3] = (flux_image_vertex){ { x,     y     }, { u0, v0 } };
                gv[4] = (flux_image_vertex){ { x + w, y + h }, { u1, v1 } };
                gv[5] = (flux_image_vertex){ { x,     y + h }, { u0, v1 } };

                vt(r)->draw_text(r, gb, gfirst, 6, s->glyph_atlas_tex, gp->color);
            }
            continue;
        }
        }
    }

    vt(r)->flush_solid(r);
    vt(r)->end_pass(r);
    vt(r)->submit(r);

    flux_arena_destroy(&arena);
    return FLUX_OK;
}
