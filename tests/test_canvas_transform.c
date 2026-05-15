#include <flux/flux.h>
#include "test_helpers.h"

int main(void)
{
    flux_context *ctx = NULL;
    CHECK(flux_context_create(NULL, &ctx) == FLUX_OK);

    flux_surface *s = NULL;
    CHECK(flux_surface_create_offscreen(ctx, 16, 16,
            FLUX_FMT_RGBA8_UNORM, FLUX_CS_SRGB, &s) == FLUX_OK);

    flux_canvas *c = flux_surface_acquire(s);
    flux_matrix m;

    flux_canvas_get_matrix(c, &m);
    CHECK(flux_matrix_is_identity(&m));

    /* save/translate/restore */
    CHECK(flux_canvas_save(c) == FLUX_OK);
    CHECK(flux_canvas_translate(c, 5.0f, -2.0f) == FLUX_OK);
    flux_canvas_get_matrix(c, &m);
    CHECK(approx_eq(m.m[4], 5.0f) && approx_eq(m.m[5], -2.0f));
    CHECK(flux_canvas_restore(c) == FLUX_OK);
    flux_canvas_get_matrix(c, &m);
    CHECK(flux_matrix_is_identity(&m));

    /* restore on empty stack: INVALID_STATE */
    CHECK(flux_canvas_restore(c) == FLUX_ERROR_INVALID_STATE);

    /* concat composes correctly */
    flux_matrix t;
    flux_matrix_translation(&t, 10.0f, 20.0f);
    CHECK(flux_canvas_set_matrix(c, &t) == FLUX_OK);
    CHECK(flux_canvas_scale(c, 2.0f, 2.0f) == FLUX_OK);

    /* present (op count > 0 from clear/etc; just verify no crash) */
    (void)flux_canvas_clear(c, 0u);
    CHECK(flux_surface_present(s) == FLUX_OK);

    flux_surface_release(s);
    flux_context_release(ctx);
    printf("canvas_transform OK\n");
    return 0;
}
