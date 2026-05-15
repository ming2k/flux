/*
 * hello_rect.c — basic shapes, gradients, clipping, and transforms.
 *
 * Build: meson compile -C build
 * Run:   ./build/examples/hello_rect
 *
 * Draws a rounded rectangle filled with a linear gradient, then
 * overlays a semi-transparent rect inside a rectangular clip.
 */
#include <flux/flux.h>
#include <stdio.h>
#include <stdlib.h>

static int die(const char *what, flux_result r)
{
    fprintf(stderr, "%s: %s\n", what, flux_result_string(r));
    return 1;
}

int main(void)
{
    int w = 400, h = 300;

    flux_context_desc desc = { .size = sizeof(desc) };
    flux_context *ctx = NULL;
    flux_result r;
    if ((r = flux_context_create(&desc, &ctx)) != FLUX_OK) return die("context_create", r);

    flux_surface *s = NULL;
    if ((r = flux_surface_create_offscreen(ctx, w, h,
            FLUX_FMT_RGBA8_UNORM, FLUX_CS_SRGB, &s)) != FLUX_OK) {
        flux_context_release(ctx);
        return die("surface_create", r);
    }

    flux_canvas *c = flux_surface_acquire(s);
    flux_canvas_clear(c, flux_color_rgba(245, 245, 250, 255));

    /* Rounded-rect filled with a linear gradient. */
    flux_path *path = NULL;
    flux_path_create(ctx, &path);
    flux_path_add_round_rect(path, &(flux_rect){ 50, 50, 300, 200 }, 20.0f);

    flux_color   grad_colors[2] = { 0xFFFF0000u, 0xFF0000FFu };
    float        grad_stops [2] = { 0.0f, 1.0f };
    flux_linear_gradient_desc gdesc = {
        .size       = sizeof(gdesc),
        .start      = { 50.0f,  50.0f },
        .end        = { 350.0f, 250.0f },
        .colors     = grad_colors,
        .stops      = grad_stops,
        .stop_count = 2,
    };
    flux_gradient *grad = NULL;
    flux_gradient_create_linear(ctx, &gdesc, &grad);

    flux_paint *paint = NULL;
    flux_paint_create(ctx, &paint);
    flux_paint_set_color   (paint, flux_color_rgba(255, 255, 255, 255));
    flux_paint_set_gradient(paint, grad);
    (void)flux_canvas_fill_path(c, path, paint);

    /* Clipped overlay. */
    flux_canvas_clip_rect(c, &(flux_rect){ 150, 100, 100, 100 });
    (void)flux_canvas_fill_rect(c,
        &(flux_rect){ 100, 75, 200, 150 },
        flux_color_rgba(0, 255, 0, 128));

    flux_surface_present(s);

    uint8_t *pixels = malloc((size_t)w * h * 4);
    if (pixels) {
        flux_surface_read_pixels(s, pixels, 0);
        FILE *f = fopen("hello_rect.ppm", "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", w, h);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    fwrite(&pixels[(y * w + x) * 4], 3, 1, f);
            fclose(f);
        }
        free(pixels);
    }

    flux_paint_release(paint);
    flux_gradient_release(grad);
    flux_path_release(path);
    flux_surface_release(s);
    flux_context_release(ctx);
    return 0;
}
