/*
 * Surface lifecycle: create, acquire, present, destroy.
 *
 * Surfaces own a renderer (backend) and a canvas (recording state).
 * The execution engine bridges them at present time.
 */
#include "internal.h"

#include <stdlib.h>
#include <string.h>

/* ---- offscreen surface (software renderer) ---- */

fx_surface *fx_surface_create_offscreen(fx_context *ctx,
                                        int32_t width, int32_t height,
                                        fx_pixel_format format,
                                        fx_color_space cs)
{
    if (!ctx || width <= 0 || height <= 0) return nullptr;
    (void)format; (void)cs;

    fx_surface *s = calloc(1, sizeof(*s));
    if (!s) return nullptr;

    s->ctx = ctx;
    s->is_offscreen = true;

    /* Create the software renderer for this surface. */
    s->renderer = fx_renderer_create_software((uint32_t)width, (uint32_t)height);
    if (!s->renderer) {
        free(s);
        return nullptr;
    }

    s->canvas.owner = s;
    fx_matrix_identity(&s->canvas.current_matrix);
    s->canvas.dpr = 1.0f;

    return s;
}

/* ---- vulkan surface (stub — delegates to legacy path) ---- */

#include "flux/flux_vulkan.h"

fx_surface *fx_surface_create_vulkan(fx_context *ctx,
                                     VkSurfaceKHR vk_surface,
                                     int32_t width, int32_t height,
                                     fx_color_space cs)
{
    /* Vulkan surface creation is not yet ported to the vtable.
     * For now, return a stub that uses the software renderer. */
    (void)vk_surface; (void)cs;
    return fx_surface_create_offscreen(ctx, width, height, FX_FMT_BGRA8_UNORM, FX_CS_SRGB);
}

/* ---- destroy ---- */

void fx_surface_destroy(fx_surface *s)
{
    if (!s) return;

    if (s->renderer) {
        const struct fx_renderer_vtbl *vt = fx_renderer_vt(s->renderer);
        vt->destroy(s->renderer);
    }

    fx_canvas_dispose(&s->canvas);
    free(s);
}

/* ---- resize ---- */

void fx_surface_resize(fx_surface *s, int32_t w, int32_t h)
{
    if (!s) return;
    s->needs_recreate = true;
    (void)w; (void)h;
}

/* ---- DPR ---- */

void fx_surface_set_dpr(fx_surface *s, float dpr)
{
    if (!s || dpr <= 0.0f) return;
    s->canvas.dpr = dpr;
}

float fx_surface_get_dpr(const fx_surface *s)
{
    return s ? s->canvas.dpr : 1.0f;
}

/* ---- render-to-texture ---- */

fx_image *fx_image_create_from_surface(fx_surface *s)
{
    if (!s || !s->renderer) return nullptr;

    const struct fx_renderer_vtbl *vt = fx_renderer_vt(s->renderer);
    fx_r_texture *rtex = vt->surface_texture(s->renderer);
    if (!rtex) return nullptr;

    fx_image *img = calloc(1, sizeof(*img));
    if (!img) return nullptr;

    img->ctx = s->ctx;
    img->rtex = rtex;
    /* Set desc from surface dimensions */
    uint32_t sw = 0, sh = 0;
    vt->surface_extent(s->renderer, &sw, &sh);
    img->desc.width = sw;
    img->desc.height = sh;
    img->desc.format = FX_FMT_BGRA8_UNORM;

    return img;
}

/* ---- acquire / present ---- */

fx_canvas *fx_surface_acquire(fx_surface *s)
{
    if (!s) return nullptr;

    if (s->renderer) {
        const struct fx_renderer_vtbl *vt = fx_renderer_vt(s->renderer);
        vt->begin_frame(s->renderer);
    }

    /* Reset recording state for the new frame. */
    fx_canvas_reset(&s->canvas);
    s->canvas.dpr = s->canvas.dpr > 0.0f ? s->canvas.dpr : 1.0f;

    return &s->canvas;
}

void fx_surface_present(fx_surface *s)
{
    if (!s || !s->renderer) return;

    /* Execute all recorded ops through the renderer vtable. */
    fx_engine_execute(&s->canvas, s->renderer);
}

/* ---- read pixels ---- */

bool fx_surface_read_pixels(fx_surface *s, void *data, size_t stride)
{
    if (!s || !s->renderer || !data) return false;
    const struct fx_renderer_vtbl *vt = fx_renderer_vt(s->renderer);
    return vt->read_pixels(s->renderer, data, stride);
}
