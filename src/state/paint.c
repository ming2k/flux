/*
 * Paint: opaque, refcounted styling state.
 *
 * The public handle wraps a `flux_paint_state` POD which is what the
 * recording layer snapshots into ops. Geometry/engine code reads only
 * the snapshot; they never see the public handle.
 */
#include "internal.h"

flux_result flux_paint_create(flux_context *ctx, flux_paint **out_paint)
{
    if (!ctx || !out_paint) return FLUX_ERROR_INVALID_ARGUMENT;
    flux_paint *p = flux_calloc(ctx, 1, sizeof(*p));
    if (!p) return FLUX_ERROR_OUT_OF_MEMORY;

    flux_ref_init(&p->ref_count);
    p->ctx = flux_context_retain(ctx);
    p->state = (flux_paint_state){
        .color        = 0xFF000000u,         /* Opaque black. */
        .blend_mode   = FLUX_BLEND_SRC_OVER,
        .fill_rule    = FLUX_FILL_EVEN_ODD,
        .line_cap     = FLUX_CAP_BUTT,
        .line_join    = FLUX_JOIN_MITER,
        .stroke_width = 1.0f,
        .miter_limit  = 4.0f,
    };
    *out_paint = p;
    return FLUX_OK;
}

flux_paint *flux_paint_retain(flux_paint *paint)
{
    if (paint) flux_ref_retain(&paint->ref_count);
    return paint;
}

void flux_paint_release(flux_paint *paint)
{
    if (!paint) return;
    if (flux_ref_release(&paint->ref_count) == 0) {
        flux_context *ctx = paint->ctx;
        if (paint->state.gradient) flux_gradient_release(paint->state.gradient);
        flux_free(ctx, paint->state.dash_array);
        flux_free(ctx, paint);
        flux_context_release(ctx);
    }
}

/* ------------------------------------------------------------------ */
/*  Snapshot helpers (used by canvas recording)                        */
/* ------------------------------------------------------------------ */

flux_result flux_paint_snapshot(const flux_paint *src, flux_paint_state *out)
{
    if (!src || !out) return FLUX_ERROR_INVALID_ARGUMENT;

    *out = src->state;
    out->dash_array = NULL;
    out->dash_count = 0;

    if (src->state.gradient) flux_gradient_retain(src->state.gradient);

    if (src->state.dash_array && src->state.dash_count > 0) {
        size_t bytes = src->state.dash_count * sizeof(float);
        out->dash_array = flux_alloc(src->ctx, bytes);
        if (!out->dash_array) {
            if (out->gradient) flux_gradient_release(out->gradient);
            out->gradient = NULL;
            return FLUX_ERROR_OUT_OF_MEMORY;
        }
        memcpy(out->dash_array, src->state.dash_array, bytes);
        out->dash_count = src->state.dash_count;
        out->dash_phase = src->state.dash_phase;
    }
    return FLUX_OK;
}

void flux_paint_state_dispose(flux_paint_state *st, flux_context *ctx)
{
    if (!st) return;
    if (st->gradient) {
        flux_gradient_release(st->gradient);
        st->gradient = NULL;
    }
    if (st->dash_array) {
        flux_free(ctx, st->dash_array);
        st->dash_array = NULL;
        st->dash_count = 0;
    }
}

/* ------------------------------------------------------------------ */
/*  Setters                                                           */
/* ------------------------------------------------------------------ */

#define PAINT_SET(field, type, expr) \
    if (!paint) return FLUX_ERROR_INVALID_ARGUMENT;     \
    paint->state.field = (expr);                        \
    return FLUX_OK

