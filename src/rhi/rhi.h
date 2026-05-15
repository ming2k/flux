/*
 * Backend-agnostic rhi vtable.
 *
 * Every drawing operation the execution engine needs is exposed through
 * this interface. A rhi implementation owns the backend-specific
 * resources (Vulkan pipelines/buffers, software pixel arrays, etc.)
 * and translates these calls into backend commands.
 *
 * Adding a new backend means implementing this vtable — no changes
 * required in the geometry, state, or API layers above.
 */
#ifndef FLUX_RHI_H
#define FLUX_RHI_H

#include "flux/flux.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Opaque backend resource handles used by the execution engine.
 * The rhi implementation interprets these as it sees fit
 * (e.g. a Vulkan VkBuffer offset, or a software pointer). */
typedef struct flux_r_buffer   flux_r_buffer;
typedef struct flux_r_texture  flux_r_texture;

/* Sets of vertices that can be allocated from the rhi.
 * The execution engine builds these on the CPU; the rhi
 * transfers them to the backend and issues draw calls. */
typedef struct { float pos[2];           } flux_solid_vertex;
typedef struct { float pos[2]; float uv[2]; } flux_image_vertex;

typedef struct flux_rhi_device flux_rhi_device;
typedef struct flux_rhi_vtbl flux_rhi_vtbl;

struct flux_rhi_vtbl {
    /* ---- lifecycle ---- */

    void  (*destroy)(flux_rhi_device *r);

    /* ---- surface ---- */

    void  (*surface_extent)(flux_rhi_device *r, uint32_t *w, uint32_t *h);

    /* ---- frame ---- */

    void  (*begin_frame)(flux_rhi_device *r);
    void  (*begin_pass)(flux_rhi_device *r, flux_color clear);
    void  (*end_pass)(flux_rhi_device *r);
    void  (*submit)(flux_rhi_device *r);

    /* Read rendered pixels (offscreen or software backends).
     * stride is bytes per row. Returns false if unsupported. */
    bool  (*read_pixels)(flux_rhi_device *r, void *data, size_t stride);

    /* Resize the render target. Returns false on failure. */
    bool  (*resize)(flux_rhi_device *r, uint32_t w, uint32_t h);

    /* ---- vertex buffer allocation ---- */

    /* Allocate count vertices of the given type.
     * Returns a writable pointer, and sets *buf / *first_vertex
     * for later submission. */
    flux_solid_vertex * (*alloc_solid)(flux_rhi_device *r, size_t count,
                                       flux_r_buffer **buf, uint32_t *first);
    flux_image_vertex * (*alloc_image)(flux_rhi_device *r, size_t count,
                                       flux_r_buffer **buf, uint32_t *first);

    /* ---- draw commands ---- */

    /* Solid colour draws may be batched internally by the rhi.
     * The execution engine calls flush_solid() when batching must end
     * (pipeline change, clip change, etc.). */
    void  (*draw_solid)(flux_rhi_device *r, flux_r_buffer *buf,
                        uint32_t first, uint32_t count, flux_color color);
    void  (*flush_solid)(flux_rhi_device *r);

    void  (*draw_image)(flux_rhi_device *r, flux_r_buffer *buf,
                        uint32_t first, uint32_t count,
                        flux_r_texture *tex);

    void  (*draw_text)(flux_rhi_device *r, flux_r_buffer *buf,
                       uint32_t first, uint32_t count, flux_color color);

    void  (*draw_gradient)(flux_rhi_device *r, flux_r_buffer *buf,
                           uint32_t first, uint32_t count,
                           const flux_gradient *grad);

    /* ---- clipping and stencil ---- */

    void  (*scissor)(flux_rhi_device *r, int32_t x, int32_t y,
                     uint32_t w, uint32_t h);

    void  (*stencil_clear)(flux_rhi_device *r, int32_t x, int32_t y,
                           uint32_t w, uint32_t h);

    /* Draw triangle mesh into the stencil buffer.
     * fill_rule: 0 = even-odd (inc-wrap), 1 = non-zero (front inc, back dec). */
    void  (*stencil_fill)(flux_rhi_device *r, flux_r_buffer *buf,
                          uint32_t first, uint32_t count,
                          int fill_rule);

    void  (*stencil_ref)(flux_rhi_device *r, uint32_t ref);

    /* Cover passes: draw over path bounds where stencil == ref.
     * Used by path stencil fill (2nd pass) after stencil_fill. */
    void  (*cover_solid)(flux_rhi_device *r, flux_r_buffer *buf,
                         uint32_t first, uint32_t count, flux_color color);

    void  (*cover_gradient)(flux_rhi_device *r, flux_r_buffer *buf,
                            uint32_t first, uint32_t count,
                            const flux_gradient *grad);

    /* ---- blur ---- */

    /* Apply a separable Gaussian blur to the current render target.
     * sigma: standard deviation; <= 0 means no-op.
     * Called between begin_pass / end_pass (requires active render pass). */
    void  (*blur)(flux_rhi_device *r, float sigma);

    /* ---- texture resources ---- */

    /* Allocate a texture that draw_image can reference.
     * data may be NULL for uninitialised textures. */
    flux_r_texture * (*texture_alloc)(flux_rhi_device *r,
                                      uint32_t w, uint32_t h,
                                      flux_pixel_format fmt,
                                      const void *data, size_t stride);
    void  (*texture_free)(flux_rhi_device *r, flux_r_texture *tex);

    /* Update a sub-region of an existing texture. */
    void  (*texture_update)(flux_rhi_device *r, flux_r_texture *tex,
                            const void *data,
                            uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h);

    /* ---- surface-as-texture (render-to-texture) ---- */

    /* Wrap the current render target as a texture.
     * Used by flux_image_create_from_surface. Returns NULL if not supported. */
    flux_r_texture * (*surface_texture)(flux_rhi_device *r);
};

/* Convenience: access the vtable through the handle. */
static inline const struct flux_rhi_vtbl *
flux_rhi_vt(const flux_rhi_device *r)
{
    return *(const struct flux_rhi_vtbl **)r;
}

#endif /* FLUX_RHI_H */
