/*
 * Image: pixel data resource. The CPU-side copy is optional and used by
 * the software backend / readback. The backend texture handle (`rtex`)
 * is owned by the RHI; flux_image holds it but does not free it (the
 * RHI tears it down when the surface is destroyed).
 */
#include "internal.h"

static size_t packed_stride(uint32_t width, flux_pixel_format fmt)
{
    return (size_t)width * flux_pixel_format_bytes(fmt);
}

flux_result flux_image_create(flux_context *ctx, const flux_image_desc *desc,
                              flux_image **out_image)
{
    if (!ctx || !desc || !out_image) return FLUX_ERROR_INVALID_ARGUMENT;
    if (desc->size < sizeof(uint32_t)) return FLUX_ERROR_INVALID_ARGUMENT;
    if (desc->width == 0 || desc->height == 0) return FLUX_ERROR_INVALID_ARGUMENT;
    uint32_t bpp = flux_pixel_format_bytes(desc->format);
    if (bpp == 0) return FLUX_ERROR_INVALID_ARGUMENT;

    flux_image *img = flux_calloc(ctx, 1, sizeof(*img));
    if (!img) return FLUX_ERROR_OUT_OF_MEMORY;

    flux_ref_init(&img->ref_count);
    img->ctx = flux_context_retain(ctx);
    img->desc = *desc;
    img->desc.data = NULL;     /* never re-export the caller's buffer pointer */
    if (img->desc.stride == 0)
        img->desc.stride = packed_stride(desc->width, desc->format);

    if (desc->data) {
        size_t in_stride = desc->stride > 0 ? desc->stride : packed_stride(desc->width, desc->format);
        img->data_size = (size_t)desc->height * in_stride;
        img->data = flux_alloc(ctx, img->data_size);
        if (!img->data) {
            flux_context_release(img->ctx);
            flux_free(ctx, img);
            return FLUX_ERROR_OUT_OF_MEMORY;
        }
        memcpy(img->data, desc->data, img->data_size);
    }

    *out_image = img;
    return FLUX_OK;
}

flux_image *flux_image_retain(flux_image *image)
{
    if (image) flux_ref_retain(&image->ref_count);
    return image;
}

void flux_image_release(flux_image *image)
{
    if (!image) return;
    if (flux_ref_release(&image->ref_count) == 0) {
        flux_context *ctx = image->ctx;
        if (image->surface) flux_surface_release(image->surface);
        flux_free(ctx, image->data);
        flux_free(ctx, image);
        flux_context_release(ctx);
    }
}

flux_result flux_image_update(flux_image *image, const void *data, size_t stride)
{
    if (!image || !data) return FLUX_ERROR_INVALID_ARGUMENT;
    if (stride == 0) stride = packed_stride(image->desc.width, image->desc.format);
    size_t sz = (size_t)image->desc.height * stride;
    void *new_data = flux_alloc(image->ctx, sz);
    if (!new_data) return FLUX_ERROR_OUT_OF_MEMORY;
    memcpy(new_data, data, sz);

    flux_free(image->ctx, image->data);
    image->data = new_data;
    image->data_size = sz;
    image->desc.stride = stride;
    return FLUX_OK;
}

flux_result flux_image_update_region(flux_image *image,
                                     uint32_t x, uint32_t y,
                                     uint32_t w, uint32_t h,
                                     const void *data, size_t stride)
{
    if (!image || !data) return FLUX_ERROR_INVALID_ARGUMENT;
    if (w == 0 || h == 0) return FLUX_ERROR_INVALID_ARGUMENT;
    if (x + w > image->desc.width || y + h > image->desc.height)
        return FLUX_ERROR_OUT_OF_RANGE;

    uint32_t bpp = flux_pixel_format_bytes(image->desc.format);
    size_t src_stride = stride > 0 ? stride : (size_t)w * bpp;

    if (!image->data) {
        size_t bytes = (size_t)image->desc.height * image->desc.stride;
        image->data = flux_calloc(image->ctx, 1, bytes);
        if (!image->data) return FLUX_ERROR_OUT_OF_MEMORY;
        image->data_size = bytes;
    }

    uint8_t *dst = (uint8_t *)image->data + (size_t)y * image->desc.stride + (size_t)x * bpp;
    const uint8_t *src = data;
    size_t row_bytes = (size_t)w * bpp;
    for (uint32_t row = 0; row < h; ++row) {
        memcpy(dst, src, row_bytes);
        dst += image->desc.stride;
        src += src_stride;
    }
    return FLUX_OK;
}

flux_result flux_image_get_size(const flux_image *image, uint32_t *out_w, uint32_t *out_h)
{
    if (!image) return FLUX_ERROR_INVALID_ARGUMENT;
    if (out_w) *out_w = image->desc.width;
    if (out_h) *out_h = image->desc.height;
    return FLUX_OK;
}

flux_pixel_format flux_image_get_format(const flux_image *image)
{
    return image ? image->desc.format : FLUX_FMT_RGBA8_UNORM;
}

const void *flux_image_data(const flux_image *image, size_t *out_size, size_t *out_stride)
{
    if (!image) return NULL;
    if (out_size)   *out_size   = image->data_size;
    if (out_stride) *out_stride = image->desc.stride;
    return image->data;
}

flux_result flux_image_create_from_surface(flux_surface *s, flux_image **out_image)
{
    if (!s || !s->rhi || !out_image) return FLUX_ERROR_INVALID_ARGUMENT;

    const struct flux_rhi_vtbl *vt = flux_rhi_vt(s->rhi);
    flux_r_texture *rtex = vt->surface_texture(s->rhi);
    if (!rtex) return FLUX_ERROR_BACKEND_FAILURE;

    flux_image *img = flux_calloc(s->ctx, 1, sizeof(*img));
    if (!img) return FLUX_ERROR_OUT_OF_MEMORY;

    flux_ref_init(&img->ref_count);
    img->ctx     = flux_context_retain(s->ctx);
    img->surface = flux_surface_retain(s);
    img->rtex    = rtex;

    uint32_t sw = 0, sh = 0;
    vt->surface_extent(s->rhi, &sw, &sh);
    img->desc.size   = sizeof(img->desc);
    img->desc.width  = sw;
    img->desc.height = sh;
    img->desc.format = s->format;
    img->desc.stride = packed_stride(sw, s->format);

    *out_image = img;
    return FLUX_OK;
}
