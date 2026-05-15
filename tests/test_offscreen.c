#include <flux/flux.h>
#include "test_helpers.h"

int main(void)
{
    flux_context *ctx = NULL;
    CHECK(flux_context_create(NULL, &ctx) == FLUX_OK);

    flux_surface *s = NULL;
    CHECK(flux_surface_create_offscreen(ctx, 32, 32,
            FLUX_FMT_RGBA8_UNORM, FLUX_CS_SRGB, &s) == FLUX_OK);

    int32_t w = 0, h = 0;
    CHECK(flux_surface_get_size(s, &w, &h) == FLUX_OK);
    CHECK(w == 32 && h == 32);
    CHECK(flux_surface_get_format(s) == FLUX_FMT_RGBA8_UNORM);
    CHECK(approx_eq(flux_surface_get_dpr(s), 1.0f));

    flux_canvas *c = flux_surface_acquire(s);
    CHECK(c != NULL);
    CHECK(flux_canvas_clear(c, flux_color_rgba(255, 0, 0, 255)) == FLUX_OK);
    CHECK(flux_canvas_fill_rect(c, &(flux_rect){ 4, 4, 8, 8 },
                                 flux_color_rgba(0, 0, 255, 255)) == FLUX_OK);
    CHECK(flux_surface_present(s) == FLUX_OK);

    static uint8_t pixels[32 * 32 * 4];
    CHECK(flux_surface_read_pixels(s, pixels, 0) == FLUX_OK);

    /* Outside the rect: red. */
    uint8_t *p_out = &pixels[(0 * 32 + 0) * 4];
    CHECK(p_out[0] == 255 && p_out[1] == 0 && p_out[2] == 0);

    /* Inside: blue. */
    uint8_t *p_in = &pixels[(6 * 32 + 6) * 4];
    CHECK(p_in[0] == 0 && p_in[1] == 0 && p_in[2] == 255);

    flux_surface_release(s);
    flux_context_release(ctx);
    printf("offscreen OK\n");
    return 0;
}
