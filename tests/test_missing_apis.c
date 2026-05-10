#include "flux/flux.h"
#include "flux/flux_vulkan.h"
#include <stdio.h>
#include <string.h>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", \
                    #cond, __FILE__, __LINE__); \
            return 1; \
        } \
    } while (0)

int main(void)
{
    fx_context_desc desc = {
        .app_name = "test_missing_apis",
        .enable_validation = false,
    };
    fx_context *ctx = fx_context_create(&desc);
    if (!ctx) {
        fprintf(stderr, "Vulkan not available, skipping\n");
        return 0;
    }

    /* fx_context_get_instance */
    CHECK(fx_context_get_instance(ctx) != VK_NULL_HANDLE);

    /* fx_surface_resize */
    fx_surface *s = fx_surface_create_offscreen(ctx, 32, 32,
                                                FX_FMT_RGBA8_UNORM,
                                                FX_CS_SRGB);
    CHECK(s != nullptr);
    fx_surface_resize(s, 64, 64);
    /* Resize doesn't take effect until next acquire on swapchain surfaces;
     * for offscreen it is a no-op, but it must not crash. */
    /* fx_context_get_device_caps — device is initialized lazily on first surface */
    fx_device_caps caps = { 0 };
    CHECK(fx_context_get_device_caps(ctx, &caps));
    CHECK(caps.api_version != 0);
    CHECK(caps.max_image_dimension_2d > 0);

    fx_surface_set_dpr(s, 2.0f);
    CHECK(fx_surface_get_dpr(s) == 2.0f);
    fx_surface_set_dpr(s, 0.0f);
    CHECK(fx_surface_get_dpr(s) == 2.0f);

    fx_surface_destroy(s);

    /* fx_draw_image */
    fx_image *img = fx_image_create(ctx, &(fx_image_desc){
        .width = 2, .height = 2,
        .format = FX_FMT_RGBA8_UNORM,
        .data = &(uint32_t[4]){ 0xFF0000FF, 0x00FF00FF, 0x0000FFFF, 0xFFFFFFFF },
    });
    CHECK(img != nullptr);

    s = fx_surface_create_offscreen(ctx, 16, 16,
                                    FX_FMT_RGBA8_UNORM, FX_CS_SRGB);
    fx_canvas *c = fx_surface_acquire(s);
    CHECK(fx_draw_image(c, img, nullptr, &(fx_rect){ 0, 0, 2, 2 }));
    fx_surface_present(s);
    fx_surface_destroy(s);
    fx_image_destroy(img);

    /* Glyph upload and draw */
    uint8_t glyph_bitmap[4] = { 255, 255, 255, 255 };
    CHECK(fx_glyph_upload(ctx, 1, glyph_bitmap, 2, 2, 0, 2, 2));

    fx_glyph_run *run = fx_glyph_run_create(1);
    CHECK(run != nullptr);
    CHECK(fx_glyph_run_append(run, 1, 0.0f, 0.0f));

    s = fx_surface_create_offscreen(ctx, 8, 8, FX_FMT_RGBA8_UNORM, FX_CS_SRGB);
    CHECK(s != nullptr);
    c = fx_surface_acquire(s);
    fx_paint paint;
    fx_paint_init(&paint, fx_color_rgba(255, 255, 255, 255));
    CHECK(fx_draw_glyph_run(c, run, 0.0f, 0.0f, &paint));
    fx_surface_present(s);
    fx_surface_destroy(s);
    fx_glyph_run_destroy(run);

    fx_context_destroy(ctx);
    printf("missing_apis OK\n");
    return 0;
}
