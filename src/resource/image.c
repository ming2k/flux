/* Minimal image implementation. */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

fx_image *fx_image_create(fx_context *ctx, const fx_image_desc *desc)
{
    if (!ctx || !desc || desc->width == 0 || desc->height == 0) return nullptr;

    fx_image *img = calloc(1, sizeof(*img));
    if (!img) return nullptr;

    img->ctx = ctx;
    img->desc = *desc;

    if (desc->data) {
        size_t stride = desc->stride > 0 ? desc->stride : desc->width * 4;
        img->data_size = (size_t)desc->height * stride;
        img->data = malloc(img->data_size);
        if (!img->data) { free(img); return nullptr; }
        memcpy(img->data, desc->data, img->data_size);
    }

    return img;
}

void fx_image_destroy(fx_image *image)
{
    if (!image) return;
    free(image->data);
    free(image);
}

bool fx_image_update(fx_image *image, const void *data, size_t stride)
{
    if (!image || !data) return false;
    if (stride == 0) stride = (size_t)image->desc.width * 4;
    size_t sz = (size_t)image->desc.height * stride;
    free(image->data);
    image->data = malloc(sz);
    if (!image->data) return false;
    image->data_size = sz;
    memcpy(image->data, data, sz);
    return true;
}

bool fx_image_get_desc(const fx_image *image, fx_image_desc *out)
{
    if (!image || !out) return false;
    *out = image->desc;
    return true;
}

const void *fx_image_data(const fx_image *image, size_t *out_size, size_t *out_stride)
{
    if (!image) return nullptr;
    if (out_size) *out_size = image->data_size;
    if (out_stride) *out_stride = image->desc.stride > 0 ? image->desc.stride : image->desc.width * 4;
    return image->data;
}
