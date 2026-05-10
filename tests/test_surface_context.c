#include "internal.h"
#include "flux/flux_vulkan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__,     \
                    __LINE__);                                                 \
            return false;                                                      \
        }                                                                      \
    } while (0)

static fx_context *g_ctx = nullptr;

static bool init_ctx(void)
{
    if (g_ctx)
        return true;
    fx_context_desc desc = { .app_name = "test_surface",
                              .enable_validation = false };
    g_ctx = fx_context_create(&desc);
    if (!g_ctx) {
        fprintf(stderr,
                "Vulkan not available, skipping GPU surface tests\n");
    }
    return true;
}

/* ---------- Surface creation with invalid parameters ---------- */

static bool test_surface_offscreen_zero_size(void)
{
    if (!g_ctx)
        return true;
    CHECK(fx_surface_create_offscreen(g_ctx, 0, 64, FX_FMT_RGBA8_UNORM,
                                       FX_CS_SRGB)
          == nullptr);
    CHECK(fx_surface_create_offscreen(g_ctx, 64, 0, FX_FMT_RGBA8_UNORM,
                                       FX_CS_SRGB)
          == nullptr);
    CHECK(fx_surface_create_offscreen(g_ctx, -1, 64, FX_FMT_RGBA8_UNORM,
                                       FX_CS_SRGB)
          == nullptr);
    return true;
}

static bool test_surface_offscreen_all_formats(void)
{
    if (!g_ctx)
        return true;
    fx_pixel_format formats[] = { FX_FMT_BGRA8_UNORM, FX_FMT_RGBA8_UNORM,
                                   FX_FMT_A8_UNORM };
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        fx_surface *s = fx_surface_create_offscreen(g_ctx, 8, 8, formats[i],
                                                     FX_CS_SRGB);
        CHECK(s != nullptr);
        fx_surface_destroy(s);
    }
    return true;
}

static bool test_surface_dpr_persistence(void)
{
    if (!g_ctx)
        return true;
    fx_surface *s = fx_surface_create_offscreen(g_ctx, 32, 32,
                                                 FX_FMT_RGBA8_UNORM, FX_CS_SRGB);
    CHECK(s != nullptr);

    fx_surface_set_dpr(s, 2.0f);
    CHECK(fx_surface_get_dpr(s) == 2.0f);

    fx_surface_set_dpr(s, 0.0f); /* invalid, should be ignored */
    CHECK(fx_surface_get_dpr(s) == 2.0f);

    fx_surface_set_dpr(s, -1.0f); /* invalid, should be ignored */
    CHECK(fx_surface_get_dpr(s) == 2.0f);

    fx_surface_set_dpr(s, 3.5f);
    CHECK(fx_surface_get_dpr(s) == 3.5f);

    fx_surface_destroy(s);
    return true;
}

static bool test_surface_resize_idempotent(void)
{
    if (!g_ctx)
        return true;
    fx_surface *s = fx_surface_create_offscreen(g_ctx, 32, 32,
                                                 FX_FMT_RGBA8_UNORM, FX_CS_SRGB);
    CHECK(s != nullptr);

    fx_surface_resize(s, 64, 64);
    fx_surface_resize(s, 64, 64); /* same size */
    fx_surface_resize(s, 16, 16);
    fx_surface_resize(s, 128, 128);

    fx_surface_destroy(s);
    return true;
}

/* ---------- Context capabilities ---------- */

static bool test_context_device_caps_basic(void)
{
    if (!g_ctx)
        return true;

    /* Device is initialized lazily; create a surface first */
    fx_surface *s = fx_surface_create_offscreen(g_ctx, 8, 8,
                                                 FX_FMT_RGBA8_UNORM, FX_CS_SRGB);
    if (!s)
        return true;

    fx_device_caps caps;
    CHECK(fx_context_get_device_caps(g_ctx, &caps));
    CHECK(caps.api_version != 0);
    CHECK(caps.max_image_dimension_2d > 0);
    CHECK(caps.max_color_attachments > 0);

    fx_surface_destroy(s);
    return true;
}