flux_result flux_paint_set_color       (flux_paint *paint, flux_color v)      { PAINT_SET(color, flux_color, v); }
flux_result flux_paint_set_stroke_width(flux_paint *paint, float v)           { if (!paint || v < 0.0f) return FLUX_ERROR_INVALID_ARGUMENT; paint->state.stroke_width = v; return FLUX_OK; }
flux_result flux_paint_set_miter_limit (flux_paint *paint, float v)           { if (!paint || v < 1.0f) return FLUX_ERROR_INVALID_ARGUMENT; paint->state.miter_limit = v;  return FLUX_OK; }
flux_result flux_paint_set_line_cap    (flux_paint *paint, flux_line_cap v)   { PAINT_SET(line_cap, flux_line_cap, v); }
flux_result flux_paint_set_line_join   (flux_paint *paint, flux_line_join v)  { PAINT_SET(line_join, flux_line_join, v); }
flux_result flux_paint_set_fill_rule   (flux_paint *paint, flux_fill_rule v)  { PAINT_SET(fill_rule, flux_fill_rule, v); }
flux_result flux_paint_set_blend_mode  (flux_paint *paint, flux_blend_mode v) { PAINT_SET(blend_mode, flux_blend_mode, v); }

#undef PAINT_SET

flux_result flux_paint_set_gradient(flux_paint *paint, flux_gradient *grad)
{
    if (!paint) return FLUX_ERROR_INVALID_ARGUMENT;
    if (paint->state.gradient == grad) return FLUX_OK;
    if (grad) flux_gradient_retain(grad);
    if (paint->state.gradient) flux_gradient_release(paint->state.gradient);
    paint->state.gradient = grad;
    return FLUX_OK;
}

flux_result flux_paint_set_dash(flux_paint *paint, const float *dashes,
                                uint32_t count, float phase)
{
    if (!paint) return FLUX_ERROR_INVALID_ARGUMENT;

    flux_free(paint->ctx, paint->state.dash_array);
    paint->state.dash_array = NULL;
    paint->state.dash_count = 0;
    paint->state.dash_phase = 0.0f;

    if (count > 0 && dashes) {
        size_t bytes = count * sizeof(float);
        paint->state.dash_array = flux_alloc(paint->ctx, bytes);
        if (!paint->state.dash_array) return FLUX_ERROR_OUT_OF_MEMORY;
        memcpy(paint->state.dash_array, dashes, bytes);
        paint->state.dash_count = count;
        paint->state.dash_phase = phase;
    }
    return FLUX_OK;
}

/* ------------------------------------------------------------------ */
/*  Getters                                                           */
/* ------------------------------------------------------------------ */

flux_color      flux_paint_get_color       (const flux_paint *p) { return p ? p->state.color        : 0u;                   }
float           flux_paint_get_stroke_width(const flux_paint *p) { return p ? p->state.stroke_width : 0.0f;                 }
float           flux_paint_get_miter_limit (const flux_paint *p) { return p ? p->state.miter_limit  : 0.0f;                 }
flux_line_cap   flux_paint_get_line_cap    (const flux_paint *p) { return p ? p->state.line_cap     : FLUX_CAP_BUTT;        }
flux_line_join  flux_paint_get_line_join   (const flux_paint *p) { return p ? p->state.line_join    : FLUX_JOIN_MITER;      }
flux_fill_rule  flux_paint_get_fill_rule   (const flux_paint *p) { return p ? p->state.fill_rule    : FLUX_FILL_EVEN_ODD;   }
flux_blend_mode flux_paint_get_blend_mode  (const flux_paint *p) { return p ? p->state.blend_mode   : FLUX_BLEND_SRC_OVER;  }
flux_gradient  *flux_paint_get_gradient    (const flux_paint *p) { return p ? p->state.gradient     : NULL;                 }
uint32_t        flux_paint_get_dash_count  (const flux_paint *p) { return p ? p->state.dash_count   : 0u;                   }
float           flux_paint_get_dash_phase  (const flux_paint *p) { return p ? p->state.dash_phase   : 0.0f;                 }

uint32_t flux_paint_copy_dash(const flux_paint *p, float *out, uint32_t cap)
{
    if (!p || !p->state.dash_array) return 0;
    if (out && cap > 0) {
        uint32_t n = p->state.dash_count < cap ? p->state.dash_count : cap;
        memcpy(out, p->state.dash_array, n * sizeof(float));
    }
    return p->state.dash_count;
}
