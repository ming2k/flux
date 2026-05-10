/*
 * Vulkan renderer backend — vtable implementation stub.
 *
 * Full Vulkan integration is provided by the existing surface_vk.c
 * path. When this stub is completed, it will wrap the Vulkan pipeline
 * behind the renderer vtable, enabling the same execution engine to
 * drive both software and hardware rendering.
 */
#include "vk_internal.h"
#include <stdlib.h>

/* Minimal self-referencing cast */
#define VKR(r) ((vk_renderer *)(r))

typedef struct {
    const fx_renderer_vtbl *vtbl;
    struct fx_context *ctx;
    bool owns_ctx;
    uint32_t w, h;
    bool offscreen;
} vk_renderer;

/* ---- vtable stubs ---- */

static void vk_destroy(fx_renderer *r)          { free(VKR(r)); }
static void vk_surface_extent(fx_renderer *r, uint32_t *w, uint32_t *h) {
    *w = VKR(r)->w; *h = VKR(r)->h;
}
static void vk_begin_frame(fx_renderer *r)       { (void)r; }
static void vk_begin_pass(fx_renderer *r, fx_color c)  { (void)r; (void)c; }
static void vk_end_pass(fx_renderer *r)          { (void)r; }
static void vk_submit(fx_renderer *r)            { (void)r; }
static bool vk_read_pixels(fx_renderer *r, void *data, size_t stride) {
    (void)r; (void)data; (void)stride; return false;
}
static fx_solid_vertex *vk_alloc_solid(fx_renderer *r, size_t n,
                                       fx_r_buffer **b, uint32_t *first) {
    (void)r; *b = nullptr; *first = 0;
    return calloc(n, sizeof(fx_solid_vertex));
}
static fx_image_vertex *vk_alloc_image(fx_renderer *r, size_t n,
                                       fx_r_buffer **b, uint32_t *first) {
    (void)r; *b = nullptr; *first = 0;
    return calloc(n, sizeof(fx_image_vertex));
}
static void vk_draw_solid(fx_renderer *r, fx_r_buffer *buf,
                          uint32_t first, uint32_t n, fx_color c)
{ (void)r; (void)buf; (void)first; (void)n; (void)c; }
static void vk_flush_solid(fx_renderer *r)       { (void)r; }
static void vk_draw_image(fx_renderer *r, fx_r_buffer *buf,
                          uint32_t first, uint32_t n, fx_r_texture *tex)
{ (void)r; (void)buf; (void)first; (void)n; (void)tex; }
static void vk_draw_text(fx_renderer *r, fx_r_buffer *buf,
                         uint32_t first, uint32_t n, fx_color c)
{ (void)r; (void)buf; (void)first; (void)n; (void)c; }
static void vk_draw_gradient(fx_renderer *r, fx_r_buffer *buf,
                             uint32_t first, uint32_t n, const fx_gradient *g)
{ (void)r; (void)buf; (void)first; (void)n; (void)g; }
static void vk_scissor(fx_renderer *r, int32_t x, int32_t y, uint32_t w, uint32_t h)
{ (void)r; (void)x; (void)y; (void)w; (void)h; }
static void vk_stencil_clear(fx_renderer *r, int32_t x, int32_t y, uint32_t w, uint32_t h)
{ (void)r; (void)x; (void)y; (void)w; (void)h; }
static void vk_stencil_fill(fx_renderer *r, fx_r_buffer *buf, uint32_t first, uint32_t n, int fill_rule)
{ (void)r; (void)buf; (void)first; (void)n; (void)fill_rule; }
static void vk_stencil_ref(fx_renderer *r, uint32_t ref)  { (void)r; (void)ref; }
static void vk_cover_solid(fx_renderer *r, fx_r_buffer *buf,
                           uint32_t first, uint32_t n, fx_color c)
{ (void)r; (void)buf; (void)first; (void)n; (void)c; }
static void vk_cover_gradient(fx_renderer *r, fx_r_buffer *buf,
                              uint32_t first, uint32_t n, const fx_gradient *g)
{ (void)r; (void)buf; (void)first; (void)n; (void)g; }
static fx_r_texture *vk_texture_alloc(fx_renderer *r, uint32_t w, uint32_t h,
                                       fx_pixel_format fmt, const void *data, size_t stride)
{ (void)r; (void)w; (void)h; (void)fmt; (void)data; (void)stride; return nullptr; }
static void vk_texture_free(fx_renderer *r, fx_r_texture *tex) { (void)r; (void)tex; }
static void vk_texture_update(fx_renderer *r, fx_r_texture *tex,
                              const void *data, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{ (void)r; (void)tex; (void)data; (void)x; (void)y; (void)w; (void)h; }

static void vk_blur(fx_renderer *r, float sigma) { (void)r; (void)sigma; }

static fx_r_texture *vk_surface_texture(fx_renderer *r) { (void)r; return nullptr; }

static const fx_renderer_vtbl vk_vtbl = {
    .destroy        = vk_destroy,
    .surface_extent = vk_surface_extent,
    .begin_frame    = vk_begin_frame,
    .begin_pass     = vk_begin_pass,
    .end_pass       = vk_end_pass,
    .submit         = vk_submit,
    .read_pixels    = vk_read_pixels,
    .alloc_solid    = vk_alloc_solid,
    .alloc_image    = vk_alloc_image,
    .draw_solid     = vk_draw_solid,
    .flush_solid    = vk_flush_solid,
    .draw_image     = vk_draw_image,
    .draw_text      = vk_draw_text,
    .draw_gradient  = vk_draw_gradient,
    .scissor        = vk_scissor,
    .stencil_clear  = vk_stencil_clear,
    .stencil_fill   = vk_stencil_fill,
    .stencil_ref    = vk_stencil_ref,
        .cover_solid    = vk_cover_solid,
        .cover_gradient = vk_cover_gradient,
        .blur           = vk_blur,
        .texture_alloc  = vk_texture_alloc,
        .texture_free   = vk_texture_free,
        .texture_update = vk_texture_update,
        .surface_texture = vk_surface_texture,
};

fx_renderer *fx_renderer_create_vulkan(fx_context *ctx,
                                       void *vk_surface, int32_t w, int32_t h)
{
    vk_renderer *vk = calloc(1, sizeof(*vk));
    if (!vk) return nullptr;
    vk->ctx = ctx;
    vk->vtbl = &vk_vtbl;
    vk->w = (uint32_t)w;
    vk->h = (uint32_t)h;
    (void)vk_surface;
    return (fx_renderer *)vk;
}