static bool test_context_device_caps_null(void)
{
    fx_device_caps caps;
    CHECK(!fx_context_get_device_caps(nullptr, &caps));
    return true;
}

static bool test_context_get_instance(void)
{
    if (!g_ctx)
        return true;
    VkInstance inst = fx_context_get_instance(g_ctx);
    CHECK(inst != VK_NULL_HANDLE);
    return true;
}

/* ---------- Canvas operations on real surface ---------- */

static bool test_canvas_basic_clear(void)
{
    if (!g_ctx)
        return true;
    fx_surface *s = fx_surface_create_offscreen(g_ctx, 8, 8,
                                                 FX_FMT_RGBA8_UNORM, FX_CS_SRGB);
    CHECK(s != nullptr);

    fx_canvas *c = fx_surface_acquire(s);
    CHECK(c != nullptr);
    fx_clear(c, fx_color_rgba(255, 0, 0, 255));
    fx_surface_present(s);

    fx_surface_destroy(s);
    return true;
}

static bool test_canvas_op_count_empty(void)
{
    if (!g_ctx)
        return true;
    fx_surface *s = fx_surface_create_offscreen(g_ctx, 8, 8,
                                                 FX_FMT_RGBA8_UNORM, FX_CS_SRGB);
    CHECK(s != nullptr);

    fx_canvas *c = fx_surface_acquire(s);
    CHECK(c != nullptr);
    CHECK(fx_canvas_op_count(c) == 0);

    fx_surface_destroy(s);
    return true;
}

static bool test_canvas_acquire_null(void)
{
    CHECK(fx_surface_acquire(nullptr) == nullptr);
    return true;
}

/* ---------- Multiple surfaces per context ---------- */

static bool test_multiple_surfaces_same_context(void)
{
    if (!g_ctx)
        return true;
    fx_surface *s1 = fx_surface_create_offscreen(g_ctx, 16, 16,
                                                  FX_FMT_RGBA8_UNORM, FX_CS_SRGB);
    fx_surface *s2 = fx_surface_create_offscreen(g_ctx, 32, 32,
                                                  FX_FMT_RGBA8_UNORM, FX_CS_SRGB);
    CHECK(s1 != nullptr);
    CHECK(s2 != nullptr);
    CHECK(s1 != s2);

    fx_canvas *c1 = fx_surface_acquire(s1);
    fx_canvas *c2 = fx_surface_acquire(s2);
    CHECK(c1 != nullptr);
    CHECK(c2 != nullptr);
    CHECK(c1 != c2);

    /* Don't present to avoid GPU queue congestion in tests */
    fx_surface_destroy(s1);
    fx_surface_destroy(s2);
    return true;
}

/* ---------- Surface readback ---------- */

static bool test_surface_read_pixels_null_buffer(void)
{
    if (!g_ctx)
        return true;
    fx_surface *s = fx_surface_create_offscreen(g_ctx, 4, 4,
                                                 FX_FMT_RGBA8_UNORM, FX_CS_SRGB);
    CHECK(s != nullptr);
    CHECK(!fx_surface_read_pixels(s, nullptr, 16));
    fx_surface_destroy(s);
    return true;
}

int main(void)
{
    init_ctx();
    if (!test_surface_offscreen_zero_size()) return 1;
    if (!test_surface_offscreen_all_formats()) return 1;
    if (!test_surface_dpr_persistence()) return 1;
    if (!test_surface_resize_idempotent()) return 1;
    if (!test_context_device_caps_basic()) return 1;
    if (!test_context_device_caps_null()) return 1;
    if (!test_context_get_instance()) return 1;
    if (!test_canvas_basic_clear()) return 1;
    if (!test_canvas_op_count_empty()) return 1;
    if (!test_canvas_acquire_null()) return 1;
    if (!test_multiple_surfaces_same_context()) return 1;
    if (!test_surface_read_pixels_null_buffer()) return 1;

    if (g_ctx)
        fx_context_destroy(g_ctx);
    printf("surface_context OK\n");
    return 0;
}
