#include <flux/flux.h>
#include "test_helpers.h"
#include <string.h>

int main(void)
{
    flux_context *ctx = NULL;
    CHECK(flux_context_create(NULL, &ctx) == FLUX_OK);

    /* Create from initial data. */
    static uint8_t pixels[16 * 16 * 4];
    for (size_t i = 0; i < sizeof(pixels); ++i) pixels[i] = (uint8_t)i;

    flux_image_desc desc = {
        .size   = sizeof(desc),
        .width  = 16,
        .height = 16,
        .format = FLUX_FMT_RGBA8_UNORM,
        .data   = pixels,
        .stride = 16 * 4,
    };
    flux_image *img = NULL;
    CHECK(flux_image_create(ctx, &desc, &img) == FLUX_OK);

    uint32_t w = 0, h = 0;
    CHECK(flux_image_get_size(img, &w, &h) == FLUX_OK);
    CHECK(w == 16 && h == 16);
    CHECK(flux_image_get_format(img) == FLUX_FMT_RGBA8_UNORM);

    /* CPU copy round-trips. */
    size_t out_size = 0, out_stride = 0;
    const void *data = flux_image_data(img, &out_size, &out_stride);
    CHECK(data != NULL);
    CHECK(out_stride == 16 * 4);
    CHECK(out_size   == 16 * 16 * 4);
    CHECK(memcmp(data, pixels, sizeof(pixels)) == 0);

    /* Replace via update. */
    static uint8_t alt[16 * 16 * 4];
    memset(alt, 0xAB, sizeof(alt));
    CHECK(flux_image_update(img, alt, 16 * 4) == FLUX_OK);
    data = flux_image_data(img, &out_size, &out_stride);
    CHECK(memcmp(data, alt, sizeof(alt)) == 0);

    /* Sub-region update */
    static uint8_t patch[4 * 4 * 4];
    memset(patch, 0x33, sizeof(patch));
    CHECK(flux_image_update_region(img, 1, 2, 4, 4, patch, 4 * 4) == FLUX_OK);
    data = flux_image_data(img, &out_size, &out_stride);
    const uint8_t *bytes = data;
    CHECK(bytes[(2 * 16 + 1) * 4] == 0x33);

    /* Out-of-range region rejected. */
    CHECK(flux_image_update_region(img, 14, 14, 4, 4, patch, 4 * 4) == FLUX_ERROR_OUT_OF_RANGE);

    flux_image_release(img);
    flux_context_release(ctx);
    printf("image OK\n");
    return 0;
}
