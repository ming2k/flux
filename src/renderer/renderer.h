/*
 * Backend-agnostic renderer vtable.
 *
 * Every drawing operation the execution engine needs is exposed through
 * this interface. A renderer implementation owns the backend-specific
 * resources (Vulkan pipelines/buffers, software pixel arrays, etc.)
 * and translates these calls into backend commands.
 *
 * Adding a new backend means implementing this vtable — no changes
 * required in the geometry, state, or API layers above.
 */
#ifndef FX_RENDERER_H
#define FX_RENDERER_H

#include "flux/flux.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Opaque backend resource handles used by the execution engine.
 * The renderer implementation interprets these as it sees fit
 * (e.g. a Vulkan VkBuffer offset, or a software pointer). */
typedef struct fx_r_buffer   fx_r_buffer;
typedef struct fx_r_texture  fx_r_texture;

/* Sets of vertices that can be allocated from the renderer.
 * The execution engine builds these on the CPU; the renderer
 * transfers them to the backend and issues draw calls. */
typedef struct { float pos[2];           } fx_solid_vertex;
typedef struct { float pos[2]; float uv[2]; } fx_image_vertex;

typedef struct fx_renderer fx_renderer;
typedef struct fx_renderer_vtbl fx_renderer_vtbl;

struct fx_renderer_vtbl {
    /* ---- lifecycle ---- */

    void  (*destroy)(fx_renderer *r);

    /* ---- surface ---- */

    void  (*surface_extent)(fx_renderer *r, uint32_t *w, uint32_t *h);

    /* ---- frame ---- */

    void  (*begin_frame)(fx_renderer *r);
    void  (*begin_pass)(fx_renderer *r, fx_color clear);
    void  (*end_pass)(fx_renderer *r);
    void  (*submit)(fx_renderer *r);

    /* Read rendered pixels (offscreen or software backends).
     * stride is bytes per row. Returns false if unsupported. */
    bool  (*read_pixels)(fx_renderer *r, void *data, size_t stride);

    /* ---- vertex buffer allocation ---- */

    /* Allocate count vertices of the given type.
     * Returns a writable pointer, and sets *buf / *first_vertex
     * for later submission. */
    fx_solid_vertex * (*alloc_solid)(fx_renderer *r, size_t count,
                                     fx_r_buffer **buf, uint32_t *first);
    fx_image_vertex * (*alloc_image)(fx_renderer *r, size_t count,
                                     fx_r_buffer **buf, uint32_t *first);

    /* ---- draw commands ---- */

    /* Solid colour draws may be batched internally by the renderer.
     * The execution engine calls flush_solid() when batching must end
     * (pipeline change, clip change, etc.). */
    void  (*draw_solid)(fx_renderer *r, fx_r_buffer *buf,
                        uint32_t first, uint32_t count, fx_color color);
    void  (*flush_solid)(fx_renderer *r);

    void  (*draw_image)(fx_renderer *r, fx_r_buffer *buf,
                        uint32_t first, uint32_t count,
                        fx_r_texture *tex);

    void  (*draw_text)(fx_renderer *r, fx_r_buffer *buf,
                       uint32_t first, uint32_t count, fx_color color);

    void  (*draw_gradient)(fx_renderer *r, fx_r_buffer *buf,
                           uint32_t first, uint32_t count,
                           const fx_gradient *grad);

    /* ---- clipping and stencil ---- */

    void  (*scissor)(fx_renderer *r, int32_t x, int32_t y,
                     uint32_t w, uint32_t h);

    void  (*stencil_clear)(fx_renderer *r, int32_t x, int32_t y,
                           uint32_t w, uint32_t h);

    /* Draw triangle mesh into the stencil buffer.
     * fill_rule: 0 = even-odd (inc-wrap), 1 = non-zero (front inc, back dec). */
    void  (*stencil_fill)(fx_renderer *r, fx_r_buffer *buf,
                          uint32_t first, uint32_t count,
                          int fill_rule);

    void  (*stencil_ref)(fx_renderer *r, uint32_t ref);

    /* Cover passes: draw over path bounds where stencil == ref.
     * Used by path stencil fill (2nd pass) after stencil_fill. */
    void  (*cover_solid)(fx_renderer *r, fx_r_buffer *buf,
                         uint32_t first, uint32_t count, fx_color color);

    void  (*cover_gradient)(fx_renderer *r, fx_r_buffer *buf,
                            uint32_t first, uint32_t count,
                            const fx_gradient *grad);

    /* ---- blur ---- */

    /* Apply a separable Gaussian blur to the current render target.
     * sigma: standard deviation; <= 0 means no-op.
     * Called between begin_pass / end_pass (requires active render pass). */
    void  (*blur)(fx_renderer *r, float sigma);

    /* ---- texture resources ---- */

    /* Allocate a texture that draw_image can reference.
     * data may be NULL for uninitialised textures. */
    fx_r_texture * (*texture_alloc)(fx_renderer *r,
                                    uint32_t w, uint32_t h,
                                    fx_pixel_format fmt,
                                    const void *data, size_t stride);
    void  (*texture_free)(fx_renderer *r, fx_r_texture *tex);

    /* Update a sub-region of an existing texture. */
    void  (*texture_update)(fx_renderer *r, fx_r_texture *tex,
                            const void *data,
                            uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h);

    /* ---- surface-as-texture (render-to-texture) ---- */

    /* Wrap the current render target as a texture.
     * Used by fx_image_create_from_surface. Returns NULL if not supported. */
    fx_r_texture * (*surface_texture)(fx_renderer *r);
};

/* Convenience: access the vtable through the handle. */
static inline const struct fx_renderer_vtbl *
fx_renderer_vt(const fx_renderer *r)
{
    return *(const struct fx_renderer_vtbl **)r;
}

#endif /* FX_RENDERER_H */
