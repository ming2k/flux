/*
 * minimal.c — simplest possible flux usage.
 *
 * Build: meson compile -C build
 * Run:   ./build/examples/minimal
 *
 * Creates a 256x256 offscreen surface, fills it with a solid colour,
 * and writes the resulting image to minimal.ppm.
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
    flux_context_desc desc = { .size = sizeof(desc) };
    flux_context *ctx = NULL;
    flux_result r;

    if ((r = flux_context_create(&desc, &ctx)) != FLUX_OK)
        return die("context_create", r);

    flux_surface *surface = NULL;
    r = flux_surface_create_offscreen(ctx, 256, 256,
                                      FLUX_FMT_RGBA8_UNORM, FLUX_CS_SRGB,
                                      &surface);
    if (r != FLUX_OK) { flux_context_release(ctx); return die("surface_create", r); }

    flux_canvas *c = flux_surface_acquire(surface);
    (void)flux_canvas_clear(c, flux_color_rgba(0, 100, 200, 255));
    (void)flux_surface_present(surface);

    static uint8_t pixels[256 * 256 * 4];
    if ((r = flux_surface_read_pixels(surface, pixels, 0)) != FLUX_OK) {
        flux_surface_release(surface);
        flux_context_release(ctx);
        return die("read_pixels", r);
    }

    FILE *f = fopen("minimal.ppm", "wb");
    if (f) {
        fprintf(f, "P6\n256 256\n255\n");
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x)
                fwrite(&pixels[(y * 256 + x) * 4], 3, 1, f);
        fclose(f);
    }

    flux_surface_release(surface);
    flux_context_release(ctx);
    return 0;
}
