#include "flux/flux.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", \
                    #cond, __FILE__, __LINE__); \
            return 1; \
        } \
    } while (0)

static bool pixel_near(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                       uint8_t er, uint8_t eg, uint8_t eb, uint8_t ea,
                       int tol)
{
    return abs((int)r - (int)er) <= tol &&
           abs((int)g - (int)eg) <= tol &&
           abs((int)b - (int)eb) <= tol &&
           abs((int)a - (int)ea) <= tol;
}

static void check_pixel(const uint8_t *px, uint8_t er, uint8_t eg, uint8_t eb, uint8_t ea,
                        int x, int y)
{
    if (!pixel_near(px[0], px[1], px[2], px[3], er, eg, eb, ea, 8)) {
        fprintf(stderr, "pixel mismatch at (%d,%d): got (%u,%u,%u,%u) "
                        "expected (%u,%u,%u,%u)\n",
                x, y, px[0], px[1], px[2], px[3], er, eg, eb, ea);
        exit(1);
    }
}

int main(void)
{
    fx_context_desc desc = { .app_name = "test_multi_subpath" };
    fx_context *ctx = fx_context_create(&desc);
    if (!ctx) {
        fprintf(stderr, "Vulkan not available, skipping GPU multi-subpath test\n");
        return 0;
    }

    fx_surface *s = fx_surface_create_offscreen(ctx, 64, 64,
                                                FX_FMT_RGBA8_UNORM,
                                                FX_CS_SRGB);
    CHECK(s != nullptr);

    /* --- Multi-subpath fill: two separate triangles --- */
    fx_canvas *c = fx_surface_acquire(s);
    CHECK(c != nullptr);
    fx_clear(c, fx_color_rgba(0, 0, 0, 255));

    fx_path *path = fx_path_create();
    CHECK(path != nullptr);

    /* Triangle 1: top-left */
    fx_path_move_to(path, 4.0f, 4.0f);
    fx_path_line_to(path, 28.0f, 4.0f);
    fx_path_line_to(path, 4.0f, 28.0f);
    fx_path_close(path);

    /* Triangle 2: bottom-right */
    fx_path_move_to(path, 36.0f, 36.0f);
    fx_path_line_to(path, 60.0f, 36.0f);
    fx_path_line_to(path, 36.0f, 60.0f);
    fx_path_close(path);

    fx_paint paint;
    fx_paint_init(&paint, fx_color_rgba(255, 0, 0, 255));
    (void)fx_fill_path(c, path, &paint);
    fx_surface_present(s);

    uint8_t *pixels = malloc(64 * 64 * 4);
    CHECK(pixels != nullptr);
    CHECK(fx_surface_read_pixels(s, pixels, 64 * 4));

    /* Inside triangle 1 should be red */
    check_pixel(&pixels[(8 * 64 + 8) * 4], 255, 0, 0, 255, 8, 8);
    /* Inside triangle 2 should be red */
    check_pixel(&pixels[(40 * 64 + 40) * 4], 255, 0, 0, 255, 40, 40);
    /* Middle gap should remain black */
    check_pixel(&pixels[(32 * 64 + 32) * 4], 0, 0, 0, 255, 32, 32);

    free(pixels);
    fx_path_reset(path);

    /* --- Donut / hole fill: outer rect with inner hole --- */
    c = fx_surface_acquire(s);
    fx_clear(c, fx_color_rgba(0, 0, 0, 255));

    /* Outer rect: counter-clockwise */
    fx_path_move_to(path, 10.0f, 10.0f);
    fx_path_line_to(path, 50.0f, 10.0f);
    fx_path_line_to(path, 50.0f, 50.0f);
    fx_path_line_to(path, 10.0f, 50.0f);
    fx_path_close(path);

    /* Inner rect: clockwise (hole) */
    fx_path_move_to(path, 20.0f, 20.0f);
    fx_path_line_to(path, 20.0f, 40.0f);
    fx_path_line_to(path, 40.0f, 40.0f);
    fx_path_line_to(path, 40.0f, 20.0f);
    fx_path_close(path);

    fx_paint_init(&paint, fx_color_rgba(255, 0, 0, 255));
    (void)fx_fill_path(c, path, &paint);
    fx_surface_present(s);

    pixels = malloc(64 * 64 * 4);
    CHECK(pixels != nullptr);
    CHECK(fx_surface_read_pixels(s, pixels, 64 * 4));

    /* Between outer and inner rect should be red */
    check_pixel(&pixels[(15 * 64 + 15) * 4], 255, 0, 0, 255, 15, 15);
    /* Inside the hole should remain black */
    check_pixel(&pixels[(30 * 64 + 30) * 4], 0, 0, 0, 255, 30, 30);
    /* Outside outer rect should remain black */
    check_pixel(&pixels[(5 * 64 + 5) * 4], 0, 0, 0, 255, 5, 5);

    free(pixels);
    fx_path_reset(path);

    /* --- Multi-subpath stroke: two separate open polylines --- */
    c = fx_surface_acquire(s);
    fx_clear(c, fx_color_rgba(0, 0, 0, 255));

    fx_path_move_to(path, 4.0f, 8.0f);
    fx_path_line_to(path, 28.0f, 8.0f);
    fx_path_line_to(path, 16.0f, 24.0f);

    fx_path_move_to(path, 36.0f, 40.0f);
    fx_path_line_to(path, 60.0f, 40.0f);
    fx_path_line_to(path, 48.0f, 56.0f);

    paint.stroke_width = 2.0f;
    (void)fx_stroke_path(c, path, &paint);
    fx_surface_present(s);

    pixels = malloc(64 * 64 * 4);
    CHECK(pixels != nullptr);
    CHECK(fx_surface_read_pixels(s, pixels, 64 * 4));

    /* On first stroke segment should be red */
    check_pixel(&pixels[(8 * 64 + 16) * 4], 255, 0, 0, 255, 16, 8);
    /* On second stroke segment should be red */
    check_pixel(&pixels[(40 * 64 + 48) * 4], 255, 0, 0, 255, 48, 40);
    /* Gap between strokes should remain black */
    check_pixel(&pixels[(32 * 64 + 32) * 4], 0, 0, 0, 255, 32, 32);

    free(pixels);
    fx_path_destroy(path);
    fx_surface_destroy(s);
    fx_context_destroy(ctx);

    printf("test_multi_subpath: OK\n");
    return 0;
}
