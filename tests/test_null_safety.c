/* Verify every retain/release/free is NULL-safe and that getters tolerate
 * NULL handles by returning sentinel defaults. */
#include <flux/flux.h>
#include "test_helpers.h"

int main(void)
{
    /* Retain/release on NULL must not crash. */
    flux_context_release(NULL);
    flux_surface_release(NULL);
    flux_path_release(NULL);
    flux_paint_release(NULL);
    flux_gradient_release(NULL);
    flux_image_release(NULL);
    flux_glyph_run_release(NULL);

    CHECK(flux_context_retain(NULL)    == NULL);
    CHECK(flux_surface_retain(NULL)    == NULL);
    CHECK(flux_path_retain(NULL)       == NULL);
    CHECK(flux_paint_retain(NULL)      == NULL);
    CHECK(flux_gradient_retain(NULL)   == NULL);
    CHECK(flux_image_retain(NULL)      == NULL);
    CHECK(flux_glyph_run_retain(NULL)  == NULL);

    /* Getters on NULL return sentinel defaults. */
    CHECK(flux_paint_get_color(NULL)      == 0u);
    CHECK(flux_paint_get_dash_count(NULL) == 0u);
    CHECK(flux_glyph_run_count(NULL)      == 0);
    CHECK(flux_path_verb_count(NULL)      == 0);
    CHECK(flux_surface_get_dpr(NULL)      == 1.0f);
    CHECK(flux_image_get_format(NULL)     == FLUX_FMT_RGBA8_UNORM);

    /* Bad arguments return INVALID_ARGUMENT, not crash. */
    CHECK(flux_context_create(NULL, NULL)             == FLUX_ERROR_INVALID_ARGUMENT);
    CHECK(flux_path_create(NULL, NULL)                == FLUX_ERROR_INVALID_ARGUMENT);
    CHECK(flux_paint_create(NULL, NULL)               == FLUX_ERROR_INVALID_ARGUMENT);
    CHECK(flux_gradient_create_linear(NULL, NULL, NULL) == FLUX_ERROR_INVALID_ARGUMENT);

    printf("null_safety OK\n");
    return 0;
}
