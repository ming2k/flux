/*
 * Surface lifecycle: own an RHI device + canvas; bridge them at present.
 *
 * The flux_surface is the unit of "where pixels go." It binds together:
 *   - a context (for allocator, logger, thread affinity);
 *   - an RHI (the backend that owns swapchains, pipelines, buffers);
 *   - a canvas (the recording state for one in-flight frame).
 */
#include "internal.h"
#include "rhi/rhi.h"

#ifndef FLUX_NO_VULKAN
#include "flux/flux_vulkan.h"
#endif

/* ------------------------------------------------------------------ */
/*  Refcounting                                                       */
/* ------------------------------------------------------------------ */

flux_surface *flux_surface_retain(flux_surface *s)
{
    if (s) flux_ref_retain(&s->ref_count);
    return s;
}

void flux_surface_release(flux_surface *s)
{
    if (!s) return;
    if (flux_ref_release(&s->ref_count) == 0) {
        if (s->rhi) {
            if (s->glyph_atlas_tex)
                flux_rhi_vt(s->rhi)->texture_free(s->rhi, s->glyph_atlas_tex);
            flux_rhi_vt(s->rhi)->destroy(s->rhi);
        }
        flux_canvas_dispose(&s->canvas);
        flux_context *ctx = s->ctx;
        flux_free(ctx, s);
        flux_context_release(ctx);
    }
}

/* ------------------------------------------------------------------ */
/*  Construction                                                      */
/* ------------------------------------------------------------------ */

static flux_result init_surface(flux_surface *s, flux_context *ctx,
                                flux_rhi_device *rhi, bool offscreen,
                                int32_t w, int32_t h, flux_pixel_format fmt)
{
    flux_ref_init(&s->ref_count);
    s->ctx          = flux_context_retain(ctx);
    s->rhi          = rhi;
    s->is_offscreen         = offscreen;
    s->width                = w;
    s->height               = h;
    s->format               = fmt;
    s->glyph_atlas_tex      = NULL;
    s->glyph_atlas_revision = 0;

    flux_canvas_init(&s->canvas, s);
    return FLUX_OK;
}

flux_result flux_surface_create_offscreen(flux_context *ctx,
                                          int32_t width, int32_t height,
                                          flux_pixel_format format,
                                          flux_color_space cs,
                                          flux_surface **out_surface)
{
    if (!ctx || width <= 0 || height <= 0 || !out_surface)
        return FLUX_ERROR_INVALID_ARGUMENT;
    (void)cs;

    flux_rhi_device *rhi = flux_rhi_create_software((uint32_t)width, (uint32_t)height);
    if (!rhi) return FLUX_ERROR_BACKEND_FAILURE;

    flux_surface *s = flux_calloc(ctx, 1, sizeof(*s));
    if (!s) {
        flux_rhi_vt(rhi)->destroy(rhi);
        return FLUX_ERROR_OUT_OF_MEMORY;
    }
    init_surface(s, ctx, rhi, true, width, height, format);
    *out_surface = s;
    return FLUX_OK;
}

#ifndef FLUX_NO_VULKAN
flux_result flux_surface_create_vulkan(flux_context *ctx,
                                       const flux_vulkan_device *device,
                                       VkSurfaceKHR vk_surface,
                                       int32_t width, int32_t height,
                                       flux_color_space cs,
                                       flux_surface **out_surface)
{
    if (!ctx || !device || !vk_surface || width <= 0 || height <= 0 || !out_surface)
        return FLUX_ERROR_INVALID_ARGUMENT;
    (void)cs;

    flux_rhi_device *rhi = flux_rhi_create_vulkan(device, vk_surface, width, height);
    if (!rhi) return FLUX_ERROR_BACKEND_FAILURE;

    flux_surface *s = flux_calloc(ctx, 1, sizeof(*s));
    if (!s) {
        flux_rhi_vt(rhi)->destroy(rhi);
        return FLUX_ERROR_OUT_OF_MEMORY;
    }
    init_surface(s, ctx, rhi, false, width, height, FLUX_FMT_BGRA8_UNORM);
    *out_surface = s;
    return FLUX_OK;
}
#endif

/* ------------------------------------------------------------------ */
/*  Resize / inspection                                               */
/* ------------------------------------------------------------------ */

flux_result flux_surface_resize(flux_surface *s, int32_t w, int32_t h)
{
    if (!s || w <= 0 || h <= 0) return FLUX_ERROR_INVALID_ARGUMENT;
    if (s->rhi && !flux_rhi_vt(s->rhi)->resize(s->rhi, (uint32_t)w, (uint32_t)h))
        return FLUX_ERROR_BACKEND_FAILURE;
    s->width  = w;
    s->height = h;
    return FLUX_OK;
}

flux_result flux_surface_get_size(const flux_surface *s, int32_t *out_w, int32_t *out_h)
{
    if (!s) return FLUX_ERROR_INVALID_ARGUMENT;
    if (out_w) *out_w = s->width;
    if (out_h) *out_h = s->height;
    return FLUX_OK;
}

flux_pixel_format flux_surface_get_format(const flux_surface *s)
{
    return s ? s->format : FLUX_FMT_RGBA8_UNORM;
}

flux_result flux_surface_set_dpr(flux_surface *s, float dpr)
{
    if (!s || dpr <= 0.0f) return FLUX_ERROR_INVALID_ARGUMENT;
    s->canvas.dpr = dpr;
    return FLUX_OK;
}

float flux_surface_get_dpr(const flux_surface *s)
{
    return s ? s->canvas.dpr : 1.0f;
}

/* ------------------------------------------------------------------ */
/*  Read pixels                                                       */
/* ------------------------------------------------------------------ */

flux_result flux_surface_read_pixels(flux_surface *s, void *data, size_t stride)
{
    if (!s || !s->rhi || !data) return FLUX_ERROR_INVALID_ARGUMENT;
    if (!flux_rhi_vt(s->rhi)->read_pixels(s->rhi, data, stride))
        return FLUX_ERROR_BACKEND_FAILURE;
    return FLUX_OK;
}

/* ------------------------------------------------------------------ */
/*  Acquire / present                                                 */
/* ------------------------------------------------------------------ */

flux_canvas *flux_surface_acquire(flux_surface *s)
{
    if (!s) return NULL;
    if (s->rhi) flux_rhi_vt(s->rhi)->begin_frame(s->rhi);
    flux_canvas_reset(&s->canvas);
    if (s->canvas.dpr <= 0.0f) s->canvas.dpr = 1.0f;
    return &s->canvas;
}

flux_result flux_surface_present(flux_surface *s)
{
    if (!s || !s->rhi) return FLUX_ERROR_INVALID_ARGUMENT;
    return flux_engine_execute(&s->canvas, s->rhi);
}
