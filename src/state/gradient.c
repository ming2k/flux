/*
 * Gradient: opaque, refcounted, immutable after creation.
 *
 * Stops are validated at create time: count in [2, FLUX_MAX_GRADIENT_STOPS],
 * stop offsets monotonically non-decreasing in [0,1], colors stored
 * pre-converted to normalised RGBA float for the engine.
 */
#include "internal.h"

static void color_to_floats(flux_color c, float out[4])
{
    out[0] = (float)((c >> 16) & 0xFFu) * (1.0f / 255.0f);
    out[1] = (float)((c >>  8) & 0xFFu) * (1.0f / 255.0f);
    out[2] = (float)((c      ) & 0xFFu) * (1.0f / 255.0f);
    out[3] = (float)((c >> 24) & 0xFFu) * (1.0f / 255.0f);
}

static flux_result validate_stops(const flux_color *colors, const float *stops,
                                  uint32_t count)
{
    if (!colors || !stops) return FLUX_ERROR_INVALID_ARGUMENT;
    if (count < 2 || count > FLUX_MAX_GRADIENT_STOPS)
        return FLUX_ERROR_OUT_OF_RANGE;
    float prev = -1.0f;
    for (uint32_t i = 0; i < count; ++i) {
        if (stops[i] < 0.0f || stops[i] > 1.0f) return FLUX_ERROR_OUT_OF_RANGE;
        if (stops[i] < prev) return FLUX_ERROR_INVALID_ARGUMENT;
        prev = stops[i];
    }
    return FLUX_OK;
}

flux_gradient *flux_gradient_retain(flux_gradient *g)
{
    if (g) flux_ref_retain(&g->ref_count);
    return g;
}

void flux_gradient_release(flux_gradient *g)
{
    if (!g) return;
    if (flux_ref_release(&g->ref_count) == 0) {
        flux_context *ctx = g->ctx;
        flux_free(ctx, g);
        flux_context_release(ctx);
    }
}

flux_result flux_gradient_create_linear(flux_context *ctx,
                                        const flux_linear_gradient_desc *desc,
                                        flux_gradient **out)
{
    if (!ctx || !desc || !out) return FLUX_ERROR_INVALID_ARGUMENT;
    if (desc->size < sizeof(uint32_t)) return FLUX_ERROR_INVALID_ARGUMENT;
    flux_result r = validate_stops(desc->colors, desc->stops, desc->stop_count);
    if (r != FLUX_OK) return r;

    flux_gradient *g = flux_calloc(ctx, 1, sizeof(*g));
    if (!g) return FLUX_ERROR_OUT_OF_MEMORY;

    flux_ref_init(&g->ref_count);
    g->ctx        = flux_context_retain(ctx);
    g->mode       = 0;
    g->extend     = desc->extend;
    g->start[0]   = desc->start.x;
    g->start[1]   = desc->start.y;
    g->end[0]     = desc->end.x;
    g->end[1]     = desc->end.y;
    g->stop_count = desc->stop_count;
    for (uint32_t i = 0; i < desc->stop_count; ++i) {
        color_to_floats(desc->colors[i], g->colors[i]);
        g->stops[i] = desc->stops[i];
    }

    *out = g;
    return FLUX_OK;
}

flux_result flux_gradient_create_radial(flux_context *ctx,
                                        const flux_radial_gradient_desc *desc,
                                        flux_gradient **out)
{
    if (!ctx || !desc || !out) return FLUX_ERROR_INVALID_ARGUMENT;
    if (desc->size < sizeof(uint32_t)) return FLUX_ERROR_INVALID_ARGUMENT;
    if (desc->radius <= 0.0f) return FLUX_ERROR_INVALID_ARGUMENT;
    flux_result r = validate_stops(desc->colors, desc->stops, desc->stop_count);
    if (r != FLUX_OK) return r;

    flux_gradient *g = flux_calloc(ctx, 1, sizeof(*g));
    if (!g) return FLUX_ERROR_OUT_OF_MEMORY;

    flux_ref_init(&g->ref_count);
    g->ctx        = flux_context_retain(ctx);
    g->mode       = 1;
    g->extend     = desc->extend;
    g->start[0]   = desc->center.x;
    g->start[1]   = desc->center.y;
    g->end[0]     = desc->radius;
    g->end[1]     = 0.0f;
    g->stop_count = desc->stop_count;
    for (uint32_t i = 0; i < desc->stop_count; ++i) {
        color_to_floats(desc->colors[i], g->colors[i]);
        g->stops[i] = desc->stops[i];
    }

    *out = g;
    return FLUX_OK;
}
